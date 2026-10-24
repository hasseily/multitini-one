# SPDX-License-Identifier: GPL-3.0-only
# Resource inference only; no routed timing or physical audio acceptance.
set root [file normalize [file join [file dirname [info script]] ..]]
read_verilog -sv [file join $root hdl common multitini_audio_capture.sv]
synth_design -mode out_of_context -flatten_hierarchy none \
    -part xc7z020clg484-2 -top multitini_audio_capture
report_utilization -file audio_capture_utilization.rpt
set bram36 [get_cells -quiet -hier -filter {REF_NAME =~ RAMB36*}]
set bram18 [get_cells -quiet -hier -filter {REF_NAME =~ RAMB18*}]
set distributed [get_cells -quiet -hier -filter {REF_NAME =~ RAMD* || REF_NAME =~ RAMS* || REF_NAME =~ RAM32* || REF_NAME =~ RAM64* || REF_NAME =~ RAM128* || REF_NAME =~ RAM256*}]
if {[llength $bram36] != 0 || [llength $bram18] != 1 || [llength $distributed] != 0} {
    error "Audio capture requires exactly one RAMB18 and no LUTRAM: got [llength $bram36] RAMB36, [llength $bram18] RAMB18, [llength $distributed] distributed RAM"
}
puts "PASS: audio capture uses exactly one RAMB18 and no LUTRAM"
