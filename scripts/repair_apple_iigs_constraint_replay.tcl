# SPDX-License-Identifier: GPL-3.0-only
# Supplemental routed-checkpoint repair for Vivado 2025.2 constraint replay.
# The caller publishes this file, the exact MCP-only XDC and all receipts.
# It does not change RTL, clocks, multipliers or physical implementation.
namespace eval ::iigs_replay {
    variable baseline
    variable stream
}

proc ::iigs_replay::scopes {} {
    set native [get_clocks -quiet apple_iigs_clk14]
    if {[llength $native] != 1} {error "Replay requires one native clock"}
    set eject [get_cells -hierarchical -quiet -regexp \
        {.*g_apple_iigs_machine[/.]machine_adapter/native_eject35_q_reg$}]
    set config [get_cells -hierarchical -quiet -regexp \
        {.*g_apple_iigs_machine[/.]machine_adapter/loader_i/config_bytes_reg(\[[0-9]+\])?(_rep[^/]*)?$}]
    set publication [get_cells -hierarchical -quiet -regexp \
        {.*g_apple_iigs_machine[/.]machine_adapter/(native_(reset|cold)_q|woz_(sd_(addr|data|ack|write)|mount|readonly|size)_q)_reg(\[[0-9]+\])*(_rep[^/]*)?$}]
    set stall [get_cells -hierarchical -quiet -regexp \
        {.*g_apple_iigs_machine[/.]machine_adapter/native_stall_q_reg$}]
    return [list \
        eject $native [get_pins -of_objects $eject -filter {REF_PIN_NAME == D}] 2 end \
        configuration [get_pins -of_objects $config -filter {REF_PIN_NAME == C}] $native 3 start \
        publication [get_pins -of_objects $publication -filter {REF_PIN_NAME == C}] $native 3 start \
        stall $native [get_pins -of_objects $stall -filter {REF_PIN_NAME == D}] 3 end]
}

proc ::iigs_replay::path_signature {path} {
    if {[llength $path] != 1} {error "Replay check requires one timing path"}
    set signature [dict create]
    foreach field {SLACK REQUIREMENT EXCEPTION STARTPOINT_CLOCK ENDPOINT_CLOCK} {
        dict set signature $field [get_property $field $path]
    }
    set slack [dict get $signature SLACK]
    if {![string is double -strict $slack] || ![expr {abs($slack) < Inf}] || $slack < 0} {
        error "Replay check found invalid or negative slack: $signature"
    }
    return $signature
}

proc ::iigs_replay::snapshot {} {
    set primary [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_c64*}]
    if {[llength $primary] != 1} {error "Replay requires one primary clock"}
    set period [get_property PERIOD $primary]
    set result [dict create]
    foreach delay {max min} {
        dict set result global $delay [path_signature \
            [get_timing_paths -quiet -delay_type $delay -max_paths 1]]
    }
    foreach {label from to multiplier reference} [scopes] {
        if {[llength $from] == 0 || [llength $to] == 0} {
            error "Empty replay scope: $label"
        }
        dict set result $label from [lsort $from]
        dict set result $label to [lsort $to]
        foreach delay {max min} {
            set signature [path_signature [get_timing_paths -quiet -from $from -to $to \
                -delay_type $delay -max_paths 1]]
            set expected [expr {$delay eq "max" ? $multiplier * $period : 0.0}]
            if {abs([dict get $signature REQUIREMENT] - $expected) > 0.004} {
                error "Replay changed $label $delay requirement: $signature"
            }
            set exception [dict get $signature EXCEPTION]
            if {![regexp "Setup +-$reference +$multiplier" $exception]} {
                error "Replay lost $label setup exception: $signature"
            }
            if {$delay eq "min" && ![regexp "Hold +-$reference +[expr {$multiplier - 1}]" $exception]} {
                error "Replay lost $label companion hold exception: $signature"
            }
            dict set result $label $delay $signature
        }
    }
    return $result
}

proc ::iigs_replay::check {stage} {
    variable baseline
    variable stream
    set current [snapshot]
    puts $stream "STAGE $stage"
    puts $stream $current
    flush $stream
    if {$current ne $baseline} {
        error "IIgs replay changed an effective requirement, scope, exception or slack at $stage"
    }
    puts "APPLE_IIGS_REPLAY_EQUIVALENCE_PASS stage=$stage groups=4 setup_hold_pairs=4"
}

proc ::iigs_replay::begin {candidate report} {
    variable baseline
    variable stream
    set stream [open $report w]
    set baseline [snapshot]
    puts $stream "STAGE original_routed_checkpoint"
    puts $stream $baseline
    flush $stream
    # Each setup command resets only its original exact scope. Its following
    # unchanged companion restores normal hold; the candidate has no other XDC.
    read_xdc $candidate
    check after_exact_scope_reserialization
}

proc ::iigs_replay::finish {} {
    variable stream
    check final_export
    close $stream
}
