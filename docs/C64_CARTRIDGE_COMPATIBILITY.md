# C64 cartridge and expansion compatibility

The `CART_COEXIST` expansion capability replaces the blanket REU/cartridge
exclusion. The stored REU, UCI and Ultimate Audio settings remain requests.
The FPGA suppresses a peripheral only while the attached cartridge conflicts
with it. Ejecting that cartridge restores the requested peripheral. Loading
temporarily suppresses expansion accesses. Older images retain their previous
exclusion, and the new ARM firmware rejects FC3 before streaming to them.

## Supported profiles

| CRT type | Cartridge | REU / UCI | Ultimate Audio |
| --- | --- | --- | --- |
| 0 | Normal | Allowed | Allowed |
| 3, subtype 0 | Final Cartridge III | Blocked | Blocked |
| 3, subtype 1 | Final Cartridge III with REU, including UBoot64 | Allowed | Blocked |
| 4 | Simons' BASIC | Allowed | Allowed |
| 5 | Ocean type 1 | Allowed | Allowed |
| 7 | Fun Play | Allowed | Allowed |
| 8 | Super Games | Blocked | Blocked |
| 10 | Epyx FastLoad | Blocked | Blocked |
| 11 | Westermann Learning | Blocked | Blocked |
| 13 | Final Cartridge I/II | Blocked | Blocked |
| 15 | C64 Game System | Allowed | Allowed |
| 17 | Dinamic | Allowed | Allowed |
| 18 | Zaxxon | Allowed | Allowed |
| 19 | Magic Desk | Allowed | Allowed |
| 21 | Comal 80 | Allowed | Allowed |
| 22 | Waterloo / Structured BASIC | Allowed | Allowed |
| 32 | EasyFlash | REU blocked; UCI at `$DE1B-$DE1F` | Blocked |
| 28 | Mikro Assembler | Blocked | Blocked |
| 43 | Prophet64 | Blocked | Blocked |
| 54 | Kingsoft Business Basic | Allowed | Allowed |
| 57 | RGCD | Allowed | Allowed |
| 64 | Blackbox V8 | Blocked | Blocked |
| 65 | Blackbox V3 | Blocked | Blocked |
| 66 | Blackbox V4 | Blocked | Blocked |
| 71 | Blackbox V9 | Allowed | Allowed |
| 85 | Magic Desk 2 | Allowed | Allowed |
| 86 | Protovision Megabyter, ROM-only profile | Allowed | Allowed |
| 87 | Protovision TwoMegabyter | Allowed | Allowed |

All profiles except FC3 require subtype 0. FC3 requires exactly four ordered
16 KiB ROM CHIP packets, banks 0 through 3, at `$8000`, with EXROM/GAME both
zero. Writable chips, unknown subtypes and unsupported profiles remain rejected.
FC3's `$DFFF` banking, software disable and NMI control use the imported mapper.
No physical freezer-button action is added. Cartridge presence still selects
the exact CPU path; this change does not enable cartridge CPU acceleration.

### Additional ROM profiles

Capability bit 20 (`CART_ROM_EXT`) admits these eight types. Firmware checks
it before resetting the machine or streaming content, so new ARM firmware alone
does not send them to an older FPGA image. All require subtype 0 and ordered
ROM CHIP packets with bank numbers starting at zero and load address `$8000`.

| Type | CHIP layout | EXROM / GAME |
| --- | --- | --- |
| 11 | One 16 KiB bank | 0 / 0 |
| 54 | Three 8 KiB banks | 0 / 0 |
| 64 | Two or four 16 KiB banks | 0 / 0 |
| 65 | One 8 KiB bank | 0 / 1 |
| 66 | One 16 KiB bank | 0 / 0 |
| 71 | Two 16 KiB banks | 1 / 0 |
| 86 | 1 to 256 8 KiB banks, power-of-two count | 0 / 1 |
| 87 | 1 to 128 16 KiB banks, power-of-two count | 0 / 0 |

Westermann switches from 16 KiB to 8 KiB mode on an IO2 read. Blackbox V3
uses IO1/IO2 writes to disable/enable its ROM; V4 uses IO2/IO1 reads.
Blackbox V8 uses IO2 address bits for mode and inverted bank selection and
resets to its last populated bank. Kingsoft switches between 16 KiB mapping
and address-dependent Ultimax mapping. Blackbox V9 uses IO1 address bits for
bank and mode control and exposes the selected ROM on IO1 reads. The VICE
source's explicitly speculative Blackbox V9 disable and screen hacks are not
implemented; title compatibility still needs board testing.

Megabyter uses IO1 writes to select an 8 KiB bank. This is the ROM-only U64
profile, including its 2 MiB capacity; it does not implement the physical
Megabyter's 29F800 flash programming. TwoMegabyter IO1 writes with address bit 1
clear select a 16 KiB bank; writes with that bit set control GAME with data
bit 0 and EXROM with data bit 1. Both reset to bank zero and mirror unused bank
bits to the loaded ROM size. TwoMegabyter resets to 16 KiB mode. Its IO1 reads
and IO2 accesses do not change the mapper. The existing cartridge window limits
both types to 2 MiB; Ultimate's 4 MiB TwoMegabyter profile is not admitted.

