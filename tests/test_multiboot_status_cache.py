"""Run the production display cache without using a flash or hardware device."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "multiboot_platform.h"
#include "qspi_nor.h"
static unsigned golden_reads,version_reads,slot_reads,select_reads,probes,live_actions;
static int metadata_status=MB_OK,payload_valid=1,appletini_present=1,multitini_present=1;
int qspi_nor_init(qspi_nor_t *n,uint8_t a,uint32_t s) { (void)n;(void)a;(void)s;return 0; }
uint32_t qspi_nor_capacity_bytes(const qspi_nor_t *n) { (void)n;return MB_FLASH_SIZE; }
int qspi_nor_read(qspi_nor_t *n,uint32_t a,void *d,uint32_t s) { (void)n;(void)a;(void)d;(void)s;assert(0);return -1; }
int qspi_nor_erase_region(qspi_nor_t *n,uint32_t a,uint32_t s) { (void)n;(void)a;(void)s;assert(0);return -1; }
int qspi_nor_program(qspi_nor_t *n,uint32_t a,const void *d,uint32_t s) { (void)n;(void)a;(void)d;(void)s;assert(0);return -1; }
int mb_golden_available(const mb_flash_t *f,uint32_t *o) { assert(f);++golden_reads;if(o)*o=0;return MB_OK; }
int mb_golden_installed_version(const mb_flash_t *f,char *v,uint32_t n,uint32_t *o) { assert(f&&n>=7);++version_reads;strcpy(v,"B2.0.3");if(o)*o=0;return MB_OK; }
int mb_metadata_load(const mb_flash_t *f,mb_metadata_t *m) { assert(f);memset(m,0,sizeof(*m));m->default_product=MB_PRODUCT_APPLETINI;return metadata_status; }
int mb_slot_probe(const mb_flash_t *f,uint32_t p,mb_image_t *o) { assert(f);(void)o;++probes;return (p==MB_PRODUCT_APPLETINI?appletini_present:multitini_present)?MB_OK:MB_E_UNAVAILABLE; }
int mb_slot_verify(const mb_flash_t *f,uint32_t p,mb_image_t *o) { assert(f);(void)p;(void)o;++slot_reads;return payload_valid?MB_OK:MB_E_CRC; }
int mb_select(const mb_flash_t *f,uint32_t p,mb_selection_t *s) { assert(f);(void)p;++select_reads;memset(s,0,sizeof(*s));s->product=MB_PRODUCT_APPLETINI;return MB_OK; }
int mb_set_default(const mb_flash_t *f,uint32_t p) { assert(f);(void)p;++live_actions;return payload_valid?MB_OK:MB_E_CRC; }
int mb_set_request(const mb_flash_t *f,uint32_t p) { assert(f);(void)p;++live_actions;return payload_valid?MB_OK:MB_E_CRC; }
int mb_clear_request(const mb_flash_t *f) { assert(f);return MB_OK; }
int main(void) {
    mb_platform_status_t s;
    char version[32];
    /* Startup warms the full Golden identity once, before a tab is opened. */
    assert(mb_platform_status_ui(MB_PRODUCT_MULTITINI,&s)==MB_OK);
    assert(s.golden_available&&s.appletini_present&&s.multitini_present);
    assert(s.default_product==MB_PRODUCT_APPLETINI&&golden_reads==1&&version_reads==1);
    assert(!slot_reads&&!select_reads);
    for(unsigned i=0;i<100;i++) {
        assert(mb_platform_status_ui(MB_PRODUCT_MULTITINI,&s)==MB_OK);
        assert(mb_platform_golden_version_cached(version,sizeof(version))==MB_OK);
        assert(!strcmp(version,"B2.0.3"));
    }
    assert(golden_reads==1&&version_reads==1&&!slot_reads&&!select_reads&&probes==202);
    /* Menu hints can survive a payload corruption; actions must reject it. */
    payload_valid=0;
    assert(mb_platform_status_ui(MB_PRODUCT_MULTITINI,&s)==MB_OK&&s.appletini_present);
    assert(mb_platform_set_default(MB_PRODUCT_APPLETINI)==MB_E_CRC);
    assert(mb_platform_request(MB_PRODUCT_APPLETINI)==MB_E_CRC&&live_actions==2);
    assert(mb_platform_golden_offset(NULL)==MB_OK&&golden_reads==2);
    appletini_present=0;
    assert(mb_platform_status_ui(MB_PRODUCT_MULTITINI,&s)==MB_OK&&!s.appletini_present);
    metadata_status=MB_E_READ;
    assert(mb_platform_status_ui(MB_PRODUCT_MULTITINI,&s)==MB_E_READ&&!s.appletini_present&&!s.multitini_present);
    assert(mb_platform_golden_version_cached(version,1)==MB_E_ARGUMENT&&!version[0]);
    assert(mb_platform_status_ui(MB_PRODUCT_MULTITINI,NULL)==MB_E_ARGUMENT);
    return 0;
}
'''


class StatusCacheTests(unittest.TestCase):
    def test_repeated_menu_entries_do_not_rescan_payloads(self):
        compiler = os.environ.get("CC") or shutil.which("gcc") or "C:/msys64/ucrt64/bin/gcc.exe"
        with tempfile.TemporaryDirectory(prefix="multiboot-status-") as name:
            folder = Path(name)
            source = folder / "cache.c"
            executable = folder / "cache.exe"
            source.write_text(HARNESS)
            env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get("PATH", ""))
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "tests/xilinx_stubs"), "-I", str(ROOT / "firmware/golden/lib"),
                str(source), str(ROOT / "firmware/golden/lib/multiboot_platform.c"),
                "-o", str(executable)], check=True, env=env)
            subprocess.run([str(executable)], check=True, env=env)


if __name__ == "__main__":
    unittest.main()
