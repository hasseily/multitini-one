# Multitini One: Product and Build Plan

Status: planning baseline, written 2026-08-29

Accelerator selection fix, 2026-10-16: None means stock CPU speed. Normalize
older saved None configurations, disable speed selection while None is active,
and use the existing safe machine restart when changing accelerator cards.
The restart clears guest ZipGS speed, enable, unlock, cache and delay overrides;
the saved menu settings apply on restart. Speed changes within ZipGS stay live.
Deliver M0.5.5 with an ARM-only rebuild and explicitly reuse all eight qualified
FPGA images. Do not start an FPGA rebuild until the weak 14-to-57 MHz hardware
speedup is diagnosed. The owner confirms System 6.0.1 boots on the M0.5.4 core;
the new card-selection behavior still needs board verification.

Acceleration rework, 2026-10-16: the owner reports about 2x at 57 MHz in
real use. Profile the real ROM/OS boot and synthetic loops, then remove the
measured limits while keeping stock timing: no emulated 1 MHz Mega II sync
while accelerated (the core has no physical bus), posted DDR writes, 32-byte
burst lines, the IIgs HP2 port on the machine clock, faster accelerated DMA,
slot ROM without the sync, an IWM mode-register decode that matches a real
IWM, and a selected-drive IWM hold. Keep compatibility holds and sound GLU
pacing. Implement in parallel tracks, run fast regressions per track and the
long boot, profile and Vivado runs once at the end.

Acceleration follow-up, 2026-10-15: the owner reports unchanged System 6 boot
and Finder redraw after changing speed in the Multitini menu. Measure the
real ROM/OS workload and paused menu speed changes. Correct false paddle
slowdowns from ROM-backed IRQ dispatch and allow reads of fast shadow RAM
to use the accelerated path, while retaining native shadow writes and direct
Mega II memory timing. An empty floppy drive's motor latch must not hold an
HDD-only session at stock speed; retain native holds for mounted floppy
activity and actual IWM accesses. Use the documented ZipGS default with AppleTalk/IRQ
delay off; preserve explicitly saved settings. Add an immediately applied,
saved IIgs mouse sensitivity setting in firmware, preserving the existing FPGA
input protocol and older saved settings. Regressions must distinguish
requested speed, actual CPU progress and compatibility holdoffs. Rebuild only
affected firmware and IIgs images, retain the other seven qualified images,
and deliver a private ROM-inclusive M0.5.3 package for board acceptance.

IIgs follow-up, 2026-10-15: reproduce the owner's ROM3 copyright-screen stop
at stock speed with the actual ROM and System 6.0.1 hard disk. Correct the
external CPU's RTC register decoding and fetch the ROM-backed interrupt vectors
at C071-C07F through DDR while retaining native I/O timing. Qualify cold boot
before rebuilding IIgs. Correct fractional IWM timer carry and the byte-latch
expiry counter accidentally guarded by debug logging, using reproducible ports
of the exact pinned source. Add 800 KiB ProDOS PO/2MG in the 3.5-inch drive and
140 KiB DOS-order DSK in the 5.25-inch drive through bounded, writable GCR
track conversion. Preserve container headers, write protection and recoverable
writes. Test private media
without modifying the originals or including game bytes in source or delivery.
Release matching ARM/IIgs as M0.5.2, retain the seven qualified other images,
and preserve M0.5.1 NES and About behavior. Board acceptance remains separate.

Scope update, 2026-10-15: branch `core-nes` ports the pinned MiSTer NES machine
through the common shell and firmware. Preserve the complete NTSC 256x240
raster, native CPU/PPU/APU timing, two controllers and durable battery saves.
Cartridges remain user inputs. Reuse compatible existing core images with
their original build identities and source archives. Directed simulations,
license checks and a strict routed NES build precede a board-test package.
See [the NES integration](docs/NES_PLAN.md). Board acceptance is separate.

Cartridge follow-up, 2026-10-15: the owner's five NES cartridges all use
NES 2.0 headers and are rejected before FPGA transfer. Parse the supported
NES 2.0 memory, timing and mapper fields, then stream a canonical header while
preserving every PRG/CHR byte and the original file. Preserve Contra's mapper 2
submapper 2 AND bus conflicts through the existing ROM-read/stall path. Rebuild
NES and ARM as M0.5.1; retain the seven other qualified FPGA images. Test the
exact private files locally, keep game bytes out of source and delivery, and
separate parser/stream/CPU evidence from unperformed board gameplay.

NES clock follow-up, 2026-10-15: explicitly reset the imported CPU tick counter
and qualify PPU cycle skipping for PAL only. Verify native NTSC CPU enables
every 12 master ticks, including stack, DMA and NMI/RTI execution. Allow private
cartridge cold initialization to finish before checking rendered frames.

Menu follow-up, 2026-10-15: add About after System in the main firmware. Move
every row from Firmware build through Audio output inclusive to About, keeping
their order, values, help and live refresh. Preserve the other System entries.

Follow-up, 2026-10-15: M0.4.0 adds primary-clock 65816 execution with selectable
28.64/57.27 MHz fast-cycle targets, a coherent small read cache ahead of the
32 KiB DDR cache, and an advertised per-image speed capability. Slow RAM,
shadow writes, disk controllers, audio and video retain native timing. Both
menu and emulated ZipGS software controls use the installed maximum. Recover
the complete 640-pixel SHR line by fetching the first pixel byte earlier;
test every output column, including fill mode, with a distinct blue border.
The IIgs browser must enter nested folders, retain relative media paths and
return to its media root without permitting traversal outside that root.

