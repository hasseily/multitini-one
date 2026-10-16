# Apple IIgs CPU validation

Initial ISA qualification was recorded on 2026-10-14; current CPU integration
was rechecked on 2026-10-15. The nine CPU/microcode source files retain their
qualified hashes, so the original full vector result carries forward by source
identity. This is not a new 5.12-million-vector run. The complete-machine
M0.4.0 image passes routed timing: setup `+0.040 ns`, hold `+0.049 ns` and
pulse width `+1.250 ns`. Physical compatibility remains a hardware test.

The accepted `xc7z020clg484-2` implementation uses 35,813 LUTs, 24,529 flip-flops,
119 RAMB36 plus 10 RAMB18 blocks (124 of 140 equivalent block-RAM tiles), and
24 DSPs. All 55,633 nets are routed; the CDC gate has zero critical rows.
The original 174-file capture and source archives are unchanged. Physical
replication/routing and the exact-scope Vivado constraint replay are published
with the selected reports under `reports/repair/`. The final export reruns
the native phase proof and verifies all four setup/normal-hold pairs after
methodology, power and exception reporting. See the
[reproduction procedure](APPLE_IIGS_PLAN.md#validation-gates).

The optimized CPU completed **5,119,956 exact golden-vector passes**, with
**44 separately reviewed WDC boundary disagreements** and **zero unexpected
failures**, out of 5,120,000 vectors. The 44 are not golden passes.

## Sources and repeatable checks

Machine source: [MiSTer Apple IIgs](https://github.com/MiSTer-devel/Apple-IIgs_MiSTer/tree/9adbba8622f378253765b7d438b6cbcc4d03fc57),
commit `9adbba8622f378253765b7d438b6cbcc4d03fc57`. The portable CPU and microcode
are derived reproducibly by `scripts/port_apple_iigs_core.py`.

Independent functional oracle: [SingleStepTests/65816](https://github.com/SingleStepTests/65816/tree/c5f9c8b32c7d9a207340df6960571bbda252c8fa),
commit `c5f9c8b32c7d9a207340df6960571bbda252c8fa`, MIT, Thomas Harte et al.
There are 256 opcodes, both emulation and native modes, and 10,000 vectors per
opcode/mode. The approximately 2.87 GB dataset stays in the ignored build tree;
its pinned Git blobs were verified when fetched. Every execution records SHA256
hashes of the selected raw JSON files and all model inputs.

With that checkout at the pinned HEAD, run:

```text
python scripts/test_apple_iigs_cpu.py --all-opcodes --fetch-only
python scripts/test_apple_iigs_cpu.py --all-opcodes --accept-documented-boundaries --build-dir build/apple-iigs-cpu-optimized
python scripts/test_apple_iigs_cpu_policy.py
python scripts/test_apple_iigs_ports.py --build-dir build/core-gs3/cpu-ports-primary-peek-tests
```

Omitting `--accept-documented-boundaries` applies the raw oracle and makes all
44 disagreements fatal. With the option, only entries in
[`apple_iigs_cpu_known_vectors.json`](../hdl/sim/apple_iigs_cpu_known_vectors.json)
receive a boundary tag. The runner verifies the raw file hash, exact vector name,
zero-based index, and SHA256 of the complete canonical vector JSON. It then
checks **all ten final registers, every specified final RAM byte, and the exact
write sequence** against the explicit alternate result. The pointer case also
requires reads at `$F4FF` and `$F400` and rejects a read at `$F500`.
An allowlisted vector unexpectedly matching the raw oracle fails, as does a
missing expected exception or any other changed result.

Negative policy tests passed: the recognized control is classified separately;
a missing tag, changed final register, and changed stack write data each fail.
Evidence is in `build/apple-iigs-cpu-optimized/policy-validation.json`.

Full run inputs, exact counts, individual alternate outcomes, and source hashes
are in `build/apple-iigs-cpu-optimized/validation.json`; its execution log is
`build/apple-iigs-cpu-optimized/simulate.log`.

## Review of the 44 oracle disagreements

The primary reference is the March 13, 2024
[WDC W65C816S datasheet, page 50](https://www.wdc65xx.com/wdc/documentation/w65c816s.pdf#page=50).
Section 7.1 explicitly includes `JSR (a,x)` among emulation operations whose
multi-byte stack accesses can cross page 1. Section 7.2.2 keeps emulation direct
accesses in the selected page when DH is nonzero and DL is zero, with stated
exceptions for long indirect addressing and PEI. `SBC (d,x)` is not an exception.

**43 `FC` / JSR (a,X) vectors:** E=1 and the initial S low byte is `$00`.
A running emulation CPU starts the access at `$0100`. The raw oracle puts return
address byte two at `$01FF`; the core decrements the intermediate full-width
stack address and writes it at `$00FF`, then restores architectural S=`$01FE`.
All final registers match the raw oracle. Both return bytes are checked:
`[$0100]=high(PC+2)` followed by `[$00FF]=low(PC+2)`. `$01FF` must retain its
initial value (zero in these sparse fixtures), all other specified RAM must
match, and no additional write is accepted. These are the page-crossing
operations described by WDC section 7.1.

**One `E1` / SBC (d,X) vector, `e1 e 8669`:** E=1, D=`$F400`, operand=`$B0`,
X=`$4F`, A=`$3109`, P=`$B2`, DBR=`$66`. The pointer low byte is read at `$F4FF`.
The raw vector reads its high byte at `$F500`, forming `$663E2F`, with data `$B3`,
and expects A=`$3155`, P=`$30`. The WDC page rule instead reads `$F400`, which is
zero in this fixture, forming `$66002F`, also zero in the sparse memory.
The required core result is A=`$3108`, P=`$31`; all other final registers and RAM
must match the raw vector and there must be no write. The expected final PC is
`$1105` and S is `$0147`.

These decisions follow the manufacturer's documented addressing rules; they
are not claims of physical chip measurements. The independent executed-program
bench `tb_apple_iigs_cpu_boundaries.sv` makes the wrapped and unwrapped pointer
locations hold different data and separately checks the extended stack access.
It passed with the frozen optimized CPU.

Raw vector files used by this review:

| File | SHA256 |
| --- | --- |
| `fc.e.json` | `6c8e56f17f0b23b496df3b8ef47de42dae5390f7bfd285e450f00db82e00c0fa` |
| `e1.e.json` | `e7b1d4ea4b44950a66006c7d6de5c94c83476c4993042b5e66ed90b706d445f7` |

Exact allowlist below. The hash covers each complete original vector, using
UTF-8 JSON with sorted keys and separators `(',', ':')`. Names are the source
names; JSON indices are one less than the numeric suffix.

| Vector name | Boundary | Canonical vector SHA256 |
| --- | --- | --- |
| `fc e 458` | Stack | `787a91fecdeec7f8bc5cdf616d790ba0a48e250156b46bbe94e5c95c94de6784` |
| `fc e 811` | Stack | `e808f0844789d6d27a5626136f35d5c99bd147437a4f1b78a6397e6da07b7d53` |
| `fc e 831` | Stack | `f710d98c3a2dc949721fef45c8bd1ff40abe0bb8c4a7e979a86562d0b78ea73a` |
| `fc e 1155` | Stack | `5d1ebcda96e5217d5e906ad1754c545ea6bae39adfce9b1c3ca2214f38e5e083` |
| `fc e 1322` | Stack | `69707f57f7384b81d4c8643160ea65e00ca1b2ca163b200aabefb3bf21989173` |
| `fc e 1600` | Stack | `c7a0679c57609e96b4676abbd69e5af926cd2406edb6e8e7de167e49dc30f330` |
| `fc e 1727` | Stack | `ff840c36a0334696d98787f111865bd714688ba142972d036b9c53216191a33b` |
| `fc e 1755` | Stack | `cf2c9a9409a23f6cad5a0fabdd6c96a7d4df04e2238a1ba9e3606b80f349aefb` |
| `fc e 1964` | Stack | `0b5eaaf0ddb1d8327ca43ab3caa120d83742784f562873d9fe00e858b6693da0` |
| `fc e 2289` | Stack | `df0e9f2604ac6e6f2cabe67137a10fc8d4dc9869d6f4fe52ea962babb5d72bfd` |
| `fc e 2589` | Stack | `aca99a1c86c1349e2f7b33552d998f1bdd6b74b8b77a6e828d1c3c93a20e5795` |
| `fc e 2857` | Stack | `47c7dff9fcd09861778d0db7603f5fa244c803171cb6fdeeaf444549cfed2528` |
| `fc e 2923` | Stack | `8deb7b1a31bbd8c6e0e35b7c07184d55ba1534caa54b094c743a437929a0e139` |
| `fc e 3001` | Stack | `375cf014551473288701fcf94dce472d337e85096ffe2ab78cdeb058c79f35d4` |
| `fc e 3191` | Stack | `cd08015d54dc56e87a54b3eb9604848770069579ba237a797e6cf621bb10e464` |
| `fc e 3213` | Stack | `417847791753bccc1f9bb9da40902867b2b3610b6faa92c42de11e6de9ad6579` |
| `fc e 3466` | Stack | `f3a70bf192f2ced26d7ad6b3328da8cdea764b03321fb967f35c89a9f72965e2` |
| `fc e 4265` | Stack | `69f6fcf6f8a84331c7efdc465398ac92338b3d63a73ae29026a895394fdf5eca` |
| `fc e 4346` | Stack | `71d4f6d44b36fe930469d862ba327847a7374576ca781cf151e131db9d6a5c42` |
| `fc e 4648` | Stack | `9e0a73ca36fec9f78817d3a8a185b1071f3478c726ba8b22afc6887074cff0b3` |
| `fc e 4773` | Stack | `d7e49e0b7648aed9a204e3676255aa8f96bb0f1888d40de84c0ae387b8556d4a` |
| `fc e 4904` | Stack | `befe9a19b2e18dee01f36c210fc8554d6403d438f31b3eec394ca466566647cd` |
| `fc e 6055` | Stack | `a81b269fc3e43a4b8d7b328337eb7da64738c2153004da507bd6d6bb49cc79d3` |
| `fc e 6139` | Stack | `05a61c625c4813a6ccb54b719d92ce9d3d89016706a2b314cc398e38abd05503` |
| `fc e 6201` | Stack | `c2751ee1273dd152d2e9b995ee57f17940cdf85f6910ec319f1a0942daecca3a` |
| `fc e 6351` | Stack | `858c27d38282539dcbac4b8b917115f5d36796d7f98ba8e898183076ba6ee146` |
| `fc e 6799` | Stack | `8915db40f8d29577ce34dc133d68a4c299ba0ea69aa6f4babe73ee6b413c4690` |
| `fc e 7003` | Stack | `711b64f3c96cb85acb0b9d996249ace6cde13212c4682eef9365df9d1aa5d25d` |
| `fc e 7054` | Stack | `58f4cb8bee569325789c7771ce433e50206b8d4434010e9a46d218a1fc567c45` |
| `fc e 7122` | Stack | `d21ec09226f1a805f18e0b11701b160b7277e1785f112ec57d0c708399474887` |
| `fc e 7155` | Stack | `2e0b13c3313eb6059a35bbc03729e1e745c9efbe00985628634ce725c58dbbf0` |
| `fc e 7314` | Stack | `7f212356d32273328730955889e391b2165e9f1c3f64e7a6910d48b94f240628` |
| `fc e 8224` | Stack | `548f660fd9eabb4eda31f31fc0c563bac8c8321bef8b22c517ea3290b3b36266` |
| `fc e 8255` | Stack | `9127c361de5d1c65a977b1d840f40c99174958b59df8dbfa81652622515e0abf` |
| `fc e 8303` | Stack | `cc3a152aead1ff41f796c1b20723d2ce6a1728d89cd5943d8d0f6b1a2ab334dd` |
| `fc e 8371` | Stack | `8392dfd5b9b4be4a1469b6254c9c1c3ca7beb3432de43f768bbbee371563e44b` |
| `fc e 8428` | Stack | `24341200a0ab114b41c61c495ebfd7e7639cc4acf21377a1ddf316bffeb2f0e3` |
| `fc e 8926` | Stack | `886006c22ff9a2a2055cccee30f2f5281f6e52f5015cf0df8089543fd0dc6e70` |
| `fc e 8947` | Stack | `31314925e5bcb28d70f1f4a3c47ebc4c5c0e4917594d0348d46de9821159588a` |
| `fc e 9030` | Stack | `7f95fe046a800efcb74bfb750307b26febf43356beda4f8434598ea3edda3ae3` |
| `fc e 9088` | Stack | `e8faeba2a4da3e569be0e128a188fb9a564a02da057c10f81f5a0650ea74d423` |
| `fc e 9145` | Stack | `da2528f6e92c66caf66927e8e1226b06c409147946c978809d4504abb4cacf1e` |
| `fc e 9282` | Stack | `7742bcbea94323556df3ef3e199475088062103019f480b9ec983c59dd7306e2` |
| `e1 e 8669` | Direct pointer | `dcedf445855c74fe321f60a8d485040b5c25a59e31c07a229cc916529f76d25b` |

## Bounded CPU corrections and directed tests

The derived CPU retains the pinned implementation and applies three corrections
exposed by the functional and sequence tests:

- WAI wakes on asserted active-low IRQ, including the masked IRQ resume case.
- Architectural S high byte returns to `$01` at instruction completion in
  emulation, preserving intermediate extended stack accesses.
- X/Y high bytes clear in the flag-load cycle when PLP, RTI, or SEP sets the
  eight-bit index-width flag.

The current directed run passed complete machine lint and all five benches:

| Bench | Checked behavior |
| --- | --- |
| `tb_apple_iigs_ports.sv` | RAM enable/latency/write-first, runtime ROM upload, blank/restored PRAM |
| `tb_apple_iigs_cpu_stall.sv` | Read and write RDY stalls hold address/data/status; one resumed write |
| `tb_apple_iigs_interrupts.sv` | WAI sleep/masked IRQ wake, IRQ/NMI entry and RTI, STP/reset |
| `tb_apple_iigs_mcode.sv` | 6,148 comparisons: all 2,048 ROM words, EN hold, STATE0 and async reset |
| `tb_apple_iigs_cpu_boundaries.sv` | Independent executed WDC direct-pointer and extended-stack programs |

Hashes and results are recorded in
`build/core-gs3/cpu-ports-primary-peek-tests/validation.json`. Full machine/adapter,
DDR, media, video and final routed timing qualification are separate from this
CPU suite and belong to the release's machine/build evidence.

## Microcode resource result

The 2,048 x 53-bit microcode table now infers synchronous block ROM. An
EN-qualified word register preserves its single-edge lookup latency; a separate
asynchronously reset default selector handles reset and STATE0. The table word
has no reset, allowing Xilinx block RAM inference. The comparison bench uses
the original pinned module as its reference, including stall and reset behavior.

Vivado 2025.2 OOC synthesis for `xc7z020clg484-2` measured:

| Measure | Original | Optimized |
| --- | ---: | ---: |
| Raw LUT primitives | 563 | 61 |
| Slice LUTs after Vivado's LUT-combining adjustment | 495 | 61 |
| Registers | 53 | 1 |
| RAMB36 / block RAM tiles | 0 | 3 |
| F7 / F8 muxes | 49 / 8 | 0 / 0 |

Reproduce with `vivado -mode batch -source scripts/synth_apple_iigs_mcode.tcl`.
Evidence: `build/apple-iigs-mcode-tests/resource.log`,
`original-utilization.rpt`, and `optimized-utilization.rpt` in that directory.
The original machine-context synthesis attributed 2,176 LUTs to microcode;
that context-dependent number must not be substituted for the OOC table above.
The OOC comparison has no timing constraints or routed timing proof. The final
full-core build determines overall resources, critical paths and achievable
acceleration on this board.
The initial M0.3 release used a 14.318182 MHz CPU clock ceiling. The M0.4
engine below runs the same qualified P65C816 and microcode on the primary
57.272728 MHz clock. It does not claim a globally optimal CPU implementation.

## Production adapter execution throughput

Run `python scripts/test_gs_acceleration.py` after assembling the licensed HDD
fixture with `python scripts/port_apple_iigs_core.py --build-hdd-rom`.
This test instantiates the production adapter and uploads an original synthetic
system ROM, zeroed auxiliary fixtures, the licensed HDD ROM, and each speed
configuration through the production loader. Normal ZipGS speaker, paddle,
counter and IRQ delay settings remain enabled; CPS follow remains off.

The native 16-bit kernel is `INC A; INY; DEX; BNE`, confined to one 8-byte ROM
cache line. It executes 16,384 iterations, excluding the first 256 for warmup.
Each cached measured interval covers exactly **16,128 iterations and 145,151 actual
CPU advances**, with **zero cache misses and zero cache wait ticks**. All
settings reach the same checked final registers and long RAM store:
A=`$5234`, X=`$0000`, Y=`$4000`, P=`$02`, S=`$01FF`, D/DBR/PBR/E=0,
PC=`$E08A`, and `[$02:0000]=$5234`.

| Configuration | Elapsed primary ticks | Effective CPU MHz | Ratio to measured native |
| --- | ---: | ---: | ---: |
| 0 | 2,909,404 | 2.857353 | 1.000000 |
| 1 | 2,322,416 | 3.579546 | 1.252749 |
| 2 | 1,741,812 | 4.772727 | 1.670332 |
| 3 | 1,161,208 | 7.159091 | 2.505498 |
| 4 | 580,604 | 14.318182 | 5.010995 |
| 5 | 290,302 | 28.636364 | 10.021991 |
| 6 | 145,151 | 57.272728 | 20.043982 |
| ZipGS software 100%, starting at host setting 3 | 145,151 | 57.272728 | 20.043982 |

Rates use the nominal 57.272728 MHz primary clock and counted elapsed ticks.
The test enforces increasing execution rates and primary-clock divisions
20/16/12/8/4/2/1 within 0.5%. Native reaches 99.780574% of its nominal rate because of
the native NTSC line stretch; accelerated settings match their nominal rates
in this measured window. Source hashes, raw counts and rate calculations are
in `build/core-gs3/acceleration-primary-peek-tests/validation.json`; the independent register,
memory, cycle-count and cache assertions are in its `simulate.log`.

The program executes **CLI** with interrupt sources inactive. Normal ZipGS
IRQ delay intentionally restores native speed while I=1, so an SEI-only
program cannot demonstrate configured acceleration. The interval excludes
startup, cache misses, I/O and the final RAM store. This register-only warm-ROM
kernel proves selectable execution throughput; it does not predict whole
applications, disk/graphics throughput, physical oscillator accuracy or
performance measured on the card. A coherent eight-word asynchronous L0
serves this warm loop; the synchronous 32 KiB L1 and DDR remain behind it.
CPU writes, loader traffic, HDD DMA and software cache-disable invalidate L0.
Since the [acceleration rework](APPLE_IIGS_ACCELERATION.md), writes are posted
into a merging buffer rather than waiting for DDR acknowledgement. A
cache-missing application still has lower effective throughput than this peak
kernel.
The ninth measurement explicitly disables the cache through `$C059.7`:
**80,640 completed CPU memory reads produce exactly 80,640 DDR requests**,
with 645,120 wait ticks and the same final state. This latency-controlled case
measures 10.519421 MHz, not the cached peak. The earlier retained-token defect
made only 64,512 requests; the regression rejected it. Generic peak-test
descriptions in the raw JSON apply to its eight cached cases; the ninth case's
explicit `cache_bypass`, read, miss and wait fields describe this separate test.

The eight-word read cache uses raw CPU address tags to keep MMU remapping off
the CPU data path. Actual memory classification still controls CPU completion
and DDR requests. Native I/O retirement, configuration commit, resets, every
write, loader activity, DMA and cache-disable invalidate it. Native retirement
clears the cache after native read data is captured. Invalid internal CPU cycles
retain the L1 floating-bus value. Vector pulls bypass this small cache: exact
forced-ROM vector bytes can share a raw eight-byte word with RAM. The larger
L1 cache retains physical tags and services those vector reads normally.

Run `python scripts/test_apple_iigs_logical_cache.py --build-dir build/core-gs3/logical-cache-primary-peek-tests`
for language-card read switches, RAMRD main/aux mapping, invalid-cycle floating
bus, mixed vector/RAM words, warm reset and live ROM selection with both ROM
variants loaded. Three deliberately broken variants must fail at their exact
checks: VPB-only word tags, omitted configuration invalidation and omitted raw
CPU validity. These expected rejections are separate from the passing
production fixture. The test drives the CPU bus boundary; actual instruction
execution remains covered by the throughput, machine and CPU tests.

CPU read-data selection now has separate CPU and native/DMA views. A second
instance of the pinned stateless MMU receives the raw P65 address and the same
complete switch/configuration state; only its internal-IO classification is
used for CPU ZipGS reads. ZipGS exposes a second read-only selector over the
same register state. The original shared MMU and ZipGS selector still serve
native/DMA reads. CPU fallback bytes use the raw address's low three bits;
MMU and ROM remapping preserve those bits. CPU enable remains blocked during
HDD DMA. This removes DMA-address logic from the CPU data selector while
retaining native byte selection, invalid-cycle floating bus and mapping rules.
CPU completion still uses the actual memory/native classification. No CPU/cache
timing exception is added.

Run `python scripts/test_apple_iigs_cpu_peek.py --build-dir build/core-gs3/cpu-peek-primary-freeze-tests`.
Each of its zero- and 1-ps clock runs passes **589,824** bank/config comparisons,
**24,576** deliberate CPU-versus-DMA IO-class disagreements, **16,384**
independent ZipGS read-selector/state comparisons, and **256** cached/bypass,
valid/invalid byte-lane combinations. The bank/config sweep covers every CPU
bank and slot-selection byte, IOLC/all-bank shadow/read-write combinations,
both ROM/VPB modes, full MMU input-state patterns and Zip visibility. The test
also retains all six real loader/native-IO hot-mapping and floating-bus groups,
checks native/DMA data separately, and verifies that DMA leaves CPU and retained
floating-bus state held. These are bus-boundary tests; the nine throughput and
twelve machine scenarios separately execute the actual P65 CPU.

ZipGS software percentages now quantize down against the installed maximum;
100% selects its maximum and 50% selects half that rate. The host retains
the finer low rates when the sixteen software percentages cannot express
them. The calibration counter still toggles every 7,331 native CLK14 ticks.
The derived reset value `$C059=$77` makes default IRQ delay agree with the
host setting; upstream `$57` left bit 5 clear despite its accompanying comment.

Run `python scripts/test_gs_external_cpu.py --build-dir build/core-gs3/external-cpu-primary-peek-tests`
for all 48 software-percentage mappings across maximum settings 4/5/6,
host capability clamping, disable/re-enable, calibration timing, SEI/CLI
compatibility, and independent direct-clock CPU/native-bus tests. The bridge
tests verify exact SCC, DOC, clock, text and slow-RAM results, exactly-once
native returns, and architectural holds on CPU CE=0. Native effects remain
on CLK14; a phase1 preparation flag and phase0 ready publication ensure a
complete native read-preparation edge precedes the phase2 commit publication.

The same script also drives the CPU output boundary through 14 isolated ISR
watch scenarios with both zero and 1 ps native-clock propagation. The upstream
IRQ controller watches an opcode fetch at `$FE:FC3A` to latch a VBL cause. At
57 MHz a fetch can fit entirely between native edges, so the port captures
accepted `CPU_CE` fetches and their complete event-time eligibility on the
primary clock. An accumulator coalesces requests until phase0 publication;
the published flag stays stable across the following native edge. The native
controller consumes it independently of later VBlank/INTEN changes, retaining
the existing C047 clear priority. Already-pending VBL causes do not create new
requests. Cases cover every arrival phase, consecutive publication windows,
coalescing, CE=0, enable/blank eligibility, the last VBlank tick, later enable
changes, pending/clear behavior, and reset. This boundary test qualifies event
transport; it is separate from P65 instruction execution and ROM boot tests.

SEI/CLI speed gating remains a primary-clock combinational path from the
CPU's architectural I flag. Native FPI divider status can adapt one sampling
edge later around a speed transition; actual I/O remains held until its
prepared native commit. Since the acceleration rework, accelerated E0/E1 RAM
and shadow writes use the primary-clock slow-RAM path instead. The active transient opcode
watch is transported explicitly; unused TransWarp boot/fetch watches do not
participate in this build.

An isolated Vivado 2025.2 routed P65C816 trial at 57.272728 MHz on
`xc7z020clg484-2` measured internal setup slack **+2.575 ns**, internal hold
**+0.145 ns**, 1,042 slice LUTs, 262 registers and three RAMB36 blocks.
Evidence is in `build/core-gs3/cpu-ooc/` (`cpu57.tcl`, `internal_paths.rpt`,
`internal_hold.rpt`, `utilization.rpt`). This OOC trial has ideal zero-delay
external ports: its input-hold failures are not a closed whole-system result.
The complete machine must separately pass all actual setup, hold and CDC gates.

## Native clock publication

The native clock uses a synchronous `BUFGCE` on the primary clock, enabled for
phase3. Its low-transparent enable latch gives one positive native edge every
four primary ticks, preserving the 14.318182 MHz tick cadence. The high pulse
is one primary half-period, or one eighth of the native period. CPU registers
remain on the 57.272728 MHz primary clock; native peripherals and slow-RAM port
A retain the native positive edge, while video RAM port B uses the primary clock.

The old fabric divider's one extra phase0 edge immediately after global reset
is omitted. Native reset is still held during that startup interval, before any
live transaction. The native clock test extracts the actual production clock
block and compares it with the prior divider across five reset epochs. At each
of zero and 1 ps clock propagation, it verifies 135 live edges, 541 live-phase
comparisons, and 5,256 pulse-width checks, including the five reset-held startup
omissions. Run `python scripts/test_apple_iigs_native_clock.py --build-dir build/core-gs3/native-clock-final`.

Phase1 preparation, phase0 readiness/event publication, phase2 native hold and
commit publication, phase3 native capture, and primary retirement retain their
existing order. Synthesis/export gates must prove the exact clock input and
phase3 enable, positive-edge sequential consumers, and absence of clock-as-data
loads. Actual clock pulse width, CE setup/hold, related-clock setup/hold and CDC
coverage remain full-build timing gates; the functional pulse test does not
substitute for them. A historical experiment label remains in one frozen
adapter comment; the selected clock logic is production source.

## Native memory completion and pause

`python scripts/test_gs_memory_phases.py` exercises the actual native CPU
through the production cache and adapter at CPU setting 6. Final runs are in
`build/core-gs3/phase-primary-peek-enabled-tests/` and
`build/core-gs3/phase-primary-peek-disabled-tests/`; reproduce the latter with
`--cache-bypass`. Their 64 scenarios cover
all four DDR response phases, initially absent and primed cache lines, zero
and 1 ps generated-clock delay, and phase-2/3 pause assertion and release.
All 2,048 byte writes are accepted, acknowledged, advanced and retired exactly
once. Readback, final memory and untouched DDR lanes agree. There are no
overwritten completions or parent/native commit races.

The CPU now consumes completions on the primary clock and retires the cache
on the same edge. The native HDD DMA path retains its phase1 lookup and phase2
published hold. Both phase2 and phase3 CPU pauses preserve outstanding writes
until resume; all 128 pause/resume events complete and retire once. The tests
verify actual primary CPU enable and the architectural/bus tuple while held.
Source hashes accompany the raw logs. Routed timing and hardware behavior
remain separate gates.

The acceleration rework posts writes, so the test now checks each byte's
retirement against its cache enqueue and compares DDR only after the buffer
drains. It also adds read pauses, eight scenarios per cache mode. Those runs
first read the program's code into the cache, then store and read back one
cache line at a time, newest word first. One kind pauses four target
read-backs during their line fill or single-beat bypass read. The other
pauses four reads that wait for posted writes to drain: the read-back of the
word just written with the cache on, or, in bypass mode, the instruction
fetch after a store. Each paused read retires once, after release; paused
read-backs return the stored byte.

`python scripts/test_apple_iigs_adapter.py --build-dir build/core-gs3/adapter-primary-peek-tests`
passes twelve machine cases: seven host speed settings, cached and
software-bypassed HDD DMA at the 57 MHz CPU setting, native WOZ reads, track
writes and dirty eject. Every HDD sector byte, failed-write retry and ACK
retention is checked. This is synthetic transport qualification, not an Apple
system ROM boot or physical media acceptance result.

The 128 KiB slow/video RAM uses eight 16 KiB banks. Each port registers its
bank selection on the same enabled edge as its data read, preserving one-edge
latency, write-first behavior, zero initialization and output hold when disabled.
Run `python scripts/test_apple_iigs_slowram.py --build-dir build/core-gs3/slowram-native-gate-mapping-final-tests --vivado C:/Xilinx/2025.2/Vivado/bin/vivado.bat`
to check the full address space with unrelated port clocks, cross-port readback
and every bank boundary. The test passes 393,248 reads, 262,160 writes and
655,424 edge checks. The isolated `xc7z020clg484-2` synthesis and placement
maps 32 parallel 16Kx2 RAMB36 blocks with no cascade extensions or DRC errors.
This avoids the unbanked array's illegal optimized cascade address mapping;
it is a RAM mapping gate, with complete-machine timing checked separately.
Concurrent cross-clock accesses to the same byte during a write remain undefined.

## Scope and remaining physical proof

The independent vector run compares final registers and relevant RAM after
single-instruction state injection. It is not complete bus-cycle equivalence,
all operand combinations, or physical IIgs hardware qualification. Arbitrary
emulation input S high bytes in the dataset are normalized to the architectural
page `$01` before execution. Block-move vectors use the oracle's bounded cycle
window, rather than requiring an arbitrarily long move to finish. WAI/STP and
hardware interrupt sequencing use the separate directed benches.

Firmware/FPGA builds and synthetic machine tests do not prove ROM boot, ADB
feel, sound, live video, removable-media reliability or acceleration on the
physical card. Those remain hardware acceptance checks in the release test plan.

The historical M0.4.0 source-bound functional records are indexed in
`build/core-gs3/cpu-integration-primary-peek-summary.json`. All 13 selected
records matched that build's source inputs, and its 174 captured inputs were
checked unchanged. M0.5.3 records current integration checks in
`build/core-gs-m053/cpu-hdl-acceleration-final-regressions.json` and paused live
configuration checks in `build/core-gs-m053/paused-settings-final/validation.json`.
It retains the ISA qualification by verifying all nine CPU leaf hashes.

M0.5.3 also tests the actual shadow-read and shadow-write paths. A warm read
loop improves 22.0483x at maximum speed, while a native shadow-store loop
improves 2.00558x. Both use the production CPU and memory adapter. These
workload-specific measurements are not a Finder redraw benchmark. Fresh
firmware defaults IRQ delay to Off; synthetic peak tests may explicitly enable
different compatibility settings to exercise their gates. See the
[M0.5.3 record](APPLE_IIGS_M053_BUILD.md) for reproduction and evidence scope.
Routed signoff is separate in the selected image's reports and export receipts.
