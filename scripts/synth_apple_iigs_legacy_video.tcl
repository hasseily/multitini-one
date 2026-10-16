# SPDX-License-Identifier: GPL-3.0-only
# Isolate the real VGC in the same Vivado synthesis engine used for delivery.
if {$argc != 1} {error "usage: synth_apple_iigs_legacy_video.tcl OUTPUT_DIRECTORY"}
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file normalize [lindex $argv 0]]
file mkdir $out
set_param general.maxThreads 8
create_project -in_memory -part xc7z020clg484-2
set_property include_dirs [list [file join $root third_party/apple-iigs/upstream/rtl]] [current_fileset]
read_verilog -sv [file join $root hdl/core/apple_iigs/ports/runtime_rom.sv]
read_verilog -sv [file join $root hdl/core/apple_iigs/ports/vgc.sv]
synth_design -top vgc -part xc7z020clg484-2 -mode out_of_context -flatten_hierarchy none
write_checkpoint -force [file join $out synthesized.dcp]
write_verilog -force -mode funcsim [file join $out synthesized.v]
report_utilization -hierarchical -file [file join $out utilization.rpt]
puts "PASS LEGACY_VIDEO_SYNTHESIS"
close_project
