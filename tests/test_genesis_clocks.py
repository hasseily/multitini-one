#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed tests for Genesis generated-clock constraints and signoff."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
XDC = (ROOT / 'hdl/board/multitini_genesis_timing.xdc').read_text()
EXPORTER = (ROOT / 'scripts/build_and_export_xsa.tcl').read_text()
START = EXPORTER.index('if {$image_kind eq "GENESIS"} {')
END = EXPORTER.index('set psram_forwarded_clocks', START)
CLOCK_GATE = EXPORTER[START:END]
TCL = shutil.which('tclsh')

STUB = r'''
set image_kind GENESIS
set prefix g_genesis_machine.machine_adapter
if {[string match slash-* $scenario]} {set prefix g_genesis_machine/machine_adapter}
set ref BUFGCTRL
if {[string match *-bufgce $scenario]} {set ref BUFGCE}
if {$scenario eq "bad-buffer"} {set ref LUT1}
proc require_one_object {description objects} {
    if {[llength $objects] != 1} {error "Expected one $description: $objects"}
}
proc get_cells {args} {
    set cell $::prefix/chip_clock_i
    if {$::scenario eq "missing-buffer"} {return {}}
    if {![regexp [lindex $args end] $cell]} {return {}}
    if {$::scenario eq "duplicate-buffer"} {return [list $cell extra]}
    return $cell
}
proc get_pins {args} {
    if {[lsearch -exact $args -regexp] >= 0} {
        set pins [list $::prefix/machine_i/audio_divider_reg/C $::prefix/machine_i/audio_divider_reg/Q]
        if {$::scenario eq "missing-divider"} {return {}}
        set result {}
        foreach pin $pins {if {[regexp [lindex $args end] $pin]} {lappend result $pin}}
        if {$::scenario eq "duplicate-divider"} {lappend result extra}
        return $result
    }
    return [lindex $args end]
}
proc get_nets {args} {return [lindex $args end]}
proc get_clocks {args} {
    if {[lsearch -exact $args -of_objects] < 0} {
        if {$::scenario eq "missing-audio-clock"} {return {}}
        return genesis_audio_clk
    }
    set object [lindex $args end]
    if {[string match */audio_divider_reg/Q $object]} {
        if {$::scenario eq "wrong-audio-output"} {return wrong_clock}
        return genesis_audio_clk
    }
    if {$::scenario eq "missing-parent" && [string match */I0 $object]} {return {}}
    if {$::scenario eq "different-gated-clock" && [string match */chip_clock_i/O $object]} {return wrong_clock}
    if {$::scenario eq "wrong-audio-source" && [string match */audio_divider_reg/C $object]} {return wrong_clock}
    return parent_clock
}
proc get_property {property object} {
    switch -- $property {
        REF_NAME {return $::ref}
        IS_TIED {return [expr {$::scenario ne "untied-control"}]}
        TYPE {
            set pin [file tail $object]
            set type [expr {$pin in {I1 S0 IGNORE1} ? "POWER" : "GROUND"}]
            if {$::scenario eq "unsafe-$pin"} {return [expr {$type eq "POWER" ? "GROUND" : "POWER"}]}
            return $type
        }
        XILINX_LEGACY_PRIM {if {$::scenario eq "wrong-legacy"} {return BUFGMUX}; return BUFGCE}
        CE_TYPE_CE0 {if {$::scenario eq "async-enable"} {return ASYNC}; return SYNC}
        PRESELECT_I0 {return [expr {$::scenario ne "wrong-preselect0"}]}
        PRESELECT_I1 {return [expr {$::scenario eq "wrong-preselect1"}]}
        INIT_OUT {return [expr {$::scenario eq "wrong-initial-clock"}]}
        IS_GENERATED {return [expr {$::scenario ne "unrelated-clock"}]}
        IS_USER_GENERATED {return [expr {$::scenario ne "automatic-audio-clock"}]}
        IS_INVERTED {return [expr {$::scenario eq "inverted-audio-clock"}]}
        DIVIDE_BY {if {$::scenario eq "wrong-divide-ratio"} {return 3}; return 2}
        SOURCE {if {$::scenario eq "wrong-source-pin"} {return wrong_pin}; return $::prefix/machine_i/audio_divider_reg/C}
        SOURCE_PINS {if {$::scenario eq "wrong-output-pin"} {return wrong_pin}; return $::prefix/machine_i/audio_divider_reg/Q}
        MASTER_CLOCK {if {$::scenario eq "wrong-master"} {return wrong_clock}; return parent_clock}
        PERIOD {
            if {$::scenario in {rounded-ntsc rounded-negative rounded-invalid}} {
                if {$object eq "parent_clock"} {return 9.311}
                if {$::scenario eq "rounded-negative"} {return 18.621}
                if {$::scenario eq "rounded-invalid"} {return 18.624}
                return 18.623
            }
            if {$::scenario eq "rounded-pal"} {
                if {$object eq "parent_clock"} {return 9.397}
                return 18.794
            }
            if {$object eq "parent_clock"} {return 10.0}
            if {$::scenario eq "wrong-divider"} {return 10.0}
            return 20.0
        }
    }
    error "Unexpected property $property"
}
proc create_generated_clock {args} {
    if {[lindex $args 1] ne "genesis_audio_clk" || [lindex $args 5] != 2} {error "Wrong generated clock settings"}
    require_one_object "divider source" [lindex $args 3]
    require_one_object "divider output" [lindex $args 6]
    if {[lindex $args 3] ne "$::prefix/machine_i/audio_divider_reg/C" ||
        [lindex $args 6] ne "$::prefix/machine_i/audio_divider_reg/Q"} {error "Wrong divider pins"}
}
'''

