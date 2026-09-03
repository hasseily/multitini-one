/* SPDX-License-Identifier: GPL-3.0-only */
#include "core_catalog_sd.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    char names[MT_CORE_CATALOG_MAX_CORES][MT_CORE_PACKAGE_ID_MAX];
    size_t count;
    size_t rejected;
} directory_list_t;

static bool valid_leaf(const char *name)
{
    size_t index;
    if (name == NULL || name[0] == '\0') return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        const char ch = name[index];
        if (index + 1U >= MT_CORE_PACKAGE_ID_MAX ||
            !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_')) {
            return false;
        }
    }
    return true;
}

static int collect_directory(void *context, const char *name, size_t size,
                             bool is_directory)
{
    directory_list_t *const list = context;
    (void)size;
    if (!is_directory) return 0;
    if (!valid_leaf(name) || list->count >= MT_CORE_CATALOG_MAX_CORES) {
        ++list->rejected;
        return 0;
    }
    (void)strcpy(list->names[list->count], name);
    ++list->count;
    return 0;
}

static void sort_directories(directory_list_t *list)
{
    size_t index;
    for (index = 1U; index < list->count; ++index) {
        char value[MT_CORE_PACKAGE_ID_MAX];
        size_t position = index;
        (void)strcpy(value, list->names[index]);
        while (position != 0U &&
               strcmp(list->names[position - 1U], value) > 0) {
            (void)strcpy(list->names[position], list->names[position - 1U]);
            --position;
        }
        (void)strcpy(list->names[position], value);
    }
}

static bool make_path(const char *leaf, const char *tail, char *path,
                      size_t capacity)
{
    const size_t root_size = strlen(MT_CORE_CATALOG_ROOT);
    const size_t leaf_size = strlen(leaf);
    const size_t tail_size = strlen(tail);
    if (root_size + 1U + leaf_size + 1U + tail_size + 1U > capacity)
        return false;
    memcpy(path, MT_CORE_CATALOG_ROOT, root_size);
    path[root_size] = '/';
    memcpy(path + root_size + 1U, leaf, leaf_size);
    path[root_size + 1U + leaf_size] = '/';
    memcpy(path + root_size + 2U + leaf_size, tail, tail_size + 1U);
    return true;
}

static bool make_directory(const char *leaf, char *path, size_t capacity)
{
    const size_t root_size = strlen(MT_CORE_CATALOG_ROOT);
    const size_t leaf_size = strlen(leaf);
    if (root_size + 1U + leaf_size + 1U > capacity) return false;
    memcpy(path, MT_CORE_CATALOG_ROOT, root_size);
    path[root_size] = '/';
    memcpy(path + root_size + 1U, leaf, leaf_size + 1U);
    return true;
}

void mt_core_catalog_sd_scan(mt_core_catalog_sd_t *catalog)
{
    directory_list_t directories = {{{0}}, 0U, 0U};
    size_t index;
    static uint8_t manifest[MT_CORE_PACKAGE_TEXT_MAX + 1U];

    if (catalog == NULL) return;
    memset(catalog, 0, sizeof(*catalog));
    catalog->last_manifest_status = MT_CORE_PACKAGE_OK;
    catalog->sd = mt_sd_storage_list(MT_CORE_CATALOG_ROOT,
                                     collect_directory, &directories);
    catalog->rejected = directories.rejected;
    if (catalog->sd.status != MT_SD_OK) return;
    sort_directories(&directories);

    for (index = 0U; index < directories.count; ++index) {
        char manifest_path[MT_CORE_DIRECTORY_MAX + 16U];
        mt_core_selection_t selection;
        mt_sd_result_t read_result;
        mt_core_package_status_t parse_result;
        if (!make_path(directories.names[index], MT_CORE_CATALOG_MANIFEST,
                       manifest_path, sizeof(manifest_path))) {
            ++catalog->rejected;
            continue;
        }
        read_result = mt_sd_storage_read_file_max(
            manifest_path, manifest, MT_CORE_PACKAGE_TEXT_MAX);
        if (read_result.status != MT_SD_OK) {
            ++catalog->rejected;
            continue;
        }
        manifest[read_result.size] = '\0';
        memset(&selection, 0, sizeof(selection));
        parse_result = mt_core_package_parse(
            (const char *)manifest, read_result.size, &selection.package);
        if (parse_result != MT_CORE_PACKAGE_OK ||
            strcmp(selection.package.id, directories.names[index]) != 0 ||
            !make_directory(directories.names[index], selection.directory,
                            sizeof(selection.directory))) {
            catalog->last_manifest_status = parse_result ==
                MT_CORE_PACKAGE_OK ? MT_CORE_PACKAGE_INVALID_VALUE :
                parse_result;
            ++catalog->rejected;
            continue;
        }
        catalog->cores[catalog->count++] = selection;
    }
}

int mt_core_catalog_sd_find(const mt_core_catalog_sd_t *catalog,
                            uint32_t core_id)
{
    size_t index;
    if (catalog == NULL) return -1;
    for (index = 0U; index < catalog->count; ++index) {
        if (catalog->cores[index].package.core_id == core_id)
            return (int)index;
    }
    return -1;
}
