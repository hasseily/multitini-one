#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the real Tcl policy and clock gate at both threshold boundaries."""
import json
import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
TCL = shutil.which('tclsh')

STUB = r'''
proc get_clocks {args} {
    if {[lsearch -exact $args -of_objects]>=0} {
        if {[string match */CLKARDCLK [lindex $args end]]} {return fast}
        return native
    }
    return [lindex $args end]
}
proc get_cells {args} {
    set result {}; for {set i 0} {$i<22} {incr i} {lappend result ram$i}; return $result
}
proc get_pins {args} {return [lindex $args end]}
proc get_timing_paths {args} {
    if {[lsearch -exact $args -rise_from]>=0} {return {}}
    set dest [lindex $args [expr {[lsearch -exact $args -to]+1}]]
    if {[string match *ADDR* $dest]} {return half}
    set src [lindex $args [expr {[lsearch -exact $args -from]+1}]]
    return ${src}_${dest}
}
proc get_property {key object} {
    switch -- $key {
        PERIOD {if {$object eq "fast"} {return 10.0}; return 20.0}
        MASTER_CLOCK {return parent}
        IS_CLKARDCLK_INVERTED {return 1}
        IS_CLKBWRCLK_INVERTED {return 0}
        SLACK {return $::slack}
        REQUIREMENT {if {$object eq "half"} {return 5.0}; return 10.0}
        UNCERTAINTY {return 0.4}
        USER_UNCERTAINTY {return $::duty}
    }
    error "Unexpected property $key"
}
'''


