# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production RTC I2C transactions and cached System/FAT clock state."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "scripts/run_native_tool.py"


def production_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}", start) + len("\n}")
    return source[start:end]


PREAMBLE = r'''
#include "multitini_clock.h"
#include "multitini_fatfs_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(v) do { if (!(v)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #v); exit(1); \
} } while (0)
#define XST_SUCCESS 0
#define MT_I2C_BUSY_POLLS 8U
typedef struct { unsigned unused; } XIicPs;
static XIicPs i2c_instance;
static bool i2c_ready, board_clock_available, board_clock_valid;
static mt_clock_time_t board_clock;
static uint8_t rtc_registers[9], pointer_reg, last_write[10];
static unsigned send_calls, recv_calls, busy_calls, write_calls, transaction;
static unsigned fail_send_call, fail_recv_call, busy_transaction;
static bool replace_written_time;
static mt_clock_time_t readback_override;
static const mt_clock_time_t initial_time = {2026U, 9U, 18U, 12U, 34U, 56U};

static int XIicPs_MasterSendPolled(XIicPs *instance, uint8_t *data,
                                  int length, uint8_t address)
{
    CHECK(instance == &i2c_instance && address == MT_PCF8563_I2C_ADDRESS);
    CHECK(length == 1 || length == 10);
    ++transaction;
    ++send_calls;
    if (send_calls == fail_send_call) return -1;
    CHECK(data[0] == 0U); /* Both snapshots and writes start at CTRL1. */
    pointer_reg = data[0];
    if (length == 10) {
        ++write_calls;
        memcpy(last_write, data, sizeof(last_write));
        memcpy(rtc_registers, data + 1U, sizeof(rtc_registers));
        if (replace_written_time)
            CHECK(mt_clock_encode(&readback_override, rtc_registers + 2U));
    }
    return XST_SUCCESS;
}

static int XIicPs_MasterRecvPolled(XIicPs *instance, uint8_t *data,
                                  int length, uint8_t address)
{
    CHECK(instance == &i2c_instance && address == MT_PCF8563_I2C_ADDRESS);
    CHECK(pointer_reg == 0U && (length == 2 || length == 9));
    ++transaction;
    ++recv_calls;
    if (recv_calls == fail_recv_call) return -1;
    memcpy(data, rtc_registers, (size_t)length);
    return XST_SUCCESS;
}

static int XIicPs_BusIsBusy(XIicPs *instance)
{
    CHECK(instance == &i2c_instance);
    ++busy_calls;
    return transaction == busy_transaction ? 1 : 0;
}
'''


