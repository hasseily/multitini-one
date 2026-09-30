#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from hasseily/appletini-one 1fd4f371e8f4ef08bc63875550564590609fcfeb.
"""Run the production USB1 host glue against a native USB3300/MMIO model."""

from pathlib import Path
import subprocess
import os
import shutil
import sys
import textwrap


def find_native_c_compiler():
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if compiler is None and os.name == "nt":
        candidate = Path("C:/msys64/ucrt64/bin/gcc.exe")
        if candidate.is_file():
            compiler = str(candidate)
    return compiler


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/usb1_phy_startup_test"
FRONTEND = ROOT / "firmware/platform/xilinx"


def main() -> int:
    compiler = find_native_c_compiler()
    if compiler is None:
        raise RuntimeError("A native C compiler is required for USB1 PHY tests")
    BUILD.mkdir(parents=True, exist_ok=True)
    stubs = {
        "sleep.h": "void usleep(unsigned int delay);\n",
        "xil_types.h": "#include <stdint.h>\ntypedef intptr_t INTPTR;\ntypedef uintptr_t UINTPTR;\n",
        "xil_cache.h": "#include <stdint.h>\nvoid Xil_DCacheInvalidateRange(intptr_t addr, uint32_t size);\nvoid Xil_DCacheFlushRange(intptr_t addr, uint32_t size);\n",
        "xparameters.h": "#define XPAR_XUSBPS_1_BASEADDR 0xE0003000U\n",
        "xil_printf.h": "int xil_printf(const char *format, ...);\n",
        "xil_io.h": "#include <stdint.h>\nuint32_t Xil_In32(uintptr_t addr);\nvoid Xil_Out32(uintptr_t addr, uint32_t value);\n",
        "usbh_core.h": """
            #ifndef TEST_USBH_CORE_H
            #define TEST_USBH_CORE_H
            #include <stdint.h>
            struct usbh_bus {
                struct { uintptr_t reg_base; } hcd;
                void *hub_mq;
                void *hub_sem;
            };
            extern struct usbh_bus g_usbhost_bus[1];
            #define USB_ERR_IO 12
            #define USB_SPEED_UNKNOWN 0
            #define USB_SPEED_LOW 1
            #define USB_SPEED_FULL 2
            #define USB_SPEED_HIGH 3
            void USBH_IRQHandler(uint8_t busid);
            #endif
        """,
        "xusbps_hw.h": """
            #ifndef TEST_XUSBPS_HW_H
            #define TEST_XUSBPS_HW_H
            #include <stdint.h>
            #define XUSBPS_CMD_OFFSET 0x140U
            #define XUSBPS_ISR_OFFSET 0x144U
            #define XUSBPS_IER_OFFSET 0x148U
            #define XUSBPS_ULPIVIEW_OFFSET 0x170U
            #define XUSBPS_PORTSCR1_OFFSET 0x184U
            #define XUSBPS_OTGCSR_OFFSET 0x1A4U
            #define XUSBPS_MODE_OFFSET 0x1A8U
            #define XUSBPS_CMD_RS_MASK 0x01U
            #define XUSBPS_CMD_RST_MASK 0x02U
            #define XUSBPS_IXR_HCH_MASK 0x1000U
            #define XUSBPS_IXR_ALL 0xFFFFFFFFU
            #define XUSBPS_MODE_CM_MASK 0x03U
            #define XUSBPS_MODE_CM_HOST_MASK 0x03U
            #define XUSBPS_PORTSCR_CCS_MASK 0x01U
            #define XUSBPS_PORTSCR_CSC_MASK 0x02U
            #define XUSBPS_PORTSCR_PE_MASK 0x04U
            #define XUSBPS_PORTSCR_PEC_MASK 0x08U
            #define XUSBPS_PORTSCR_OCC_MASK 0x20U
            #define XUSBPS_PORTSCR_PP_MASK 0x1000U
            #define XUSBPS_PORTSCR_PHCD_MASK 0x00800000U
            #define XUSBPS_PORTSCR_PSPD_MASK 0x0C000000U
            void XUsbPs_ResetHw(uintptr_t base);
            #endif
        """,
    }
    for name, source in stubs.items():
        (BUILD / name).write_text(textwrap.dedent(source), encoding="utf-8")
    hid = (FRONTEND / "usb_hid_service.c").read_text(encoding="utf-8")
    start = hid.index("int usb_hid_service_start(void)")
    end = hid.index("void usb_hid_service_set_active_core(", start)
    (BUILD / "usb1_hid_lifecycle.inc").write_text(hid[start:end], encoding="utf-8")
    executable = BUILD / "usb1_phy_startup.exe"
    subprocess.run([
        sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
        str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror", *(["-static"] if os.name == "nt" else []),
        "-DCONFIG_USB_DCACHE_ENABLE", "-I", str(BUILD), "-I", str(FRONTEND),
        str(ROOT / "tests/host/test_usb1_phy_startup.c"),
        str(FRONTEND / "cherryusb_zynq_hc.c"), "-o", str(executable),
    ], check=True, cwd=ROOT, timeout=60)
    subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", str(executable)], check=True, cwd=ROOT, timeout=15)

    hub = (FRONTEND / "cherryusb_usbh_hub_poll.c").read_text(encoding="utf-8")
    start = hub.index("int usbh_hub_initialize(")
    end = hub.index("\n}", start) + len("\n}")
    (BUILD / "usb1_hub_initialize.inc").write_text(hub[start:end], encoding="utf-8")
    hub_executable = BUILD / "usb1_hub_init.exe"
    subprocess.run([
        sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
        str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        *(["-static"] if os.name == "nt" else []), "-I", str(BUILD),
        str(ROOT / "tests/host/test_usb1_hub_init.c"), "-o", str(hub_executable),
    ], check=True, cwd=ROOT, timeout=60)
    subprocess.run([
        sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
        str(hub_executable),
    ], check=True, cwd=ROOT, timeout=15)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
