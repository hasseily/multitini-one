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

These are bounded reference checks. Whole-machine MAME equivalence, VIC BA/AEC
timing, peripheral timing and physical-board acceptance remain separate gates.
The installed MAME 0.289 executable is a different reference revision and is
not used as evidence for this pinned source oracle. Native timing is the
conformance target; accelerated CPU scheduling is an extension whose accepted
CPU bus semantics must remain consistent with the reference.

Run from the repository root:

```powershell
python scripts/c64_mame_oracle.py
python scripts/c64_mame_oracle.py --image build/c64-acceleration-tests/cpu/memory.bin
python scripts/test_c64_mame_port.py
```

The optional image is exactly 64 KiB and may be an original synthetic test.
The runner writes generated C++, executables, CSV vectors and a provenance
receipt under `build/c64-mame-oracle/`. A missing compiler, changed source or
failed sentinel is an error, not a skipped conformance pass. No network fetch
is required to reproduce these tests.