Port Appletini's USB0 mass-storage SD export as an exclusive modal operation:
flush and close media, drain the running core and display, unmount FatFs,
export the physical card, then sync/detach/remount and restart the core.
Require durable writes before USB success acknowledgement. Package required
user-owned boot ROMs from the explicit private collection into each core's
roms directory; retain ROM-free current sources and per-image source archives.
Include media loaders and empty media directories, with no games or OS disks.
Build only the changed IIgs FPGA image and matching ARM firmware. Explicitly
retain the six qualified older shell/core images after ABI/platform checks.
CPU/cache/native transaction simulations, USB failure tests and strict routed
timing precede the hardware-test package. Physical acceptance remains separate.

Build follow-up, 2026-10-15: M0.3.1 separates firmware release identity from
qualified FPGA image identity. Explicitly selected compatible images retain
their original source archives, build IDs, interface versions and timing
evidence. ARM-only changes reuse those images after integrity and platform
checks. Rebuild only the images affected by machine or common-shell changes.
IIgs ROMs live in `multitini/cores/apple-iigs/roms/`; disks and PRAM live in
the sibling `multitini/cores/apple-iigs/media/` directory.

Scope update, 2026-10-14: branch `core-gs` imports the pinned MiSTer Apple IIgs
machine, runtime ROMs, HDD/WOZ media, persistent PRAM and selectable accelerated
65816 operation. Reuse the shared shell and firmware; add a coherent DDR
RAM/ROM cache and HP3 source video, with a matching FSBL/supervisor and complete
FPGA build matrix. See [the IIgs integration](docs/APPLE_IIGS_PLAN.md).
Instruction/transport tests and strict routed builds precede hardware testing;
no physical support is claimed by the port alone.

Scope update, 2026-09-18: ship one C64 variant, PAL, with nominal 60 Hz DVI
output. C64 NTSC is no longer part of the build, package, or release gates.

Scope update, 2026-09-30: import the remaining applicable Appletini shared
platform work, including optional display and golden-update follow-ups, on a
new branch. Six nominal 60 Hz output modes preserve complete centered machine
images. 1080p remains the reset default; smaller modes use fixed fit scales.
See [the parity record](docs/appletini-parity.md) for scope and test limits.

Scope update, 2026-09-30: implement the modern Nuked-MD Genesis / Mega Drive
core on branch `genesis`, after Appletini parity. See
[the Genesis integration](docs/GENESIS_PLAN.md) for architecture and test gates.

## 1. Goal

Scope update, 2026-10-13: branch `multiboot` adds shared Golden B2.0.0,
Appletini and Multitini firmware slots, a saved power-on choice and explicit
menu switching in both products. Every boot verifies the selected image in
flash and falls back to another verified image if it is absent or invalid.
Retain the selected FPGA/core images. See [multiboot](docs/MULTIBOOT.md) for
the layout, migration, update failure handling and board test limits.

Appletini restoration follow-up, 2026-10-14: Golden B2.0.6 accepts original
recovery-capable manifest-v1 Appletini packages from F1.1.0 onward. Identify the
version and Appletini UART in the loaded frontend; keep Bootgen bounds, full
CRC and recovery-marker checks. Restore the original bytes only in the 6 MiB
Appletini slot, preserving Multitini and the saved startup choice. Commit a
one-shot Appletini request so the restored F runs immediately. Preserve the
backward-readable legacy metadata representation. Add Golden UART
`boot appletini` and `boot multitini` for older menus. Retain F1.3.0-dev10,
M0.2.3, FSBL, FPGA/core and CPU1 inputs. Native archive and interrupted-write
tests precede the Golden-only ARM build; board restoration remains untested.

Golden follow-up, 2026-10-13: B2.0.1 checks FIRMWARE.BIN,
FIRMWARE-APPLETINI.BIN and FIRMWARE-MULTITINI.BIN in one sequential pass.
The verified image contents choose the destination regardless of filename.
Successful closed updates replace .BIN with .OK; rejected files stay for retry.

Appletini startup follow-up, 2026-10-13: the owner reports F1.3.0-dev stopping
after the SMARTPORT handoff on an Apple //e with Golden 1.x. F1.3.0-dev2 restores
the established CPU1 cold wake and reserves an independently checked 64 KiB ARM
stack. Retain the exact selected FPGA, FSBL and CPU1 inputs. The firmware-first
migration remains the target; replacement board validation is pending.

About update follow-up, 2026-10-13: the owner confirms F1.3.0-dev2 boots with
Golden 1.x. F1.3.0-dev3 reports the installed Golden version and adds an explicit
About-tab Golden update action when BOOT.BIN exists. M0.2.1 adds the matching
System action. Both use Cancel-default confirmation and the existing trial
staging/repair process. B2.0.2 reserves a checked 64 KiB Golden stack. Retain all
selected FPGA/core images. Actual menu-driven Golden update remains untested.

Golden menu correction, 2026-10-13: the owner confirms F1.3.0-dev3 reads installed
B1.2.0 but its update times out on the obsolete DMA-hold interface. F1.3.0-dev4
uses the retained FPGA's existing Apple RESET control with checked readback and
physical status, and reserves the real PS-DMA port before flash access. Permit
the update from the initial physical boot menu only; disable it after handoff.
Standalone ONE//e remains eligible. Retain M0.2.1, B2.0.2 and all FPGA/core inputs.
The replacement menu-driven update still needs the owner's board test.

Multiboot handoff correction, 2026-10-13: the owner reports B2.0.2 and M0.2.1
installation followed by unexpected M startup, slow System entry and SD unmount
failure. B2.0.3 preserves the verified existing tagged startup choice on first
metadata adoption. M0.2.2 fixes FatFs detach option 0 and holds CPU1 only after
fallible handoff checks. M and F1.3.0-dev5 use bounded advisory menu probes and
a once-verified Golden cache; actual boot actions still validate full live images.
Retain all selected FPGA/core, FSBL and CPU1 inputs. Runtime switching needs a
replacement board test; existing saved M startup choice requires one explicit edit.

