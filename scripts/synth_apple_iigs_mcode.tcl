# SPDX-License-Identifier: GPL-3.0-only
# Reproduce the isolated microcode resource comparison; no timing claim.
# vivado -mode batch -source scripts/synth_apple_iigs_mcode.tcl
set_param general.maxThreads 2
set gs_root [file normalize [file join [file dirname [info script]] ..]]
set gs_report_dir [file join $gs_root build apple-iigs-mcode-tests]
if {$argc > 0} { set gs_report_dir [file normalize [lindex $argv 0]] }
file mkdir $gs_report_dir
foreach gs_variant {original optimized} {
    create_project -in_memory -part xc7z020clg484-2 gs_mcode_$gs_variant
    read_verilog -sv [file join $gs_root third_party/apple-iigs/upstream/rtl/65C816/P65C816_pkg.sv]
    if {$gs_variant eq "original"} {
        read_verilog -sv [file join $gs_root third_party/apple-iigs/upstream/rtl/65C816/mcode.sv]
    } else {
        read_verilog -sv [file join $gs_root hdl/core/apple_iigs/ports/mcode.sv]
    }
    synth_design -top mcode -part xc7z020clg484-2 -mode out_of_context
    report_utilization -file [file join $gs_report_dir $gs_variant-utilization.rpt]
    puts "MCode $gs_variant raw_LUT_primitives=[llength [get_cells -hier -filter {REF_NAME =~ LUT*}]] RAMB36=[llength [get_cells -hier -filter {REF_NAME =~ RAMB36*}]]"
    close_project
}
