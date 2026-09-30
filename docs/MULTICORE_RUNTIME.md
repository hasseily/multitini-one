# Multicore Runtime and Menu

Status: the C64 and GBC functional baselines are accepted. The current package
uses ARM firmware `20260917`, supervisor and C64 PAL/NTSC FPGA images `20260915`
(ABI 0.10), and the unchanged PSRAM-enabled GBC image `20260913` (ABI 0.9).
See [Current release](CURRENT_RELEASE.md) for exact artifacts and installation.

The runtime includes versioned settings, explicit Autoboot, a supervisor-only
root FPGA image, direct-load transport, and per-core configuration. C64 offers
paired fast serial ROMs with a parallel drive cable and 8 KiB drive RAM, plus
independent Smart 2x CPU turbo. Both options default off. The owner confirms
fast loading, Ultima II native launch and disk swapping with a clean Player
image, and GBC battery saving. These observations do not qualify every game,
rollback case, or repeated-switch/media combination.

## Design

The ARM application remains alive while DevCfg/PCAP replaces the complete PL
image. This is full PL reconfiguration, not partial reconfiguration. Every
loadable image therefore contains the same board shell, AXI register ABI,
text overlay, video output, audio output, and input boundary. Candidate ABI
0.11 adds 128x60 text and automatic menu pause; see
[Shared front end](SHARED_FRONTEND.md). Only the
machine adapter and its clock generator differ.

The PS supplies the same 150 MHz `FCLK_CLK2` to every image. Each image's MMCM
derives its own machine clock, so the C64 PAL and NTSC packages do not require
the ARM to retune a PS clock during a switch.

The PS supplies the AXI/control domain directly from `FCLK_CLK0`, and the
control reset is synchronized from the matching PS fabric-reset output without
depending on a PL lock detector. Full-PL PCAP replaces every PL MMCM, so both
halves of the resident supervisor's control path must remain independent of
them. A separate reset domain waits for the dedicated 200 MHz IDELAY-reference
wizard to lock before releasing `IDELAYCTRL`.

A switch follows this fail-closed sequence:

1. Ask the active core service to flush and eject writable media.
2. Assert core reset, mute audio, and stop forwarding USB input.
3. Verify the selected raw bitstream's exact size and CRC32 while the old PL is
   still running.
4. Isolate and reset the fabric, enable PCAP, load the full image, wait for DMA
   and configuration completion, then release isolation.
5. Probe shell magic, shell ID, core ID, and ABI before using the new image.
6. Start the selected core's services, reload its required content, remount the
   previous disk when appropriate, then restore input and audio.

If loading, probing, or activation fails, the supervisor repeats steps 3
through 6 with the last known active package. A failed media flush stops before
audio or input is disabled. A rollback failure leaves the supervisor in a
failed state instead of pretending that a core is running.

## SD card layout

Each core is self-contained under its catalog directory:

```text
0:/multitini/cores/c64/core.cfg
0:/multitini/cores/c64/c64-pal.bin
0:/multitini/cores/c64/c64-ntsc.bin
0:/multitini/cores/c64/roms/basic.bin
0:/multitini/cores/c64/roms/kernal.bin
0:/multitini/cores/c64/roms/characters.bin
0:/multitini/cores/c64/roms/1541.bin
0:/multitini/cores/c64/roms/fast/kernal.bin  (optional, exactly 8 KiB)
0:/multitini/cores/c64/roms/fast/1541.bin    (optional, exactly 16 KiB)
0:/multitini/cores/c64/media/*.d64
0:/multitini/cores/c64/media/*.g64
0:/multitini/cores/c64/media/*.crt
0:/multitini/cores/c64/media/*.prg
0:/multitini/cores/gbc/core.cfg
0:/multitini/cores/gbc/gbc-default.bin
0:/multitini/cores/gbc/media/*.gb
0:/multitini/cores/gbc/media/*.gbc
0:/multitini/cores/gbc/media/*.gb.sav
0:/multitini/cores/gbc/media/*.gbc.sav
```

Battery-backed external cartridge RAM is stored beside its cartridge as
`<cartridge-name>.sav`; for example, `Zelda.gbc` uses `Zelda.gbc.sav`.
The file browser ignores save and journal files. A missing save starts erased
to `0xff`. Each update is first written and synced as
`<cartridge-name>.sav.mtj`, then copied and synced to the ordinary save file;
a complete journal is replayed at the next load if power was lost between
those steps.