Golden completion follow-up, 2026-10-13: the owner confirms the previous bundle
works and requests `BOOT.BIN` acknowledgement as `BOOT.OK`. B2.0.4 acknowledges
only a closed source whose complete bytes match live verified primary Golden,
after trial repair, with primary-boot retry. Filesystem failures do not undo
verified flash or block boot. Retain M0.2.2, F1.3.0-dev5 and all hardware inputs.

Switch performance follow-up, 2026-10-13: M0.2.3, F1.3.0-dev6 and B2.0.5 remove
duplicate full scans within switch preparation and Golden selection. The owner
chooses target payload validation after reset in Golden, with fallback; the
running firmware verifies Golden and probes the target before saving its request.
Keep full validation when first adopting metadata. Use a constant CRC
table and 4 KiB flash reads, with qualified quad I/O and conservative fallback.
Keep Golden's one-second serial recovery window. Retain every selected FPGA,
core, FSBL and CPU1 input. Native validation and ARM builds precede board timing.

Metadata diagnostic follow-up, 2026-10-13: the owner reports faster switching,
but Appletini warns that boot metadata is invalid. F1.3.0-dev7 recognizes
the two CRC-protected multiboot records and the original FF0000 legacy record,
without accepting read failures or writing metadata. Keep actual Golden version
display independent of this diagnostic. Retain M0.2.3, B2.0.5 and all hardware
inputs exactly. Only Appletini needs rebuilding; board HUD confirmation is pending.

Power help and screenshot follow-up, 2026-10-13: the owner clarifies that
Multitini inside an Apple //e uses host 5V without the standalone jumper. The
jumper supplies 5V only outside the Apple for ONE//e or Multitini. F1.3.0-dev8
corrects Boot help and captures the full output with its completed-slot FPGA
mask and geometry, preserving existing software effects and raw A2 captures.
Use larger bounded PNG batches while retaining close/remount and cleanup safety.
Retain M0.2.3, B2.0.5, FPGA/core, FSBL and CPU1 inputs. Native and retained-RTL
checks precede board screenshot timing and display comparison.

Saved vTW startup follow-up, 2026-10-13: F1.3.0-dev9 holds the physical //e
identification return through a firmware C8 patch, starts vTW before the first
prompt/menu, then applies its saved speed at handoff without another reset.
Keep initial ROM menu timing at 1 MHz and restore Disk II after CORE_RUN echoes.
ONE//e must release the native hold and publish menu PAUSE before its first run.
The reported bezel-only readings showed ONE//e disabled. Retain all FPGA/core,
FSBL, CPU1, M0.2.3 and B2.0.5 inputs. Require native ROM/runtime fault checks and
the owner's first-boot/menu-exit test before claiming board behavior.

Slowdown preset follow-up, 2026-10-14: add 32, 64 and 128 cycles to Appletini's
menu in F1.3.0-dev10, retaining its 512-cycle default and qualified FPGA inputs.
The owner isolates a TURBO Speed Race penalty to enabled Disk II slot 6.
Idle classic-cycle replay in the retained HDL gates CPU progress; the existing
Disable DiskII Acceleration setting provides a normal 1 MHz disk-path workaround.
An accelerated idle-path optimization is separate FPGA work. Retain M0.2.3 and
B2.0.5 exactly and keep the slowdown preset update in ARM firmware.

Firmware release, 2026-10-12: name Multitini firmware releases with an `M`
prefix, starting at `M0.1.0`. Add a shared controller menu gesture: hold
Select and Start together for 500 ms. Preserve ordinary solo game presses,
F12, dedicated Menu/Home buttons and the existing explicit menu-exit rule.
Build ARM firmware only and retain the exact supervisor and machine images
from `build/firmware-efx1`. See [M0.1.0](docs/FIRMWARE_M0.1.0.md).

Runtime follow-up, 2026-10-12: investigate the owner's PHO4 Genesis transfer,
effects and Video size failures. The continuously locked clock regression
establishes a Video size controller stall; fix geometry-only changes without
clock reprogramming. Preserve old bundles and distinguish that tested fix
from the unresolved Genesis/effects observations. See
[the investigation](docs/PHOSPHOR_PHOS4_DIAGNOSIS.md).

Display correction, 2026-10-12: center the visible compact three-line splash
block, including its F12 prompt. Measure ink against outgoing DVI DE and
compensate the standby color register's extra pixel of latency. Use padded
font rows to avoid runtime glyph-index arithmetic. Register mode-derived
canvas and glyph origins in the pixel domain during the existing reset hold,
without changing the color pipeline. PHO5 rebuilds all six FPGA
images for these corrections and the geometry-only Video size transaction. Keep DIA4 as a
separate diagnostic step with retained PHO4 hardware. See
[the PHO5 scope](docs/PHOSPHOR_PHOS5.md).

Use the Appletini ONE card as a stand-alone FPGA computer, outside an Apple II.
Use the board's 5 V jumper for power outside the Apple. Multitini installed in
an Apple //e gets 5 V from the host without that jumper. Build one system that can
load machine cores, start with the GPL MiSTer C64 core, and then use the same
work for other cores whose licenses allow sale with the hardware.

The first release goal is not a general MiSTer clone. It is a sound C64 product
on this board with a clean path to more cores.

The first C64 release should support:

- One PAL C64 variant through the board's nominal 60 Hz DVI output.
- Audio through the AK4493 DAC, with SPDIF as a useful test path.
- USB keyboard and common USB gamepads.
- C64 system ROMs supplied by the user unless shipping rights are secured.
- D64 and G64 disk images, with at least drive 8.
- PRG loading.
- CRT cartridge images, including common bank switch types.
- Save data and settings on the SD volume.
- A small on-screen menu or an early serial menu.
- A repeatable source build and update image.

Later work may add tape, a second drive, advanced carts, REU, network use, and
more machine cores. Do not let those delay the first working C64.

## 2. Fixed Assumptions

