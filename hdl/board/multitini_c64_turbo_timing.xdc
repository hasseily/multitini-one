# SPDX-License-Identifier: GPL-3.0-only
# C64-only, implementation-only. The -2 MMCM output duty precision includes
# the global buffer (DS187 Table 72). Add it only on half-period CPU paths.
set_clock_uncertainty -setup 0.200 -rise_from [get_clocks clk_out2_zynq_ps_bd_clk_wiz_c64_0] -fall_to [get_clocks clk_out2_zynq_ps_bd_clk_wiz_c64_0]
set_clock_uncertainty -setup 0.200 -fall_from [get_clocks clk_out2_zynq_ps_bd_clk_wiz_c64_0] -rise_to [get_clocks clk_out2_zynq_ps_bd_clk_wiz_c64_0]
