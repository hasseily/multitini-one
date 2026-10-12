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
#define MT_VIDEO_FRAME_STATUS_QUIESCENT 1U
#define MT_SD_OK 0
#define DATA_SYNC ((void)0)
typedef struct { int status; } sd_result_t;
static struct { bool has_active; int active; } supervisor;
static mb_flash_t flash;
static jmp_buf reset_jump;
static int fault, clear_failure, restore_fault;
static bool core_paused, input_ready, video_suspended, worker_held, usb_started, mounted, request, reset_issued;
static unsigned reset_calls, resume_calls, usb_restarts, remounts, requests, request_clears;
static unsigned status_refreshes, effects_configured, register_writes, uart_waits;
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
        assert(value==MT_PS_SOFTWARE_RESET && request && core_paused && worker_held && !usb_started && !mounted);
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
int mb_slot_verify(const mb_flash_t *f, uint32_t product, mb_image_t *out) {
    assert(f==&flash && product==MB_PRODUCT_APPLETINI && out==NULL);
    return fault==2 ? MB_E_UNAVAILABLE : MB_OK;
}
int mb_platform_golden_offset(uint32_t *offset) {
    *offset=MB_GOLDEN_TRIAL_OFFSET; return fault==3 ? MB_E_UNAVAILABLE : MB_OK;
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
static sd_result_t mt_sd_storage_unmount(void) {
    if (fault==10) return (sd_result_t){1};
    mounted=false; return (sd_result_t){0};
}
static bool mt_video_worker_hold_for_reset(void) { worker_held=true; return fault!=11; }
int mb_platform_request(uint32_t product) {
    assert(product==MB_PRODUCT_APPLETINI && core_paused && worker_held && !usb_started && !mounted);
    ++requests; request=true; return fault==12 ? MB_E_PROGRAM : MB_OK;
}
static int xil_printf(const char *fmt, ...) { (void)fmt; return 0; }
static bool wait_uart0_transmit_idle(void) { ++uart_waits; return fault!=13; }
int mb_platform_clear_request(void) {
    ++request_clears;
    if (clear_failure) return MB_E_PROGRAM;
    request=false; return MB_OK;
}
static sd_result_t mt_sd_storage_mount(void) {
    ++remounts; if (restore_fault==1) return (sd_result_t){1}; mounted=true; return (sd_result_t){0};
}
static bool mt_video_worker_resume(void) {
    ++resume_calls; if (restore_fault==2) return false; video_suspended=worker_held=false; return true;
}
static int usb_hid_service_start(void) {
    ++usb_restarts; if (restore_fault==3) return -1; usb_started=true; return 0;
}
static void configure_selection_effects(const void *active) { assert(active==&supervisor.active); ++effects_configured; }
static void refresh_multiboot_status(void) { ++status_refreshes; }
static void set_supervisor_error(const char *message) { snprintf(error_message,sizeof(error_message),"%s",message); }
'''

SCENARIO = r'''
int main(int argc, char **argv) {
    assert(argc==2); (void)argv;
    for (fault=0; fault<=14; ++fault) {
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
        assert(!reset_issued && !request && !core_paused && input_ready && usb_started && mounted);
        assert(!video_suspended && !worker_held && boot_register==0x123U);
        assert(status_refreshes==1 && strstr(error_message,"Switch cancelled")!=NULL);
        assert(request_clears==(fault>=12 ? 1U : 0U));
        assert(register_writes==(fault==14 ? 2U : 0U));
        assert(usb_restarts==(fault>=9 ? 1U : 0U));
        assert(remounts==(fault>=11 ? 1U : 0U));
        assert(resume_calls==(fault>=6 ? 1U : 0U));
        assert(effects_configured==(fault>=6 ? 1U : 0U));
    }
    /* A failed restore retains core reset, blocks input and skips effects. */
    fault=13;
    for (restore_fault=1; restore_fault<=4; ++restore_fault) {
        core_paused=video_suspended=worker_held=request=reset_issued=false;
        input_ready=usb_started=mounted=true;
        effects_configured=0; boot_register=0x123U; register_writes=0;
        if (!setjmp(reset_jump)) switch_to_appletini();
        assert(!reset_issued && !request && core_paused && !input_ready);
        assert(effects_configured==0 && boot_register==0x123U);
        assert(strstr(error_message,"Switch cancelled")!=NULL);
    }
    /* The original reset state must survive a cancelled switch. */
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
    puts("PASS: production switch success, 14 cancellation faults, four restore failures, held reset and request-clear failure");
    return 0;
}
'''


class MultibootSwitchTests(unittest.TestCase):
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
                                     "-I", str(ROOT / "firmware/golden/lib"), str(harness),
                                     "-o", str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                     str(executable), "faults"], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("14 cancellation faults", result.stdout)


if __name__ == "__main__":
    unittest.main()
