# SPDX-License-Identifier: GPL-3.0-only
# Build, report, gate timing, and export the tracked Multitini hardware platform.

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ".."]]
set build_dir [file join $repo_root build vivado]
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

report_timing_summary -delay_type min_max -max_paths 100 -check_timing_verbose \
    -report_unconstrained \
    -file [file join $report_dir timing_summary.rpt]
report_clock_interaction -file [file join $report_dir clock_interaction.rpt]
report_cdc -details -file [file join $report_dir cdc.rpt]
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
if {[regexp -nocase {There (are|is) [1-9][0-9]*} $coverage_checks]} {
    error "Timing coverage gate found one or more unconstrained objects"
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
