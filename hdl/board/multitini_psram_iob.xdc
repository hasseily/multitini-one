# SPDX-License-Identifier: GPL-3.0-only
################################################################################
# Multitini One PSRAM I/O-register packing
#
# This file must be visible during synthesis so identical output-enable launch
# registers are preserved and replicated into their destination I/O cells.
# create_project.tcl disables it for the PSRAM-inactive supervisor and
# first-pass GBC builds.
################################################################################

set_property IOB TRUE [get_cells -hierarchical {*psram_t_reg*}]
set_property IOB TRUE [get_cells -hierarchical {*psram_ce_n_reg*}]
set_property IOB TRUE [get_cells -hierarchical {*psram_a_o_reg*}]
set_property IOB TRUE [get_cells -hierarchical {*psram_b_o_reg*}]