- The card runs without an Apple II and must boot, take input, show video, and
  play sound on its own.
- The intended jumper supplies 5 V for standalone use outside the Apple.
  Installed in an Apple //e, Multitini uses host 5 V without that jumper.
- The Apple II bus has no role in the first Multitini image. Hold all Apple bus
  outputs in safe idle states. Do not depend on Apple clocks or reset.
- The hardware will be sold. Core source and all Multitini firmware source will
  stay public and free of charge.
- Start from the C64 MiSTer core instead of writing a C64 from scratch.
- Keep a core adapter layer so later ports can share the same board services.
- Treat PSRAM as useful, fixed-time memory, but do not force a core to use it.
  Use block RAM and DDR where they fit better.

## 3. Known Board Hardware

The source board is Appletini ONE. Its key parts are:

| Part | Device | Planned use |
| --- | --- | --- |
| SoC | Xilinx XC7Z020-2CLG484I, Zynq-7020 | Machine core in PL; control and file work on ARM |
| Main RAM | Two Samsung K4B4G1646E DDR3L parts, 1 GB total | Large core RAM, buffers, disk cache, ARM work |
| Fast local RAM | Two Lyontek LY68L6400SLIT PSRAM parts, 16 MB total, SPI/QPI | Fixed-time core memory where measured speed fits |
| Boot flash | 128 Mbit / 16 MB QSPI NOR | Golden boot and field update image |
| Video | TFP410PAP parallel RGB to DVI, up to 165 MHz input clock | Direct digital video output |
| Audio | AK4493SEQ stereo DAC; I2S input | Main sound output |
| USB | Two USB3300 USB 2.0 ULPI PHYs | Keyboard, pads, and storage as board wiring allows |
| SD | Zynq PS SD interface | Core files, ROMs, disks, carts, saves, settings |
| Network | WIZnet W5100S | Later network features; not needed for the first C64 |
| Other | RTC, temperature sensor, dual UART | Time, board checks, logs, recovery, test control |

The sibling `../appletini-one` source already has:

- A Vivado 2025.2 project made by Tcl scripts.
- Zynq PS DDR setup and ARM firmware.
- DVI output logic and constraints for the TFP410.
- I2S and SPDIF output paths.
- PSRAM control at about 133 MHz.
- USB, SD, QSPI, UART, and W5100 support.
- A golden updater and one firmware slot in QSPI.
- Clock, reset, and crossing helper modules.

Verify each item on stand-alone hardware. Existing Apple II firmware may hide
power, clock, reset, or start-up needs that do not hold outside the host.

## 4. Main Design Choice

Port MiSTer cores through a small, stable Multitini interface. Do not port the
whole MiSTer Linux system first, and do not make each core speak to the Zynq PS
in a new way.

Use this split:

```text
SD / USB input / menu / settings / image files
                    |
             ARM control program
                    |
       command, status, and data channels
                    |
        Multitini common FPGA shell
       /          |           |        \
 core adapter   video       audio     memory
       |
 imported machine core
```

The common shell owns board pins and board rules. A core adapter changes the
small Multitini interface into the signals that one imported core expects.

### 4.1 Common FPGA shell

The shell should provide:

- Clock and reset requests for the active core.
- A simple control register bank from the ARM.
- A byte stream for ROM, cart, tape, and other direct loads.
- A sector request path for mounted disk images.
- Keyboard, gamepad, mouse, and user-button events.
- Raw core video in a stated pixel format, plus sync and blanking.
- Signed stereo samples with a stated sample rate and width.
- Optional block RAM, PSRAM, and DDR services.
- Save-data read and write requests.
- Core ID, build ID, status, error, and debug counters.

Keep the first form small. Add a service only when the C64 port or the next
chosen core needs it.

### 4.2 ARM control program

The ARM side should:

- Mount or read the SD file system.
- List and choose cores and content files.
- Load a core image if partial or full PL loading is used.
- Load user ROMs and cart files into the core.
- Serve disk sector reads and writes with set upper time bounds.
- Turn USB events into stable input events for the PL.
- Save settings and changed media safely.
- Report clear errors through UART and, later, the menu.
- Check the core and shell interface versions before start.

The first build may use UART commands instead of a full menu. This cuts risk
while video, audio, input, and file service are still under test.

### 4.3 MiSTer compatibility boundary

Reuse machine logic from a MiSTer core. Replace MiSTer platform parts such as
`emu`, `hps_io`, menu strings, DDRAM ports, video filters, and board PLLs with a
Multitini adapter.

Do not promise that an unchanged MiSTer `.rbf` file will run. MiSTer targets a
different FPGA family, DDR path, ARM link, pin map, and build chain. Source-level
porting is the planned form of compatibility.

Keep imported source near upstream. Put board changes in wrappers when
possible. Record the upstream URL and commit for every port.

## 5. Memory Plan

The board has three useful memory classes. Measure them before fixing the core
interface.

### 5.1 Block RAM

Use FPGA block RAM for state that needs short, fixed delays:

- C64 main RAM if resource use permits.
- Character, color, and small ROM stores.
- FIFOs, line buffers, and drive state.
- Small cart RAM and control tables.

The Zynq-7020 has limited block RAM, so synthesis must decide the final split.
Do not count only bytes; check ports, widths, and clock rates too.

### 5.2 PSRAM

The 16 MB PSRAM has fixed command timing once its controller and schedule are
fixed. That makes it valid for many old systems even if each first read takes
longer than SDRAM.

Good uses may include:

- REU or other expansion RAM.
- Cartridge storage.
- Drive buffers.
- Frame or line work that can use bursts.
- Main RAM for cores whose bus rate and wait rules fit measured service time.

Build a test that measures:

- First-byte read delay.
- First-byte write delay.
- Burst read and write rate.
- Read-to-write turn cost.
- Worst delay with all planned clients active.
- Errors across board temperature and a long run.

