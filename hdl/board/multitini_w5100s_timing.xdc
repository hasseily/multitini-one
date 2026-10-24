# Loaded only for the C64 image by the project generator. Endpoint assertions
# run in scripts/check_w5100s_constraints.tcl after routing.
# SPDX-License-Identifier: GPL-3.0-only
# Host-only W5100S parallel bus, normal 100 MHz W5100S SYS_CLK.
# No external clock: bound IOB clock-to-pad and pad-to-IOB data routes.
# Each output path <=4ns gives <=4ns inter-output skew. With <=2ns
# differential PCB skew, the three-clock 22.5ns address setup retains the
# datasheet's 10ns minimum. Board propagation/skew is an explicit board gate.
# Read round trip: <=4ns output + <=5ns PCB roundtrip +35ns W5100S read
# access + <=9ns input =53ns, below the 120ns read strobe. A whole extra
# native clock of capture margin remains even with 7.5ns FPGA setup/uncertainty.
# Internal register paths retain ordinary control-clock setup/hold analysis.
set net_out [get_ports {eth_a[*] eth_rd_n eth_wr_n eth_cs_n eth_rst_n eth_d[*]}]
set net_in [get_ports {eth_d[*]}]
set net_launch [get_cells -hierarchical -filter {REF_NAME =~ FD* && (NAME =~ *w5100s*eth_a_reg* || NAME =~ *w5100s*eth_d_o_reg* || NAME =~ *w5100s*eth_d_t_reg* || NAME =~ *w5100s*eth_rd_n_reg* || NAME =~ *w5100s*eth_wr_n_reg* || NAME =~ *w5100s*eth_cs_n_reg* || NAME =~ *w5100s*eth_rst_n_reg*)}]
set net_capture [get_cells -hierarchical -filter {REF_NAME =~ FD* && NAME =~ *w5100s*captured_data_reg*}]
set net_launch_c [get_pins -of_objects $net_launch -filter {REF_PIN_NAME == C}]
set net_capture_d [get_pins -of_objects $net_capture -filter {REF_PIN_NAME == D}]
# A sequential clock pin is the valid startpoint (UG903). Starting at Q
# segments timing propagation and omits clock-to-Q from the external bound.
set_max_delay -datapath_only 4.000 -from $net_launch_c -to $net_out
set_max_delay -datapath_only 9.000 -from $net_in -to $net_capture_d
set_property IOB TRUE $net_launch
set_property IOB TRUE $net_capture
