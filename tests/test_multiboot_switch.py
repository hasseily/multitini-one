"""Execute the production firmware-switch transaction with injected failures."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

PREAMBLE = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "multiboot_platform.h"
#include "sd_storage.h"
#define MT_DEVCFG_MULTIBOOT 1U
#define MT_DEVCFG_MULTIBOOT_MASK 0x1fffU
#define MT_SLCR_UNLOCK 2U
#define MT_PS_RESET_CONTROL 3U
#define MT_SLCR_LOCK 4U
#define MT_SLCR_UNLOCK_KEY 0xdf0dU
#define MT_SLCR_LOCK_KEY 0x767bU
#define MT_PS_SOFTWARE_RESET 1U
#define MT_REG_CORE_ID 5U
#define MT_REG_VIDEO_FRAME_STATUS 6U
#define MT_REG_CONTROL 7U
#define MT_CONTROL_CORE_RESET_REQUEST 1U
#define MT_GENESIS_CORE_ID 9U
#define MT_IIGS_CORE_ID 10U
#define MT_VIDEO_FRAME_STATUS_QUIESCENT 1U
#define DATA_SYNC ((void)0)
static struct { bool has_active; int active; } supervisor;
static struct { bool visible; } supervisor_menu;
static mb_flash_t flash;
static jmp_buf reset_jump;
static int fault, clear_failure, restore_fault;
static bool core_paused, input_ready, video_suspended, worker_held, usb_started, mounted, request, reset_issued;
static bool golden_update_worker_held;
static bool golden_mode;
static unsigned reset_calls, resume_calls, usb_restarts, remounts, requests, request_clears;
static unsigned status_refreshes, effects_configured, register_writes, uart_waits;
static unsigned unmounts;
static uint32_t boot_register;
static uint64_t ticks;
static char error_message[160];

static uint32_t Xil_In32(uint32_t address) {
    assert(address==MT_DEVCFG_MULTIBOOT);
    if (fault==14 && register_writes==1) return boot_register^1U;
    return boot_register;
}
static void Xil_Out32(uint32_t address, uint32_t value) {
    if (address==MT_DEVCFG_MULTIBOOT) { boot_register=value; ++register_writes; return; }
    if (address==MT_SLCR_UNLOCK) { assert(value==MT_SLCR_UNLOCK_KEY); return; }
    if (address==MT_PS_RESET_CONTROL) {
        assert(value==MT_PS_SOFTWARE_RESET && (golden_mode || request) && core_paused && worker_held && !usb_started && !mounted);
        assert(boot_register==MB_GOLDEN_TRIAL_OFFSET/0x8000U);
        reset_issued=true; longjmp(reset_jump,1);
    }
    assert(address==MT_SLCR_LOCK && value==MT_SLCR_LOCK_KEY);
}
static uint32_t shell_read(uint32_t address) {
    if (address==MT_REG_CORE_ID) return MT_GENESIS_CORE_ID;
    if (address==MT_REG_CONTROL) return core_paused ? MT_CONTROL_CORE_RESET_REQUEST : 0U;
    assert(address==MT_REG_VIDEO_FRAME_STATUS);
    return fault==8 ? 0U : MT_VIDEO_FRAME_STATUS_QUIESCENT;
}
const mb_flash_t *mb_platform_flash(void) { return fault==1 ? NULL : &flash; }
int mb_slot_probe(const mb_flash_t *f, uint32_t product, mb_image_t *out) {
    assert(f==&flash && product==MB_PRODUCT_APPLETINI && out==NULL);
    return fault==2 ? MB_E_UNAVAILABLE : MB_OK;
}
static bool mt_screenshot_service_cancel(void) { return fault!=4; }
static int set_core_reset_request(bool asserted) {
    ++reset_calls;
    if (asserted && fault==5) return -1;
    if (!asserted && restore_fault==4) return -1;
    core_paused=asserted; return 0;
}
static void usb_hid_service_set_core_ready(bool ready) { input_ready=ready; }
static void mt_video_effect_service_suspend(void) { assert(core_paused); video_suspended=true; }
static uint64_t monotonic_ms(void *ctx) { (void)ctx; ticks+=100; return ticks; }
static bool mt_video_effect_service_quiescent(void) { return fault!=6; }
static bool mt_video_worker_quiesce(void) { return fault!=7; }
static void usb_hid_service_poll_deferred(void) {}
static int usb_hid_service_stop(void) { usb_started=false; return fault==9 ? -1 : 0; }
mt_sd_result_t mt_sd_storage_unmount(void) {
    ++unmounts;
    if (fault==10 || (fault==15 && unmounts==2)) return (mt_sd_result_t){MT_SD_FILE_BUSY,0U,0U};
    mounted=false; return (mt_sd_result_t){MT_SD_OK,0U,0U};
}
const char *mt_sd_status_name(mt_sd_status_t status) { (void)status; return "mock SD status"; }
static bool mt_video_worker_hold_for_reset(void) {
    /* The final hold must follow the committed request and boot register,
     * so those failure paths can resume a drained worker without reset. */
    assert((golden_mode || request) && boot_register==MB_GOLDEN_TRIAL_OFFSET/0x8000U && !usb_started && !mounted);
    worker_held=true; return fault!=11;
}
int mb_platform_prepare_request(uint32_t product, uint32_t *offset) {
    assert(product==MB_PRODUCT_APPLETINI && core_paused && (!worker_held || golden_update_worker_held) && !usb_started && !mounted);
    *offset=MB_GOLDEN_TRIAL_OFFSET;
    if(fault==3) return MB_E_UNAVAILABLE;
    ++requests; request=true; return fault==12 ? MB_E_PROGRAM : MB_OK;
}
static int xil_printf(const char *fmt, ...) { (void)fmt; return 0; }
int mb_platform_read_report(char *text,uint32_t size) { assert(size);text[0]=0;return MB_OK; }
static bool wait_uart0_transmit_idle(void) { ++uart_waits; return fault!=13; }
int mb_platform_clear_request(void) {
    ++request_clears;
    if (clear_failure) return MB_E_PROGRAM;
    request=false; return MB_OK;
}
mt_sd_result_t mt_sd_storage_mount(void) {
    ++remounts; if (restore_fault==1 || (fault==19 && remounts==1)) return (mt_sd_result_t){MT_SD_MOUNT_FAILED,0U,0U};
    mounted=true; return (mt_sd_result_t){MT_SD_OK,0U,0U};
}
static bool mt_video_worker_resume(void) {
    assert(!golden_update_worker_held && !worker_held);
    ++resume_calls; if (restore_fault==2) return false; video_suspended=worker_held=false; return true;
}
static int usb_hid_service_start(void) {
    ++usb_restarts; if (restore_fault==3) return -1; usb_started=true; return 0;
}
static void configure_selection_effects(const void *active) { assert(active==&supervisor.active); ++effects_configured; }
static void refresh_multiboot_status(void) { ++status_refreshes; }
static void set_supervisor_error(const char *message) { snprintf(error_message,sizeof(error_message),"%s",message); }
static void show_handoff_progress(const char *message) { assert(message!=NULL && message[0]); }
'''

GOLDEN_PREAMBLE = r'''
#include "golden_self_update.h"
#define XPAR_XUARTPS_0_BASEADDR 123U
static unsigned golden_runs, reader_closes, file_refreshes;
static int document_reader;
static void mt_text_reader_close(void *reader) { assert(reader==&document_reader); ++reader_closes; }
static void set_supervisor_status(const char *message) { assert(message!=NULL && message[0]); }
static void refresh_golden_update_file(void) { ++file_refreshes; }
int golden_self_update_run(const char *path,uint32_t uart,uint32_t mirror,golden_self_update_result_t *result) {
    assert(!strcmp(path,"0:/BOOT.BIN") && uart==123U && mirror==123U);
    assert(core_paused && video_suspended && (!worker_held || golden_update_worker_held) && !usb_started && mounted);
    ++golden_runs;
    if (fault==16) {
        result->error_code=17; strcpy(result->error_msg,"BOOT CRC mismatch"); return -1;
    }
    result->found_source=result->updated=result->verified=result->reset_required=1;
    result->boot_offset=GOLDEN_SELF_UPDATE_TRIAL_OFFSET;
    if (fault==17) result->verified=0;
    if (fault==18) result->boot_offset=GOLDEN_SELF_UPDATE_PRIMARY_OFFSET;
    return 0;
}
'''

GOLDEN_SCENARIO = r'''
static void initial_state(void) {
    golden_mode=true; supervisor_menu.visible=true; supervisor.has_active=true;
    core_paused=video_suspended=worker_held=golden_update_worker_held=reset_issued=false;
    input_ready=usb_started=mounted=request=true;
    reset_calls=resume_calls=usb_restarts=remounts=requests=request_clears=0;
    status_refreshes=effects_configured=register_writes=uart_waits=unmounts=0;
    golden_runs=reader_closes=file_refreshes=0;
    ticks=0; boot_register=0x123U; error_message[0]=0;
}
int main(void) {
    const int faults[]={0,4,5,6,7,8,9,10,11,13,14,15,16,17,18,19};
    for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);++i) {
        fault=faults[i]; initial_state();
        if (!setjmp(reset_jump)) self_update_golden(false);
        /* Neither successful staging nor cancellation touches a saved
         * firmware choice or an existing one-shot firmware request. */
        assert(request && requests==0 && request_clears==0);
        if (fault==0) {
            assert(reset_issued && golden_runs==1 && unmounts==2 && remounts==1);
            assert(core_paused && worker_held && golden_update_worker_held);
            assert(!input_ready && !usb_started && !mounted && resume_calls==0);
            continue;
        }
        assert(!reset_issued && mounted && usb_started && supervisor_menu.visible);
        assert(file_refreshes==1 && error_message[0] && boot_register==0x123U);
        if (fault==11) {
            assert(core_paused && worker_held && golden_update_worker_held && !input_ready);
            assert(resume_calls==0 && effects_configured==0 && register_writes==2);
        } else {
            assert(!core_paused && !worker_held && !golden_update_worker_held && input_ready);
            assert(!video_suspended && register_writes==(fault==14 ? 2U : 0U));
            assert(resume_calls==(fault>=6 ? 1U : 0U));
        }
        if (fault==10 || fault==19) assert(golden_runs==0);
        if (fault==15 || fault==16 || fault==17 || fault==18) assert(golden_runs==1);
    }
    /* A later validation fault must leave a prior failed CPU1 hold intact. */
    initial_state(); fault=11;
    self_update_golden(false);
    assert(golden_update_worker_held && worker_held);
    fault=16; resume_calls=effects_configured=0;
    self_update_golden(false);
    assert(core_paused && worker_held && golden_update_worker_held && !input_ready);
    assert(resume_calls==0 && effects_configured==0 && !reset_issued);
    fault=0;
    if (!setjmp(reset_jump)) self_update_golden(false);
    assert(reset_issued && worker_held && golden_update_worker_held && resume_calls==0);
    puts("PASS: production Golden update success, 15 cancellation boundaries, late CPU1 hold, preserved choices and safe retry");
    return 0;
}
'''

SCENARIO = r'''
int main(int argc, char **argv) {
    assert(argc==2); (void)argv;
    for (fault=0; fault<=14; ++fault) {
        golden_update_worker_held=false;
        supervisor_menu.visible=true;
        core_paused=video_suspended=worker_held=request=reset_issued=false;
        input_ready=usb_started=mounted=true;
        reset_calls=resume_calls=usb_restarts=remounts=requests=request_clears=0;
        status_refreshes=effects_configured=register_writes=uart_waits=0;
        ticks=0; boot_register=0x123U; error_message[0]=0;
        supervisor.has_active=true;
        if (!setjmp(reset_jump)) switch_to_appletini();
        if (fault==0) {
            assert(reset_issued && request && core_paused && worker_held && !input_ready && !usb_started && !mounted);
            assert(register_writes==1 && request_clears==0 && status_refreshes==0);
            continue;
        }
        if (fault==11) {
            assert(!reset_issued && !request && core_paused && !input_ready && usb_started && mounted);
            assert(golden_update_worker_held && worker_held && video_suspended);
            assert(resume_calls==0 && effects_configured==0 && request_clears==1 && register_writes==2);
            assert(boot_register==0x123U && strstr(error_message,"CPU1 could not stop safely"));
            continue;
        }
        assert(!reset_issued && !request && !core_paused && input_ready && usb_started && mounted);
        assert(!video_suspended && !worker_held && boot_register==0x123U);
        assert(status_refreshes==1 && strstr(error_message,"Switch cancelled")!=NULL);
        assert(request_clears==(fault>=11 || fault==3 ? 1U : 0U));
        assert(register_writes==(fault==14 ? 2U : 0U));
        assert(usb_restarts==(fault>=9 || fault==3 ? 1U : 0U));
        assert(remounts==(fault>=11 || fault==3 ? 1U : 0U));
        assert(resume_calls==(fault>=6 || fault==3 ? 1U : 0U));
        assert(effects_configured==(fault>=6 || fault==3 ? 1U : 0U));
    }
    /* A failed restore retains core reset, blocks input and skips effects. */
    fault=13;
    for (restore_fault=1; restore_fault<=4; ++restore_fault) {
        golden_update_worker_held=false;
        core_paused=video_suspended=worker_held=request=reset_issued=false;
        input_ready=usb_started=mounted=true;
        effects_configured=0; boot_register=0x123U; register_writes=0;
        if (!setjmp(reset_jump)) switch_to_appletini();
        assert(!reset_issued && !request && core_paused && !input_ready);
        assert(effects_configured==0 && boot_register==0x123U);
        assert(strstr(error_message,"Switch cancelled")!=NULL);
    }
    /* The original reset state must survive a cancelled switch. */
    golden_update_worker_held=false;
    restore_fault=0; core_paused=true; input_ready=false;
    video_suspended=worker_held=request=reset_issued=false;
    usb_started=mounted=true; effects_configured=0; boot_register=0x123U; register_writes=0;
    if (!setjmp(reset_jump)) switch_to_appletini();
    assert(!reset_issued && !request && core_paused && !input_ready && mounted && usb_started);
    /* A write fault while clearing a request is reported without resetting. */
    clear_failure=1; core_paused=false; input_ready=true;
    if (!setjmp(reset_jump)) switch_to_appletini();
    assert(!reset_issued && request && !core_paused && input_ready);
    assert(strstr(error_message,"pending request could not be cleared")!=NULL);
    /* Retrying after an attempted hold cannot warm restart CPU1 even if the
     * next attempt fails before reaching its own hold operation. */
    clear_failure=0; fault=10; golden_update_worker_held=worker_held=true;
    core_paused=true; input_ready=false; mounted=usb_started=true;
    request=false; reset_issued=false; resume_calls=effects_configured=0;
    switch_to_appletini();
    assert(core_paused && worker_held && golden_update_worker_held && !input_ready);
    assert(!reset_issued && resume_calls==0 && effects_configured==0 && mounted && usb_started);
    fault=0; boot_register=0x123U; register_writes=0;
    if (!setjmp(reset_jump)) switch_to_appletini();
    assert(reset_issued && worker_held && golden_update_worker_held && resume_calls==0);
    puts("PASS: production switch success, 14 cancellation faults, four restore failures, held reset, safe retry and request-clear failure");
    return 0;
}
'''


class MultibootSwitchTests(unittest.TestCase):
    def run_native(self, source: str) -> str:
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("A host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="multiboot-handoff-") as name:
            directory = Path(name)
            harness = directory / "handoff.c"
            harness.write_text(source)
            executable = directory / ("handoff.exe" if os.name == "nt" else "handoff")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                     compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                                     "-I", str(ROOT / "firmware/golden/lib"),
                                     "-I", str(ROOT / "firmware/platform/xilinx"), str(harness),
                                     "-o", str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                     str(executable), "faults"], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout

    def test_switch_success_and_every_cancellation_boundary(self):
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            self.skipTest("A host C compiler is required")
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        start = source.index("static void switch_to_appletini(void)\n{")
        end = source.index("\n}", start) + 2
        with tempfile.TemporaryDirectory(prefix="multiboot-switch-") as name:
            directory = Path(name)
            harness = directory / "switch.c"
            harness.write_text(PREAMBLE + source[start:end] + SCENARIO)
            executable = directory / ("switch.exe" if os.name == "nt" else "switch")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                     compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                                     "-I", str(ROOT / "firmware/golden/lib"),
                                     "-I", str(ROOT / "firmware/platform/xilinx"), str(harness),
                                     "-o", str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                     str(executable), "faults"], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("14 cancellation faults", result.stdout)

    def test_golden_update_success_cancellation_and_safe_retry(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        start = source.index("static void self_update_golden(bool recovery_only)\n{")
        end = source.index("\n}", start) + 2
        self.assertIn("15 cancellation boundaries", self.run_native(
            PREAMBLE + GOLDEN_PREAMBLE + source[start:end] + GOLDEN_SCENARIO))

    def test_system_uses_advisory_status_and_startup_warms_cache(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        start = source.index("static void refresh_multiboot_status(void)\n{")
        refresh = source[start:source.index("\n}", start) + 2]
        self.assertIn("mb_platform_status_ui(", refresh)
        self.assertIn("mb_platform_golden_version_cached(", refresh)
        self.assertNotIn("mb_platform_status(", refresh)
        self.assertNotIn("mb_platform_flash(", refresh)
        self.assertNotIn("mb_golden_installed_version(", refresh)
        startup = source[source.index("int main(void)"):]
        warm = startup.index("refresh_multiboot_status();")
        self.assertLess(startup.index("service_supervisor_menu();"), warm)
        self.assertLess(warm, startup.index("for (;;)"))
        start = source.index("static void service_system_page(void)\n{")
        entry = source[start:source.index("\n}", start) + 2]
        self.assertIn("refresh_multiboot_status();", entry)
        self.assertNotIn("mb_slot_verify(", entry)
        default = source[source.index("action.kind == MT_MENU_ACTION_SET_BOOT_DEFAULT"):]
        self.assertLess(default.index("Checking startup firmware; please wait"),
                        default.index("mb_platform_set_default("))


if __name__ == "__main__":
    unittest.main()
