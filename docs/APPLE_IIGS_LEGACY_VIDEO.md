# IIgs legacy graphics

The IIgs VGC renders text, lores, HGR and DHGR alongside SHR. All modes use
the same native raster and the 724x496 frame transport. CPU acceleration does
not select a different renderer.

## Missing graphics diagnosis

The owner reported a blank picture while Choplifter continued running from
`8-bit Games.hdv`, both at Stock without ZipGS and with acceleration. A
read-only UART capture identified IIgs FPGA `4D000600`, ABI 0.22, complete
724x496 frames and no video drops, underruns, AXI or frame-engine errors.
The live capture used accelerated ZipGS; the Stock result is the owner's
separate observation.

Vivado 2025.2 reported undriven `GR`, `lores_mode` and `dhires_mode` nets in
the VGC. These signals were referenced before their net declaration
assignments. RTL simulation rendered HGR correctly, but synthesis removed
the graphics shift registers and the DHGR color lookup. The same warnings
appear in the original IIgs build, before the SHR border changes.

The reproducible port now declares those three nets before their first use
and retains their original expressions as continuous assignments. This
changes no clock, reset, enable, memory interface or pixel/address formula.
The pinned upstream source stays unchanged.

## Evidence and limits

An isolated Vivado functional netlist of the original VGC produces black
for an all-white HGR test. The corrected netlist produces all 531 checked
white pixels on the same scanline using AMD's functional simulation library.
The permanent `scripts/test_apple_iigs_legacy_video.py` regression then passes
18 cases and 9,198 exact pixels from the synthesized VGC: HGR, lores,
double-lores, DHGR monochrome/color, 40/80-column text, mixed graphics/text,
and SHR 320/640. It uses synthetic ROM and memory, checking settled scanline
interiors. The runner checks explicit assertions even when Xsim exits zero.
RTL tests separately cover native video timing, synchronous slow RAM, HGR
page selection, PAL/NTSC and the source normalizer. SHR 320/640/fill tests
retain all image pixels and border bands in both standards.

The isolated correction restores 163 LUTs, 38 flip-flops and one RAMB36
that had been optimized away. These are isolated synthesis differences,
not whole-image utilization or timing results.

Functional netlist tests catch this failure where RTL-only tests do not.
They do not establish routed timing or confirm Choplifter on the board.
Hardware acceptance must use the newly built IIgs image and its matching
`core.cfg`, then check text, lores, HGR, DHGR and return to the SHR desktop.
The ARM firmware, SmartPort ROM and other machine images need no logic
changes for this correction.
