# Board Facts and Open Hardware Questions

Status: source and schematic audit only. Board behavior remains unproven.

## Sources reviewed

- Appletini source commit `052fe8aa43c483c193fcbc10ecba4225272c213d`.
- `AppleTini_ONE_vA3.pdf`, 37 pages, dated 2026-07-14, schematic Git hash
  `fd745e65`.
- Appletini Vivado 2025.2 project Tcl and top-level constraints.

The schematic is marked confidential in the source repository and is not copied
into Multitini.

## Known from the reviewed sources

- Zynq device: `xc7z020clg484-2`.
- `PS_CLK` is driven by a fixed 33.333333 MHz oscillator.
- DDR is two 16-bit K4B4G1646E devices on the PS DDR controller.
- SDIO1 uses MIO 9-15.
- UART0 uses MIO 26-27. UART1 uses MIO 24-25.
- USB0 uses MIO 28-39 and connects to the system/uplink/debug USB path.
- USB1 uses MIO 40-51 and connects to the device-facing USB-C host port.
- USB1 VBUS comes from `5V0_MAIN` through a TPS25221 current-limited switch.
  USB3300 `CPEN` controls that switch and `EXTVBUS` receives its active-low
  fault indication.
- Both USB3300 RESET pins have no PS MIO reset assignment in the existing Tcl.
  The USB3300 RESET input is active high, so firmware must verify the board's
  passive/default state and keep the PHY out of reset during normal use.
- The AK4493 is at I2C address `0x10` and has a 12.288 MHz board oscillator.
- The TFP410 is strapped for 24-bit, single-edge, rising-edge input and enabled
  operation. The FPGA routes RGB565 into the available data bits.
- Both LY68L6400 PSRAM devices share `PSRAM.CLK` and `PSRAM.CE#`; each has four
  data lanes. `CE#` has a 10 kOhm pull-up.
- Main Apple-bus SN74LVC8T245 transceivers have active-low OE inputs with
  pull-ups. Direction inputs have pull-downs. Driving OE high and direction low
  agrees with the hardware safe defaults.
- The auxiliary TXS0108E OE input has a pull-down. Driving it low disables the
  auxiliary Apple control lanes.
- The dedicated Apple RESET assertion circuit is inactive when `A2CTRL_RST#`
  is high.

## Open questions requiring a board or manufacturing record

- Which connector and exact jumper modification provide stand-alone 5 V after
  the stated fix? The reviewed schematic shows the original 12 V entry and an
  Apple-bus-derived 5 V gating path; it does not document the field jumper.
- Does USB1 source VBUS correctly for every intended cable orientation and
  device class on modified production boards?
- What are the observed USB3300 RESET levels through cold boot and warm reset?
- What DAC register order and delay eliminate all start/reset noise on the
  production analog path?
- Which DVI modes lock on representative displays?
- What PSRAM capture setting and worst-case service time pass across production
  boards and temperature?
