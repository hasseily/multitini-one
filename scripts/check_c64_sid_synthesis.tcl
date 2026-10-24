# SPDX-License-Identifier: GPL-3.0-only
# Eight-pair replication resource experiment; not routed timing signoff.
set root [file normalize [file join [file dirname [info script]] ..]]
foreach source {
    third_party/c64/upstream/rtl/sid/sid_envelope.sv
    hdl/core/c64/ports/sid_voice.sv
    hdl/core/c64/ports/sid_dac.sv
    hdl/core/c64/ports/sid_filter.sv
    hdl/core/c64/ports/sid_tables.sv
    hdl/core/c64/ports/sid_top.sv
    hdl/core/c64/multitini_c64_sid_array.sv
} { read_verilog -sv [file join $root $source] }
synth_design -mode out_of_context -flatten_hierarchy rebuilt -part xc7z020clg484-2 \
    -top multitini_c64_sid_array -generic SID_COUNT=8 -generic FAST_PORT=1
create_clock -name native -period 31.7 [get_ports clk]
report_utilization -hierarchical -file sid_array_utilization.rpt
report_timing_summary -file sid_array_timing.rpt
write_checkpoint -force sid_array.dcp
puts "PASS: eight-SID array synthesis completed (OOC only)"
