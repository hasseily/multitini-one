#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the signoff Tcl against passing and deliberately broken timing databases."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
TCL = shutil.which('tclsh')

@unittest.skipUnless(TCL, 'native Tcl interpreter unavailable')
class TimingGateTests(unittest.TestCase):
    def run_tcl(self, script):
        return subprocess.run([TCL], input=script, text=True, capture_output=True)

    def test_effective_gray_bounds_fail_closed(self):
        stub = r'''
set sources {}
foreach group {video_framebuffer/capture/fifo_i/gen_cdc_pntr.wr_pntr_cdc_inst video_framebuffer/capture/fifo_i/gen_cdc_pntr.rd_pntr_cdc_inst c64_video/published_pointer_to_pixel c64_video/released_pointer_to_source} width {8 8 3 3} {
    for {set bit 0} {$bit < $width} {incr bit} {
        lappend sources [format {%s/src_gray_ff_reg[%d]} $group $bit]
    }
}
set machine 0
if {[string match "genesis-*" $scenario]} {
    set machine 4
    set sources [lrange $sources 0 15]
    foreach direction {wr rd} {
        for {set bit 0} {$bit < 8} {incr bit} {
            lappend sources [format {genesis_video/capture/fifo_i/gen_cdc_pntr.%s_pntr_cdc_inst/src_gray_ff_reg[%d]} $direction $bit]
        }
    }
    if {$scenario eq "genesis-missing-fifo"} {set sources [lrange $sources 0 23]}
}
set multitini_signoff_generics "SHELL_BUILD_ID=32'h20260930 ACTIVE_MACHINE=$machine"
if {$scenario eq "missing-build-generics"} {unset multitini_signoff_generics}
if {$scenario eq "missing-machine-generic"} {set multitini_signoff_generics {SHELL_BUILD_ID=32'h20260930}}
if {$scenario eq "unknown-machine-generic"} {set multitini_signoff_generics ACTIVE_MACHINE=5}
if {$scenario eq "duplicate-machine-generic"} {lappend multitini_signoff_generics ACTIVE_MACHINE=0}
if {$scenario eq "wrong-fifo-width"} {
    lappend sources {video_framebuffer/capture/fifo_i/gen_cdc_pntr.wr_pntr_cdc_inst/src_gray_ff_reg[8]}
}
if {$scenario eq "missing-bit"} {set sources [lrange $sources 0 end-1]}
set recovery_cells {}
set reviewed_cells {sampler/sync_meta_reg}
# Restored checkpoints have effective timing objects but no XDC Tcl state.
unset -nocomplain multitini_cdc_first
if {$scenario eq "checkpoint-stale-xdc-vars"} {set multitini_cdc_first wrong/stale/cell}
foreach family {idle_destination recovered_source} {
    foreach stage {0 1} {
        set cell [format {mailbox/g_drain_reset.%s_reg[%d]} $family $stage]
        lappend recovery_cells $cell
        if {$stage == 0} {lappend reviewed_cells $cell}
    }
}
if {$scenario eq "missing-mailbox-stage"} {set recovery_cells [lrange $recovery_cells 0 end-1]}
if {$scenario eq "renamed-mailbox-stage"} {
    lset recovery_cells 0 mailbox/g_drain_reset.renamed_reg
}
proc get_cells {args} {
    if {[lindex $args 0] eq "-of_objects"} {return constant_driver}
    if {[string match {*src_gray*} [lindex $args end]]} {return $::sources}
    if {[string match {*sync_meta_reg*} [lindex $args end]]} {
        if {$::scenario eq "missing-shell-samplers"} {return {}}
        return $::reviewed_cells
    }
    if {[string match {*g_drain_reset*} [lindex $args end]]} {return $::recovery_cells}
    return destination
}
proc get_pins {args} {
    if {[lindex $args 0] eq "-leaf"} {return constant_driver_pin}
    return [lindex $args 1]
}
proc get_nets {args} {return sampler_net}
proc get_clocks {args} {return source_clock}
proc get_filesets {args} {error "Gate must use captured metadata, not checkpoint fileset GENERIC"}
proc get_timing_paths {args} {
    set destination [lindex $args [expr {[lsearch -exact $args -to] + 1}]]
    if {$destination eq "sampler/sync_meta_reg"} {
        if {$::scenario in {constant-input nonconstant-no-path constant-net-bad-driver tied-signal-net}} {return {}}
        return shell_first_path
    }
    if {[string match {*g_drain_reset*} $destination]} {
        if {[regexp {\[0\]$} $destination]} {
            if {$::scenario eq "missing-mailbox-path"} {return {}}
            return mailbox_first_path
        }
        if {$::scenario eq "untimed-second-stage"} {return {}}
        return mailbox_path
    }
    if {$::scenario eq "false-path"} {return {}}
    return [list [lindex $args [expr {[lsearch -exact $args -from] + 1}]]]
}
proc get_property {property object} {
    switch -- $property {
        NAME {return $object}
        PERIOD {return 10.0}
        REQUIREMENT {if {$::scenario eq "unbounded" || ($::scenario eq "cut-second-stage" && $object eq "mailbox_path")} {return Inf}; if {$::scenario eq "wrong-bound"} {return 20.0}; return 10.0}
        SLACK {if {$::scenario eq "violated"} {return -0.001}; if {$::scenario eq "infinite-slack"} {return Inf}; return 0.5}
        ENDPOINT_PIN {return [string map {src_gray_ff_reg {dest_graysync_ff_reg[0]}} $object]}
        IS_TIED {return [expr {$::scenario in {constant-input constant-net-bad-driver tied-signal-net}}]}
        TYPE {if {$::scenario eq "tied-signal-net"} {return SIGNAL}; return GROUND}
        REF_NAME {if {$::scenario eq "constant-net-bad-driver"} {return LUT1}; return GND}
        ASYNC_REG {return [expr {$::scenario ne "unsafe-target"}]}
        STARTPOINT_CLOCK {return mailbox_clock}
        ENDPOINT_CLOCK {return mailbox_clock}
        EXCEPTION {
            if {($::scenario in {uncut-mailbox missed-mailbox} && $object eq "mailbox_first_path") ||
                ($::scenario eq "uncut-shell-sampler" && $object eq "shell_first_path")} {return {}}
            return {False Path}
        }
    }
    error "Unexpected property $property"
}
'''
        for scenario in ('pass', 'checkpoint-no-xdc-vars', 'checkpoint-stale-xdc-vars',
                         'constant-input', 'nonconstant-no-path', 'constant-net-bad-driver',
                         'tied-signal-net', 'missing-build-generics', 'missing-machine-generic',
                         'unknown-machine-generic', 'duplicate-machine-generic',
                         'missing-bit', 'false-path', 'unbounded', 'wrong-bound',
                         'violated', 'infinite-slack', 'unsafe-target',
                         'genesis-pass', 'genesis-missing-fifo', 'wrong-fifo-width',
                         'missed-mailbox', 'uncut-mailbox', 'cut-second-stage', 'missing-mailbox-path',
                         'missing-mailbox-stage', 'renamed-mailbox-stage',
                         'untimed-second-stage', 'missing-shell-samplers', 'uncut-shell-sampler'):
            with self.subTest(scenario=scenario):
                result = self.run_tcl(f'set scenario {scenario}\n' + stub +
                    f'if {{[catch {{source {{{ROOT / "scripts/check_video_bus_constraints.tcl"}}}}} error]}} {{puts stderr $error; exit 1}}\n')
                self.assertEqual(result.returncode, 0 if scenario in (
                    'pass', 'genesis-pass', 'checkpoint-no-xdc-vars', 'checkpoint-stale-xdc-vars',
                    'constant-input') else 1,
                                 result.stdout + result.stderr)
                if scenario == 'pass':
                    self.assertIn('22 effective Gray-pointer', result.stdout)
                elif scenario == 'genesis-pass':
                    self.assertIn('32 effective Gray-pointer', result.stdout)

    def test_run_profile_is_idempotent_and_sets_child_threads(self):
        script = r'''
set writes 0
set properties [dict create]
proc get_runs {args} {
    if {[lindex $args 0] eq "-filter"} {return ooc_synth}
    return [lindex $args 0]
}
proc list_property {run} {return {STEPS.ROUTE_DESIGN.TCL.POST STEPS.SYNTH_DESIGN.TCL.PRE}}
proc get_property {property run} {
    set key "$run/$property"
    if {[dict exists $::properties $key]} {return [dict get $::properties $key]}
    return {}
}
proc set_property {property value run} {dict set ::properties "$run/$property" $value; incr ::writes}
proc set_param {name value} {set ::thread_count $value}
proc get_param {name} {return $::thread_count}
'''
        path = ROOT / 'scripts/configure_vivado_run_profile.tcl'
        script += f'source {{{path}}}\nset initial $writes\nsource {{{path}}}\n'
        script += r'''
if {$initial == 0 || $writes != $initial || $thread_count != 8} {exit 1}
if {![string match *vivado_run_threads.tcl [get_property STEPS.SYNTH_DESIGN.TCL.PRE ooc_synth]]} {exit 2}
if {[get_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED impl_1] != 0} {exit 3}
if {![string match *finish_video_timing.tcl [get_property STEPS.ROUTE_DESIGN.TCL.POST impl_1]]} {exit 4}
'''
        result = self.run_tcl(script)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
