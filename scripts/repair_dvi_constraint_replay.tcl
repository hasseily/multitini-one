# SPDX-License-Identifier: GPL-3.0-only
# Supplemental signoff-only reserialization of the original DVI delay pair.
# Never create a clock, alter timing values, or change the implementation.
namespace eval ::dvi_replay {
    variable baseline
    variable stream
}

proc ::dvi_replay::one {description objects} {
    if {[llength $objects] != 1} {error "DVI replay requires one $description: $objects"}
}

proc ::dvi_replay::snapshot {} {
    set clock [get_clocks -quiet -filter {NAME == dvi_clk_out}]
    one "forwarded DVI clock" $clock
    set source [get_pins -hierarchical -quiet -filter {NAME =~ *video_test/dvi_clk_oddr/C}]
    one "DVI ODDR source" $source
    set master [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_video*}]
    one "DVI master clock" $master
    set destination [get_clocks -quiet -of_objects [get_ports dvi_clk]]
    if {[get_property SOURCE $clock] ne $source ||
        [get_property MASTER_CLOCK $clock] ne $master || $destination ne $clock ||
        [get_property EDGES $clock] ne "2 3 4" ||
        ![get_property IS_GENERATED $clock] || ![get_property IS_USER_GENERATED $clock] ||
        [get_property IS_INVERTED $clock] ||
        abs([get_property PERIOD $clock] - [get_property PERIOD $master]) > 0.001} {
        error "DVI replay requires the original generated clock source, master, edges and period"
    }
    set result [dict create]
    foreach field {NAME PERIOD WAVEFORM SOURCE MASTER_CLOCK EDGES IS_GENERATED IS_USER_GENERATED IS_INVERTED} {
        dict set result clock $field [get_property $field $clock]
    }
    set ports [lsort [get_ports -quiet \
        {dvi_red[*] dvi_grn[*] dvi_blu[*] dvi_de dvi_hsync dvi_vsync}]]
    if {[llength $ports] != 19} {error "DVI replay requires the original 19 output ports: $ports"}
    dict set result ports $ports
    foreach port $ports {
        foreach delay {max min} expected {1.2 -1.3} {
            set path [get_timing_paths -quiet -to $port -delay_type $delay -max_paths 1]
            one "$port $delay timing path" $path
            set report [report_timing -quiet -to $port -delay_type $delay -max_paths 1 -return_string]
            if {![regexp {Output Delay:[ \t]+([-0-9.]+)ns} $report -> external] ||
                abs(double($external) - $expected) > 0.0005 ||
                [get_property ENDPOINT_CLOCK $path] ne $clock ||
                [get_property ENDPOINT_PIN $path] ne $port} {
                error "DVI replay lost the original $port $delay output delay or reference clock"
            }
            set signature [dict create output_delay $external]
            foreach field {SLACK REQUIREMENT EXCEPTION STARTPOINT_CLOCK ENDPOINT_CLOCK STARTPOINT_PIN ENDPOINT_PIN} {
                dict set signature $field [get_property $field $path]
            }
            set slack [dict get $signature SLACK]
            if {![string is double -strict $slack] || ![expr {abs($slack) < Inf}] || $slack < 0} {
                error "DVI replay found invalid or negative $port $delay slack: $signature"
            }
            dict set result paths $port $delay $signature
        }
    }
    foreach delay {max min} {
        set path [get_timing_paths -quiet -delay_type $delay -max_paths 1]
        one "global $delay timing path" $path
        set signature [dict create]
        foreach field {SLACK REQUIREMENT EXCEPTION STARTPOINT_CLOCK ENDPOINT_CLOCK} {
            dict set signature $field [get_property $field $path]
        }
        set slack [dict get $signature SLACK]
        if {![string is double -strict $slack] || ![expr {abs($slack) < Inf}] || $slack < 0} {
            error "DVI replay found invalid or negative global $delay slack: $signature"
        }
        dict set result global $delay $signature
    }
    return $result
}

proc ::dvi_replay::check {stage} {
    variable baseline
    variable stream
    set current [snapshot]
    puts $stream "STAGE $stage"
    puts $stream $current
    flush $stream
    if {$current ne $baseline} {
        error "DVI replay changed a port, clock, output delay, path identity, requirement or slack at $stage"
    }
    puts "DVI_REPLAY_EQUIVALENCE_PASS stage=$stage ports=19 max_min_paths=38"
}

proc ::dvi_replay::begin {candidate report} {
    variable baseline
    variable stream
    set stream [open $report w]
    set baseline [snapshot]
    puts $stream "STAGE original_routed_checkpoint"
    puts $stream $baseline
    flush $stream
    read_xdc $candidate
    check after_exact_pair_reserialization
}

proc ::dvi_replay::finish {} {
    variable stream
    check final_export
    close $stream
}
