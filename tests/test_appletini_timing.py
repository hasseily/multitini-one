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
foreach group {video_framebuffer/capture/fifo_i/gen_cdc_pntr.wr_pntr_cdc_inst video_framebuffer/capture/fifo_i/gen_cdc_pntr.rd_pntr_cdc_inst c64_video/published_pointer_to_pixel c64_video/released_pointer_to_source} width {9 9 3 3} {
    for {set bit 0} {$bit < $width} {incr bit} {
        lappend sources [format {%s/src_gray_ff_reg[%d]} $group $bit]
    }
}
if {$scenario eq "missing-bit"} {set sources [lrange $sources 0 end-1]}
set multitini_cdc_first {sampler/sync_meta_reg}
proc get_cells {args} {
    if {[string match {*src_gray*} [lindex $args end]]} {return $::sources}
    return destination
}
proc get_pins {args} {return [lindex $args 1]}
proc get_clocks {args} {return source_clock}
proc get_filesets {args} {return sources_1}
proc get_timing_paths {args} {
    if {$::scenario eq "false-path"} {return {}}
    return [list [lindex $args [expr {[lsearch -exact $args -from] + 1}]]]
}
proc get_property {property object} {
    switch -- $property {
        NAME {return $object}
        PERIOD {return 10.0}
        REQUIREMENT {if {$::scenario eq "unbounded"} {return Inf}; if {$::scenario eq "wrong-bound"} {return 20.0}; return 10.0}
        SLACK {if {$::scenario eq "violated"} {return -0.001}; if {$::scenario eq "infinite-slack"} {return Inf}; return 0.5}
        ENDPOINT_PIN {return [string map {src_gray_ff_reg {dest_graysync_ff_reg[0]}} $object]}
        GENERIC {return ACTIVE_MACHINE=0}
        ASYNC_REG {return [expr {$::scenario ne "unsafe-target"}]}
    }
    error "Unexpected property $property"
}
'''
        for scenario in ('pass', 'missing-bit', 'false-path', 'unbounded', 'wrong-bound',
                         'violated', 'infinite-slack', 'unsafe-target'):
            with self.subTest(scenario=scenario):
                result = self.run_tcl(f'set scenario {scenario}\n' + stub +
                    f'if {{[catch {{source {{{ROOT / "scripts/check_video_bus_constraints.tcl"}}}}} error]}} {{puts stderr $error; exit 1}}\n')
                self.assertEqual(result.returncode, 0 if scenario == 'pass' else 1,
                                 result.stdout + result.stderr)
                if scenario == 'pass':
                    self.assertIn('24 effective Gray-pointer', result.stdout)

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
