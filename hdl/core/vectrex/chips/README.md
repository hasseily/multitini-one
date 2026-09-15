These are adapted copies of two chip blocks already used by Appletini ONE.
`PROVENANCE.json` pins their source commit and lists the changes. Copyright
notices and redistribution terms remain in each HDL file.

`multitini_vectrex_via6522` uses one `clk` domain. `slow_clock` is a one-cycle
pulse for the emulated falling PHI2/E edge; `strobe` is a one-cycle bus access.
The machine adapter must define the placement of those pulses. Vectrex uses
`timer_read_extra_clock=0` and the default `TIMER_READ_PRETICK=0`. The optional
pre-tick snapshot preserves the earlier Apple bus convention. `porta_bus`
and `portb_bus` resolve data-direction bits against the supplied external
inputs; the chip has no internal tri-state nets.

`multitini_vectrex_psg` uses `CE` for the PSG input clock. Vectrex selects
`SEL=0`, `MODE=1` for the AY amplitude table, and `IO_PORTS=1` for an AY8912.
`IOA_OEn` and `IOB_OEn` are active-low output enables. Register 7 bits 6/7
select input when clear and output when set. External input signals must
include pull-ups for undriven pins. Output-mode reads resolve the output
latch against those external pins. `IO_PORTS=0` keeps the earlier AY8913
register-14/15 latch readback; `IO_PORTS=2` enables both AY8910 ports.

The isolated regression command is `python scripts/run_vectrex_chip_tests.py`
(or `sh scripts/run_vectrex_chip_tests.sh` on Linux). On Windows it uses
the repository's tool launcher to supply the UCRT DLL path and suppress
modal loader-error dialogs.
It checks register and pin behavior with original test stimulus. Passing
these tests does not establish compatibility with physical Vectrex timing
or analogue beam behavior. The machine integration needs its own bus-phase
and vector trace tests.
