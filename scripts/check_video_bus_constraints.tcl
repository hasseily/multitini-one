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
            # XPM transports clog2(depth) pointer bits. Its separate data-count
            # outputs are one bit wider; the 256-word FIFO uses eight here.
            if {$count != 8} {error "Expected eight capture FIFO pointer bits: $group ($count)"}
            incr fifo_groups
        } else {
            error "Unreviewed Gray crossing: $group"
        }
    }
    # The exporter captures these settings before opening the implemented
    # checkpoint. A checkpoint's GateLvl fileset does not retain GENERIC.
    if {![info exists ::multitini_signoff_generics]} {
        error "CDC census requires captured build generics"
    }
    set machines {}
    foreach setting $::multitini_signoff_generics {
        if {[string match {ACTIVE_MACHINE=*} $setting]} {
            if {![regexp {^ACTIVE_MACHINE=([0-4])$} $setting -> machine]} {
                error "Unreviewed ACTIVE_MACHINE build generic: $setting"
            }
            lappend machines $machine
        }
    }
    if {[llength $machines] != 1} {error "CDC census requires one ACTIVE_MACHINE build generic"}
    set active_machine [lindex $machines 0]
    set needs_core_pointers [expr {$active_machine in {0 1}}]
    if {$core_groups != ($needs_core_pointers ? 2 : 0)} {
        error "Frame-bank Gray crossing census changed: $core_groups groups"
    }
    # A supervisor image may trim the inactive capture FIFO. Machine images
    # must retain both directions; a partially removed FIFO is never accepted.
    # Genesis additionally captures its native source frame across the machine
    # to control-clock boundary before the common effects pipeline.
    set expected_fifo_groups [expr {$active_machine == 4 ? 4 : 2}]
    if {$fifo_groups != $expected_fifo_groups &&
        !($fifo_groups == 0 && $active_machine == 2)} {
        error "Capture FIFO Gray crossing census changed: $fifo_groups groups"
    }
    # open_run/open_checkpoint restore constraint objects, not the Tcl
    # variables used while reading XDC. Query the implemented cells directly;
    # this gate must never install an exception to make its own checks pass.
    set reviewed_first_stages [get_cells -hierarchical -quiet -regexp \
        {(^|.*/)(sync_meta_reg(\[[0-9]+\])?|active_(visible|bank|dense)_meta_reg|dst_sync_reg\[0\]|(g_drain_reset[./])?(idle_destination|recovered_source)_reg\[0\]|(mailbox_idle_pixel|recovered_control|underrun_sync|live_sync|control_release)_reg\[0\])$}]
    if {[llength $reviewed_first_stages] == 0} {
        error "No reviewed shell first-stage CDC cells found"
    }
    set constant_first_stages 0
    foreach cell $reviewed_first_stages {
        if {![get_property ASYNC_REG $cell]} {error "False-path target is not a reviewed synchronizer: $cell"}
        set pin [get_pins -of_objects $cell -filter {REF_PIN_NAME == D}]
        if {[llength $pin] != 1} {error "Shell first stage has no unique D pin: $cell"}
        # An explicit -to query returns the path even when it is cut;
        # inspect its effective exception instead of expecting no path.
        set paths [get_timing_paths -quiet -to $pin -max_paths 1]
        if {[llength $paths] == 0} {
            # An inactive machine can leave a retained ASYNC_REG tied off.
            # Accept only a direct GND/VCC primitive, never a missing timed
            # path whose driver or clock is unexplained.
            set nets [get_nets -of_objects $pin]
            set drivers [get_pins -leaf -of_objects $nets -filter {DIRECTION == OUT}]
            set driver_cells [get_cells -of_objects $drivers]
            if {[llength $nets] == 1 && [get_property IS_TIED $pin] &&
                [get_property TYPE $nets] in {GROUND POWER} &&
                [llength $drivers] == 1 && [llength $driver_cells] == 1 &&
                [get_property REF_NAME $driver_cells] in {GND VCC}} {
                incr constant_first_stages
                puts "INFO: Shell first-stage input is directly constant: $cell"
                continue
            }
        }
        if {[llength $paths] != 1 ||
            [get_property EXCEPTION [lindex $paths 0]] ne "False Path"} {
            error "Shell first-stage input is not false-pathed: $cell"
        }
    }
    # Census the recovery synchronizers independently of the XDC selector.
    # A flattened generate prefix once made every first stage silently miss
    # that selector even though the other shell CDC exceptions were present.
    set recovery_cells [get_cells -hierarchical -quiet \
        -filter {ASYNC_REG == TRUE && NAME =~ *g_drain_reset*}]
    if {[llength $recovery_cells] == 0} {error "No mailbox recovery synchronizers found"}
    set recovery_groups [dict create]
    foreach cell $recovery_cells {
        set name [get_property NAME $cell]
        if {![regexp {^(.*[./]g_drain_reset)[./](idle_destination|recovered_source)_reg\[([01])\]$} \
                $name -> group family stage]} {
            error "Unreviewed mailbox recovery synchronizer: $name"
        }
        dict set recovery_groups $group "$family,$stage" $cell
    }
    set recovery_checked 0
    dict for {group stages} $recovery_groups {
        foreach family {idle_destination recovered_source} {
            foreach stage {0 1} {
                if {![dict exists $stages "$family,$stage"]} {
                    error "Mailbox recovery synchronizer stage missing: $group/$family stage $stage"
                }
            }
            set first [dict get $stages "$family,0"]
            set second [dict get $stages "$family,1"]
            if {[lsearch -exact $reviewed_first_stages $first] < 0 ||
                [lsearch -exact $reviewed_first_stages $second] >= 0} {
                error "Mailbox CDC exception must select only the first stage: $group/$family"
            }
            set paths [get_timing_paths -quiet \
                -from [get_pins -of_objects $first -filter {REF_PIN_NAME == Q}] \
                -to [get_pins -of_objects $second -filter {REF_PIN_NAME == D}] -max_paths 1]
            if {[llength $paths] != 1} {error "Mailbox second stage is not timed: $second"}
            set path [lindex $paths 0]
            set requirement [get_property REQUIREMENT $path]
            set slack [get_property SLACK $path]
            if {![finite $requirement] || $requirement <= 0.0 ||
                ![finite $slack] || $slack < 0.0 ||
                [get_property STARTPOINT_CLOCK $path] ne [get_property ENDPOINT_CLOCK $path]} {
                error "Mailbox second-stage timing missing or failing: $second ($requirement ns; $slack ns slack)"
            }
            incr recovery_checked
        }
    }
    puts "PASS: [expr {[llength $reviewed_first_stages] - $constant_first_stages}] effective shell first-stage exceptions; $constant_first_stages directly constant inputs"
    puts "PASS: $recovery_checked mailbox first-stage exceptions and timed second stages"
    puts "PASS: $checked effective Gray-pointer maximum delays are finite and met"
}
