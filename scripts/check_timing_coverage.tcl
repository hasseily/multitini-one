# SPDX-License-Identifier: GPL-3.0-only
# The bidirectional W5100S data pads have explicit point-to-point max delays.
# Vivado still labels their input delay "partial" because the same pad is an
# output. Permit only those eight named pads, after the routed path checker
# has proved both directions. Every other coverage failure remains fatal.
proc check_multitini_timing_coverage {report ethernet_checked} {
    foreach pattern {
        {There (are|is) [1-9][0-9]* register/latch pins with no clock}
        {There (are|is) [1-9][0-9]* pins that are not constrained for maximum delay}
        {There (are|is) [1-9][0-9]* input ports with no input delay specified}
        {There (are|is) [1-9][0-9]* ports with no output delay specified\. \(HIGH\)}
        {There (are|is) [1-9][0-9]* ports with partial output delay specified}
    } {
        if {[regexp -nocase $pattern $report]} {
            error "Timing coverage gate found one or more unconstrained objects"
        }
    }
    set lines [split $report \n]
    set records 0
    for {set i 0} {$i < [llength $lines]} {incr i} {
        if {![regexp -nocase {There (?:are|is) ([0-9]+) input ports with partial input delay specified} [lindex $lines $i] -> count]} {continue}
        incr records
        if {$count == 0} {continue}
        if {!$ethernet_checked || $count != 8} {
            error "Unqualified partial input timing constraints"
        }
        set ports {}
        for {incr i} {$i < [llength $lines]} {incr i} {
            set line [string trim [lindex $lines $i]]
            if {$line eq ""} {
                if {[llength $ports]} {break}
                continue
            }
            lappend ports $line
        }
        set expected {}
        for {set bit 0} {$bit < 8} {incr bit} {lappend expected [format {eth_d[%d]} $bit]}
        if {[lsort $ports] ne [lsort $expected]} {
            error "Unexpected partially constrained input ports: $ports"
        }
    }
    if {$records != 1} {error "Missing or ambiguous partial input timing coverage report"}
}
