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

proc require_packed_iob_registers {description pattern expected_count {site_pattern "OLOGIC_*"}} {
    set registers [get_cells -hierarchical -quiet -regexp $pattern]
    set count [llength $registers]
    if {$count != $expected_count} {
        error "Expected $expected_count $description I/O registers; found $count: $registers"
    }
    foreach register $registers {
        if {[get_property IOB $register] ne "TRUE"} {
            error "$description register is not marked for I/O packing: $register"
        }
        if {![string match $site_pattern [get_property LOC $register]]} {
            error "$description register is not physically packed into $site_pattern: $register"
        }
    }
}

if {![file isfile $project_file]} {
    error "Project is absent. Run scripts/create_project.tcl first: $project_file"
}
file mkdir $report_dir
open_project $project_file
source [file join $script_dir configure_vivado_run_profile.tcl]

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

set build_generics [get_property GENERIC [get_filesets sources_1]]
if {[string match "*ACTIVE_MACHINE=2*" $build_generics]} {
    set image_kind "SUPERVISOR"
} elseif {[string match "*ACTIVE_MACHINE=0*" $build_generics]} {
    set image_kind "C64"
} elseif {[string match "*ACTIVE_MACHINE=1*" $build_generics]} {
    set image_kind "GBC"
} elseif {[string match "*ACTIVE_MACHINE=3*" $build_generics]} {
    set image_kind "VECTREX"
} elseif {[string match "*ACTIVE_MACHINE=4*" $build_generics]} {
    set image_kind "GENESIS"
} else {
    error "Build generic has no recognized ACTIVE_MACHINE value: $build_generics"
}
set psram_active [expr {
    [string match "*ACTIVE_MACHINE=0*" $build_generics] ||
    [string match "*ACTIVE_MACHINE=3*" $build_generics] ||
    [string match "*ACTIVE_MACHINE=4*" $build_generics] ||
    [string match "*ENABLE_GBC_PSRAM=1'b1*" $build_generics]
}]

# Capture dirty source bytes as well as the commit before launching workers.
# A second check before export detects edits during the implementation run.
set python [auto_execok python3]
if {$python eq ""} {set python [auto_execok python]}
if {[info exists ::env(MULTITINI_PYTHON)]} {set python [list $::env(MULTITINI_PYTHON)]}
if {$python eq ""} {error "Python is required to capture exact build inputs"}
set capture_dir [file join $repo_root build input-captures "[pid]-[clock clicks]"]
set inputs_file [file join $capture_dir build_inputs.json]
set capture_command [concat $python [list [file join $script_dir capture_vectrex_build_inputs.py] \
    --machine $image_kind --output $inputs_file]]
