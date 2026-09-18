# SPDX-License-Identifier: GPL-3.0-only
"""Source checks for the Appletini-derived exFAT BSP configuration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ExfatSupportTests(unittest.TestCase):
    def test_vitis_enables_and_verifies_exfat_before_platform_build(self):
        source = (ROOT / "scripts" / "create_vitis_workspace.py").read_text(
            encoding="utf-8"
        )
        compile(source, "scripts/create_vitis_workspace.py", "exec")
        configure = source.index(
            'domain.set_config(option="lib", param="XILFFS_enable_exfat"'
        )
        regenerate = source.index("domain.regenerate()", configure)
        synchronize = source.index("enable_fatfs_exfat(domain_bsp)", regenerate)
        patch = source.index("patch_fatfs_media_validation(domain_bsp)", synchronize)
        timestamp = source.index("enable_fatfs_timestamp_hook(domain_bsp)", patch)
        verify = source.index("verify_fatfs_exfat_configuration(domain_bsp)",
                              timestamp)
        build = source.index('platform.build()', verify)
        self.assertLess(configure, regenerate)
        self.assertLess(regenerate, synchronize)
        self.assertLess(synchronize, patch)
        self.assertLess(patch, timestamp)
        self.assertLess(timestamp, verify)
        self.assertLess(verify, build)
        self.assertIn('value="true"', source[configure:regenerate])

    def test_generated_config_and_media_guards_are_checked(self):
        source = (ROOT / "scripts" / "create_vitis_workspace.py").read_text(
            encoding="utf-8"
        )
        for marker in (
            "#define FILE_SYSTEM_FS_EXFAT",
            "#define FILE_SYSTEM_USE_LFN 1",
            "XILFFS_enable_exfat:BOOL=ON",
            "value: 'true'",
            "#define MIN_EXFAT\\t0x00000100",
            "nclst < MIN_EXFAT || nclst > MAX_EXFAT",
            "fasize >= 0x200000U",
            "si < dj.dir[XDIR_NumLabel] && si < 11U",
            "__attribute__((weak)) DWORD multitini_fatfs_get_fattime (void)",
            "return multitini_fatfs_get_fattime();",
        ):
            self.assertIn(marker, source)

    def test_reused_platform_requires_the_exfat_stamp(self):
        source = (ROOT / "scripts" / "create_vitis_workspace.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("exfat=1", source)
        self.assertIn("media-validation=r0.16-patch2", source)
        self.assertIn("fat-time-hook=rtc-cache", source)
        self.assertIn(
            "stamp.read_text(encoding=\"utf-8\") != XILFFS_READY_DESCRIPTION",
            source,
        )
        build = source.index('require_success(platform.build(), "platform build")')
        stamp = source.index("mark_xilffs_ready(xpfm)", build)
        self.assertLess(build, stamp)

    def test_firmware_caches_the_pcf8563_for_fatfs_writes(self):
        main = (ROOT / "firmware" / "platform" / "xilinx" /
                "main.c").read_text(encoding="utf-8")
        hook = (ROOT / "firmware" / "platform" / "xilinx" /
                "fatfs_time_hook.c").read_text(encoding="utf-8")
        common = (ROOT / "firmware" / "common" /
                  "fatfs_time.c").read_text(encoding="utf-8")
        self.assertIn("MT_PCF8563_I2C_ADDRESS", main)
        self.assertIn("service_fatfs_time();", main)
        self.assertIn("mt_fatfs_time_update_from_pcf8563(registers + 2U)", main)
        self.assertIn("DWORD multitini_fatfs_get_fattime(void)", hook)
        self.assertIn("mt_fatfs_time_current()", hook)
        self.assertIn("cached_fattime_valid ? cached_fattime", common)

    def test_fatfs_sizes_are_not_narrowed_before_checking(self):
        source = (ROOT / "firmware" / "platform" / "xilinx" /
                  "sd_storage.c").read_text(encoding="utf-8")
        self.assertIn("fsize_to_size_saturated", source)
        self.assertIn("backend_size > (FSIZE_t)capacity", source)
        self.assertIn("info.fsize > (FSIZE_t)SIZE_MAX", source)
        self.assertNotIn("result(MT_SD_TOO_LARGE, FR_OK, (size_t)backend_size)",
                         source)
        self.assertIn(
            "result(MT_SD_SIZE_MISMATCH, FR_OK,\n"
            "                             fsize_to_size_saturated(backend_size))",
            source,
        )


if __name__ == "__main__":
    unittest.main()
