# SPDX-License-Identifier: GPL-3.0-only
# Adapted from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0.
# Check *effective* route bounds, after all XPM and board exceptions are read.
# A clock-group false path can silently defeat a max-delay while WNS passes.
namespace eval multitini_cdc_gate {
    proc finite {value} {
        expr {[string is double -strict $value] &&
              ![regexp -nocase {nan|inf} $value]}
    }
    set source_cells [get_cells -hierarchical -quiet -regexp {.*src_gray_ff_reg\[[0-9]+\]$}]
    set destination_cells [get_cells -hierarchical -quiet -regexp {.*dest_graysync_ff_reg.*}]
    set destination_pins [get_pins -of_objects $destination_cells -filter {REF_PIN_NAME == D}]
    set groups [dict create]
    set checked 0
    foreach cell $source_cells {
        set name [get_property NAME $cell]
        set group [file dirname $name]
        dict incr groups $group
        set clock_pin [get_pins -of_objects $cell -filter {REF_PIN_NAME == C}]
        set clocks [get_clocks -of_objects $clock_pin]
        if {[llength $clocks] != 1} {error "Gray source has no unique clock: $cell"}
        set limit [get_property PERIOD $clocks]
        if {![finite $limit] || $limit <= 0.0} {error "Invalid Gray source period: $cell"}
        set source_pin [get_pins -of_objects $cell -filter {REF_PIN_NAME == Q}]
        set paths [get_timing_paths -quiet -from $source_pin -to $destination_pins -max_paths 100 -nworst 1]
        if {[llength $paths] != 1} {
            error "Gray bit must have exactly one bounded crossing: $cell ([llength $paths] paths)"
        }
        set path [lindex $paths 0]
        set requirement [get_property REQUIREMENT $path]
        set slack [get_property SLACK $path]
        set endpoint [get_property ENDPOINT_PIN $path]
        if {![string match "${group}/dest_graysync_ff_reg*" $endpoint] ||
            ![finite $requirement] || abs($requirement - $limit) > 0.0005 ||
            ![finite $slack] || $slack < 0.0} {
            error "Gray route bound missing or failing: $cell -> $endpoint ($requirement ns; $slack ns slack)"
        }
        incr checked
    }
    set core_groups 0
    set fifo_groups 0
    dict for {group count} $groups {
        if {[regexp {/(published_pointer_to_pixel|released_pointer_to_source)$} $group]} {
            if {$count != 3} {error "Expected three frame-bank pointer bits: $group ($count)"}
            incr core_groups
        } elseif {[regexp {/gen_cdc_pntr\.(wr_pntr_cdc_inst|rd_pntr_cdc_inst)$} $group]} {
            if {$count != 9} {error "Expected nine capture FIFO pointer bits: $group ($count)"}
            incr fifo_groups
        } else {
            error "Unreviewed Gray crossing: $group"
        }
    }
    set generics [get_property GENERIC [get_filesets sources_1]]
    set needs_core_pointers [expr {[string match {*ACTIVE_MACHINE=0*} $generics] ||
                                  [string match {*ACTIVE_MACHINE=1*} $generics]}]
    if {$core_groups != ($needs_core_pointers ? 2 : 0)} {
        error "Frame-bank Gray crossing census changed: $core_groups groups"
    }
    # A supervisor image may trim the inactive capture FIFO. Machine images
    # must retain both directions; a partially removed FIFO is never accepted.
    # Genesis additionally captures its native source frame across the machine
    # to control-clock boundary before the common effects pipeline.
    set expected_fifo_groups [expr {[string match {*ACTIVE_MACHINE=4*} $generics] ? 4 : 2}]
    if {$fifo_groups != $expected_fifo_groups &&
        !($fifo_groups == 0 && [string match {*ACTIVE_MACHINE=2*} $generics])} {
        error "Capture FIFO Gray crossing census changed: $fifo_groups groups"
    }
    if {![info exists ::multitini_cdc_first] || [llength $::multitini_cdc_first] == 0} {
        error "No shell first-stage CDC constraints were loaded"
    }
    foreach cell $::multitini_cdc_first {
        if {![get_property ASYNC_REG $cell]} {error "False-path target is not a reviewed synchronizer: $cell"}
    }
    puts "PASS: $checked effective Gray-pointer maximum delays are finite and met"
}
