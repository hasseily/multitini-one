# Amiga real-time clock

## Defect and evidence

FPGA 4D000804 connects a constant zero to Minimig's 65-bit RTC input. The
upstream seconds divider advances from zero to 59, then holds 59 until a
new external timestamp arrives. No timestamp arrives in that build.

The ClassicWB copy of SysInfo 3.24 contains a hardware-clock fallback that
repeatedly reads the seconds units nibble at DC0001 while it equals nine.
The routine calls Exec Forbid before the loop and Permit afterward. A
clock permanently reading 59 can therefore leave its window unresponsive.
The preceding OpenResource call means this fallback is conditional; the
live guest's program counter has not been captured.

Offline fixtures execute the exact polling opcodes with both production
CPU cores against the real RTC/MMIO implementation. Both wait indefinitely
at 59 with the old logic and escape after an external timestamp toggle.
Analysis and receipts are retained under
`build/amiga-ecs/sysinfo-analysis/`. This establishes a reproducible defect
and a plausible explanation of the reported freeze, pending a board test.

## Host transport

The existing coherent 32-bit command mailbox at D000 carries RTC updates.
Status bit 28 advertises this optional extension. Firmware skips the
extension on older FPGA images whose bit 28 is zero. No ABI revision or
new clock domain is required.

| Opcode | Payload | Effect |
| --- | --- | --- |
| 5 | RTC bits 23:0 | Start a new frame |
| 6 | RTC bits 47:24 | Stage the second word after opcode 5 |
| 7 | Bits 15:0 carry RTC bits 63:48; bits 23:16 must be zero | Commit after opcode 6 |
| 4 | Existing CLEAR_ERROR | Clear the transport error and discard a partial frame |

Any other command discards partial RTC words. Out-of-order words and
reserved commit bits return an error. A partial frame never changes the
native RTC input, and RTC commands do not change UIO selection or pulse
the UIO/IDE strobes.

An accepted commit assembles all 64 data bits and toggles bit 64 together.
The service domain publishes this bundle on its falling edge, before the
next accepted native edge. The command completes only after that native
edge consumes the snapshot. If no edge arrives within 65536 service
cycles, the command returns an error and restores the last consumed bundle.
A subsequent retry toggles from the last consumed value, preventing an
unseen update from cancelling the retry. Reset clears the partial frame,
published data and committed toggle.

## Clock representation and refresh

Increasing byte order is BCD seconds, minutes, hours, day, month, year
modulo 100, weekday (Sunday zero), and 40 hex. The board's validated cached
PCF8563 time supplies these fields. A valid timestamp is seeded while the
guest CPU remains in reset, then refreshed once per second outside the
media-error gate. Invalid or unavailable board time does not prevent boot.

The deterministic native port preserves the complete incoming timestamp,
including seconds. Its seconds-only fallback rolls 59 to 00 instead of
holding 59. This guard keeps polling software moving when firmware cannot
refresh the clock. Calendar advancement remains owned by the board-clock
feed; the fallback does not claim to keep the date accurate without it.
As in the retained upstream implementation, guest writes do not set these
RTC registers. The board clock remains the time-setting authority.

## Offline qualification

The native RTC regression passes with both the real stock 68000 and turbo
68020 CPUs, production memory and native MMIO. It checks complete timestamp
bytes, 58 to 59 to 00 to 01, the exact SysInfo polling opcodes, paused native
clocks, timestamp toggles, simultaneous refresh/second boundaries and guest
reset persistence. Restoring either the dropped-seconds load or the missing
rollover fails its intended assertion. The ordinary native configuration,
CPU, repeated-RAM and blitter regressions also pass.

The host RTC regression checks three-word framing, malformed order and
reserved bits, discarded partial frames, preserved UIO selection, inactive
strobes, falling-edge publication and setup before native acceptance. A
paused native clock cannot acknowledge delivery. A timeout on the second
commit restores the first committed toggle; retry reaches the native
consumer with the new snapshot. Reset during a pending commit also passes.
Source hashes and logs remain under `build/amiga-ecs/rtc-fix/` and
`build/amiga-rtc-host-tests/`.

A separate firmware correction requests the native IDE completion IRQ
when ATA status reports ERR. The reset signature (error register one with
status 50 hex) remains noninterrupting. Native IDE regression checks ERR,
ABRT/IDNF data, nIEN suppression, alternate-status preservation and regular
status clearing of the IRQ. This defect was established independently; no
trace identifies it as the cause of the reported SysInfo freeze.

ARM 4D000805 and FPGA 4D000806 passed package validation. Final routed
setup is +0.244 ns, hold is positive but narrow at +0.008 ns, and native
clock-enable setup is +13.748 ns. Strict export gates pass.

## Installation observations

The user authorized installation on 2026-10-31. Golden Boot installed
ARM 4D000805 at the normal Multitini slot, verifying equal file/flash CRC32
A61CB503, then booted M0.8.0dev3. The FPGA loader accepted 4,045,568 bytes
with CRC32 44A98C6D and reports FPGA 4D000806 / ABI 0.27. The board RTC is
valid; no RTC seed or refresh error was logged. Host capability bit 28 is
present. These observations do not directly measure native RTC progression.

The core reports ready, fault zero and error zero, with CPU2, 2 MiB chip,
8 MiB fast, the existing ClassicWB HDF on IDE master and both DD drives
ejected. Video counters advanced at 640 by 512 with a 1280-byte stride and
no reported video errors. ROMs, Amiga settings and media were preserved;
the prior firmware and core files were backed up before replacement.

Source/output screenshots were saved on SD, but Windows did not enumerate
the post-update USB export, including one reconnect attempt. Export was
stopped cleanly and the core restarted with its menu closed. Screenshot
retrieval and physical USB export remain open checks. The user subsequently
confirmed that SysInfo works on the installed ARM 805 / FPGA 806 pair.
This accepts the reported startup/interactivity fix; benchmark accuracy,
long runs and other applications remain separate checks.