This batch adds no cartridge RAM, freezer-button path, KERNAL replacement,
flash programming or EEPROM persistence. RAM and flash CHIP packets remain
rejected.

### EasyFlash

Capability bit 21 (`CART_EASYFLASH`) admits type 32 with subtype 0. Its RAM
fills `$DF00-$DFFF`, so REU and Ultimate Audio stay suppressed. As on
Ultimate (`CART_UCI_DE1C`), UCI moves to I/O1: its registers are
`$DE1B-$DE1F` while EasyFlash is attached and take priority over the
EasyFlash register mirrors there. Flash writes, `NAME.sav` saves, CRT export
and the boot jumper are described in [EasyFlash](C64_EASYFLASH.md).

## Source of the policy

The policy follows Ultimate firmware commit
`d43546fe25b26cb332f43f00219d33153dfe44f8`, already pinned in
`provenance/sources.lock.json`: `software/io/c64/c64_crt.cc` and
`software/io/c64/c64.h`. No Ultimate implementation is copied into product code.

ROM mapper behavior is checked against VICE 3.10 commit
`4d283a2e7dd59b7e378524878e81ecc7826b700c`, under
`vice/src/c64/cart/`. Megabyter and TwoMegabyter use the ROM-only register
profiles in the pinned Ultimate reference's
`fpga/cart_slot/vhdl_source/all_carts_v5.vhd`, `c_megabyter` variants 0 and 1.
Blackbox V8 uses VICE's address-controlled and inverted-bank behavior rather
than Ultimate's differing data-controlled implementation. Kingsoft also
follows VICE's reset and dynamic mapping behavior. Expansion conflicts follow
Ultimate independently of these mapper differences.

Ultimate does not implement types 7, 17, 22, 28, 43, 57 or 85 in that reference.
Their entries apply the same conflict rule to the pinned MiSTer mapper:
IO1-only types allow expansion IO2; Mikro Assembler maps ROM into IO2;
Prophet64 changes banks on IO2 writes. They are Multitini policy extensions,
not claims of upstream Ultimate support.

## I/O and state

REU storage remains DDR and cartridge storage remains PSRAM. With a cartridge
attached, REU follows Ultimate's narrow aperture: `$DF00-$DF17` on reads and
`$DF00-$DF1F` on writes. UCI has priority at `$DF1B-$DF1F`. This leaves FC3's
`$DFFF` control register accessible. Ultimate Audio uses `$DF20-$DFFF` only
when the cartridge permits it. Selected expansion accesses are removed from
the mapper's IO2 input so they neither read cartridge ROM nor cause mapper
side effects. Without a cartridge, existing native REU register mirrors remain.

Cartridge read data remains selected through the CPU or VIC consumption phase.
The memory request strobe only starts a transaction; it no longer switches the
response back to ordinary RAM before consumption. This also applies to REU
transfers that read mapped cartridge ROM.

The existing Ultimate 3.15 UCI unlock sequence remains an explicit override
of configured cartridge UCI suppression. It does not override REU or audio
compatibility. Warm reset retains the UCI claim; loading new content or applying
UCI configuration clears it.

`C64_EXPANSION_CAPS` bit 19 identifies these rules. `C64_REU_HOST` status bits
16, 17 and 18 report effective REU, audio and UCI state through the existing
coherent status mailbox. Host REU operations check effective availability.
There are no new clocks or clock crossings.

## Validation boundary

The host parser and loader tests cover the admitted profiles, malformed CHIP
layouts, unsupported subtypes and capacity boundaries. Firmware tests check
that older FPGA images reject the new types before any reset or streaming.
The adapter bench exercises ROM banking, reset, read/write side effects and
expansion conflicts with synthetic content, including both Blackbox V8 sizes
and, on PAL, the full 2 MiB Megabyter/TwoMegabyter layouts. The loader and
mapper do not depend on the video standard, so NTSC skips those two streams.
From M0.7.1dev4 the loader writes CRT payload as whole 64-bit words, one memory
write per 8 bytes instead of one per byte.
Source tests and simulation do not establish compatibility of every title or
physical-board acceptance of UBoot64. Routed builds and board results must be
recorded against their exact artifacts.

For board acceptance, install the matching ARM firmware and C64 PAL/NTSC
images. Enable REU and Command Interface, then test UBoot64 cold boot and its
updater's SD discovery. In UCI, `/SD` is `multitini/cores/c64/media` on the card.
Check REU transfers, warm reset, cartridge replacement, and eject restoring the
requested expansions. Repeat in both video standards and with an IO2-conflicting
cartridge. Record the package identity and results; simulated acceptance does
not replace this check.
