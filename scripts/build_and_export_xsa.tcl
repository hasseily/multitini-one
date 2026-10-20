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

proc require_nes_save_ram {stage} {
    set storage [get_cells -hierarchical -quiet -filter \
        {NAME =~ *g_nes_machine*machine_adapter*memory_service*save_lanes*lane_ram* && REF_NAME =~ RAM*}]
    set registers [get_cells -hierarchical -quiet -filter \
        {NAME =~ *g_nes_machine*machine_adapter*memory_service*lane_ram* && REF_NAME =~ FD*}]
    # Vivado exposes RAM64M macro cells and their four RAMD64E atoms at once.
    # Count the physical atoms when expanded, so capacity is not doubled.
    set atoms [filter $storage {REF_NAME =~ RAMD* || REF_NAME =~ RAMS*}]
    if {[llength $atoms] != 0} {
        set storage [concat $atoms [filter $storage {REF_NAME =~ RAMB*}]]
    }
    set storage_bits 0
    foreach cell $storage {
        set reference [get_property REF_NAME $cell]
        switch -- $reference {
            RAM32M -
            RAM64M {incr storage_bits 256}
            RAMD32 -
            RAMS32 {incr storage_bits 32}
            RAMD64E -
            RAMS64E {incr storage_bits 64}
            RAMB18E1 {incr storage_bits 18432}
            RAMB36E1 {incr storage_bits 36864}
            default {
                if {![regexp {^RAM([0-9]+)X([0-9]+)} $reference -> depth width]} {
                    error "Unreviewed NES battery RAM primitive: $reference"
                }
                incr storage_bits [expr {$depth * $width}]
            }
        }
    }
    if {$storage_bits < 65536 || [llength $registers] != 0} {
        error "NES battery SRAM must retain at least 8 KiB of RAM and zero flip-flop storage: RAM_bits=$storage_bits FF=[llength $registers]"
    }
    puts "NES_SAVE_RAM_PASS stage=$stage memory_primitives=[llength $storage] storage_bits=$storage_bits storage_flipflops=[llength $registers]"
}