The catalog scans only immediate subdirectories of
`0:/multitini/cores`. Directory and package IDs use lowercase letters,
digits, hyphen, or underscore. At most 16 cores and eight variants per core
are accepted. Unknown keys, duplicates, missing fields, unsafe paths,
oversized manifests, and directory/package ID mismatches reject that package
without hiding other valid packages.

Schema 1 uses this strict form:

```ini
schema=1
id=c64
name=Commodore 64
core_id=0x43363420
abi=0.11
content_root=0:/multitini/cores/c64/media
variant=pal|PAL 50 Hz|c64-pal.bin|4045568|0x12345678
variant=ntsc|NTSC 60 Hz|c64-ntsc.bin|4045568|0x9ABCDEF0
```

The size and CRC fields are verified before PCAP starts. The shell is probed
again afterward, so a valid file containing the wrong core or ABI is not
activated. SHA-256 values are emitted as comments for release/provenance
checking; the bounded target-side loader uses size plus CRC32.

The following commands package the fresh ABI 0.11 candidate bitstreams. They
do not rebuild or relabel the accepted release's older images.

Generate the C64 candidate package with:

```powershell
python scripts/make_core_package.py `
  --id c64 `
  --name "Commodore 64" `
  --core-id 0x43363420 `
  --content-root 0:/multitini/cores/c64/media `
  --abi-minor 11 `
  --variant "pal|PAL 50 Hz|build/vivado-ui-20260919-c64-pal-final/multitini_one.runs/impl_1/multitini_one_top.bit" `
  --variant "ntsc|NTSC 60 Hz|build/vivado-ui-20260919-c64-ntsc/multitini_one.runs/impl_1/multitini_one_top.bit" `
  --output build/core-packages-ui-20260919/c64
```

Generate the GBC candidate package from the PSRAM-enabled bitstream with:

```powershell
python scripts/make_core_package.py `
  --id gbc `
  --name "Game Boy Color" `
  --core-id 0x47424320 `
  --content-root 0:/multitini/cores/gbc/media `
  --abi-minor 11 `
  --variant "default|GB/GBC|build/vivado-ui-20260919-gbc/multitini_one.runs/impl_1/multitini_one_top.bit" `
  --output build/core-packages-ui-20260919/gbc