if {[string match {*C64_NTSC_MODE=1'b1*} $build_generics]} {
    lappend capture_command --c64-video-standard NTSC
}
if {$image_kind eq "GBC" && $psram_active} {lappend capture_command --gbc-psram}
if {$image_kind eq "GENESIS" && [string match {*GENESIS_PAL_MODE=1'b1*} $build_generics]} {
    lappend capture_command --genesis-pal
}
puts [exec {*}$capture_command]

reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
require_complete_run synth_1

reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
require_complete_run impl_1
open_run impl_1


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
if {$image_kind eq "GENESIS"} {
    set chip_gate [get_cells -hierarchical -quiet \
        -filter {NAME =~ *g_genesis_machine/machine_adapter/chip_clock_i && REF_NAME == BUFGCE}]
    require_one_object "Genesis glitch-free chip clock buffer" $chip_gate
    set chip_input_clocks [get_clocks -of_objects [get_pins $chip_gate/I]]
    set chip_output_clocks [get_clocks -of_objects [get_pins $chip_gate/O]]
    require_one_object "Genesis chip parent clock" $chip_input_clocks
    require_one_object "Genesis chip gated clock" $chip_output_clocks
    if {[get_property PERIOD $chip_input_clocks] != [get_property PERIOD $chip_output_clocks]} {
        error "Genesis clock gate unexpectedly changes the chip frequency"
    }
    require_one_object "Genesis audio divider source" [get_pins -hierarchical -quiet \
        -filter {NAME =~ *g_genesis_machine/machine_adapter/machine_i/audio_divider_reg/C}]
    require_one_object "Genesis audio divider output" [get_pins -hierarchical -quiet \
        -filter {NAME =~ *g_genesis_machine/machine_adapter/machine_i/audio_divider_reg/Q}]
    set genesis_audio_clock [get_clocks -quiet genesis_audio_clk]
    require_one_object "Genesis related audio clock" $genesis_audio_clock
    if {abs([get_property PERIOD $genesis_audio_clock] -
            2.0 * [get_property PERIOD $chip_input_clocks]) > 0.0005} {
        error "Genesis audio clock must remain exactly half the chip parent frequency"
    }
}
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
    if {$psram_active && $image_kind ne "VECTREX" && [regexp \
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
set expected_psram_waivers [expr {$psram_active && $image_kind ne "VECTREX" ? 8 : 0}]
if {$cdc_psram_waived != $expected_psram_waivers} {
    error "Expected $expected_psram_waivers external PSRAM CDC waivers; found $cdc_psram_waived"
}
if {[llength $cdc_unexpected_critical] != 0} {
    error "CDC gate found unexpected critical crossings: $cdc_unexpected_critical"
}
if {$image_kind eq "VECTREX"} {
    if {[get_property DRIVE [get_ports psram_clk]] != 12 ||
        [get_property SLEW [get_ports psram_clk]] ne "FAST"} {
        error "Vectrex PSRAM clock requires the captured DRIVE12/FAST setting"
    }
    set capture_clock [get_clocks -quiet -filter {NAME =~ *clk_out2*zynq_ps_bd_clk_wiz_c64*}]
    set transmit_clock [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_c64*}]
    require_one_object "Vectrex PSRAM capture clock" $capture_clock
    set capture_period [get_property PERIOD $capture_clock]
    set transmit_period [get_property PERIOD $transmit_clock]
    set capture_rise [lindex [get_property WAVEFORM $capture_clock] 0]
    if {abs($capture_period - $transmit_period) > 0.001 ||
        abs($capture_rise - $capture_period * 279.0 / 360.0) > 0.05} {
        error "Vectrex PSRAM capture clock must match the 279-degree machine clock phase"
    }
    require_packed_iob_registers "Vectrex PSRAM input" \
        {.*psram_i/sample_[ab]_reg\[[0-3]\]$} 8 "ILOGIC_*"
    # Unlike the tunable C64/GBC PHY, this fixed capture path is statically
    # timed. Every physical input must have both setup and hold paths; a
    # forgotten input delay or an overriding false path is a build failure.
    set vectrex_data_ports [get_ports {psram_a_io[*] psram_b_io[*]}]
    if {[llength $vectrex_data_ports] != 8} {
        error "Vectrex PSRAM timing requires all eight data pins"
    }
    report_timing -from $vectrex_data_ports \
        -delay_type max -max_paths 16 -nworst 1 \
        -file [file join $report_dir psram_input_setup.rpt]
    report_timing -from $vectrex_data_ports \
        -delay_type min -max_paths 16 -nworst 1 \
        -file [file join $report_dir psram_input_hold.rpt]
    foreach data_pin $vectrex_data_ports {
        foreach delay_type {max min} {
            set input_path [get_timing_paths -quiet -from $data_pin \
                -delay_type $delay_type -max_paths 1]
            require_one_object "Vectrex PSRAM $delay_type input path for $data_pin" $input_path
            if {[get_property SLACK $input_path] < 0} {
                error "Vectrex PSRAM $delay_type input timing failed for $data_pin"
            }
        }
    }
}
source [file join $script_dir check_video_bus_constraints.tcl]
report_bus_skew -warn_on_violation -file [file join $report_dir bus_skew.rpt]
set bus_skew_text [report_bus_skew -return_string]
if {[regexp -nocase {VIOLATED} $bus_skew_text]} {
    error "Bus-skew timing failed; see bus_skew.rpt"
}
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
    if {![string is double -strict $slack] || ![expr {abs($slack) < Inf}] || $slack < 0.0} {
        error "Negative $delay_type slack: $slack ns"
    }
}

puts [exec {*}$python [file join $script_dir capture_vectrex_build_inputs.py] --verify --output $inputs_file]
file copy -force $inputs_file [file join $report_dir build_inputs.json]
file copy -force [file join $capture_dir build_inputs.sources.tar.gz] [file join $report_dir build_inputs.sources.tar.gz]
write_checkpoint -force [file join $build_dir multitini_one_impl.dcp]
write_hw_platform -fixed -include_bit -force -file $xsa_file

# The root firmware packager accepts only a supervisor-only bitstream. Machine
# builds use make_core_package.py and remain dynamically loadable SD payloads.
set bitstream_file [file join $build_dir multitini_one.runs impl_1 \
    multitini_one_top.bit]
if {![file isfile $bitstream_file]} {
    error "Implemented bitstream is absent: $bitstream_file"
}
set image_kind_stream [open "${bitstream_file}.kind" w]
puts $image_kind_stream $image_kind
close $image_kind_stream

set manifest [open [file join $report_dir build_manifest.txt] w]
puts $manifest "utc=[clock format [clock seconds] -gmt 1 -format {%Y-%m-%dT%H:%M:%SZ}]"
puts $manifest "vivado=[version -short]"
puts $manifest "part=[get_property PART [current_project]]"
puts $manifest "top=[get_property TOP [get_filesets sources_1]]"
puts $manifest "generic=[get_property GENERIC [get_filesets sources_1]]"
puts $manifest "image_kind=$image_kind"
puts $manifest "max_threads=[get_param general.maxThreads]"
puts $manifest "jobs=$jobs"
puts $manifest "inputs=build_inputs.json"
puts $manifest "sources=build_inputs.sources.tar.gz"
foreach run_name {synth_1 impl_1} {
    set run [get_runs $run_name]
    foreach property [lsort [list_property $run]] {
        if {[string match {STEPS.*} $property] || $property in {STRATEGY AUTO_INCREMENTAL_CHECKPOINT INCREMENTAL_CHECKPOINT}} {
            puts $manifest "$run_name.$property=[get_property $property $run]"
        }
    }
}
if {![catch {exec git -C $repo_root rev-parse HEAD} commit]} {
    puts $manifest "commit=[string trim $commit]"
}
if {![catch {exec git -C $repo_root status --porcelain} dirty]} {
    puts $manifest "dirty=[expr {[string trim $dirty] ne ""}]"
}
close $manifest

puts "Timing gates passed and XSA exported: $xsa_file"
