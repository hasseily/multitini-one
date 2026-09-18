# SPDX-License-Identifier: GPL-3.0-only
# Vectrex-only fixed96MHz, phase279-degree QPI capture. The generic GBC timing
# file must not be enabled alongside this file: it false-paths PSRAM inputs.
# LY68L6400 rev1.2 Figure13 launches data after falling SCK, tACLK=2..6ns.
# Reserve0..2ns external clock-forward plus data-return delay. FPGA IBUF and
# capture FF delay are analyzed by STA, not hidden in an input exception.

set vectrex_psram_oddr_c [get_pins -hierarchical -quiet \
    -filter {NAME =~ */psram_clk_oddr_i/C}]
create_generated_clock -name psram_clk_out -source $vectrex_psram_oddr_c \
    -divide_by 1 [get_ports psram_clk]
set_property SLEW FAST [get_ports psram_clk]
set_property DRIVE 12 [get_ports psram_clk]
set vectrex_psram_data_ports [get_ports {psram_a_io[*] psram_b_io[*]}]
set_output_delay -clock psram_clk_out -max 2.01 $vectrex_psram_data_ports
set_output_delay -clock psram_clk_out -min -1.99 $vectrex_psram_data_ports
set_output_delay -clock psram_clk_out -max 2.51 [get_ports psram_ce_n]
set_output_delay -clock psram_clk_out -min -2.49 [get_ports psram_ce_n]

# With one falling launch edge, -min/-max cover both rising and falling data
# transitions. There is no second launch edge requiring -add_delay.
set_input_delay -clock psram_clk_out -clock_fall -max 8.0 $vectrex_psram_data_ports
set_input_delay -clock psram_clk_out -clock_fall -min 2.0 $vectrex_psram_data_ports

# The phase279 input FFs consume the nibble from the preceding SCK period:
# setup must pair launchfall5.208 with capture18.489, not the newerfall15.625.
# Setup-only2 retains the latter edge for HOLD, so the next nibble must remain
# unable to overwrite this sample. A conventional matching hold1 would hide
# that real next-data constraint and is deliberately not used.
set vectrex_psram_capture_d [get_pins -hierarchical -quiet \
    -filter {NAME =~ */psram_i/sample_a_reg*/D || NAME =~ */psram_i/sample_b_reg*/D}]
set_multicycle_path 2 -setup -from $vectrex_psram_data_ports \
    -to $vectrex_psram_capture_d
