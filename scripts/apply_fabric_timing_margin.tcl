# SPDX-License-Identifier: GPL-3.0-only
# Adapted from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0.
set fabric_clock [get_clocks -quiet clk_fpga_0]
set pixel_clock [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_video*}]
if {[llength $fabric_clock] != 1 || [llength $pixel_clock] != 1} {
    error "Expected exactly one fabric clock and one pixel clock."
}

set margin_clocks [concat $fabric_clock $pixel_clock]
foreach margin_clock $margin_clocks {
    set margin_path [get_timing_paths -quiet -delay_type max \
        -from $margin_clock -to $margin_clock -max_paths 1]
    if {[llength $margin_path] != 1} {
        error "No setup path for $margin_clock before applying timing margin."
    }
    set user_uncertainty [get_property USER_UNCERTAINTY $margin_path]
    if {$user_uncertainty ne "" &&
        (![string is double -strict $user_uncertainty] ||
        abs(double($user_uncertainty)) > 0.0005)} {
        error "Refusing to replace $margin_clock user uncertainty: $user_uncertainty ns."
    }
}

set_clock_uncertainty -setup 0.200 $fabric_clock

foreach margin_clock $margin_clocks {
    set margin_path [get_timing_paths -quiet -delay_type max \
        -from $margin_clock -to $margin_clock -max_paths 1]
    if {[llength $margin_path] != 1} {
        error "No setup path for $margin_clock after applying timing margin."
    }
    set user_uncertainty [get_property USER_UNCERTAINTY $margin_path]
    if {$user_uncertainty eq ""} {set user_uncertainty 0.000}
    set expected [expr {$margin_clock eq $fabric_clock ? 0.200 : 0.000}]
    if {![string is double -strict $user_uncertainty] ||
        abs(double($user_uncertainty) - $expected) > 0.0005} {
        error "$margin_clock implementation margin did not apply: $user_uncertainty ns."
    }
    puts "Implementation setup margin for $margin_clock: $user_uncertainty ns"
}