@unittest.skipUnless(TCL, 'native Tcl interpreter unavailable')
class GenesisClockTests(unittest.TestCase):
    def run_case(self, scenario, script, passes):
        command = f'set scenario {scenario}\n' + STUB
        command += 'if {[catch {\n' + script + '\n} error]} {puts stderr $error; exit 1}\n'
        result = subprocess.run([TCL], input=command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0 if passes else 1, scenario + '\n' + result.stdout + result.stderr)

    def test_xdc_matches_exact_divider_in_both_hierarchies(self):
        for scenario in ('slash-mapped', 'dot-mapped', 'missing-divider', 'duplicate-divider'):
            with self.subTest(scenario=scenario):
                self.run_case(scenario, XDC, scenario in ('slash-mapped', 'dot-mapped'))

    def test_gate_rejects_unsafe_or_unconstrained_clocks(self):
        valid = ('slash-bufgce', 'dot-bufgce', 'slash-mapped', 'dot-mapped',
                 'rounded-ntsc', 'rounded-pal', 'rounded-negative')
        broken = ('bad-buffer', 'missing-buffer', 'duplicate-buffer', 'missing-divider',
                  'duplicate-divider', 'missing-audio-clock', 'wrong-audio-output',
                  'missing-parent', 'different-gated-clock', 'wrong-audio-source',
                  'untied-control', 'unsafe-I1', 'unsafe-CE1', 'unsafe-S0', 'unsafe-S1',
                  'unsafe-IGNORE0', 'unsafe-IGNORE1', 'wrong-legacy', 'async-enable',
                  'wrong-preselect0', 'wrong-preselect1', 'wrong-initial-clock',
                  'unrelated-clock', 'wrong-master', 'wrong-divider', 'rounded-invalid',
                  'automatic-audio-clock', 'inverted-audio-clock', 'wrong-divide-ratio',
                  'wrong-source-pin', 'wrong-output-pin')
        for scenario in valid + broken:
            with self.subTest(scenario=scenario):
                self.run_case(scenario, CLOCK_GATE, scenario in valid)

if __name__ == '__main__':
    unittest.main()
