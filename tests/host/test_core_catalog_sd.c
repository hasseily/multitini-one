/* SPDX-License-Identifier: GPL-3.0-only */
#include "core_catalog_sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static mt_sd_result_t sd_result(mt_sd_status_t status, size_t size)
{
    mt_sd_result_t result = {status, 0U, size};
    return result;
}

mt_sd_result_t mt_sd_storage_list(const char *path, mt_sd_list_fn callback,
                                  void *context)
{
    check(strcmp(path, MT_CORE_CATALOG_ROOT) == 0, "catalog root listed");
    (void)callback(context, "zeta", 0U, true);
    (void)callback(context, "README.txt", 12U, false);
    (void)callback(context, "Bad Name", 0U, true);
    (void)callback(context, "c64", 0U, true);
    return sd_result(MT_SD_OK, 4U);
}

mt_sd_result_t mt_sd_storage_read_file_max(const char *path, uint8_t *dst,
                                           size_t capacity)
{
    static const char valid[] =
        "schema=1\n"
        "id=c64\n"
        "name=Commodore 64\n"
        "core_id=0x43363420\n"
        "abi=0.6\n"
        "content_root=0:/multitini/cores/c64\n"
        "variant=pal|PAL 50 Hz|c64-pal.bin|4045568|0x7C3F0B9C\n";
    static const char mismatched[] =
        "schema=1\n"
        "id=not-zeta\n"
        "name=Wrong directory\n"
        "core_id=0x12345678\n"
        "abi=0.6\n"
        "content_root=0:/multitini/cores/zeta\n"
        "variant=one|One|one.bin|1048576|0x12345678\n";
    const char *text = strstr(path, "/c64/core.cfg") != NULL ? valid :
                       mismatched;
    const size_t size = strlen(text);
    check(capacity >= size, "manifest buffer capacity");
    memcpy(dst, text, size);
    return sd_result(MT_SD_OK, size);
}

int main(void)
{
    mt_core_catalog_sd_t catalog;
    mt_core_catalog_sd_scan(&catalog);
    check(catalog.count == 1U, "one valid package accepted");
    check(catalog.rejected == 2U, "unsafe and mismatched directories rejected");
    check(strcmp(catalog.cores[0].package.id, "c64") == 0,
          "stable sorted C64 package");
    check(strcmp(catalog.cores[0].directory,
                 "0:/multitini/cores/c64") == 0,
          "package directory recorded");
    check(mt_core_catalog_sd_find(&catalog, UINT32_C(0x43363420)) == 0,
          "active core found");
    check(mt_core_catalog_sd_find(&catalog, UINT32_C(0xFFFFFFFF)) == -1,
          "unknown core absent");
    puts("PASS: SD core catalog discovery");
    return 0;
}
