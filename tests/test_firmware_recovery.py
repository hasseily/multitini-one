# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import re
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FirmwareRecoveryTests(unittest.TestCase):
    def test_reboot_command_clears_multiboot_before_ps_reset(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        body = source.split("static void reboot_to_golden(bool recovery_only)", 1)[1]
        body = body.split("\n}\n", 1)[0]

        media_flush = body.index("set_core_reset_request(true)")
        usb_power_stop = body.index("usb_hid_service_stop()")
        usb_power_failure = body.index("USB1 VBUS shutdown failed")
        drain = body.index("wait_uart0_transmit_idle()")
        clear = body.index("Xil_Out32(MT_DEVCFG_MULTIBOOT, 0U)")
        barrier = body.index("DATA_SYNC", clear)
        readback = body.index("Xil_In32(MT_DEVCFG_MULTIBOOT)")
        unlock = body.index("Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY)")
        reset = body.index(
            "Xil_Out32(MT_PS_RESET_CONTROL, MT_PS_SOFTWARE_RESET)"
        )
        self.assertLess(media_flush, drain)
        self.assertLess(drain, clear)
        self.assertLess(clear, barrier)
        self.assertLess(barrier, readback)
        self.assertLess(readback, usb_power_stop)
        self.assertLess(usb_power_stop, usb_power_failure)
        self.assertLess(usb_power_failure, unlock)
        self.assertLess(unlock, reset)

        reset_wrapper = source.split(
            "static int set_core_reset_request(bool asserted)", 1
        )[1].split("\n}\n", 1)[0]
        self.assertIn("c64_d64_service_prepare_reset()", reset_wrapper)
        self.assertIn("gameboy_content_service_prepare_reset()", reset_wrapper)

    def test_recovery_command_is_explicit_and_core_reset_is_preserved(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        self.assertIn('strcmp(extended, "reboot") == 0', source)
        self.assertIn("reboot_to_golden(recovery_only);", source)
        self.assertIn("reboot_to_golden(false);", source)
        self.assertIn("case 'r':", source)
        self.assertIn("MT_COMMAND_CORE_RESET", source)
        self.assertIn(":reboot return to golden updater", source)

    def test_reboot_retries_unverified_usb1_power_shutdown(self):
        service = (
            ROOT / "firmware/platform/xilinx/usb_hid_service.c"
        ).read_text()
        stop = service.split("int usb_hid_service_stop(void)", 1)[1]
        stop = stop.split("\n}\n", 1)[0]

        self.assertIn("g_power_shutdown_required", service)
        self.assertIn(
            "g_started != 0U || g_power_shutdown_required != 0U", stop
        )
        failure = stop.rindex("g_power_shutdown_required = 1U")
        returned = stop.index("return power_result")
        self.assertLess(failure, returned)

    def test_recovery_console_remains_available_when_shell_probe_fails(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        probe_failure = source.index("if (link_status != MT_LINK_OK)")
        recovery_console = source.index("recovery_console();", probe_failure)
        i2c_initialization = source.index("status = initialize_i2c()")
        self.assertLess(probe_failure, recovery_console)
        self.assertLess(recovery_console, i2c_initialization)

        console = source.split("static void recovery_console(void)", 1)[1]
        console = console.split("\n}\n", 1)[0]
        self.assertIn("handle_extended_command(true)", console)
        self.assertNotIn("shell_read(", console)
        self.assertNotIn("mt_core_link_", console)

    def test_recovery_executes_without_runtime_or_uninitialized_services(self):
        """Execute the actual parser/reset functions against unavailable services."""
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        functions = []
        for name in ("read_extended_command", "wait_uart0_transmit_idle", "reboot_to_golden"):
            match = re.search(r"^static (?:bool|void) " + name + r"\(.*?^}\n",
                              source, re.MULTILINE | re.DOTALL)
            self.assertIsNotNone(match, name)
            functions.append(match.group())
        definitions = "\n".join(line for line in source.splitlines()
                                if line.startswith(("#define MT_DEVCFG_", "#define MT_SLCR_",
                                                    "#define MT_PS_")))
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#define MT_UART_DRAIN_POLLS 4U
#define XPAR_XUARTPS_0_BASEADDR 0xe0000000U
#define XUARTPS_SR_OFFSET 0x2cU
#define XUARTPS_SR_TXEMPTY 8U
#define XUARTPS_SR_TACTIVE 0x800U
#define MT_SD_OK 0
#define DATA_SYNC do { ++barriers; } while (0)
typedef struct { int status; unsigned backend_status; } mt_sd_result_t;
static int runtime_calls, reset_calls, usb_calls, sd_calls, ui_calls;
static int reset_failure, usb_failure, drain_failure, readback_failure;
static int writes, barriers;
static jmp_buf reset_jump;
static const char *input;
static unsigned input_position, polls;
static int uart0_try_read_byte(uint8_t *value) {
    if (++polls % 3U != 0U) return 0; /* Characters arrive separately. */
    assert(input[input_position]);
    *value = (uint8_t)input[input_position++];
    return 1;
}
static void service_runtime(void) { ++runtime_calls; }
static void xil_printf(const char *format, ...) { (void)format; }
static int set_core_reset_request(bool asserted) {
    ++reset_calls; return asserted ? reset_failure : 0;
}
static void set_supervisor_error(const char *message) { (void)message; ++ui_calls; }
static int usb_hid_service_stop(void) { ++usb_calls; return usb_failure; }
static mt_sd_result_t mt_sd_storage_unmount(void) {
    ++sd_calls; return (mt_sd_result_t){0, 0};
}
static const char *mt_sd_status_name(int status) { (void)status; return "stub"; }
static uint32_t XUartPs_ReadReg(uint32_t base, uint32_t offset) {
    assert(base == XPAR_XUARTPS_0_BASEADDR && offset == XUARTPS_SR_OFFSET);
    return drain_failure ? XUARTPS_SR_TACTIVE : XUARTPS_SR_TXEMPTY;
}
'''
        harness += definitions + r'''
static uint32_t Xil_In32(uint32_t address) {
    assert(address == MT_DEVCFG_MULTIBOOT);
    return readback_failure ? 1U : 0U;
}
static void Xil_Out32(uint32_t address, uint32_t value) {
    ++writes;
    if (address == MT_DEVCFG_MULTIBOOT) { assert(value == 0U); return; }
    if (address == MT_SLCR_UNLOCK) { assert(value == MT_SLCR_UNLOCK_KEY); return; }
    if (address == MT_PS_RESET_CONTROL) {
        assert(value == MT_PS_SOFTWARE_RESET && writes == 3 && barriers >= 2);
        longjmp(reset_jump, 1);
    }
    assert(address == MT_SLCR_LOCK && value == MT_SLCR_LOCK_KEY);
}
'''
        harness += "\n".join(functions) + r'''
static void clear(void) {
    runtime_calls = reset_calls = usb_calls = sd_calls = ui_calls = 0;
    reset_failure = usb_failure = drain_failure = readback_failure = 0;
    writes = barriers = 0; input_position = polls = 0;
}
static void expect_no_services(void) {
    assert(!runtime_calls && !reset_calls && !usb_calls && !sd_calls && !ui_calls);
}
int main(void) {
    char command[8];
    clear(); input = "reboot\r";
    assert(read_extended_command(command, sizeof command, true));
    assert(!strcmp(command, "reboot") && polls > 7U); expect_no_services();
    clear(); input = "too-long-command\n";
    assert(!read_extended_command(command, sizeof command, true));
    assert(strlen(command) == sizeof command - 1U && input_position == strlen(input));
    expect_no_services();
    clear(); input = "dir\n";
    assert(read_extended_command(command, sizeof command, false) && runtime_calls > 0);
    clear(); reset_failure = usb_failure = 1; /* Recovery cannot depend on either. */
    if (setjmp(reset_jump) == 0) { reboot_to_golden(true); assert(!"reset missing"); }
    expect_no_services(); assert(writes == 3);
    clear(); drain_failure = 1; reboot_to_golden(true);
    expect_no_services(); assert(writes == 0);
    clear(); readback_failure = 1; reboot_to_golden(true);
    expect_no_services(); assert(writes == 1);
    clear(); reset_failure = 1; reboot_to_golden(false);
    assert(reset_calls == 1 && ui_calls == 1 && !usb_calls && !sd_calls && !writes);
    clear(); usb_failure = 1; reboot_to_golden(false);
    assert(reset_calls == 2 && usb_calls == 1 && !sd_calls && writes == 1);
    clear();
    if (setjmp(reset_jump) == 0) { reboot_to_golden(false); assert(!"reset missing"); }
    assert(reset_calls == 1 && usb_calls == 1 && sd_calls == 1 && writes == 3);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test_source = path / "recovery.c"
            test_source.write_text(harness)
            executable = path / ("recovery.exe" if os.name == "nt" else "recovery")
            guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
            for command in ([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                             str(test_source), "-o", str(executable)], [str(executable)]):
                result = subprocess.run(guard + command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_boot_diagnostics_precede_worker_and_shell_access(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        main = source.split("int main(void)", 1)[1]
        banner = main.index("Multitini One diagnostic firmware")
        status = main.index('print_read_only_diagnostics("before CPU1 release");')
        worker = main.index("mt_video_worker_start()")
        after = main.index('print_read_only_diagnostics("after CPU1 release");')
        probe = main.index("mt_core_link_probe(&shell_link)")
        self.assertLess(banner, status)
        self.assertLess(main.index("Xil_SetTlbAttributes(MT_SHELL_BASE_ADDRESS, DEVICE_MEMORY)"), status)
        self.assertLess(status, worker)
        self.assertLess(worker, after)
        self.assertLess(after, probe)
        self.assertGreater(main.index("probe expected: magic="), probe)
        failure = main.split("if (link_status != MT_LINK_OK)", 1)[1].split("\n    }", 1)[0]
        self.assertNotIn("shell_read(", failure)
        callback = source.split("static uint32_t boot_probe_read32", 1)[1].split("\n}\n", 1)[0]
        self.assertEqual(callback.count("Xil_In32("), 1)
        self.assertLess(callback.index("Xil_In32("), callback.index("xil_printf("))
        self.assertIn("dense text probe: ui_status=", main)
        diagnostics = source.split("static void print_boot_ps_status(void)", 1)[1]
        diagnostics = diagnostics.split("\n}\n", 1)[0]
        self.assertNotIn("shell_read(", diagnostics)
        self.assertNotIn("Xil_Out32(", diagnostics)
        for register in ("MT_SLCR_FPGA_RESET", "MT_SLCR_LEVEL_SHIFTER",
                         "MT_DEVCFG_MULTIBOOT", "XDCFG_CTRL_OFFSET",
                         "XDCFG_STATUS_OFFSET", "XDCFG_INT_STS_OFFSET"):
            self.assertIn(register, diagnostics)

    def test_diagnostic_command_reads_only_fixed_registers_and_worker_status(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        self.assertIn('strcmp(extended, "diag") == 0', source)
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        functions = []
        for name in ("print_boot_ps_status", "print_read_only_diagnostics"):
            match = re.search(r"^static void " + name + r"\([^;]*?\)\n\{.*?^}\n",
                              source, re.MULTILINE | re.DOTALL)
            self.assertIsNotNone(match, name)
            functions.append(match.group())
        definitions = "\n".join(line for line in source.splitlines()
                                if line.startswith(("#define MT_DEVCFG_", "#define MT_SLCR_",
                                                    "#define MT_SHELL_BASE_ADDRESS")))
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "xparameters.h"
#include "xdevcfg_hw.h"
#include "multitini_regs.h"
#include "video_worker.h"
static unsigned ps_reads, shell_reads, magic_reads, worker_reads;
static void xil_printf(const char *format, ...) { (void)format; }
mt_video_worker_status_t mt_video_worker_status(void) {
    ++worker_reads; return (mt_video_worker_status_t){0};
}
''' + definitions + r'''
static uint32_t Xil_In32(uintptr_t address) {
    static const uint32_t ps[] = {
        0xF8000240U, 0xF8000900U, 0xF800702CU, 0xF8007000U,
        0xF8007014U, 0xF800700CU, 0xF8000170U, 0xF8000190U,
        0xF8000108U, 0xF8000100U, 0xF8000104U
    };
    static const uint32_t pl[] = {0x40000000U, 0x40000004U, 0x40000008U,
        0x4000000CU, 0x40000010U, 0x4000001CU, 0x400000B0U};
    for (unsigned i = 0; i < sizeof ps / sizeof ps[0]; ++i) {
        if (address == ps[i]) { assert(!shell_reads); ++ps_reads; return 0; }
    }
    for (unsigned i = 0; i < sizeof pl / sizeof pl[0]; ++i) {
        if (address == pl[i]) {
            assert(ps_reads == 11U); ++shell_reads;
            if (i == 0U) { ++magic_reads; return 0x4D5C383DU; }
            return 0;
        }
    }
    assert(!"Unexpected peripheral address"); return 0;
}
''' + "\n".join(functions) + r'''
int main(void) {
    for (unsigned i = 0; i < 2U; ++i) {
        ps_reads = shell_reads = magic_reads = worker_reads = 0;
        print_read_only_diagnostics(i ? "command" : "before CPU1 release");
        assert(ps_reads == 11U && shell_reads == 11U && magic_reads == 5U && worker_reads == 1U);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test_source = path / "diagnostic.c"
            test_source.write_text(harness)
            executable = path / ("diagnostic.exe" if os.name == "nt" else "diagnostic")
            guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
            for command in ([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                             "-I", str(ROOT / "tests/xilinx_stubs"),
                             "-I", str(ROOT / "firmware/include"),
                             "-I", str(ROOT / "firmware/platform/xilinx"),
                             str(test_source), "-o", str(executable)], [str(executable)]):
                result = subprocess.run(guard + command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_xil_printf_never_uses_long_length_formats(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        long_format = re.compile(r"%[-+ #0-9.*]*l[diuoxXfFeEgGaAcspn]")
        self.assertIsNone(long_format.search(source))

    def test_audio_commands_separate_live_tone_and_pop_safe_mute(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        self.assertIn("case 'a': set_audio(false, false);", source)
        self.assertIn("case 't': set_audio(false, true);", source)
        self.assertIn("MT_CONTROL_AUDIO_TEST_ENABLE", source)
        self.assertIn(
            "(shell_read(MT_REG_STATUS) & MT_STATUS_CORE_READY) != 0U",
            source,
        )

        body = source.split(
            "static void set_audio(bool mute, bool test_tone)", 1
        )[1].split("\n}\n", 1)[0]
        pl_unmute = body.index(
            "command(MT_COMMAND_SET_AUDIO, test_tone ? 1U : 0U, 0U)"
        )
        settle = body.index("board_delay_us(NULL, 50U)", pl_unmute)
        dac_unmute = body.index("mt_ak4493_set_mute(&dac_bus, false)")
        self.assertLess(pl_unmute, settle)
        self.assertLess(settle, dac_unmute)

    def test_boot_stays_in_supervisor_until_explicit_autoboot(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        main = source.split("int main(void)", 1)[1]
        initialization = source.split(
            "static void initialize_supervisor_menu(void)", 1
        )[1].split("\n}\n", 1)[0]

        self.assertNotIn("attempt_c64_sd_boot();", main.split(
            "initialize_supervisor_menu();", 1
        )[0])
        self.assertNotIn("mt_supervisor_set_initial", initialization)
        self.assertIn("supervisor_enter_standby()", initialization)
        self.assertIn("if (persistent_settings.auto_boot)", initialization)
        self.assertIn(
            "mt_supervisor_switch(&supervisor, &startup_auto_selection)",
            initialization,
        )
        self.assertIn(
            'set_supervisor_status("Ready; press F12 to choose a core")',
            initialization,
        )

    def test_c64_upload_is_reset_guarded_and_recovery_excludes_it(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        capacity = re.search(
            r"#define\s+MT_EXTENDED_COMMAND_CAPACITY\s+(\d+)U", source
        )
        self.assertIsNotNone(capacity)
        self.assertGreaterEqual(int(capacity.group(1)), 64)
        self.assertIn('strcmp(arguments[0], "c64rx")', source)
        self.assertIn("requested_size != (uint32_t)sizeof(c64_rom_bundle)", source)

        handler = source.split("static void handle_c64_rx_command", 1)[1]
        handler = handler.split("\n}\n", 1)[0]
        reset = handler.index("set_core_reset_request(true)")
        receive = handler.index("mt_xmodem_receive_1k")
        load = handler.index("mt_c64_load_system_roms")
        self.assertLess(reset, receive)
        self.assertLess(receive, load)
        self.assertIn("leave_core_in_reset();", handler)

        extended = source.split("static void handle_extended_command", 1)[1]
        extended = extended.split("\n}\n", 1)[0]
        self.assertIn("!recovery_only", extended)
        self.assertIn("handle_c64_rx_command(extended)", extended)

    def test_reset_wrapper_records_short_assertions_for_disk_remount(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        wrapper = source.split(
            "static int set_core_reset_request(bool asserted)", 1
        )[1].split("\n}\n", 1)[0]
        command = wrapper.index("result = command(MT_COMMAND_CORE_RESET")
        notify = wrapper.index("c64_d64_service_notify_core_reset();")
        active = wrapper.index("c64_core_reset_active = true;")
        self.assertLess(command, notify)
        self.assertLess(notify, active)
        self.assertIn("result == 0 && asserted && !currently_asserted", wrapper)

    def test_core_switch_quiesces_media_before_muting_or_input_gating(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        body = source.split("static int supervisor_quiesce(void *context)", 1)[1]
        body = body.split("\n}\n", 1)[0]
        reset = body.index("set_core_reset_request(true)")
        mute = body.index("set_audio(true")
        input_gate = body.index("usb_hid_service_set_core_ready(false)")
        self.assertLess(reset, mute)
        self.assertLess(mute, input_gate)

    def test_full_pcap_load_has_clock_queue_timeout_and_isolation_guards(self):
        source = (
            ROOT / "firmware/platform/xilinx/pl_loader.c"
        ).read_text()
        program = source.split("static mt_pl_load_result_t program_fabric", 1)[1]
        program = program.split("\n}\n", 1)[0]
        self.assertIn("enable_pcap_clock();", program)
        self.assertIn("XDCFG_STATUS_DMA_CMD_Q_F_MASK", program)
        self.assertIn("MT_PCAP_LAST_TRANSFER", source)
        self.assertIn("MT_PCAP_TIMEOUT_MS", program)
        initialize = program.index("initialize_fabric()")
        transfer = program.index("XDcfg_Transfer")
        release = program.index("set_fabric_isolation(false)")
        self.assertLess(initialize, transfer)
        self.assertLess(transfer, release)


if __name__ == "__main__":
    unittest.main()