Do not use the average rate as the core contract. Use the measured worst delay.
If a core cannot pause at a memory access, add a cache or put that state in block
RAM.

### 5.3 PS DDR3L

The 1 GB DDR belongs to the Zynq PS memory controller. It has high total rate
but its delay may vary. Use it for large stores, files, buffers, and clients that
can queue requests. If the PL needs it, define an AXI path with buffering and a
clear worst-case plan. Do not place cycle-bound C64 RAM behind an untested DDR
path.

### 5.4 First C64 memory map

Start with:

- C64 main RAM and timing-bound chip state in block RAM.
- ROM images in block RAM after ARM load.
- Disk images on SD, served through sector buffers.
- C1541 RAM and ROM in block RAM.
- REU off in the first milestone; put it in PSRAM or DDR later.
- Large or advanced cart data in PSRAM only after the memory test passes.

The C64 is a good first core because its main machine memory is small and its
base memory needs are far below the board's total memory.

## 6. C64 Port Plan

Use the current MiSTer C64 source as the base. At the time of this plan, its
top-level `c64.sv` states GNU GPL version 2 or any later version. Pin the exact
commit during import and audit every submodule license.

### 6.1 Keep from the core

Try to keep these blocks with few changes:

- CPU and C64 memory map.
- VIC-II timing and pixel output.
- SID and audio mix.
- CIA chips and keyboard matrix.
- IEC bus logic.
- 1541 and, later, 1581 drive logic.
- Cartridge and PRG load logic.
- D64 and G64 image behavior.

### 6.2 Replace or wrap

Replace MiSTer-specific parts with Multitini services:

- `emu` top-level ports.
- `hps_io` and its status bit map.
- MiSTer PLL and board clocks.
- MiSTer DDRAM service.
- MiSTer SD block service.
- MiSTer joystick, keyboard, and mouse wires.
- MiSTer video scaler and HDMI control signals.
- MiSTer menu and build glue.

First try to keep the core's inner clocks and enables at their expected rates.
Make new Xilinx clock blocks around them. Check all generated clocks and reset
release paths in Vivado.

### 6.3 Content support order

Add content in this order:

1. User-supplied C64 and drive ROMs.
2. PRG load and reset/run.
3. Read-only D64 on drive 8.
4. D64 writes with safe flush and eject.
5. CRT carts, starting with plain and common types.
6. G64 read support.
7. G64 writes only if the upstream core and file path can preserve them.
8. Tape formats.
9. Second drive and 1581.
10. REU and other add-ons.

For each type, test bad files, forced eject, reset during I/O, and power loss
where writes matter.

## 7. Video, Audio, and Input

### 7.1 Video

First show a stand-alone test pattern through the existing TFP410 path. Then
show raw C64 video without a scaler. Add a small scaler only if the display path
needs one.

Test the PAL C64 source and nominal 60 Hz output. Record:

- Core pixel and line rates.
- DVI output mode and pixel clock.
- Frame repeat or rate change method.
- Added frame delay.
- Crop and border rules.
- Display models tested.

Avoid a full frame buffer in the first path if line buffering and rate handling
work. If a frame buffer becomes required, DDR is the likely store.

### 7.2 Audio

Send a known tone through SPDIF and I2S first. Then connect the C64 samples.
Set one clear point for rate change, gain, clipping, and mute during reset or
core load. Check left and right channels and DAC start-up order.

### 7.3 Input

Start with one known USB keyboard and one known USB pad. Map keyboard events to
the C64 matrix in one table. Make joystick port swap a setting. Keep USB packet
work on ARM and send stable events to the PL.

Test held keys, key release during reset, two-key and three-key chords, pad
disconnect, and hot plug.

## 8. Core Loading Model

Choose the simplest model that works, in this order:

1. Build one full FPGA image with the common shell and C64 core.
2. Prove the full C64 product and the adapter rules.
3. Add a second core to test that the common interface is real.
4. Only then decide whether run-time full PL loading, partial reconfiguration,
   or separate QSPI/SD images gives the best user flow.

Do not start with partial reconfiguration. It adds floorplan, clock, reset,
build, and update work before the first core works.

For early development, JTAG can load full images. The release should use the
existing golden updater ideas or a new safe SD-to-QSPI flow. Keep a recovery
image that does not depend on the selected machine core.

## 9. License and Product Rules

The hardware may be sold while the core and firmware stay free. This works well
for GPL and broad licenses that allow commercial use. A zero price does not, by
itself, make use of a NonCommercial core noncommercial.

### 9.1 GPL and broad-license cores

The C64 core's GPL terms allow sale, use in a paid hardware product, changes,
and distribution. When giving out an FPGA image, also give users the full source
that matches it under the required terms.

For each release, keep:

- All HDL source used for the image.
- All local changes, not only patch files against a moving branch.
- Project Tcl, constraints, build scripts, and core settings.
- License texts and credits.
- The upstream source URL and exact commit.
- Clear steps to obtain any needed vendor tool.
- Source for any joined Multitini logic under terms that work with the core.

The Xilinx tools need not be copied into the repo, but the public files should
let a user build the same design with the stated tool release.

### 9.2 NonCommercial cores

Do not ship, host, install, or advertise an NC core as an official paid product
feature without written permission from all needed rights holders. Giving the
core away does not settle this if its main use helps sell the board.

For a wanted NC core, seek a written grant that covers:

- Porting and changing source.
- Building and giving out FPGA images.
- Bundling or preloading.
- Official downloads and update tools.
- Demo units, reviews, and product ads.
- Sales by the maker and resellers.
- Ongoing source release under the agreed public terms.

Atari 7800 is not an early target until this is settled. At the time of this
plan, the MiSTer Atari 7800 top file states CC BY-NC 4.0.

### 9.3 ROMs, media, and marks

