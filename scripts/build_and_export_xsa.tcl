# SPDX-License-Identifier: GPL-3.0-only
# Build, report, gate timing, and export the tracked Multitini hardware platform.

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ".."]]
set build_dir [file join $repo_root build vivado]
if {[info exists ::env(MULTITINI_VIVADO_DIR)]} {
    set requested_dir [string trim $::env(MULTITINI_VIVADO_DIR)]
    if {$requested_dir eq ""} {
        error "MULTITINI_VIVADO_DIR must not be empty"
    }
    if {[file pathtype $requested_dir] eq "absolute"} {
        set build_dir [file normalize $requested_dir]
    } else {
        set build_dir [file normalize [file join $repo_root $requested_dir]]
    }
}
set report_dir [file join $build_dir reports]
set project_file [file join $build_dir multitini_one.xpr]
set xsa_file [file join $build_dir multitini_one.xsa]

proc require_complete_run {run_name} {
    set run [get_runs $run_name]
    set progress [get_property PROGRESS $run]
    set status [get_property STATUS $run]
    if {$progress ne "100%" || ![string match "*Complete*" $status]} {
        error "$run_name did not complete: progress=$progress status=$status"
    }
}

proc require_one_object {description objects} {
    set count [llength $objects]
    if {$count != 1} {
        error "Expected exactly one $description; found $count"
    }
}

proc require_packed_iob_registers {description pattern expected_count} {
    set registers [get_cells -hierarchical -quiet -regexp $pattern]
    set count [llength $registers]
    if {$count != $expected_count} {
        error "Expected $expected_count $description I/O registers; found $count: $registers"
    }
    foreach register $registers {
        if {[get_property IOB $register] ne "TRUE"} {
            error "$description register is not marked for I/O packing: $register"
        }
        if {![string match "OLOGIC_*" [get_property LOC $register]]} {
            error "$description register is not physically packed into OLOGIC: $register"
        }
    }
}

if {![file isfile $project_file]} {
    error "Project is absent. Run scripts/create_project.tcl first: $project_file"
}
file mkdir $report_dir
open_project $project_file

puts "Checking HDL syntax and compile order"
update_compile_order -fileset sources_1
check_syntax

set jobs 8
if {[info exists ::env(MULTITINI_VIVADO_JOBS)]} {
    set jobs $::env(MULTITINI_VIVADO_JOBS)
    if {![string is integer -strict $jobs] || $jobs < 1} {
        error "MULTITINI_VIVADO_JOBS must be a positive integer"
    }
}

reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
require_complete_run synth_1

reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
require_complete_run impl_1
open_run impl_1

set build_generics [get_property GENERIC [get_filesets sources_1]]
set psram_active [expr {
    [string match "*ACTIVE_MACHINE=0*" $build_generics] ||
    [string match "*ENABLE_GBC_PSRAM=1'b1*" $build_generics]
}]

# XDC files cannot contain Tcl control flow. Enforce every hierarchical timing
# query here, against the fully linked and implemented design, so hierarchy
# drift cannot silently remove a generated clock or asynchronous group.
require_one_object "audio BCLK divider source" [get_pins -hierarchical -quiet \
    -regexp {.*audio_test/audio_bclk_div_reg\[1\]/Q}]
require_one_object "DVI ODDR clock pin" [get_pins -hierarchical -quiet \
    -filter {NAME =~ *video_test/dvi_clk_oddr/C}]
set psram_oddr_pins [get_pins -hierarchical -quiet \
    -filter {NAME =~ */psram_clk_oddr_i/C}]
if {$psram_active} {
    require_one_object "PSRAM ODDR clock pin" $psram_oddr_pins
} elseif {[llength $psram_oddr_pins] != 0} {
    error "Inactive PSRAM build unexpectedly contains an ODDR clock pin"
}
foreach {description pattern} {
    "control root clock" {clk_fpga_0}
    "video root clock" {*clk_out1*zynq_ps_bd_clk_wiz_video*}
    "selected machine root clock" {*clk_out1*zynq_ps_bd_clk_wiz_c64*}
} {
    require_one_object $description [get_clocks -quiet -filter "NAME =~ $pattern"]
}
require_one_object "audio BCLK generated clock" [get_clocks -quiet audio_bclk]
require_one_object "forwarded audio BCLK" [get_clocks -quiet audio_bclk_out]
require_one_object "forwarded DVI clock" [get_clocks -quiet dvi_clk_out]
set psram_forwarded_clocks [get_clocks -quiet psram_clk_out]
if {$psram_active} {
    require_one_object "forwarded PSRAM clock" $psram_forwarded_clocks
    require_packed_iob_registers "PSRAM A data" \
        {.*psram_a_o_reg\[[0-3]\]$} 4
    require_packed_iob_registers "PSRAM B data" \
        {.*psram_b_o_reg\[[0-3]\]$} 4
    require_packed_iob_registers "PSRAM chip-enable" \
        {.*psram_ce_n_reg$} 1
    # Each logical tri-state bit controls one pin in both four-bit PSRAM
    # banks. Synthesis must replicate all four registers into both IOBs.
    require_packed_iob_registers "PSRAM tri-state" \
        {.*psram_t_reg\[[0-3]\](_rep)?$} 8
} elseif {[llength $psram_forwarded_clocks] != 0} {
    error "Inactive PSRAM build unexpectedly contains a forwarded clock"
}

set lut_cells [get_cells -hierarchical -quiet -filter {REF_NAME =~ LUT*}]
set lut_outputs [get_pins -quiet -of_objects $lut_cells \
    -filter {DIRECTION == OUT}]
set lut_nets [get_nets -quiet -of_objects $lut_outputs]
set fabric_clock_sinks [get_pins -quiet -leaf -of_objects $lut_nets \
    -filter {IS_CLOCK == 1 && DIRECTION == IN}]
