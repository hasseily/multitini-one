# SPDX-License-Identifier: GPL-3.0-only
################################################################################
# Multitini One PSRAM source-synchronous timing
#
# create_project.tcl enables this implementation-only constraint set for the
# C64, Vectrex and an explicitly PSRAM-enabled GBC build. It is disabled for the
# supervisor and first-pass GBC builds, where the PSRAM pins are static.
################################################################################

# These requirements are inherited from the same driver and board interface in
# Appletini ONE: 2 ns data setup/hold, 2.5 ns chip-select setup/hold, and 10 ps
# board skew. Reads use the driver's run-time IDELAY/edge selection, so input
# paths are deliberately excluded rather than constrained to one static tap.
set psram_oddr_c [get_pins -hierarchical -quiet \
    -filter {NAME =~ */psram_clk_oddr_i/C}]
create_generated_clock -name psram_clk_out -source $psram_oddr_c \
    -divide_by 1 [get_ports psram_clk]
set psram_data_ports [get_ports {psram_a_io[*] psram_b_io[*]}]
set_output_delay -clock psram_clk_out -max 2.01 $psram_data_ports
set_output_delay -clock psram_clk_out -min -1.99 $psram_data_ports
set_output_delay -clock psram_clk_out -max 2.51 [get_ports psram_ce_n]
set_output_delay -clock psram_clk_out -min -2.49 [get_ports psram_ce_n]
set_false_path -from $psram_data_ports

# The opt-in GBC PSRAM branch remains bring-up preparation. Do not program a
# GBC PSRAM-enabled build until its controller/capture mode and run-time tap
# selection have been reviewed on hardware.