A core license does not grant rights to system ROMs, game files, disk images,
manuals, names, or logos. Do not commit or ship them without clear rights.

For C64, start with user-supplied system and drive ROMs. Store their hashes and
paths in settings if useful, but do not copy them into public test data. Use
small original test programs and disk images for automated checks.

Before public sale, have a lawyer check the release list, notices, download
flow, ads, and rights for all shipped files.

This section records a project rule, not legal advice.

### 9.4 Sources recorded at planning time

- MiSTer C64 repository: <https://github.com/MiSTer-devel/C64_MiSTer>
- C64 top-level GPL notice:
  <https://raw.githubusercontent.com/MiSTer-devel/C64_MiSTer/master/c64.sv>
- MiSTer Atari 7800 repository:
  <https://github.com/MiSTer-devel/Atari7800_MiSTer>
- Atari 7800 top-level CC BY-NC notice:
  <https://raw.githubusercontent.com/MiSTer-devel/Atari7800_MiSTer/master/Atari7800.sv>
- Creative Commons BY-NC 4.0 terms:
  <https://creativecommons.org/licenses/by-nc/4.0/legalcode.en>
- Creative Commons NonCommercial FAQ:
  <https://creativecommons.org/faq/#does-my-use-violate-the-noncommercial-clause-of-the-licenses>
- GNU GPL FAQ on sale and source duties:
  <https://www.gnu.org/licenses/gpl-faq.en.html>

These links name moving default branches. Pin source commits before import and
save the matching license text in this repository.

## 10. Work Order and Gates

Each phase ends with a result that can be checked. Do not begin broad feature
work while a lower gate still fails.

### Phase 0: New-repo and rights baseline

Tasks:

- Add a short README and a license for original Multitini work.
- Choose a GPL-compatible license for the common shell and firmware. GPLv2-or-
  later is the least hard match for the first C64 work, but check all source
  before the final choice.
- Add `third_party/`, `licenses/`, and a source and license list.
- Record the source Appletini commit used for board files.
- Record the MiSTer C64 commit and all nested source origins.
- Add rules that block ROM and game files from source control.

Gate:

- Every planned imported file has a known source and license.
- No NC code sits in the first release path.

### Phase 1: Stand-alone board start

Tasks:

- Copy the least board support needed from Appletini.
- Make a new top-level that does not wait for Apple clocks or reset.
- Hold all Apple bus drivers and control pins in safe idle states.
- Start Zynq PS, DDR, UART, SD, and the clock blocks from board power.
- Log boot cause, voltage-related status that can be read, and temperature.
- Check both USB PHY reset signals. USB3300 `RESET` is active high; keep it low
  in normal use.

Gate:

- Ten cold boots and ten warm resets pass without an Apple II.
- UART starts every time.
- ARM DDR passes a memory test.
- SD can read a fixed test file.
- No Apple bus pin drives an unsafe level.

### Phase 2: Board output and input tests

Tasks:

- Show a DVI test pattern from PL.
- Play fixed tones through SPDIF and AK4493 I2S.
- Read one USB keyboard and one gamepad.
- Add simple UART commands for reset, mode, and status.

Gate:

- DVI holds for two hours on at least two displays.
- Audio has correct channels, level, and no start or reset burst.
- Input press and release events survive hot plug and reset.

### Phase 3: Memory tests and service contracts

Tasks:

- Port or reuse the PSRAM controller and its timing rules.
- Add long PSRAM tests and worst-delay counters.
- Test PL access to DDR only if the first C64 plan needs it.
- Set fixed limits for ARM-to-PL commands and disk sector replies.

Gate:

- PSRAM passes across a long run and useful temperature range.
- The report states worst first-read, write, turn, and burst times.
- No core service depends on an unmeasured average time.

### Phase 4: C64 core, no media

Tasks:

- Import the pinned C64 core.
- Remove or wrap MiSTer platform code.
- Add Xilinx clocks and the Multitini adapter.
- Load user ROMs through the ARM link.
- Connect raw video, audio, keyboard, and one joystick.

Gate:

- The PAL core reaches the C64 ready screen on the nominal 60 Hz output.
- Keyboard and joystick work.
- A SID test plays through the DAC.
- Timing closes with no unmatched clock or timing-rule query.
- Reset and fifty cold starts work.

### Phase 5: PRG, disk, and cart support

Tasks:

- Add PRG load and reset/run.
- Add read-only D64 and full 1541 behavior.
- Add safe D64 writes and flush.
- Add common CRT types.
- Add G64 reads.

Gate:

- A stated test set boots and runs.
- Disk writes survive clean eject and reset.
- Unsupported images fail with a clear message.
- File work does not cause video or audio faults.

### Phase 6: User flow and recovery

Tasks:

- Replace needed UART steps with an on-screen menu.
- Add core, ROM, disk, cart, input, and video settings.
- Add a safe update image and recovery path.
- Add build IDs to ARM firmware, shell, and core.
- Add release packaging with matching source links.

Gate:

- A new user can boot, supply ROMs, mount a disk or cart, and start without a
  development PC.
- A failed or cut update can recover through the golden image or JTAG.
- Each public binary points to its exact matching source.

### Phase 7: Prove reuse with a second core

Choose a small core with a license that allows commercial use and whose memory
needs fit the board. Do not choose only for name value. The goal is to test the
common shell, control program, and adapter design.

Gate:

- The second port adds no new board-specific path inside the common shell unless
  the hardware truly needs it.
- Core choice, files, input, video, audio, reset, and save data use the same
  public interfaces as C64.

### Phase 8: More cores and optional features

After the second core:

- Rank cores by license, FPGA use, memory delay, clock needs, input needs, and
  port work.
- Seek rights for wanted NC cores such as the present Atari 7800 source.
- Add REU, tape, second C64 drive, network use, or larger video work based on
  user need and measured resource room.
- Review whether run-time PL loading now saves enough work to add it.