if {[llength $fabric_clock_sinks] != 0} {
    error "Fabric LUTs drive clock pins: $fabric_clock_sinks"
}

report_timing_summary -delay_type min_max -max_paths 100 -check_timing_verbose \
    -report_unconstrained \
    -file [file join $report_dir timing_summary.rpt]
report_clock_interaction -file [file join $report_dir clock_interaction.rpt]
set cdc_report [file join $report_dir cdc.rpt]
report_cdc -details -file $cdc_report
set cdc_stream [open $cdc_report r]
set cdc_text [read $cdc_stream]
close $cdc_stream

# report_cdc classifies each external PSRAM pin-to-IDDR path as CDC-13 even
# though the capture phase and IDELAY tap are selected dynamically and the
# external input path is explicitly false-pathed. Waive only those eight exact
# report rows. Every internal or otherwise unexpected Critical row remains a
# build failure, and the summary/detail counts must agree.
set cdc_summary_critical 0
set cdc_detail_critical 0
set cdc_psram_waived 0
set cdc_unexpected_critical {}
foreach cdc_line [split $cdc_text "\n"] {
    if {[regexp {^CDC-[0-9]+[ \t]+Critical[ \t]+([0-9]+)} \
            $cdc_line -> critical_count]} {
        incr cdc_summary_critical $critical_count
    }
    if {![regexp {^[ \t]*[0-9]+[ \t]+CDC-[0-9]+[ \t]+Critical[ \t]+} \
            $cdc_line]} {
        continue
    }
    incr cdc_detail_critical
    if {$psram_active && [regexp \
            {CDC-13[ \t]+Critical.*False Path[ \t]+psram_[ab]_io\[[0-3]\][ \t]+.*u_[ab]_iddr/D[ \t]*$} \
            $cdc_line]} {
        incr cdc_psram_waived
    } else {
        lappend cdc_unexpected_critical [string trim $cdc_line]
    }
}
if {$cdc_summary_critical != $cdc_detail_critical} {
    error "CDC report summary/detail mismatch: summary=$cdc_summary_critical detail=$cdc_detail_critical"
}
set expected_psram_waivers [expr {$psram_active ? 8 : 0}]
if {$cdc_psram_waived != $expected_psram_waivers} {
    error "Expected $expected_psram_waivers external PSRAM CDC waivers; found $cdc_psram_waived"
}
if {[llength $cdc_unexpected_critical] != 0} {
    error "CDC gate found unexpected critical crossings: $cdc_unexpected_critical"
}
report_bus_skew -warn_on_violation -file [file join $report_dir bus_skew.rpt]
report_drc -file [file join $report_dir drc.rpt]
report_methodology -file [file join $report_dir methodology.rpt]
report_route_status -file [file join $report_dir route_status.rpt]
report_utilization -hierarchical -file [file join $report_dir utilization.rpt]
report_power -file [file join $report_dir power.rpt]
report_clocks -file [file join $report_dir clocks.rpt]
report_exceptions -coverage -file [file join $report_dir exceptions.rpt]
check_timing -verbose -file [file join $report_dir check_timing.rpt]

# Make the checks that define missing timing coverage machine-fatal. Their
# report strings contain only violation counts for the selected categories.
set coverage_checks [check_timing -verbose -return_string -override_defaults \
    {no_clock no_input_delay no_output_delay partial_input_delay \
     partial_output_delay unconstrained_internal_endpoints}]
set coverage_stream [open [file join $report_dir timing_coverage_gate.rpt] w]
puts -nonewline $coverage_stream $coverage_checks
close $coverage_stream
set coverage_failures {
    {There (are|is) [1-9][0-9]* register/latch pins with no clock}
    {There (are|is) [1-9][0-9]* pins that are not constrained for maximum delay}
    {There (are|is) [1-9][0-9]* input ports with no input delay specified}
    {There (are|is) [1-9][0-9]* ports with no output delay specified\. \(HIGH\)}
    {There (are|is) [1-9][0-9]* input ports with partial input delay specified}
    {There (are|is) [1-9][0-9]* ports with partial output delay specified}
}
foreach pattern $coverage_failures {
    if {[regexp -nocase $pattern $coverage_checks]} {
        error "Timing coverage gate found one or more unconstrained objects"
    }
}

foreach delay_type {max min} {
    set path [get_timing_paths -quiet -delay_type $delay_type -max_paths 1]
    if {[llength $path] != 1} {
        error "No $delay_type timing path was available for signoff"
    }
    set slack [get_property SLACK $path]
    puts "Worst $delay_type slack: $slack ns"
    if {$slack < 0.0} {
        error "Negative $delay_type slack: $slack ns"
    }
}

write_checkpoint -force [file join $build_dir multitini_one_impl.dcp]
write_hw_platform -fixed -include_bit -force -file $xsa_file

set manifest [open [file join $report_dir build_manifest.txt] w]
puts $manifest "utc=[clock format [clock seconds] -gmt 1 -format {%Y-%m-%dT%H:%M:%SZ}]"
puts $manifest "vivado=[version -short]"
puts $manifest "part=[get_property PART [current_project]]"
puts $manifest "top=[get_property TOP [get_filesets sources_1]]"
puts $manifest "generic=[get_property GENERIC [get_filesets sources_1]]"
if {![catch {exec git -C $repo_root rev-parse HEAD} commit]} {
    puts $manifest "commit=[string trim $commit]"
}
if {![catch {exec git -C $repo_root status --porcelain} dirty]} {
    puts $manifest "dirty=[expr {[string trim $dirty] ne ""}]"
}
close $manifest

puts "Timing gates passed and XSA exported: $xsa_file"