```

The GBC image used for cartridge testing must be created with
`MULTITINI_ENABLE_GBC_PSRAM=1`. The default-off image is useful only for
compile integration because it has no cartridge-memory backing.

The packager invokes Bootgen's Zynq bitstream processor and defaults to ABI
0.11, matching current source. Candidate firmware requires ABI 0.11 and checks
the loaded shell's dense-layout capability. Install its matching core packages
together. When reproducing an older image, pass its actual `--abi-minor`
explicitly; changing metadata cannot add dense text or menu pause to an old
bitstream. The accepted release's pinned packages remain unchanged.

## Menu

Press `F12` to open or close the menu. While it is open, menu keystrokes are
consumed and an empty keyboard report is sent to the core so no key remains
held. `Tab` advances through Cores, Media, Video, Input, Core config, and
System. Use the
arrow keys to select or change a value, `Enter` to apply it, and `Escape` to
leave a file browser or close the menu. UART command `o` is an independent
recovery way to toggle the menu.

- Cores selects an SD package and variant. C64 PAL and NTSC are separate full
  PL images under one C64 entry.
- Media browses the active core's content directory. The C64 service mounts or
  ejects D64/G64 disks, attaches or detaches CRT cartridges, and loads loose
  PRGs. For a mounted D64, the separate `Mount & Run` row browses validated
  `$0801` PRGs inside the image, stages the selection directly, remounts that
  disk, and only then types `RUN`. G64 and protected/custom-loader disks remain
  on the normal emulated-drive path.
  The GBC service loads `.gb` or `.gbc` cartridges, restores supported
  battery-backed external RAM, shows `Saved`, `Pending write`, or
  `WRITE FAILED`, and keeps the core in reset after cartridge detach.
- Video selects active-core video or the branded Multitini standby screen and applies the active
  core's timing variant. ABI 0.16 adds six nominal 60 Hz output resolutions;
  see [Video settings](VIDEO_SETTINGS.md). The reset default is 1920x1080.
  New display switching still needs physical qualification.
- Input selects the C64 symbolic/positional keyboard map and joystick port.
  Controller setup maps axes and buttons for nine shared USB slots with live
  preview; see [Appletini imports](appletini-parity.md).
  GBC uses the shared USB gamepad path plus arrows, Z, X, Enter, and right
  Shift as a complete keyboard fallback.
- Core config contains options supplied by the active core service. C64
  provides independent `Fast serial ROMs` and `CPU turbo` controls. The fast
  option atomically validates and reloads the 8 KiB KERNAL and 16 KiB 1541
  pair under `roms/fast`; the repository builds one in `c64fast/bin` and any
  other matching pair can be installed instead. On ABI
  0.10 shells that pair also runs with a DolphinDOS-style parallel cable and
  8 KiB of drive RAM at `$8000`; stock ROMs never see the cable. CPU turbo
  selects FPGA64 Smart 2x and automatically pauses around IEC disk traffic.
  Other cores show an empty-page explanation until they define options.
- System restarts the active core, explicitly enables or disables Autoboot
  for that core, or returns to the golden updater.

`Autoboot current core` is off when no valid settings file exists and changes
only when the user presses Enter on that row. A successful manual core or
timing selection records that core's preferred variant, but it neither enables
Autoboot nor retargets an existing Autoboot choice. When Autoboot is off, reset
loads no machine core: the supervisor-only FPGA shell displays `MULTITINI` and
`Press F12` until the user chooses a core.

Settings are stored in `0:/multitini/settings.bin`. The file is versioned and
checksummed and is replaced through `settings.bin.mtj`, using the same synced
write-ahead journal policy as other writable state. It records each core's
preferred variant and attached disk or cartridge, plus the C64 keyboard map,
joystick port, fast-ROM setting, and CPU-turbo setting. Migrating a schema-one
file preserves CPU turbo but deliberately maps the former warp flag to fast
ROMs Off. A missing, truncated, incompatible, or corrupt file uses safe
defaults; in particular it cannot enable auto-boot. Media and input settings
are restored when their core becomes active. A persisted fast-ROM selection is
consulted before ROM loading; if either member is unavailable, firmware reports
the fallback and activates the stock ROM profile. Runtime changes are persisted
only after the full pair reload succeeds, with the previous active pair restored
on failure. Ejecting or detaching content clears that remembered attachment. On
a card with no settings file, the established first-run C64 `drive8.d64`
fallback remains; once the user ejects it and settings are saved, later boots
leave drive 8 empty.

The ARM renders into the inactive text bank and submits the complete plane to
the video domain. The overlay changes bank and visibility only at vertical
blank, so a menu update cannot expose a partially written screen.

## Current boundary

The generic catalog, package validation, UI plane, menu navigation, guarded
PCAP state machine, and rollback policy are common. C64 and GBC activation
services are connected. GBC now uses the same menu, USB input boundary, audio
path, and a four-bank 160x144-to-960x864 video bridge. Current source also
snapshots supported battery-backed external cartridge RAM after roughly one
second without a RAM write and commits it through the common media ABI. The
machine pauses while a coherent snapshot and SD commit are in progress.
Candidate ABI 0.11 also pauses it for the visible dense menu, with save
controllers and ARM polling still available.
Detach, reset, core switch, and golden reboot first force this barrier; a
failed commit retains the dirty image and refuses the destructive operation.

The save path covers external RAM for the battery-bearing ROM/RAM, MBC1,
MBC2, MMM01, MBC3, MBC5, MBC6, Camera, HuC3, and HuC1 layouts represented by
the cartridge header. MBC3 wall-clock state and the internal EEPROM/backup
RAM used by MBC7 and TAMA are not persisted yet. These save-path changes passed
portable firmware and HDL tests. The earlier `2026090F` PAL, NTSC, and
PSRAM-enabled GBC Vivado builds, matching Vitis supervisor, and Bootgen
packaging passed.
The earlier PSRAM-enabled `2026090E` image ran both a RAM-backed MBC5 cartridge
and a no-RAM MBC5 cartridge. After installing `2026090F`, the user confirmed
physical GBC game-save persistence on 2026-09-04.
On 2026-09-14 the owner confirmed that the current GBC implementation and
saves were tested and work. This closes the functional baseline with the
retained `20260913` core; see [GBC baseline](GBC_BASELINE.md). Broader mapper,
input-device, audio, rollback, repeated-switch, and power-loss qualification
remain separate from that acceptance.
The earlier settings and supervisor-shell work passed ABI 0.9 `20260913`
Vivado, exFAT-enabled Vitis, and package builds. Board tests remain for no-core startup,
explicit opt-in Autoboot, per-core restoration, corrupt-file fallback, and
interrupted settings writes.
