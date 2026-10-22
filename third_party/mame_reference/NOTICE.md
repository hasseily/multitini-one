# MAME C64 reference subset

This test-only subset comes from the official `mamedev/mame` repository at
commit `122719e8d13e2fdb7dd33886455484207de59fc8` (2026-10-22).
`import_manifest.json` records each exact upstream file's Git blob and SHA-256.
The files are unchanged. Their copyright notices and BSD-3-Clause grants remain
in place; `upstream/COPYING` preserves MAME's distribution and license text.

The source subset contains no Commodore ROMs, PLA fuse dump, game content or
MAME executable. It is not compiled into the Multitini FPGA or firmware.

`scripts/c64_mame_oracle.py` uses the upstream opcode generator and lists,
extracts the original ALU, prefetch, interrupt, execution and 6510 port methods,
and compiles them with a small device-framework shim. The only class-name
transformation flattens the inherited 6510 methods into the test class. Inline
CPU helpers and startup register values are extracted from the pinned source.
The shim supplies flat RAM, external port pins, time, callbacks, event input
and trace output. It disables MAME framework debugging and memory retry hooks.

The CPU oracle checks CPU-visible accepted access order and data, including
NMOS read/modify/write cycles and the 6510 port. Flat RAM receives the CPU's
write byte at addresses `$0000/$0001`; this deliberately exposes a CPU/port
boundary and does not claim the full C64's underlying RAM bus behavior.
The default external-pin input is `$A0`, with pullups and floating-bit decay
disabled. The full MAME C64 config sets pullups `$17`, floating bits `$C8` and
350,000-cycle decay on bits `$C0`. The separate production-port differential
test uses those settings and compares reset, writes, released pins, exact
expiry edges, re-drive/release and warm reset against the actual MAME methods.
The RTL ages pins on native C64 ticks, including RDY holds and independent of
extra accelerated CPU slots. Native-tick wiring is a separate integration gate.
MAME's reset DDR is `$FF`. The pre-redesign imported RTL reset it to `$00`;
the new port module matches MAME and has read-before-write reset coverage.

A separate executable runs the exact C64 `read_pla`, `read_memory` and
`write_memory` methods. Its PLA double supplies explicit output bits and its
VIC/SID/CIA/cartridge doubles expose fixed values and count calls. This checks
the handler mechanics, including underlying RAM `$0000/$0001` writes using
the VIC bus byte, color-RAM open-bus bits, VIC RAM banks and peripheral routing.
It does not evaluate the Commodore PLA truth table or emulate the peripherals.

The CPU boundary runner adds trace-relative IRQ/NMI/RDY stimuli, including
interrupt sampling around CLI/SEI/PLP, BRK/NMI vector selection, RTI and taken
branches. It compares relative CPU attempt ticks and SYNC as well as accepted
address/direction/data. Enable gaps change FPGA service spacing without
changing the stimulus time base. MAME's input and execution methods remain
unchanged.

The memory differential runner drives the production bus logic and compares
its read routing, peripheral selects and RAM/color backing effects with these
MAME handlers. It does not observe peripheral write addresses/data or register
effects. VIC addresses are active; BA input packing does not validate RTL
arbitration or inactive VIC ownership. PLA outputs are supplied
explicitly from the selected RTL decode; this is a conditional handler check,
not independent validation of the Commodore PLA truth table. Fixed decoder
fixtures, where present, are separate from the MAME comparison.

The VIC ownership runner extracts the pinned reset, cycle-decode, execution,
badline, sprite-DMA and fetch methods. Its framework, drawing, IRQ callbacks
and memory values are explicit test doubles. It compares native PAL/NTSC BA
and AEC frame traces with the production VIC under static register settings
after warm-up. It does not model rendering, collisions, lightpen activity or
mid-cycle register writes. Generated reference code retains the MAME license
and copyright notices and is excluded from product builds.

These are bounded reference checks. Whole-machine MAME equivalence, dynamic
VIC effects, other peripheral timing and physical-board acceptance remain
separate gates. A disagreement is retained as a failing trace and nonzero
test result, not waived or normalized into a passing conformance claim.
The installed MAME 0.289 executable is a different reference revision and is
not used as evidence for this pinned source oracle. Native timing is the
conformance target; accelerated CPU scheduling is an extension whose accepted
CPU bus semantics must remain consistent with the reference.

Run from the repository root:

```powershell
python scripts/c64_mame_oracle.py
python scripts/c64_mame_oracle.py --image build/c64-acceleration-tests/cpu/memory.bin
python scripts/test_c64_mame_port.py
python scripts/test_c64_mame_comparisons.py
```

The optional image is exactly 64 KiB and may be an original synthetic test.
The runner writes generated C++, executables, CSV vectors and a provenance
receipt under `build/c64-mame-oracle/`. A missing compiler, changed source or
failed sentinel is an error, not a skipped conformance pass. No network fetch
is required to reproduce these tests.