SCENARIOS = r'''
static uint32_t fat_time(const mt_clock_time_t *time)
{
    return ((uint32_t)(time->year - 1980U) << 25U) |
        ((uint32_t)time->month << 21U) | ((uint32_t)time->day << 16U) |
        ((uint32_t)time->hour << 11U) | ((uint32_t)time->minute << 5U) |
        (uint32_t)(time->second / 2U);
}

static bool same_time(const mt_clock_time_t *a, const mt_clock_time_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day &&
           a->hour == b->hour && a->minute == b->minute && a->second == b->second;
}

static void reset_io_counts(void)
{
    send_calls = recv_calls = busy_calls = write_calls = transaction = 0U;
    fail_send_call = fail_recv_call = busy_transaction = 0U;
}

static void reset_model(void)
{
    memset(rtc_registers, 0, sizeof(rtc_registers));
    memset(last_write, 0xA5, sizeof(last_write));
    CHECK(mt_clock_encode(&initial_time, rtc_registers + 2U));
    i2c_ready = true;
    board_clock_available = board_clock_valid = false;
    replace_written_time = false;
    reset_io_counts();
    mt_fatfs_time_reset();
    CHECK(refresh_fatfs_time_from_rtc() == 0);
    CHECK(board_clock_available && board_clock_valid);
    CHECK(same_time(&board_clock, &initial_time));
    CHECK(mt_fatfs_time_current() == fat_time(&initial_time));
    reset_io_counts();
}

static void test_read_failures(void)
{
    for (unsigned failure = 0U; failure < 4U; ++failure) {
        reset_model();
        if (failure == 0U) fail_send_call = 1U;
        if (failure == 1U) fail_recv_call = 1U;
        if (failure == 2U) busy_transaction = 1U;
        if (failure == 3U) busy_transaction = 2U;
        CHECK(refresh_fatfs_time_from_rtc() == -1);
        CHECK(!board_clock_available && !board_clock_valid);
        CHECK(mt_fatfs_time_current() == fat_time(&initial_time));
        CHECK(write_calls == 0U);
        if (failure >= 2U)
            CHECK(busy_calls >= MT_I2C_BUSY_POLLS && busy_calls <= MT_I2C_BUSY_POLLS + 1U);
    }
}

static void test_invalid_clock(void)
{
    for (unsigned failure = 0U; failure < 4U; ++failure) {
        reset_model();
        if (failure == 0U) rtc_registers[0] |= UINT8_C(0x20); /* STOP */
        if (failure == 1U) rtc_registers[2] |= UINT8_C(0x80); /* voltage low */
        if (failure == 2U) rtc_registers[3] = UINT8_C(0x6A); /* bad minute BCD */
        if (failure == 3U) {
            rtc_registers[5] = UINT8_C(0x31);
            rtc_registers[7] = UINT8_C(0x02); /* February 31 */
        }
        CHECK(refresh_fatfs_time_from_rtc() == -2);
        CHECK(board_clock_available && !board_clock_valid);
        CHECK(mt_fatfs_time_current() == fat_time(&initial_time));
        CHECK(write_calls == 0U);
    }
}

static void test_write_rejections(void)
{
    const mt_clock_time_t invalid = {2026U, 2U, 30U, 0U, 0U, 0U};
    reset_model();
    i2c_ready = false;
    CHECK(!set_board_clock(&initial_time));
    CHECK(send_calls == 0U && recv_calls == 0U);
    i2c_ready = true;
    CHECK(!set_board_clock(NULL) && !set_board_clock(&invalid));
    CHECK(send_calls == 0U && recv_calls == 0U);

    for (unsigned failure = 0U; failure < 7U; ++failure) {
        reset_model();
        if (failure == 0U) fail_send_call = 1U; /* read controls: pointer */
        if (failure == 1U) fail_recv_call = 1U; /* read controls: data */
        if (failure == 2U) fail_send_call = 2U; /* actual time write */
        if (failure == 3U) busy_transaction = 3U; /* write never goes idle */
        if (failure == 4U) fail_send_call = 3U; /* readback: pointer */
        if (failure == 5U) fail_recv_call = 2U; /* readback: data */
        if (failure == 6U) busy_transaction = 5U; /* readback never goes idle */
        CHECK(!set_board_clock(&initial_time));
        if (failure < 3U) CHECK(write_calls == 0U);
        if (failure >= 2U) CHECK(!board_clock_valid);
        if (failure >= 4U) CHECK(!board_clock_available);
        CHECK(mt_fatfs_time_current() == fat_time(&initial_time));
        CHECK(busy_calls <= MT_I2C_BUSY_POLLS + 4U);
    }
}

static void test_exact_write(void)
{
    const mt_clock_time_t requested = {2028U, 2U, 29U, 23U, 59U, 58U};
    reset_model();
    rtc_registers[0] = UINT8_C(0x20); /* editor can restart a stopped clock */
    rtc_registers[1] = UINT8_C(0x12); /* preserve alarm/timer controls */
    rtc_registers[2] |= UINT8_C(0x80);
    CHECK(set_board_clock(&requested));
    CHECK(write_calls == 1U && send_calls == 3U && recv_calls == 2U);
    CHECK(last_write[0] == 0U && last_write[1] == 0U && last_write[2] == UINT8_C(0x12));
    CHECK((last_write[3] & UINT8_C(0x80)) == 0U);
    CHECK(board_clock_available && board_clock_valid && same_time(&board_clock, &requested));
    CHECK(mt_fatfs_time_current() == fat_time(&requested));
}

static void test_one_second_readback(void)
{
    static const mt_clock_time_t requested[] = {
        {2028U, 2U, 28U, 23U, 59U, 59U},
        {2026U, 12U, 31U, 23U, 59U, 59U},
        {2026U, 9U, 18U, 12U, 34U, 56U}
    };
    static const mt_clock_time_t actual[] = {
        {2028U, 2U, 29U, 0U, 0U, 0U},
        {2027U, 1U, 1U, 0U, 0U, 0U},
        {2026U, 9U, 18U, 12U, 34U, 57U}
    };
    for (size_t i = 0U; i < sizeof(requested) / sizeof(requested[0]); ++i) {
        reset_model();
        replace_written_time = true;
        readback_override = actual[i];
        CHECK(set_board_clock(&requested[i]));
        CHECK(same_time(&board_clock, &actual[i]));
        CHECK(mt_fatfs_time_current() == fat_time(&actual[i]));
    }
}

static void test_wrong_readback(void)
{
    static const mt_clock_time_t actual[] = {
        {2026U, 9U, 18U, 12U, 34U, 55U}, /* backwards */
        {2026U, 9U, 18U, 12U, 34U, 58U}, /* two seconds */
        {2025U, 9U, 18U, 12U, 34U, 56U}  /* valid but wrong year */
    };
    for (size_t i = 0U; i < sizeof(actual) / sizeof(actual[0]); ++i) {
        reset_model();
        replace_written_time = true;
        readback_override = actual[i];
        CHECK(!set_board_clock(&initial_time));
        CHECK(write_calls == 1U && board_clock_available && board_clock_valid);
        CHECK(same_time(&board_clock, &actual[i]));
        CHECK(mt_fatfs_time_current() == fat_time(&actual[i]));
    }
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (strcmp(argv[1], "read-failures") == 0) test_read_failures();
    else if (strcmp(argv[1], "invalid-clock") == 0) test_invalid_clock();
    else if (strcmp(argv[1], "write-failures") == 0) test_write_rejections();
    else if (strcmp(argv[1], "exact-write") == 0) test_exact_write();
    else if (strcmp(argv[1], "one-second") == 0) test_one_second_readback();
    else if (strcmp(argv[1], "wrong-readback") == 0) test_wrong_readback();
    else CHECK(false);
    printf("PASS: System RTC runtime %s\n", argv[1]);
    return 0;
}
'''


class SystemClockRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if compiler is None and os.name == "nt":
            candidate = Path("C:/msys64/ucrt64/bin/gcc.exe")
            if candidate.is_file():
                compiler = str(candidate)
        if compiler is None:
            raise unittest.SkipTest("A host C compiler is required for RTC runtime checks")
        cls.temporary = tempfile.TemporaryDirectory(prefix="multitini-rtc-runtime-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        signatures = (
            "static int wait_i2c_idle(",
            "static int board_i2c_read(",
            "static int refresh_fatfs_time_from_rtc(",
            "static bool set_board_clock(",
        )
        harness = directory / "system_clock_runtime.c"
        harness.write_text(PREAMBLE + "\n" + "\n\n".join(
            production_function(source, signature) for signature in signatures
        ) + "\n" + SCENARIOS)
        cls.executable = directory / ("system_clock_runtime.exe" if os.name == "nt" else "system_clock_runtime")
        result = subprocess.run([
            sys.executable, str(WRAPPER), "--", compiler, "-std=c11",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wconversion",
            "-Wshadow", "-Wstrict-prototypes", "-I", str(ROOT / "firmware/include"),
            str(harness), str(ROOT / "firmware/common/clock.c"),
            str(ROOT / "firmware/common/fatfs_time.c"), "-o", str(cls.executable),
        ], capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_scenario(self, scenario):
        result = subprocess.run([
            sys.executable, str(WRAPPER), "--", str(self.executable), scenario,
        ], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS: System RTC runtime {scenario}", result.stdout)

    def test_snapshot_i2c_failures_retain_last_good_fat_time(self):
        self.run_scenario("read-failures")

    def test_stopped_low_voltage_bad_bcd_and_bad_date_clocks(self):
        self.run_scenario("invalid-clock")

    def test_invalid_input_write_failure_and_readback_failure(self):
        self.run_scenario("write-failures")

    def test_exact_write_clears_stop_preserves_controls_and_refreshes_cache(self):
        self.run_scenario("exact-write")

    def test_one_second_readback_accepts_day_and_year_rollover(self):
        self.run_scenario("one-second")

    def test_wrong_but_valid_readback_is_not_reported_as_save_success(self):
        self.run_scenario("wrong-readback")


if __name__ == "__main__":
    unittest.main()
