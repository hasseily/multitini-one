# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FirmwareRecoveryTests(unittest.TestCase):
    def test_reboot_command_clears_multiboot_before_ps_reset(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        body = source.split("static void reboot_to_golden(void)", 1)[1]
        body = body.split("\n}\n", 1)[0]

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
        self.assertLess(usb_power_stop, usb_power_failure)
        self.assertLess(usb_power_failure, drain)
        self.assertLess(drain, clear)
        self.assertLess(clear, barrier)
        self.assertLess(barrier, readback)
        self.assertLess(readback, unlock)
        self.assertLess(unlock, reset)

    def test_recovery_command_is_explicit_and_core_reset_is_preserved(self):
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        self.assertIn('strcmp(extended, "reboot") == 0', source)
        self.assertIn("reboot_to_golden();", source)
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


if __name__ == "__main__":
    unittest.main()
