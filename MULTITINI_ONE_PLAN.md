# Multitini One: Product and Build Plan

Status: planning baseline, written 2026-08-29

## 1. Goal

Use the Appletini ONE card as a stand-alone FPGA computer, outside an Apple II.
Assume the board's 5 V jumper fix is present. Build one common system that can
load machine cores, start with the GPL MiSTer C64 core, and then use the same
work for other cores whose licenses allow sale with the hardware.

The first release goal is not a general MiSTer clone. It is a sound C64 product
on this board with a clean path to more cores.

The first C64 release should support:

- PAL and NTSC video through the board's DVI output.
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
- The 5 V supply issue has been fixed with the intended jumper.
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

Test PAL and NTSC separately. Record:

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

- The core reaches the C64 ready screen in PAL and NTSC.
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
- Which C64 ROM set will the user supply, and how will the UI explain missing or
  wrong files?
- Will the first release load one full FPGA image from QSPI, or load it from SD
  after a small recovery image starts?
- Which license will cover original Multitini shell and ARM code?
- Which second core best tests reuse under a sale-friendly license?

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
4. Run it with stable PAL or NTSC video and sound.
5. Save disk changes without file loss.
6. Reset, power off, update, and recover without a development tool in the
   normal case.
7. Obtain the full matching source and build steps for every shipped core and
   firmware image.

More cores are a version 1 design goal, but not a version 1 ship gate. The C64
must work well before the project claims broad core support.