## 11. Build and Test Practice

Keep all projects script-made. Start from the Appletini flow:

```text
vivado -mode batch -source scripts/create_project.tcl
vivado -mode batch -source scripts/build_and_export_xsa.tcl
vitis -s scripts/create_vitis_workspace.py
```

The new repo may change names and package steps, but it should keep the same
rule: source and scripts define the build; generated project files do not.

Before each full build:

- Run source checks and small HDL tests.
- Check that source lists contain no missing or extra generated file.
- Check license data for new third-party files.

After each Vivado build:

- Review all critical warnings.
- Confirm every timing rule found the object it names.
- Confirm all clocks and clock groups.
- Confirm timing passes.
- Save use by block RAM, LUT, flip-flop, DSP, clock unit, and I/O bank.
- Tie the XSA, FPGA image, and firmware to one build ID.

On hardware:

- Keep UART logs for cold boot, reset, media work, and error cases.
- Add counters for dropped input, late disk sectors, audio underflow, video
  underflow, PSRAM wait, and ARM link timeouts.
- Run long tests for video, audio, PSRAM, file writes, and repeated reset.

## 12. Main Risks and Answers

| Risk | First answer |
| --- | --- |
| Board does not start cleanly without Apple power or clocks | Prove stand-alone boot before core work; remove all Apple clock and reset needs |
| USB port wiring or power role does not match stand-alone host use | Check the board plan and prove one port with a known device in Phase 2 |
| C64 import uses more FPGA parts than expected | Synthesize the inner core early; cut optional REU, filters, second SID, and second drive first |
| MiSTer Intel FPGA parts are mixed into machine logic | Isolate and replace PLL, RAM, and platform wrappers; keep inner logic unchanged |
| PSRAM delay breaks a core bus | Measure worst delay; use block RAM or a cache for state that cannot wait |
| PS DDR delay varies | Keep it off cycle-bound paths; buffer and queue bulk work |
| File writes corrupt disk images | Use write-protect first, then temp or journal rules and clear flush/eject states |
| Video rates fail on common displays | Test raw modes first, then add a stated rate-change path |
| GPL source does not match a shipped image | Pin every source and publish the full build input for each binary |
| An NC core helps sell the hardware | Do not make it official without written commercial rights |
| System ROM or game rights are unclear | Require user files and ship only original tests until rights are clear |
| Common shell grows around C64 quirks | Port a small second core before setting the version 1 interface |

## 13. Decisions Still Needed

### Decisions recorded after the baseline plan

- 2026-10-12: The owner reported an off-center standby in non-1080p modes and
  uncovered config-menu strips, including 48 bottom rows at 1024x768. PHO4
  centers the existing artwork per output, retains the text grid and glyph
  sizes, and extends nearest edge-cell backgrounds/transparency through the
  remainder strips. Full-screen screenshots follow the same policy. Keep
  clocks, resets, commits and data latency unchanged. Rebuild matching firmware
  and all six FPGA images as `-phos4`, build ID 0x50484F34, ABI 0.21. New splash
  designs are proposals only until the owner selects a direction.
- 2026-10-12: The owner reported PHO1 Blur too strong even at Light and asked
  to weaken every level, then approved a per-core Video size control with
  1x, 2x, 3x, 4x and Auto. PHO3 / ABI 0.21 adds this to the PHO2 calibration.
  Auto retains every current core/mode viewport. Manual sizes multiply native
  pixels; signed floor centering clips excess at the display edges. Capture
  and direct bridges retain complete Auto rasters; only native DDR scanout
  uses manual geometry, with black on startup/miss. A seven-bit mailbox commits
  mode and size together after the normal drain/hold/relock transaction.
  Native frame sizes, HDMI timings, Blur/Glow/Dot bleed and controller maps
  remain independent of the new size. Saved schema 9 uses a reserved profile
  byte; older profiles migrate to Auto. Rebuild supervisor and all five machine
  variants with build ID 0x50484F33 and suffix -phos3. Timing, scanout stalls,
  clipped first/last pixels and complete hidden native capture are release
  gates. Keep PHO1 artifacts and unfinished PHO2 build evidence intact.
- 2026-10-12: The owner reported PHO1 Blur too strong even at Light and asked
  to weaken every level. PHO2 / ABI 0.20 retains the Appletini H3, H3/V3 and
  H5/V3 footprints, with normalized rounded kernels blended at 1/8, 1/4 and
  1/2 against the original persisted pixel. Keep flat RGB565 colors exact,
  Glow/Dot bleed/persistence unchanged, and all pipeline clocks and frame
  transfers fixed. Rebuild matching firmware and all six images as `-phos2`.
  Preserve the PHO1 delivery and keep board acceptance separate.

- 2026-10-11: The owner replaced independent axis smoothing with Appletini
  Blur, Dot bleed and Glow, pinned to F1.2.5 commit 886ea50e. PHO1 / ABI 0.19
  supersedes GL01 and AX01 effect semantics. Build a firmware with suffix
  `-phos1`, supervisor, C64 PAL, PSRAM GBC, Vectrex, and Genesis NTSC/PAL.
  Measure resource/timing/frame-processing cost and retain exact source and
  build inputs. Board performance remains a separate measurement.

- 2026-10-11: The owner requested fixed-radius glow after comparing Appletini
  F1.2.5 with Multitini's independent smoothing. GL01 / ABI 0.18 uses the same
  3x3 native-pixel Bloom halo at Light, Medium, and Strong; strength changes
  added light only. Keep both smoothing axes independent. Record software,
  FPGA simulation, routed-build, and board evidence separately.
