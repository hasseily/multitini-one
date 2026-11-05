# SPDX-License-Identifier: GPL-3.0-only
# Adapted from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0.
# Shared profiles for create_project and build_and_export_xsa.
# Apply the fabric margin before placement and run one routed repair.
namespace eval multitini_run_profile {
    # Preserve completed runs when their settings already match the profile.
    proc set_if_changed {run property expected} {
        if {[get_property $property $run] ne $expected} {
            set_property $property $expected $run
        }
        if {[get_property $property $run] ne $expected} {
            error "Vivado run setting did not apply: $run $property"
        }
    }
    set helper_dir [file dirname [file normalize [info script]]]
    set threads_hook [file join $helper_dir vivado_run_threads.tcl]
    set margin_apply [file join $helper_dir apply_fabric_timing_margin.tcl]
    set route_finish [file join $helper_dir finish_video_timing.tcl]
    foreach hook [list $threads_hook $margin_apply $route_finish] {
        if {![file isfile $hook]} {error "Missing Vivado run hook: $hook"}
    }
    source $threads_hook
    set synth [get_runs synth_1]
    set impl [get_runs impl_1]
    set_if_changed $synth STRATEGY {Vivado Synthesis Defaults}
    set_if_changed $impl STRATEGY {Vivado Implementation Defaults}

    set synth_settings [dict create \
        WRITE_INCREMENTAL_SYNTH_CHECKPOINT 1 \
        STEPS.SYNTH_DESIGN.ARGS.DIRECTIVE Default \
        STEPS.SYNTH_DESIGN.ARGS.GLOBAL_RETIMING auto \
        STEPS.SYNTH_DESIGN.ARGS.CONTROL_SET_OPT_THRESHOLD auto \
        STEPS.SYNTH_DESIGN.TCL.PRE $threads_hook]
    set impl_settings [dict create \
        STEPS.OPT_DESIGN.IS_ENABLED 1 \
        STEPS.OPT_DESIGN.ARGS.DIRECTIVE Explore \
        STEPS.POWER_OPT_DESIGN.IS_ENABLED 0 \
        STEPS.PLACE_DESIGN.ARGS.DIRECTIVE Default \
        STEPS.POST_PLACE_POWER_OPT_DESIGN.IS_ENABLED 0 \
        STEPS.PHYS_OPT_DESIGN.IS_ENABLED 1 \
        STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE Explore \
        STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE Explore \
        STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED 0 \
        STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE Default \
        STEPS.INIT_DESIGN.TCL.PRE $threads_hook \
        STEPS.OPT_DESIGN.TCL.PRE $margin_apply \
        STEPS.ROUTE_DESIGN.TCL.POST $route_finish]
    # Read the selected design, not a possibly stale process environment.
    # C64's eight SID engines need the measured sequential-area packing pass.
    # Other machines retain the shared defaults above.
    set source_sets [get_filesets sources_1]
    if {[llength $source_sets] != 1} {error "Expected one top-level source fileset"}
    set top_generics [get_property GENERIC $source_sets]
    set c64_profile [regexp {(^|[[:space:]])ACTIVE_MACHINE=0($|[[:space:]])} $top_generics]
    set ap68040_profile [regexp {(^|[[:space:]])AMIGA_AP68040=1'b1($|[[:space:]])} $top_generics]
    if {$c64_profile} {
        set control_merge [file join $helper_dir optimize_c64_control_sets.tcl]
        if {![file isfile $control_merge]} {error "Missing C64 run hook: $control_merge"}
        dict set impl_settings STEPS.OPT_DESIGN.ARGS.DIRECTIVE ExploreSequentialArea
        dict set impl_settings STEPS.OPT_DESIGN.TCL.POST $control_merge
        dict set impl_settings STEPS.PLACE_DESIGN.ARGS.DIRECTIVE Explore
        # Pixel and CPU half-cycle placement margin; finish_c64_timing restores it.
        set placement_margin [file join $helper_dir apply_c64_placement_margin.tcl]
        set cpu_finish [file join $helper_dir finish_c64_timing.tcl]
        foreach hook [list $placement_margin [file join $helper_dir c64_placement_margin.tcl] \
                          $cpu_finish [file join $helper_dir optimize_c64_cpu_margin.tcl]] {
            if {![file isfile $hook]} {error "Missing C64 timing hook: $hook"}
        }
        dict set impl_settings STEPS.PLACE_DESIGN.TCL.PRE $placement_margin
        dict set impl_settings STEPS.ROUTE_DESIGN.TCL.POST $cpu_finish
    } elseif {$ap68040_profile} {
        # Preserve the timing-driven placer for the control and video domains.
        # Moderate area synthesis leaves room for it in this dense image.
        dict set synth_settings STEPS.SYNTH_DESIGN.ARGS.DIRECTIVE AreaOptimized_medium
        dict set synth_settings STEPS.SYNTH_DESIGN.ARGS.CONTROL_SET_OPT_THRESHOLD 8
        dict set impl_settings STEPS.OPT_DESIGN.ARGS.DIRECTIVE ExploreArea
    }
    # Other cores keep one shared pass; C64 also targets the CPU half cycles.
    foreach run [list $synth $impl] settings [list $synth_settings $impl_settings] {
        # Clear stale hooks/options without clearing a desired hook first.
        foreach property [list_property $run] {
            if {([regexp {^STEPS\..*\.TCL\.(PRE|POST)$} $property] ||
                 [string match {STEPS.*.ARGS.MORE OPTIONS} $property]) &&
                ![dict exists $settings $property]} {
                dict set settings $property {}
            }
        }
        dict set settings INCREMENTAL_CHECKPOINT {}
        dict set settings AUTO_INCREMENTAL_CHECKPOINT 1
        dict for {property expected} $settings {
            set_if_changed $run $property $expected
        }
    }
    # IP/OOC synthesis runs use other source filesets; leave their strategies
    # and post hooks intact. Do not alter other top-level experiment runs.
    foreach run [get_runs -filter {IS_SYNTHESIS && SRCSET != sources_1}] {
        set_if_changed $run STEPS.SYNTH_DESIGN.TCL.PRE $threads_hook
    }
    puts "Vivado profile: SYNTH [dict get $synth_settings STEPS.SYNTH_DESIGN.ARGS.DIRECTIVE], OPT [dict get $impl_settings STEPS.OPT_DESIGN.ARGS.DIRECTIVE], PLACE [dict get $impl_settings STEPS.PLACE_DESIGN.ARGS.DIRECTIVE], PHYS enabled=[dict get $impl_settings STEPS.PHYS_OPT_DESIGN.IS_ENABLED], ROUTE Explore, routed PHYS AggressiveExplore[expr {$c64_profile ? ", C64 pixel/CPU placement margin" : ""}]"
}
