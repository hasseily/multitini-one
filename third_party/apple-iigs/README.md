# Pinned Apple IIgs machine source

The exact source subset is from MiSTer Apple IIgs commit
`9adbba8622f378253765b7d438b6cbcc4d03fc57`.

`import_manifest.json` records every imported path, its license and the
license evidence. The original GPL notices and both GPL texts remain in
`upstream/`. `provenance/imports.lock.json` contains every exact file hash.

The active Multitini build selects original CPU/chip logic and generated
ports under `hdl/core/apple_iigs/ports`. Reproduce and audit them with:

```powershell
python scripts/audit_apple_iigs_import.py --source build/upstream-apple-iigs
python scripts/port_apple_iigs_core.py --check
python scripts/test_apple_iigs_ports.py
```

The ports preserve the original CPU bus and VGC raster timing. Their changes
are bounded:

- Infer enabled synchronous, write-first dual-port RAM in place of Intel
  `altsyncram`. Keep slow/video RAM and DOC wave RAM inside the FPGA.
- Load character, ADB and slot-7 controller ROMs at runtime. No Apple or
  TransWarp firmware bytes are included in this source import.
- Start PRAM with an invalid zero checksum so the user's system ROM installs
  its own defaults; restore saved PRAM through the existing backup port.
- Hold CPU and bus side effects through external memory stalls, including
  writes and HDD DMA. Export raw bus cadence and raster position to the
  adapter.
- Correct the CPU's WAI wake test to use active-low `IRQ_N`, normalize the
  emulation stack page at instruction completion, and clear index high
  bytes in the same status-width change cycle. Independent SingleStep
  vectors cover arithmetic, stack and flag outcomes; directed tests also
  cover IRQ/NMI entry, masked-IRQ wake, RTI, STP and reset.
- Restore `default_nettype wire` at UART boundaries, declare ES5503's
  procedural channel output as `reg`, and move IIR declarations before use.
- Infer the 2048-by-53 microcode table as synchronous block ROM. A separate
  asynchronous reset selector supplies the original reset/state-zero word,
  retaining the original one-edge decode latency and clock-enable hold.
- Add explicit WOZ dirty/flush signals and scale real-time settle/flush
  delays for the selected controller clock.
- Represent TransWarp as an absent card. ZipGS register logic and host CPU
  speed controls remain available without TransWarp ROM dependencies.

The HDD slot ROM is open-source AppleWin firmware. Assemble its licensed
source, rather than copying a generated upstream image:

```powershell
python scripts/port_apple_iigs_core.py --build-hdd-rom
```

The generated `build/apple-iigs-assets/hdd.rom` is exactly 256 bytes, with a
source/output hash record beside it. The Apple system, character and ADB
ROMs remain user-supplied runtime files.

Independent CPU inputs come from MIT-licensed SingleStepTests/65816 commit
`c5f9c8b32c7d9a207340df6960571bbda252c8fa`, kept under the ignored build
directory. `scripts/test_apple_iigs_cpu.py` records the exact vector hashes
and functional register/RAM checks. Directed tests provide separate stall
and interrupt evidence. Neither source audit nor simulation establishes
physical board support.