proc require_iigs_phase_fanin {description pins marker} {
    if {[llength $pins] == 0} {error "IIgs phase gate has no $description pins"}
    foreach pin $pins {
        set cone [all_fanin -quiet -flat -to $pin]
        set nets [get_nets -quiet -of_objects $cone]
        if {[lsearch -exact $nets $marker] < 0} {
            error "IIgs $description lost its reviewed phase qualification: $pin ($marker)"
        }
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
} elseif {[string match "*ACTIVE_MACHINE=5*" $build_generics]} {
    set image_kind "APPLE_IIGS"
} elseif {[string match "*ACTIVE_MACHINE=6*" $build_generics]} {
    set image_kind "NES"
} else {
    error "Build generic has no recognized ACTIVE_MACHINE value: $build_generics"
}
set psram_active [expr {
    [string match "*ACTIVE_MACHINE=0*" $build_generics] ||
    [string match "*ACTIVE_MACHINE=3*" $build_generics] ||
    [string match "*ACTIVE_MACHINE=4*" $build_generics] ||
    [string match "*ACTIVE_MACHINE=6*" $build_generics] ||
    [string match "*ENABLE_GBC_PSRAM=1'b1*" $build_generics]
}]

# Capture dirty source bytes as well as the commit before launching workers.
# A second check before export detects edits during the implementation run.
set python [auto_execok python3]
if {$python eq ""} {set python [auto_execok python]}
if {[info exists ::env(MULTITINI_PYTHON)]} {set python [list $::env(MULTITINI_PYTHON)]}
if {$python eq ""} {error "Python is required to capture exact build inputs"}
# Vivado sets PYTHONHOME/PYTHONPATH for its own Python runtime. An explicitly
# selected host interpreter must not import that runtime's standard library.
lappend python -I
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
if {$image_kind eq "NES"} {
    open_run synth_1
    require_nes_save_ram synthesis
    close_design
}

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
if {$image_kind eq "NES"} {
    require_nes_save_ram implementation
    set nes_clock [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_c64*}]
    require_one_object "NES service clock" $nes_clock
    set nes_period [get_property PERIOD $nes_clock]
    set nes_mmcm [get_cells -hierarchical -quiet -filter \
        {REF_NAME == MMCME2_ADV && NAME =~ *clk_wiz_c64*}]
    require_one_object "NES service MMCM" $nes_mmcm
    foreach {property expected} {CLKFBOUT_MULT_F 31.5 DIVCLK_DIVIDE 5 CLKOUT0_DIVIDE_F 22} {
        if {abs([get_property $property $nes_mmcm] - $expected) > 0.000001} {
            error "NES MMCM $property differs from the reviewed value $expected"
        }
    }
    set nes_parent [get_clocks -quiet -of_objects [get_pins $nes_mmcm/CLKIN1]]
    require_one_object "NES MMCM parent clock" $nes_parent
    set nes_parent_period [get_property PERIOD $nes_parent]
    if {$nes_parent ne "clk_fpga_2" || abs($nes_parent_period - 1000.0 / 150.0) > 0.001} {
        error "NES MMCM requires the reviewed 150 MHz PS clock: $nes_parent $nes_parent_period ns"
    }
    # PS7 and generated clocks use picosecond precision. Bound that rounding
    # independently from the exact physical MMCM ratio; never relax timing.
    set nes_ratio [expr {5.0 * 22.0 / 31.5}]
    set nes_derived_period [expr {$nes_parent_period * $nes_ratio}]
    set nes_nominal_tolerance [expr {0.001 * (1.0 + $nes_ratio)}]
    if {abs($nes_period - $nes_derived_period) > 0.001 ||
        abs($nes_period - 1000.0 / 42.954544) > $nes_nominal_tolerance} {
        error "NES service clock differs from its reviewed MMCM ratio: $nes_period ns"
    }
    puts "NES_CLOCK_RATIO_PASS parent=$nes_parent parent_period=$nes_parent_period M=31.5 D=5 O=22 derived_period=$nes_derived_period actual_period=$nes_period"
    set nes_register_pins [get_pins -hierarchical -quiet \
        -filter {NAME =~ *g_nes_machine*machine_adapter*/C}]
    if {[llength $nes_register_pins] == 0} {error "NES adapter has no clocked registers"}
    set nes_register_clocks [get_clocks -quiet -of_objects $nes_register_pins]
    if {$nes_register_clocks ne $nes_clock} {
        error "NES machine state must retain its single root clock and clock-enable stalls: $nes_register_clocks"
    }
    puts "NES_CLOCK_PASS root=$nes_clock period=$nes_period registers=[llength $nes_register_pins]"
}
if {$image_kind eq "APPLE_IIGS"} {
    foreach {name divide} {clk28 2} {
        set divider_source [get_pins -hierarchical -quiet -regexp \
            [format {.*g_apple_iigs_machine[/.]machine_adapter/%s_divider_reg/C$} $name]]
        set divider_output [get_pins -hierarchical -quiet -regexp \
            [format {.*g_apple_iigs_machine[/.]machine_adapter/%s_divider_reg/Q$} $name]]
        require_one_object "IIgs $name divider source" $divider_source
        require_one_object "IIgs $name divider output" $divider_output
        set clock [get_clocks -quiet "apple_iigs_${name}"]
        require_one_object "IIgs related $name clock" $clock
        set master [get_clocks -of_objects $divider_source]
        require_one_object "IIgs $name divider parent" $master
        if {![get_property IS_GENERATED $clock] ||
            ![get_property IS_USER_GENERATED $clock] ||
            [get_property IS_INVERTED $clock] ||
            [get_property DIVIDE_BY $clock] ne "$divide" ||
            [get_property SOURCE $clock] ne $divider_source ||
            [get_property MASTER_CLOCK $clock] ne $master ||
            abs([get_property PERIOD $clock] - $divide * [get_property PERIOD $master]) > 0.003} {
            error "IIgs $name clock must retain the reviewed /$divide divider"
        }
        set buffer [get_cells -hierarchical -quiet -regexp \
            [format {.*g_apple_iigs_machine[/.]machine_adapter/%s_buffer_i$} $name]]
        require_one_object "IIgs $name clock buffer" $buffer
        if {[get_property REF_NAME $buffer] ne "BUFG"} {
            error "IIgs $name divider must use a dedicated BUFG clock buffer"
        }
        puts "APPLE_IIGS_CLOCK_PASS name=$name parent=$master period=[get_property PERIOD $clock]"
    }
# Ignored strict candidate: source/gate/waveform identity, no timing exceptions.
set iigs_gate [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk14_buffer_i$}]
require_one_object "IIgs native SYNC gate" $iigs_gate
set iigs_gate_ref [get_property REF_NAME $iigs_gate]
if {$iigs_gate_ref eq "BUFGCE"} {
    set iigs_gate_input [get_pins $iigs_gate/I]
    set iigs_gate_ce [get_pins $iigs_gate/CE]
    if {[get_property CE_TYPE $iigs_gate] ne "SYNC" ||
        [get_property IS_I_INVERTED $iigs_gate] != 0 ||
        [get_property IS_CE_INVERTED $iigs_gate] != 0} {
        error "IIgs BUFGCE must retain non-inverted SYNC behavior"
    }
} elseif {$iigs_gate_ref eq "BUFGCTRL"} {
    foreach {pin expected_type} {I1 POWER CE1 GROUND S0 POWER S1 GROUND IGNORE0 GROUND IGNORE1 POWER} {
        set object [get_pins $iigs_gate/$pin]
        set nets [get_nets -of_objects $object]
        require_one_object "IIgs gate $pin pin" $object
        require_one_object "IIgs gate $pin constant net" $nets
        if {![get_property IS_TIED $object] || [get_property TYPE $nets] ne $expected_type} {
            error "IIgs mapped clock gate has unsafe $pin control: expected $expected_type"
        }
    }
    if {[get_property XILINX_LEGACY_PRIM $iigs_gate] ne "BUFGCE" ||
        [get_property CE_TYPE_CE0 $iigs_gate] ne "SYNC" ||
        ![get_property PRESELECT_I0 $iigs_gate] || [get_property PRESELECT_I1 $iigs_gate] ||
        [get_property INIT_OUT $iigs_gate] != 0} {
        error "IIgs BUFGCTRL does not retain the reviewed BUFGCE behavior"
    }
    set iigs_gate_input [get_pins $iigs_gate/I0]
    set iigs_gate_ce [get_pins $iigs_gate/CE0]
} else {error "Unsupported IIgs native clock gate: $iigs_gate_ref"}
set iigs_gate_output [get_pins $iigs_gate/O]
require_one_object "IIgs native gate input" $iigs_gate_input
require_one_object "IIgs native gate enable" $iigs_gate_ce
require_one_object "IIgs native gate output" $iigs_gate_output
if {[get_property IS_INVERTED $iigs_gate_input] != 0 || [get_property IS_INVERTED $iigs_gate_ce] != 0} {
    error "IIgs native gate input/CE polarity changed"
}
set iigs_gate_parent [get_clocks -quiet -of_objects $iigs_gate_input]
require_one_object "IIgs native gate parent" $iigs_gate_parent
set iigs_native_clock [get_clocks -quiet apple_iigs_clk14]
require_one_object "IIgs native pulse clock" $iigs_native_clock
set iigs_primary_period [get_property PERIOD $iigs_gate_parent]
set iigs_native_waveform [get_property WAVEFORM $iigs_native_clock]
if {![get_property IS_GENERATED $iigs_native_clock] ||
    ![get_property IS_USER_GENERATED $iigs_native_clock] ||
    [get_property IS_INVERTED $iigs_native_clock] ||
    [get_property SOURCE $iigs_native_clock] ne $iigs_gate_input ||
    [get_property SOURCE_PINS $iigs_native_clock] ne $iigs_gate_output ||
    [get_property MASTER_CLOCK $iigs_native_clock] ne $iigs_gate_parent ||
    [get_property EDGES $iigs_native_clock] ne "1 2 9" ||
    abs([get_property PERIOD $iigs_native_clock]-4.0*$iigs_primary_period)>0.003 ||
    [llength $iigs_native_waveform]!=2 ||
    abs([lindex $iigs_native_waveform 0])>0.001 ||
    abs([lindex $iigs_native_waveform 1]-0.5*$iigs_primary_period)>0.003} {
    error "IIgs native clock must retain exact source I0/I, destination O and edges {1 2 9}"
}
puts "APPLE_IIGS_NATIVE_GATE_IDENTITY_PASS primitive=$iigs_gate_ref source=$iigs_gate_input output=$iigs_gate_output edges=[get_property EDGES $iigs_native_clock] waveform=$iigs_native_waveform"

    set primary [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_c64*}]
    require_one_object "IIgs primary clock for accelerated CPU" $primary
    set cpu_registers [get_cells -hierarchical -quiet -regexp \
        {.*g_apple_iigs_machine[/.]machine_adapter/machine_i/cpu/(A|X|Y|D|SP|T|PBR|DBR|P|PC|IR|STATE)_reg(\[[0-9]+\])?(_rep[^/]*)?$}]
    if {[llength $cpu_registers] < 120} {error "IIgs accelerated CPU register census changed"}
    foreach cell $cpu_registers {
        set pin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == C}]
        require_one_object "IIgs architectural CPU clock pin" $pin
        set clock [get_clocks -quiet -of_objects $pin]
        if {[llength $clock] != 1 || $clock ne $primary} {
            error "IIgs CPU architectural state is not on the primary clock: $cell ($clock)"
        }
    }
    set cpu_endpoints [get_pins -quiet -of_objects $cpu_registers -filter {REF_PIN_NAME == D}]
    set cpu_path [get_timing_paths -quiet -from $primary -to $cpu_endpoints -delay_type max -max_paths 1]
    require_one_object "IIgs single-cycle CPU path" $cpu_path
    if {abs([get_property REQUIREMENT $cpu_path]-[get_property PERIOD $primary])>0.004} {
        error "IIgs accelerated CPU must retain ordinary single-cycle primary timing"
    }
    set data_ram [get_cells -hierarchical -quiet -regexp \
        {.*g_apple_iigs_machine[/.]machine_adapter/cache_i/data_reg_[0-9]+$}]
    if {[llength $data_ram] < 1 || [llength $data_ram] > 8} {
        error "IIgs 32 KiB cache data RAM census changed: [llength $data_ram]"
    }
    foreach cell $data_ram {
        if {[get_property REF_NAME $cell] ni {RAMB36E1 RAMB18E1}} {
            error "IIgs cache data is no longer block RAM: $cell"
        }
    }
    puts "APPLE_IIGS_ACCEL_CLOCK_PASS cpu_registers=[llength $cpu_registers] data_ram=[llength $data_ram] period=[get_property PERIOD $primary]"
    source [file join $script_dir check_apple_iigs_phase_constraints.tcl]
}
if {$image_kind eq "GENESIS"} {
    set chip_gate [get_cells -hierarchical -quiet -regexp \
        {.*g_genesis_machine[/.]machine_adapter/chip_clock_i$}]
    require_one_object "Genesis glitch-free chip clock buffer" $chip_gate
    set chip_ref [get_property REF_NAME $chip_gate]
    if {$chip_ref eq "BUFGCE"} {
        set chip_input_pin [get_pins $chip_gate/I]
    } elseif {$chip_ref eq "BUFGCTRL"} {
        # A mapped BUFGCE uses input zero and retains hardware CE qualification.
        # Reject mux/bypass variants instead of treating every BUFGCTRL as a gate.
        foreach {pin expected_type} {I1 POWER CE1 GROUND S0 POWER S1 GROUND IGNORE0 GROUND IGNORE1 POWER} {
            set object [get_pins $chip_gate/$pin]
            set nets [get_nets -of_objects $object]
            require_one_object "Genesis gate $pin pin" $object
            require_one_object "Genesis gate $pin constant net" $nets
            if {![get_property IS_TIED $object] || [get_property TYPE $nets] ne $expected_type} {
                error "Genesis mapped clock gate has unsafe $pin control: expected $expected_type"
            }
        }
        if {[get_property XILINX_LEGACY_PRIM $chip_gate] ne "BUFGCE" ||
            [get_property CE_TYPE_CE0 $chip_gate] ne "SYNC" ||
            ![get_property PRESELECT_I0 $chip_gate] || [get_property PRESELECT_I1 $chip_gate] ||
            [get_property INIT_OUT $chip_gate] != 0} {
            error "Genesis BUFGCTRL does not retain the reviewed BUFGCE behavior"
        }
        set chip_input_pin [get_pins $chip_gate/I0]
    } else {
        error "Unsupported Genesis clock-gate primitive: $chip_ref"
    }
    set chip_input_clocks [get_clocks -of_objects $chip_input_pin]
    set chip_output_clocks [get_clocks -of_objects [get_pins $chip_gate/O]]
    require_one_object "Genesis chip parent clock" $chip_input_clocks
    require_one_object "Genesis chip gated clock" $chip_output_clocks
    if {$chip_input_clocks ne $chip_output_clocks} {error "Genesis clock gate changes the parent clock"}
    set audio_source [get_pins -hierarchical -quiet -regexp \
        {.*g_genesis_machine[/.]machine_adapter/machine_i/audio_divider_reg/C$}]
    set audio_output [get_pins -hierarchical -quiet -regexp \
        {.*g_genesis_machine[/.]machine_adapter/machine_i/audio_divider_reg/Q$}]
    require_one_object "Genesis audio divider source" $audio_source
    require_one_object "Genesis audio divider output" $audio_output
    set genesis_audio_clock [get_clocks -quiet genesis_audio_clk]
    require_one_object "Genesis related audio clock" $genesis_audio_clock
    # PERIOD properties are rounded to 1 ps. Verify the exact divider and pins,
    # then allow at most 1.5 ps for rounding two parent periods and one output.
    if {[get_clocks -of_objects $audio_source] ne $chip_input_clocks ||
        [get_clocks -of_objects $audio_output] ne $genesis_audio_clock ||
        ![get_property IS_GENERATED $genesis_audio_clock] ||
        ![get_property IS_USER_GENERATED $genesis_audio_clock] ||
        [get_property IS_INVERTED $genesis_audio_clock] ||
        [get_property DIVIDE_BY $genesis_audio_clock] ne "2" ||
        [get_property SOURCE $genesis_audio_clock] ne $audio_source ||
        [get_property SOURCE_PINS $genesis_audio_clock] ne $audio_output ||
        [get_property MASTER_CLOCK $genesis_audio_clock] ne $chip_input_clocks ||
        abs([get_property PERIOD $genesis_audio_clock] - 2.0 * [get_property PERIOD $chip_input_clocks]) > 0.0015} {
        error "Genesis audio clock must be related to and exactly half the chip parent frequency"
    }
    puts "GENESIS_CLOCK_GATE_PASS primitive=$chip_ref parent=$chip_input_clocks audio=$genesis_audio_clock period=[get_property PERIOD $genesis_audio_clock]"

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
# Use the build metadata captured before open_run restores the checkpoint.
# XDC Tcl variables and non-RTL fileset generics are not checkpoint state.
set ::multitini_signoff_generics $build_generics
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
puts $manifest "generic=$build_generics"
puts $manifest "image_kind=$image_kind"
puts $manifest "max_threads=[get_param general.maxThreads]"
puts $manifest "jobs=$jobs"
puts $manifest "inputs=build_inputs.json"
puts $manifest "sources=build_inputs.sources.tar.gz"
foreach run_name {synth_1 impl_1} {
    set run [get_runs $run_name]
    foreach property [lsort [list_property $run]] {
        if {[string match {STEPS.*} $property] || $property in {STRATEGY AUTO_INCREMENTAL_CHECKPOINT INCREMENTAL_CHECKPOINT WRITE_INCREMENTAL_SYNTH_CHECKPOINT}} {
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
