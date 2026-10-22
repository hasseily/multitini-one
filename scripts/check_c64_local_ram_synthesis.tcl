# SPDX-License-Identifier: GPL-3.0-only
# Run in a dedicated build directory with Vivado 2025.2. This is a resource
# inference check, not routed timing signoff or machine-level qualification.
set root [file normalize [file join [file dirname [info script]] ..]]
read_verilog -sv [file join $root hdl core c64 multitini_c64_local_ram.sv]
synth_design -mode out_of_context -flatten_hierarchy none \
    -part xc7z020clg484-2 -top multitini_c64_local_ram
report_utilization -file local_ram_utilization.rpt
set bram36 [get_cells -quiet -hier -filter {REF_NAME =~ RAMB36*}]
set bram18 [get_cells -quiet -hier -filter {REF_NAME =~ RAMB18*}]
set distributed [get_cells -quiet -hier -filter {REF_NAME =~ RAMD* || REF_NAME =~ RAMS* || REF_NAME =~ RAM32* || REF_NAME =~ RAM64* || REF_NAME =~ RAM128* || REF_NAME =~ RAM256*}]
set half_blocks [expr {2 * [llength $bram36] + [llength $bram18]}]
if {$half_blocks != 32 || [llength $distributed] != 0} {
    error "C64 local RAM inference: expected 16 RAMB36 equivalents and no LUTRAM; got [llength $bram36] RAMB36, [llength $bram18] RAMB18, [llength $distributed] distributed RAM cells"
}
puts "PASS: C64 64 KiB local RAM uses 16 RAMB36 equivalents and no LUTRAM"
