# SPDX-License-Identifier: GPL-3.0-only
# C64 placement margin. The shared profile protects only clk_fpga_0 before
# placement, so the 148.5 MHz pixel paths were placed with no margin and dev9
# NTSC failed on them. Place, phys_opt and route therefore see 0.300 ns on the
# pixel clock and on both CPU half-cycle relations (normally 0.200 ns), so the
# CPU island is not traded away for video slack.
# apply: PLACE_DESIGN pre-hook, after the 0.200 ns fabric margin is in place.
# restore: first step of finish_c64_timing.tcl, before the shared finish passes.
namespace eval c64_placement_margin {
    proc clocks {} {
        set fabric [get_clocks -quiet clk_fpga_0]
        set pixel [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_video*}]
        set fast [get_clocks -quiet clk_out2_zynq_ps_bd_clk_wiz_c64_0]
        if {[llength $fabric] != 1 || [llength $pixel] != 1 || [llength $fast] != 1} {
            error "Expected exactly one fabric, pixel and C64 CPU clock."
        }
        return [list $fabric $pixel $fast]
    }
    proc uncertainty {args} {
        set path [get_timing_paths -quiet -delay_type max -max_paths 1 {*}$args]
        if {[llength $path] != 1} {error "No setup path for C64 placement margin: $args"}
        set value [get_property USER_UNCERTAINTY $path]
        if {$value eq ""} {return 0.0}
        if {![string is double -strict $value]} {error "Nonnumeric clock uncertainty: $value"}
        return $value
    }
    # Require the fabric, pixel and CPU half-cycle setup uncertainties.
    proc require {fabric_expected pixel_expected cpu_expected} {
        lassign [clocks] fabric pixel fast
        foreach {clock expected} [list $fabric $fabric_expected $pixel $pixel_expected] {
            set actual [uncertainty -from $clock -to $clock]
            if {abs($actual - $expected) > 0.0005} {
                error "$clock setup uncertainty must be $expected ns, got $actual ns."
            }
        }
        foreach edges {{rise fall} {fall rise}} {
            lassign $edges launch capture
            set actual [uncertainty -${launch}_from $fast -${capture}_to $fast]
            if {abs($actual - $cpu_expected) > 0.0005} {
                error "C64 $launch-to-$capture setup uncertainty must be $cpu_expected ns, got $actual ns."
            }
        }
    }
    proc set_values {pixel_value cpu_value} {
        lassign [clocks] fabric pixel fast
        set_clock_uncertainty -setup $pixel_value $pixel
        set_clock_uncertainty -setup $cpu_value -rise_from $fast -fall_to $fast
        set_clock_uncertainty -setup $cpu_value -fall_from $fast -rise_to $fast
    }
    proc apply {} {
        require 0.200 0.000 0.200
        set_values 0.300 0.300
        require 0.200 0.300 0.300
        puts "C64_PLACEMENT_MARGIN_APPLIED pixel_ns=0.300 cpu_half_ns=0.300 fabric_ns=0.200"
    }
    proc restore {} {
        require 0.200 0.300 0.300
        set_values 0.000 0.200
        require 0.200 0.000 0.200
        puts "C64_PLACEMENT_MARGIN_RESTORED pixel_ns=0.000 cpu_half_ns=0.200 fabric_ns=0.200"
    }
}