- 2026-10-02: The owner requested independent horizontal smoothing before and
  during scaling and vertical-only smoothing, with stable per-core strengths
  across output modes. After the software preview, increase all horizontal
  levels and bring vertical Light/Medium closer to Strong by using the same
  five-row footprint. Existing presets need no migration. Preserve core
  timing, nominal 60 Hz output, and the ordered frame
  pipeline. ABI 0.17 identifies the revised effect semantics; see
  [axis smoothing](docs/VIDEO_AXIS_SMOOTHING.md). Board acceptance remains pending.
- 2026-09-30: The owner requested all remaining applicable Appletini imports,
  including the optional follow-ups, committed on a new branch. This includes
  controller mapping and transports, timing/build hardening, six display modes,
  and recoverable golden self-update. Apple bus and slot-card behavior remains
  outside the standalone product. Local tests do not establish board support.
- 2026-09-18: The owner confirmed the VID5 blur correction is fixed on the
  board and requested closing the GBC startup investigation. Preserve the
  exact delivered core update and retained VID4 firmware/Vectrex. The startup
  report is no longer an active task or acceptance blocker; no cause or fix
  is claimed.
- 2026-09-18: VID4's inverse-square blur correction was too faint on C64 and
  GBC. Keep source-aware strength, but require distinct Light/Medium/Strong
  results in the actual RGB565 pixels, with preserved handheld detail on
  Light. Numerical display-space variance alone is not visual acceptance.
  Deliver the correction as a C64 PAL/GBC core update using the existing
  firmware.
- 2026-09-18: Native-resolution blur must account for each adapter's output
  scale. Keep the existing Vectrex appearance while reducing blur on enlarged
  low-resolution source pixels; apply the correction independently per axis
  without adding frame transfers or relaxing uninterrupted 60 Hz gameplay.
- 2026-09-18: Video output must remain at nominal 60 Hz throughout gameplay,
  with no processing-frame drops. Interruptions while changing cores are
  acceptable. CPU polling, latest-frame selection, or repeated
  processed frames are not acceptable ways to make persistence, blur, and
  bloom fit their budget. Use an autonomous ordered FPGA frame pipeline;
  CPU1 prepares controls outside the frame deadline. Qualify all effects
  together with zero capture drops, display underruns, and deadline misses.
  Board throughput remains a separate acceptance gate from arithmetic tests.
- 2026-09-15: The owner reported successful physical testing of the VEC3
  Vectrex/video update and requested commit and push. Border controls live in
  Video; off-screen borders clip without shrinking the original game image.
  Integer-size controls were removed. See `docs/VIDEO_TEST_BUILD.md` for the
  tested bundle identity and the scope of the owner report.
- 2026-09-14: The owner requested branch `vectrex`, a fresh Vectrex machine layer
  reusing existing chip implementations (including Appletini ONE's VIA/PSG),
  and Vectrex32 support. Named acceptance includes well-known Vectrex games and
  the owner's Vecalabeth. Follow `docs/VECTREX_PLAN.md`; source/build results
  remain separate from actual gameplay and board acceptance.
- 2026-09-14: The owner accepts the current GBC functional baseline, including
  tested saves. The second-core reuse milestone is complete for that scope.
  Preserve PSRAM-enabled GBC `20260913` in the current package. Broader mapper,
  RTC, failure-case, and product qualification remain separate work. See
  `docs/GBC_BASELINE.md` for the acceptance and provenance limits.
- 2026-08-29: Game Boy Color is the second core used to prove reuse. It must use
  the same load, input, video, audio, reset, and common-memory boundaries rather
  than adding another board shell.
- 2026-08-29: The content rule applies to C64, GBC, and future cores. A system
  ROM, bootstrap, or test image that is widely and easily available from an
  ordinary stable public source may be pinned with provenance and a checksum.
  Warez-only material is excluded and remains user content. See
  `docs/CONTENT_POLICY.md`.

Answer these from tests, source review, or product choice. Do not guess when the
answer changes hardware safety or a public license duty.

- Which board connector supplies stand-alone 5 V after the jumper fix?
- Which USB PHY and connector can act as host, and how does the board switch or
  limit VBUS?
- What fixed board clock starts the PL and PS without Apple clocks?
- Which video modes does the present TFP410 and connector wiring pass on real
  displays?
- What AK4493 start-up and mute order prevents noise?
- What PSRAM speed and worst delay pass on production boards?
- How much block RAM and logic does the chosen C64 source use after Intel parts
  are replaced?
- Which C64 ROM set will be pinned under the content policy, or supplied by the
  user, and how will the UI explain missing or wrong files?
- Will the first release load one full FPGA image from QSPI, or load it from SD
  after a small recovery image starts?
- Which license will cover original Multitini shell and ARM code?

## 14. First Session Checklist

The next work session should do this, in order:

1. Read this file and `AGENTS.md`.
2. Inspect the new Git state; do not assume any code has been copied yet.
3. Record the exact `../appletini-one` commit and working-tree state.
4. Find and record the board source for stand-alone 5 V, clocks, USB VBUS, USB
   PHY reset, TFP410, AK4493, SD, QSPI, and DDR.
5. Choose the license for new original code after checking C64 and all helper
   licenses.
6. Add the basic source tree and license list.
7. Build the smallest stand-alone image: UART boot plus a DVI test pattern.
8. Do not import the full C64 core until that image starts on the board without
   an Apple II.

## 15. Meaning of Done for Version 1

Version 1 is done when a person can power the fixed board outside an Apple II,
connect DVI, audio, a USB keyboard or pad, and an SD card, then:

1. Boot into a clear menu or simple start screen.
2. Select user-supplied C64 ROMs.
3. Load a PRG, mount a D64 or G64 image, or load a supported CRT file.
4. Run the PAL core with stable nominal 60 Hz output and sound.
5. Save disk changes without file loss.
6. Reset, power off, update, and recover without a development tool in the
   normal case.
7. Obtain the full matching source and build steps for every shipped core and
   firmware image.

More cores are a version 1 design goal, but not a version 1 ship gate. The C64
must work well before the project claims broad core support.
