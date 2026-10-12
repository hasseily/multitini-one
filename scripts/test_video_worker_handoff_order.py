#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check publication and hold ordering around the real CPU1 control helpers."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    source = (ROOT / "firmware/platform/xilinx/video_worker.c").read_text()
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    start = source.split("bool mt_video_worker_start(void)", 1)[1].split(
        "mt_video_worker_status_t mt_video_worker_status(void)", 1
    )[0]
    operations = (
        "mailbox->magic = MT_VIDEO_WORKER_MAGIC",
        "memcpy((void *)entry",
        "Xil_DCacheFlushRange",
        "Xil_Out32(MT_CPU1_BOOT_VECTOR, MT_VIDEO_WORKER_ENTRY)",
        "if (!cpu1_release()) return false",
        "wake()",
        "started = true",
    )
    positions = [start.index(operation) for operation in operations]
    assert positions == sorted(positions), "CPU1 release precedes complete code/mailbox/vector publication"
    assert start.index("if (held_for_reset && !mt_video_worker_hold_for_reset()) return false") < start.index(
        "memcpy((void *)entry"
    ), "Partial hold recovery copies code before hardware reset is verified"
    hold = source.split("bool mt_video_worker_hold_for_reset(void)", 1)[1]
    assert hold.index("if (!mt_video_worker_quiesce()) return false") < hold.index(
        "Xil_Out32(MT_SLCR_UNLOCK"
    ), "CPU1 hardware hold begins before mailbox quiescence"
    print("PASS: CPU1 code/mailbox/vector publication precedes release and SEV; hold follows quiescence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