@unittest.skipUnless(TCL, 'native Tcl interpreter unavailable')
class C64TimingPolicyTests(unittest.TestCase):
    def execute(self, body):
        prefix = (f'source {{{ROOT / "scripts/c64_timing_policy.tcl"}}}\n'
                  f'source {{{ROOT / "scripts/check_c64_turbo_synthesis.tcl"}}}\n')
        return subprocess.run([TCL], input=prefix + STUB +
                              f'if {{[catch {{\n{body}\n}} failure]}} {{puts stderr $failure; exit 1}}\n',
                              text=True, capture_output=True)

    def test_named_exception_is_build_scoped_and_strict(self):
        cases = [('default', '4D00C708', 'C64', 0.100, True),
                 ('default', '4D00C708', 'C64', 0.099, False),
                 ('default', '4D00C709', 'C64', 0.100, True),
                 ('dev8-over-0.050', '4D00C708', 'C64', 0.051, True),
                 ('dev8-over-0.050', '4D00C708', 'C64', 0.050, False),
                 ('dev8-over-0.050', '4D00C708', 'C64', 0.049, False),
                 ('dev8-over-0.050', '4D00C709', 'C64', 0.120, False),
                 ('dev8-over-0.050', '4D00C708', 'NES', 0.120, False),
                 ('0.050', '4D00C708', 'C64', 0.120, False)]
        for policy, build_id, machine, slack, passes in cases:
            with self.subTest(policy=policy, build=build_id, machine=machine, slack=slack):
                result = self.execute(f'''
set slack {slack}; set duty 0.200
set policy [c64_resolve_timing_policy {machine} {{ACTIVE_MACHINE=0 SHELL_BUILD_ID=32'h{build_id}}} {policy}]
check_c64_turbo_timing test fast native [dict get $policy minimum_setup_ns] 1 0.200 [dict get $policy minimum_exclusive]
''')
                self.assertEqual(result.returncode, 0 if passes else 1, result.stdout + result.stderr)

    def test_default_argument_and_duty_gate_remain(self):
        for slack, duty, passes in ((0.100, 0.200, True), (0.051, 0.200, False),
                                    (0.150, 0.199, False)):
            with self.subTest(slack=slack, duty=duty):
                result = self.execute(f'set slack {slack}; set duty {duty}\ncheck_c64_turbo_timing test fast native')
                self.assertEqual(result.returncode, 0 if passes else 1, result.stdout + result.stderr)

    def test_ambiguous_or_malformed_build_identity_rejected(self):
        for identity in ('', "XSHELL_BUILD_ID=32'h4D00C708", "SHELL_BUILD_ID=32'h4D00C708garbage",
                         "SHELL_BUILD_ID=32'h4D00C708 SHELL_BUILD_ID=32'h4D00C708"):
            with self.subTest(identity=identity):
                result = self.execute(f'c64_resolve_timing_policy C64 {{{identity}}} dev8-over-0.050')
                self.assertNotEqual(result.returncode, 0)

    def test_receipt_binds_all_three_path_groups(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='timing-policy-test-') as folder:
            path = Path(folder) / 'policy.json'
            result = self.execute(f'''
set slack 0.055; set duty 0.200
set policy [c64_resolve_timing_policy C64 {{SHELL_BUILD_ID=32'h4D00C708}} dev8-over-0.050]
set paths [check_c64_turbo_timing test fast native 0.050 1 0.200 true]
c64_write_timing_policy {{{path}}} $policy $paths
''')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            record = json.loads(path.read_text())
            self.assertEqual(record['policy'], 'dev8-over-0.050')
            self.assertEqual(record['build_id'], '4D00C708')
            self.assertEqual(record['minimum_setup_ns'], 0.050)
            self.assertIs(record['minimum_exclusive'], True)
            self.assertEqual(record['duty_user_minimum_ns'], 0.200)
            self.assertEqual([(p['from'], p['to']) for p in record['setup_paths']],
                             [('fast', 'fast'), ('native', 'fast'), ('fast', 'native')])
            self.assertEqual([p['slack_ns'] for p in record['setup_paths']], [0.055] * 3)

    def test_initial_capture_records_explicit_policy_and_variant(self):
        spec = importlib.util.spec_from_file_location(
            'timing_policy_capture', ROOT / 'scripts/capture_vectrex_build_inputs.py')
        capture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(capture)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='timing-policy-capture-') as folder:
            for standard in ('PAL', 'NTSC'):
                output = Path(folder) / (standard + '.json')
                args = ['capture', '--machine', 'C64', '--c64-video-standard', standard,
                        '--c64-timing-policy', 'dev8-over-0.050', '--output', str(output)]
                with mock.patch('sys.argv', args), mock.patch.object(capture, 'inputs', return_value=[]), \
                        mock.patch.object(capture, 'archive_inputs'), \
                        mock.patch.object(capture.subprocess, 'check_output', return_value='frozen-head\n'), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(capture.main(), 0)
                record = json.loads(output.read_text())
                self.assertEqual(record['configuration']['c64_video_standard'], standard)
                self.assertEqual(record['configuration']['c64_timing_policy'], 'dev8-over-0.050')
                self.assertEqual(record['git_head'], 'frozen-head')

    def test_cpu_pressure_restores_exact_constraints_even_on_error(self):
        stub = r'''
set rf 0.200; set fr 0.200; set other 0.0; set changed_clock 0; set calls 0
proc get_clocks {args} {
    if {![llength $args]} {return {clk_out2_zynq_ps_bd_clk_wiz_c64_0 clk_out1_zynq_ps_bd_clk_wiz_c64_0 clk_fpga_0 clk_out1_zynq_ps_bd_clk_wiz_video_0}}
    return [lindex $args end]
}
proc get_cells {args} {return clock_mmcm}
proc get_timing_paths {args} {
    set type [lindex $args [expr {[lsearch -exact $args -delay_type]+1}]]
    foreach edge {rise fall} {
        if {[lsearch -exact $args -${edge}_from]>=0} {set src $edge}
        if {[lsearch -exact $args -${edge}_to]>=0} {set dst $edge}
    }
    if {[info exists src]} {return $type/$src/$dst}
    return $type/other
}
proc get_property {key object} {
    if {$key eq "USER_UNCERTAINTY"} {
        if {$object eq "max/rise/fall"} {return $::rf}
        if {$object eq "max/fall/rise"} {return $::fr}
        return $::other
    }
    if {$key eq "SLACK"} {return 0.060}
    return identity-$::changed_clock
}
proc set_clock_uncertainty {args} {
    if {[lindex $args 0] ne "-setup" || [llength $args]!=6} {error "Unexpected clock constraint mutation"}
    set value [lindex $args 1]
    if {$value ni {0.200 0.300}} {error "Unexpected setup pressure"}
    if {[lindex $args 2] eq "-rise_from" && [lindex $args 4] eq "-fall_to"} {set ::rf $value
    } elseif {[lindex $args 2] eq "-fall_from" && [lindex $args 4] eq "-rise_to"} {set ::fr $value
    } else {error "Unexpected setup edge pair"}
}
proc phys_opt_design {args} {
    if {$args ne "-directive AggressiveExplore" || $::rf!=0.300 || $::fr!=0.300} {error "Wrong physical optimization target"}
    incr ::calls
    switch -- $::scenario {
        optimizer-error {error "synthetic optimizer failure"}
        clock-drift {set ::changed_clock 1}
        hold-drift {set ::other 0.125}
    }
}
'''
        for scenario in ('pass', 'optimizer-error', 'clock-drift', 'hold-drift'):
            with self.subTest(scenario=scenario):
                script = (f'set scenario {scenario}\n' + stub +
                          'set failed [catch {source {' +
                          (ROOT / 'scripts/optimize_c64_cpu_margin.tcl').as_posix() +
                          '}} message]\n' + '''
if {$rf!=0.200 || $fr!=0.200 || $calls!=1} {puts stderr "Restoration or bounded-pass count failed"; exit 1}
if {$failed!=[expr {$scenario ne "pass"}]} {puts stderr "Wrong failure result: $message"; exit 2}
puts "PASS restored constraints and correct failure status"
''')
                result = subprocess.run([TCL], input=script, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('PASS restored constraints', result.stdout)


if __name__ == '__main__':
    unittest.main()
