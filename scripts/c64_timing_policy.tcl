# SPDX-License-Identifier: GPL-3.0-only
# The normal release floor remains inclusive +0.100 ns. This named exception
# is explicitly restricted to the user-authorized dev8 C708 image, and is
# strictly greater than +0.050 ns. It changes no clock or timing constraint.
proc c64_resolve_timing_policy {machine generics requested} {
    if {$requested ni {default dev8-over-0.050}} {
        error "Unknown MULTITINI_C64_TIMING_POLICY: $requested"
    }
    if {$machine ne "C64"} {
        if {$requested ne "default"} {error "C64 timing exception requested for $machine"}
        return {}
    }
    set matches {}
    foreach word $generics {
        if {[string match SHELL_BUILD_ID=* $word]} {
            if {![regexp {^SHELL_BUILD_ID=32'h([0-9a-fA-F]{8})$} $word -> identity]} {
                error "Malformed C64 build identity"
            }
            lappend matches [string toupper $identity]
        }
    }
    if {[llength $matches] != 1} {error "Missing or ambiguous C64 build identity"}
    set build_id [lindex $matches 0]
    set minimum 0.100
    set exclusive false
    if {$requested eq "dev8-over-0.050"} {
        if {$build_id ne "4D00C708"} {error "Dev8 timing exception requires build 4D00C708"}
        set minimum 0.050
        set exclusive true
    }
    return [dict create schema 1 policy $requested build_id $build_id \
        minimum_setup_ns $minimum minimum_exclusive $exclusive duty_user_minimum_ns 0.200]
}

proc c64_write_timing_policy {path policy setup_paths} {
    if {[llength $setup_paths] != 3} {error "C64 timing policy needs all three clock-path groups"}
    set entries {}
    foreach item $setup_paths {
        foreach key {from to} {
            if {![regexp {^[a-zA-Z0-9_]+$} [dict get $item $key]]} {error "Unsafe clock identity in timing receipt"}
        }
        lappend entries [format {    {"from":"%s","to":"%s","slack_ns":%s,"requirement_ns":%s}} \
            [dict get $item from] [dict get $item to] [dict get $item slack_ns] [dict get $item requirement_ns]]
    }
    set stream [open $path w]
    puts $stream "\{"
    puts $stream "  \"schema\":1,"
    puts $stream "  \"policy\":\"[dict get $policy policy]\","
    puts $stream "  \"build_id\":\"[dict get $policy build_id]\","
    foreach key {minimum_setup_ns minimum_exclusive duty_user_minimum_ns} {
        puts $stream "  \"$key\":[dict get $policy $key],"
    }
    puts $stream "  \"setup_paths\":\[\n[join $entries ,\n]\n  \]"
    puts $stream "\}"
    close $stream
}
