# SPDX-License-Identifier: GPL-3.0-only
# One bounded physical pass targeting only the two CPU half-cycle setup
# relations. The permanent 0.200 ns uncertainty is restored before signoff.
namespace eval c64_cpu_margin {
    proc uncertainty {path} {
        set value [get_property USER_UNCERTAINTY $path]
        if {$value eq ""} {return 0.0}
        if {![string is double -strict $value]} {error "Nonnumeric clock uncertainty"}
        return $value
    }
    proc require_halves {fast expected} {
        foreach edges {{rise fall} {fall rise}} {
            lassign $edges launch capture
            set path [get_timing_paths -quiet -${launch}_from $fast -${capture}_to $fast -delay_type max -max_paths 1]
            if {[llength $path]!=1 || abs([uncertainty $path]-$expected)>0.00001} {
                error "C64 $launch-to-$capture setup uncertainty must be $expected ns"
            }
            puts "C64_CPU_MARGIN launch=$launch capture=$capture uncertainty_ns=$expected slack_ns=[get_property SLACK $path]"
        }
    }
    proc clock_identity {} {
        set result {}
        foreach clock [lsort [get_clocks]] {
            foreach property {PERIOD WAVEFORM MASTER_CLOCK SOURCE SOURCE_PINS} {
                dict set result $clock $property [get_property $property $clock]
            }
        }
        set mmcm [get_cells -hier -filter {REF_NAME == MMCME2_ADV && NAME =~ *clk_wiz_c64*}]
        if {[llength $mmcm]!=1} {error "Missing exact C64 clock MMCM"}
        foreach property {DIVCLK_DIVIDE CLKFBOUT_MULT_F CLKOUT0_DIVIDE_F CLKOUT1_DIVIDE CLKOUT0_PHASE CLKOUT1_PHASE CLKOUT0_DUTY_CYCLE CLKOUT1_DUTY_CYCLE} {
            dict set result MMCM $property [get_property $property $mmcm]
        }
        return $result
    }
    proc other_uncertainties {fast native} {
        set result {}
        foreach type {max min} {
            foreach edges {{rise rise} {rise fall} {fall rise}} {
                lassign $edges launch capture
                if {$type eq "max" && $launch ne $capture} {continue}
                set path [get_timing_paths -quiet -${launch}_from $fast -${capture}_to $fast -delay_type $type -max_paths 1]
                if {[llength $path]!=1} {error "Missing unchanged CPU $edges $type relation"}
                dict set result "cpu-$launch-$capture-$type" [uncertainty $path]
            }
            foreach pair [list [list $native $fast] [list $fast $native]] {
                lassign $pair src dst
                set path [get_timing_paths -quiet -from $src -to $dst -delay_type $type -max_paths 1]
                if {[llength $path]!=1} {error "Missing unchanged related-clock $type relation"}
                dict set result "$src-$dst-$type" [uncertainty $path]
            }
            foreach clock [get_clocks {clk_fpga_0 clk_out1_zynq_ps_bd_clk_wiz_video_0}] {
                set path [get_timing_paths -quiet -from $clock -to $clock -delay_type $type -max_paths 1]
                if {[llength $path]!=1} {error "Missing unchanged $clock $type relation"}
                dict set result "$clock-$type" [uncertainty $path]
            }
        }
        return $result
    }
    set fast [get_clocks -quiet clk_out2_zynq_ps_bd_clk_wiz_c64_0]
    set native [get_clocks -quiet clk_out1_zynq_ps_bd_clk_wiz_c64_0]
    if {[llength $fast]!=1 || [llength $native]!=1} {error "Missing exact C64 related clock pair"}
    require_halves $fast 0.200
    set original_identity [clock_identity]
    set original_other [other_uncertainties $fast $native]
    set failed [catch {
        set_clock_uncertainty -setup 0.300 -rise_from $fast -fall_to $fast
        set_clock_uncertainty -setup 0.300 -fall_from $fast -rise_to $fast
        require_halves $fast 0.300
        if {[clock_identity] ne $original_identity || [other_uncertainties $fast $native] ne $original_other} {
            error "C64 margin changed a non-target clock relation"
        }
        phys_opt_design -directive AggressiveExplore
    } failure options]
    # Restore even if an optimizer or validation error is raised.
    set_clock_uncertainty -setup 0.200 -rise_from $fast -fall_to $fast
    set_clock_uncertainty -setup 0.200 -fall_from $fast -rise_to $fast
    require_halves $fast 0.200
    if {[clock_identity] ne $original_identity || [other_uncertainties $fast $native] ne $original_other} {
        error "C64 clock or non-target uncertainty changed during physical optimization"
    }
    if {$failed} {return -options $options $failure}
    puts "C64_CPU_MARGIN_RESTORED setup_half_ns=0.200 clocks_unchanged=true non_target_uncertainties_unchanged=true"
}
