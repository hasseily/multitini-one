# Amiga storage, CPU and keyboard extensions

## Status and compatibility

ARM 815 / FPGA 813 / helper07 is the retained tested rollback baseline for
the block transport, CPU choices and shortcuts. Fast IDE is a separate
extension described below. Its source and test results do not establish
card compatibility or a measured throughput improvement; those require
the selected image's qualification and board evidence. The later
[817/818 release record](AMIGA_M080DEV10.md) confirms installed Fast IDE at
28 MHz and records the remaining performance measurements.

Runtime ABI 0.27 remains unchanged. Independent D000 capability bits
identify optional machine extensions:

| Bit | Meaning | Behaviour when absent |
| --- | --- | --- |
| 25 | One-sector IDE block transport | Use the existing per-word IDE host commands |
| 24 | 68020 14 MHz execution-enable profile | Offer the existing stock and 28 MHz choices |
| 23 | Native fast IDE guest interface and actual-mode readback | Use Classic IDE; retain the saved policy |

Older ARM firmware ignores these bits. New ARM firmware checks them before
using the extension. A saved 14 MHz preference on an older FPGA runs at the
explicitly reported 28 MHz fallback and retains the preference for a capable
core. Existing stock/28 MHz values, AM01 configuration framing and settings
format remain unchanged. The new CPU value is 3; it is not sent to an old
loader that cannot accept it.

## IDE block transport

The block path moves exactly one 512-byte sector between ARM and the native
IDE management FIFO. It replaces the control transaction for each 16-bit
word with one descriptor and completion. ATA command interpretation, disk
files, interrupts, native FIFO timing and disk recovery journals retain
their existing owners.

| Offset | Meaning |
| --- | --- |
| D100 | Busy, error, done, error code and completed sequence |
| D104 | Submit sequence, word count and direction |
| D108 | Completed native 16-bit word count |
| D200 through D3FF | 128 aligned 32-bit words, one sector |

The command is `(sequence << 16) | (256 << 1) | direction`. Sequence zero
and counts other than 256 are invalid. Direction zero sends staged ARM data
to the native FIFO for a guest disk read. Direction one captures the native
FIFO into ARM's buffer for a guest disk write. D100 bits 31, 30 and 29 are
busy, error and done; bits 28 through 24 hold the error code, and bits 15
through 0 hold the completed sequence. Successful completion requires the
matching sequence, done, no error and D108 equal to 256.

ARM may access the sector buffer only while it owns it. Outbound sectors
require all 128 full-word writes before submission. Byte order matches the
legacy path: the lower half of an ARM word holds the even native word and
the upper half holds the odd word. Command and completion handshakes transfer
ownership across the control and native service clocks; RAM data is not
synchronized bit by bit. Completion follows the final native strobe and RAM
write retirement.

The legacy host and block mover cannot own the native interface together.
An uncertain or partial transfer is not retried through the per-word path.
Firmware preserves disk and journal state, reports the fault and requires a
full FPGA reinitialization through Golden or a power cycle. Selecting the
already active core does not reload its FPGA. Capability absence is the
supported legacy fallback.

## 68020 14 MHz choice

The existing 28 MHz 68020 setting remains unchanged. The 14 MHz setting
uses the existing opposite native bus phases, `ph1n || ph2n`, to permit TG68K
execution on alternating accepted native edges. These phases include the
chipset-ready slot. CPU memory retirement is qualified by the actual
`cpu_step` enable. No independent divider, new clock, PLL change or chipset
slowdown is introduced.

The PAL execution-enable ceiling is approximately 14.1875 MHz. This describes
available execution steps, not a separate CPU oscillator or measured
instruction throughput. Memory stalls and native clock holds still reduce
wall-clock progress. Native CPU tests must prove stock, 28 MHz and 14 MHz
RAM, blitter and delayed-interface behaviour before a build is accepted.

Capability bit 24 or 23 gates host opcode 11, which reads the actual native
configuration through the existing coherent mailbox. Bits 0..4 contain CPU
configuration and bits 5..11 contain memory configuration. With capability
23, bit 12 reports the actual native fast IDE selection; its meaning must
not be inferred on an older core. CPU status
reports can distinguish native 0B/03 mode bits from a requested ARM setting;
a saved preference alone does not prove that the core applied it.

## Fast IDE and Classic compatibility

`IDE mode` offers Auto and Classic. Auto is the default. It uses the native
fast IDE path only for a 68020 profile when D000 capability bit 23 is present.
Stock 68000, Classic policy and older FPGA images always use Classic IDE.
Both 14 MHz and 28 MHz retain their existing CPU execution enables and clocks.

The pinned Minimig selection is `ide_fast = ~ide_config[5] & cpucfg[1]`.
Firmware writes the hard-disk configuration through native UIO register F8:
`0x0007` for capable Auto/68020, and `0x0027` for Classic, stock or an older
FPGA. Bit 5 is an inverted compatibility flag. This is the guest-side
interface choice, independent of the ARM block transport capability.

The configuration structure adds `ide_compat`: 0 is Auto, 1 is Classic.
AM01 byte 10 carries this policy; bytes 0..9 retain their existing meanings,
and bytes 11..15 remain zero on the configuration stream. Existing settings
with byte 10 zero remain byte compatible and select Auto. New firmware sends
byte 10 zero to an older strict FPGA loader, explicitly selects Classic,
and retains the requested policy in its settings file.

An explicit Classic setting has byte 10 equal to one. ARM 815/816 reject that
new settings value. Preserve pre-update settings backups when preparing an
older ARM rollback; restoring only the old firmware is insufficient after
saving Classic. The enclosing settings checksum and media-name layout are
unchanged.

Changing the policy drains pending transfers and disk journals, saves the
settings, and fully restarts the guest with its mounts. An unfinished guest
write can refuse the change without replacing settings or mounts. The ATA
backend still handles the same commands, 512-byte sectors, bulk or per-word
host transport, error IRQs and durable write journals. Fast IDE does not enable
guest ATA DMA, change multiple-sector command support, or overlap new disk
reads with guest consumption.

`:amigaide status` separates saved policy, expected selection and native
readback. It reads actual bit 12 only with capability 23. `:amigaide auto`
and `:amigaide classic` use the same quiesce/save/restart path as the menu and
refuse while USB storage, screenshots or a display request own the relevant
resources. Requesting the already selected IDE policy performs no reset or
settings write. The existing `:amigaideprof` profiler remains disabled by default;
use fresh captures for Fast/Classic comparisons with the same CPU, HDF and
guest workload. Its combined DRQ-to-request interval includes guest work,
CDC visibility and ARM polling; it is not pure guest PIO time.

## Keyboard shortcuts

The USB Application/Menu key aliases Right Amiga. Left and right Windows/GUI
keys retain their Left Amiga and Right Amiga mappings.

Hold Control, Left Amiga and Right Amiga to request a guest reset. Either
Control key is accepted; Menu may supply Right Amiga. Reports from attached
keyboards are aggregated. Control plus both Amiga keys queues the same action
as System -> Restart active core; Control + Left Windows + Menu also works.
Mounted media and configuration are retained, and pending host writes are
drained before core reset is asserted and released. It is not a firmware or board restart, and does
not promise execution of AmigaOS reset-warning handlers.

One held chord produces one reset. Menu visibility, disconnects, core changes
or an unready core cancel pending requests. Held keys must be released before
the shortcut can rearm after a menu or reset transition.

## Qualification and measurement

Focused tests cover complete native FIFO transfers, byte order, sequence and
ownership errors, stalls, reset boundaries, legacy interleaving and incomplete
staging. A negative control must reject an unsafe FIFO read gap. Production
XPM RAM parameters and read latency need validation as well as the portable
simulation model. Keyboard tests cover aliases, release ordering and deferred
reset lifecycle. CPU tests distinguish execution opportunities from consumed
steps and elapsed performance.

The fresh build must preserve positive routed setup, hold, pulse-width and
native clock-enable margins, complete routes, zero unexpected critical CDC,
valid bus skew, DRC and timing coverage, exact source capture and matching PS7
initialization. A final routed timing failure stops further builds; other
failures are repaired without weakening these gates.

Root owns all card access and benchmarks. The bulk benchmark measures only
ARM-to-FIFO transport, not whole-filesystem throughput. Compare legacy and
bulk paths using the same image and CPU profile, and record exact firmware/FPGA
identities, test size, elapsed time and errors. Compare the 14 MHz and 28 MHz
choices separately. The package remains ROM-free and retains the original helper07;
supply Kickstart as described in `sd/multitini/cores/amiga/roms/readme.txt`.
