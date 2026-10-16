# SPDX-License-Identifier: GPL-3.0-only
# IIgs-only phase constraints. Called by the normal timing-gated exporter.
# Keep this separate from the shared exporter so other machine captures do
# not depend on IIgs-only constraint-query refinements.
set iigs_byte_tick [get_nets -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/byte_tick$}]
require_one_object "IIgs kept phase0 loader decode" $iigs_byte_tick
# CLK28 is a reserved interface input in this imported VGC revision. Its
# divider has a different phase from CLK14; no current logic consumes it.
# Fail closed if an imported update activates any sequential consumer,
# since that update must also introduce its real edge relationship in XDC.
set iigs_reserved_clk28_buffer [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk28_buffer_i$}]
require_one_object "IIgs reserved CLK28 buffer" $iigs_reserved_clk28_buffer
set iigs_reserved_clk28_output [get_pins -quiet -of_objects $iigs_reserved_clk28_buffer \
    -filter {REF_PIN_NAME == O}]
require_one_object "IIgs reserved CLK28 output" $iigs_reserved_clk28_output
set iigs_reserved_clk28_registers [all_registers -clock [get_clocks apple_iigs_clk28]]
if {[llength $iigs_reserved_clk28_registers] != 0} {
    error "IIgs reserved CLK28 acquired live clocked registers; review its true phase"
}
foreach cell [all_fanout -flat -only_cells -from $iigs_reserved_clk28_output] {
    if {[get_property IS_SEQUENTIAL $cell]} {
        error "IIgs reserved CLK28 acquired a sequential consumer: $cell"
    }
}
puts "APPLE_IIGS_RESERVED_CLK28_PASS sequential_consumers=0"
set iigs_config_registers [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/loader_i/config_bytes_reg(\[[0-9]+\])?(_rep[^/]*)?$}]
if {[llength $iigs_config_registers] == 0} {error "IIgs configuration register census changed"}
# Synthesis can recompute the phase0 decode inside the configuration CE LUT
# rather than route the kept byte_tick net into it. Prove the actual LUT cone
# instead: with either phase bit high, every possible value of all remaining
# sequential inputs must still yield CE=0. Unknown primitives remain unknown,
# which makes this proof fail conservatively if the cone structure changes.
proc iigs_phase_logic_node {pin} {
    global iigs_phase_logic_nodes
    if {[dict exists $iigs_phase_logic_nodes $pin]} {return [dict get $iigs_phase_logic_nodes $pin]}
    set nets [get_nets -quiet -segments -of_objects $pin]
    set node [dict create nets [lsort $nets] driver {} cell {} ref {}]
    if {[llength $nets] == 0} {
        dict set iigs_phase_logic_nodes $pin $node
        return $node
    }
    set drivers [get_pins -quiet -leaf -of_objects $nets -filter {DIRECTION == OUT}]
    if {[llength $drivers] == 1} {
        set driver [lindex $drivers 0]
        set cell [get_cells -quiet -of_objects $driver]
        dict set node driver $driver
        dict set node cell $cell
        dict set node ref [get_property REF_NAME $cell]
    }
    dict set iigs_phase_logic_nodes $pin $node
    return $node
}
proc iigs_phase_logic_inputs {cell width} {
    global iigs_phase_logic_inputs_cache
    set key [list $cell $width]
    if {[dict exists $iigs_phase_logic_inputs_cache $key]} {return [dict get $iigs_phase_logic_inputs_cache $key]}
    set inputs {}
    for {set bit 0} {$bit < $width} {incr bit} {
        lappend inputs [get_pins -quiet "$cell/I$bit"]
    }
    dict set iigs_phase_logic_inputs_cache $key $inputs
    return $inputs
}
proc iigs_phase_logic_direct_known {node} {
    global iigs_phase_logic_fixed_drivers
    return [expr {[dict exists $iigs_phase_logic_fixed_drivers [dict get $node driver]] ||
        [dict get $node ref] in {GND VCC} ||
        [regexp {.*machine_adapter/phase_reg\[([01])\]$} [dict get $node cell]]}]
}
proc iigs_phase_logic_rank {pin} {
    # Only choose evaluation order here; every unevaluated input remains X.
    # Direct phase/fixed-Q/constants precede shallow phase-only decode LUTs,
    # mixed decodes, then unrelated combinational CPU/cache cones.
    set node [iigs_phase_logic_node $pin]
    if {[iigs_phase_logic_direct_known $node]} {return 0}
    set ref [dict get $node ref]
    if {![regexp {^LUT([1-6])$} $ref -> width] && $ref ne "LUT6_2"} {
        return [expr {$ref in {MUXF7 MUXF8} ? 3 : 0}]
    }
    if {$ref eq "LUT6_2"} {set width [expr {[get_property REF_PIN_NAME [dict get $node driver]] eq "O5" ? 5 : 6}]}
    set known 0
    foreach input [iigs_phase_logic_inputs [dict get $node cell] $width] {
        if {[iigs_phase_logic_direct_known [iigs_phase_logic_node $input]]} {incr known}
    }
    if {$known == $width} {return 1}
    return [expr {$known != 0 ? 2 : 3}]
}
proc iigs_phase_lut_constant {table indices} {
    set result X
    foreach index $indices {
        set value [expr {($table >> $index) & 1}]
        if {$result eq "X"} {set result $value
        } elseif {$result != $value} {return X}
    }
    return $result
}
proc iigs_phase_logic_value {pin phase} {
    global iigs_phase_logic_cache iigs_phase_logic_fixed_drivers
    set node [iigs_phase_logic_node $pin]
    set key [list $phase [dict get $node nets] $iigs_phase_logic_fixed_drivers]
    if {[dict exists $iigs_phase_logic_cache $key]} {return [dict get $iigs_phase_logic_cache $key]}
    dict set iigs_phase_logic_cache $key X
    set driver [dict get $node driver]
    if {$driver eq ""} {return X}
    if {[dict exists $iigs_phase_logic_fixed_drivers $driver]} {
        set result [dict get $iigs_phase_logic_fixed_drivers $driver]
        dict set iigs_phase_logic_cache $key $result
        return $result
    }
    set cell [dict get $node cell]
    set ref [dict get $node ref]
    if {[regexp {.*machine_adapter/phase_reg\[([01])\]$} $cell -> bit]} {
        set result [expr {($phase >> $bit) & 1}]
    } elseif {$ref eq "GND"} {set result 0
    } elseif {$ref eq "VCC"} {set result 1
    } elseif {[regexp {^LUT([1-6])$} $ref -> width] || $ref eq "LUT6_2"} {
        if {$ref eq "LUT6_2"} {set width [expr {[get_property REF_PIN_NAME $driver] eq "O5" ? 5 : 6}]}
        set init [get_property INIT $cell]
        if {![regexp {^[0-9]+'h([0-9a-fA-F]+)$} $init -> hex]} {error "Unknown IIgs phase-proof LUT INIT: $cell $init"}
        set table [expr "0x$hex"]
        set indices {}
        for {set index 0} {$index < (1 << $width)} {incr index} {
            lappend indices $index
        }
        set result [iigs_phase_lut_constant $table $indices]
        if {$result eq "X"} {
            set inputs [iigs_phase_logic_inputs $cell $width]
            set order {}
            for {set bit 0} {$bit < $width} {incr bit} {
                lappend order [list [iigs_phase_logic_rank [lindex $inputs $bit]] $bit]
            }
            foreach ranked [lsort -integer -index 0 $order] {
                set bit [lindex $ranked 1]
                set known [iigs_phase_logic_value [lindex $inputs $bit] $phase]
                if {$known eq "X"} {continue}
                set remaining {}
                foreach index $indices {
                    if {$known == (($index >> $bit) & 1)} {lappend remaining $index}
                }
                set indices $remaining
                set result [iigs_phase_lut_constant $table $indices]
                if {$result ne "X"} {break}
            }
        }
    } elseif {$ref in {MUXF7 MUXF8}} {
        set select [iigs_phase_logic_value [get_pins -quiet "$cell/S"] $phase]
        if {$select in {0 1}} {
            set result [iigs_phase_logic_value [get_pins -quiet "$cell/I$select"] $phase]
        } else {
            set a [iigs_phase_logic_value [get_pins -quiet "$cell/I0"] $phase]
            set b [iigs_phase_logic_value [get_pins -quiet "$cell/I1"] $phase]
            set result [expr {$a eq $b ? $a : "X"}]
        }
    } else {set result X}
    dict set iigs_phase_logic_cache $key $result
    return $result
}
set iigs_phase_logic_cache [dict create]
set iigs_phase_logic_fixed_drivers [dict create]
set iigs_phase_logic_nodes [dict create]
set iigs_phase_logic_inputs_cache [dict create]
foreach pin [get_pins -of_objects $iigs_config_registers -filter {REF_PIN_NAME == CE}] {
    foreach phase {1 2 3} {
        if {[iigs_phase_logic_value $pin $phase] ne "0"} {
            error "IIgs configuration CE is not proven inactive in phase$phase: $pin"
        }
    }
}
set iigs_woz_registers [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_eject35_q_reg$}]
require_one_object "IIgs native eject phase1 latch" [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_eject35_q_reg$}]
foreach cell $iigs_woz_registers {
    set ce [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == CE}]
    set data [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == D}]
    foreach phase {0 2 3} {
        # Eject pulses have CE=1 and force D=0 outside phase1.
        if {[iigs_phase_logic_value $ce $phase] ne "0" &&
            [iigs_phase_logic_value $data $phase] ne "0"} {
            error "IIgs WOZ tuple is not proven inactive in phase$phase: $cell"
        }
    }
}
set iigs_phase0_registers [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/(native_(reset|cold)_q|woz_(sd_(addr|data|ack|write)|mount|readonly|size)_q|hdd_(sd_(addr|data|ack|write|error)|mount|readonly|size)_q)_reg(\[[0-9]+\])*(_rep[^/]*)?$}]
if {[llength $iigs_phase0_registers] < 27} {error "IIgs phase0 native publication register census changed"}
require_one_object "IIgs HDD ACK phase0 publication" [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/hdd_sd_ack_q_reg\[0\]$}]
require_one_object "IIgs HDD result phase0 publication" [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/hdd_sd_error_q_reg\[0\]$}]
require_one_object "IIgs native reset publication" [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_reset_q_reg$}]
require_one_object "IIgs native cold-reset publication" [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_cold_q_reg$}]
foreach pin [get_pins -of_objects $iigs_phase0_registers -filter {REF_PIN_NAME == CE}] {
    foreach phase {1 2 3} {
        if {[iigs_phase_logic_value $pin $phase] ne "0"} {
            error "IIgs native publication CE is not proven inactive in phase$phase: $pin"
        }
    }
}
set iigs_native_stall_register [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_stall_q_reg$}]
require_one_object "IIgs phase2 native stall publication" $iigs_native_stall_register
set iigs_native_stall_ce [get_pins -quiet -of_objects $iigs_native_stall_register -filter {REF_PIN_NAME == CE}]
set iigs_native_stall_endpoint [get_pins -quiet -of_objects $iigs_native_stall_register -filter {REF_PIN_NAME == D}]
# Vivado may implement the publication enable as a D-input feedback mux
# with CE tied high. Prove the actual hold function, not just a CE spelling.
# Partition only the exact global-reset net and this register's own Q;
# every other sequential input retains its independent unknown value.
set iigs_native_reset_register [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_reset_q_reg$}]
set iigs_global_reset_pin [get_pins -quiet -of_objects $iigs_native_reset_register -filter {REF_PIN_NAME == PRE}]
require_one_object "IIgs native global reset assertion pin" $iigs_global_reset_pin
set iigs_global_reset_driver [get_pins -quiet -leaf \
    -of_objects [get_nets -quiet -segments -of_objects $iigs_global_reset_pin] -filter {DIRECTION == OUT}]
require_one_object "IIgs native global reset driver" $iigs_global_reset_driver
set iigs_native_stall_q [get_pins -quiet -of_objects $iigs_native_stall_register -filter {REF_PIN_NAME == Q}]
set iigs_native_stall_r [get_pins -quiet -of_objects $iigs_native_stall_register -filter {REF_PIN_NAME == R}]
set iigs_native_stall_s [get_pins -quiet -of_objects $iigs_native_stall_register -filter {REF_PIN_NAME == S}]
require_one_object "IIgs native stall feedback Q" $iigs_native_stall_q
set iigs_native_stall_ref [get_property REF_NAME $iigs_native_stall_register]
if {$iigs_native_stall_ref ni {FDRE FDSE}} {
    error "IIgs native stall publication primitive changed"
}
if {$iigs_native_stall_ref eq "FDRE"} {
    require_one_object "IIgs native stall synchronous clear pin" $iigs_native_stall_r
} else {
    require_one_object "IIgs native stall synchronous set pin" $iigs_native_stall_s
}
foreach reset_value {0 1} {
    foreach old_value {0 1} {
        set iigs_phase_logic_fixed_drivers [dict create \
            $iigs_global_reset_driver $reset_value $iigs_native_stall_q $old_value]
        foreach phase {0 1 2 3} {
            if {$iigs_native_stall_ref eq "FDRE" && [iigs_phase_logic_value $iigs_native_stall_r $phase] ne "0"} {
                error "IIgs native stall reset pin can clear the published hold"
            }
            set forced_set 0
            if {$iigs_native_stall_ref eq "FDSE"} {
                set forced_set [iigs_phase_logic_value $iigs_native_stall_s $phase]
                if {$reset_value==0 && $forced_set ne "0"} {
                    error "IIgs native stall set pin can override its phase publication"
                }
            }
            set ce [iigs_phase_logic_value $iigs_native_stall_ce $phase]
            set data [iigs_phase_logic_value $iigs_native_stall_endpoint $phase]
            if {$reset_value == 1} {
                if {$forced_set ne "1" && ($ce ne "1" || $data ne "1")} {
                    error "IIgs global reset does not unconditionally assert native stall"
                }
            } elseif {$phase != 2 && $ce ne "0" && $data ne $old_value} {
                error "IIgs native stall is not proven held in phase$phase"
            }
        }
    }
}
set iigs_phase_logic_fixed_drivers [dict create]
# The same phase evaluator proves the actual mapped CE cone and counter.
foreach phase {0 1 2 3} {
    if {[iigs_phase_logic_value $iigs_gate_ce $phase] ne [expr {$phase==3 ? "1" : "0"}]} {
        error "IIgs native SYNC CE is not exactly phase3: phase$phase"
    }
}
foreach bit {0 1} {
    set cell [get_cells -hierarchical -quiet -regexp \
        [format {.*g_apple_iigs_machine[/.]machine_adapter/phase_reg\[%s\]$} $bit]]
    require_one_object "IIgs phase$bit state" $cell
    if {[get_property REF_NAME $cell] ne "FDRE"} {error "IIgs phase counter primitive changed"}
    foreach role {C R CE D} {
        set pin [get_pins -quiet -of_objects $cell -filter "REF_PIN_NAME == $role"]
        require_one_object "IIgs phase$bit $role pin" $pin
        set phase_pin($role) $pin
    }
    set clock_pin $phase_pin(C)
    if {[get_clocks -quiet -of_objects $clock_pin] ne $iigs_gate_parent || [get_property IS_INVERTED $clock_pin]!=0} {
        error "IIgs phase counter must use positive primary edges"
    }
    foreach reset_value {0 1} {
        set iigs_phase_logic_fixed_drivers [dict create $iigs_global_reset_driver $reset_value]
        foreach phase {0 1 2 3} {
            set r [iigs_phase_logic_value $phase_pin(R) $phase]
            set ce [iigs_phase_logic_value $phase_pin(CE) $phase]
            set d [iigs_phase_logic_value $phase_pin(D) $phase]
            set expected [expr {$reset_value ? 0 : ((($phase+1)&3)>>$bit)&1}]
            set actual X
            if {$r eq "1"} {set actual 0
            } elseif {$r eq "0" && $ce eq "0"} {set actual [expr {($phase>>$bit)&1}]
            } elseif {$r eq "0" && $ce eq "1"} {set actual $d}
            if {$actual ne "$expected"} {error "IIgs phase counter modulo4/reset proof failed bit$bit phase$phase reset$reset_value"}
        }
    }
}
set iigs_phase_logic_fixed_drivers [dict create]
set iigs_native_clock_pins [get_pins -quiet -leaf \
    -of_objects [get_nets -quiet -segments -of_objects $iigs_gate_output] -filter {DIRECTION == IN}]
if {[llength $iigs_native_clock_pins]<1000} {error "IIgs native consumer census changed"}
foreach pin $iigs_native_clock_pins {
    set cell [get_cells -quiet -of_objects $pin]
    set ref [get_property REF_NAME $cell]
    if {![get_property IS_CLOCK $pin] || [get_property IS_INVERTED $pin]!=0 ||
        [get_clocks -quiet -of_objects $pin] ne $iigs_native_clock ||
        $ref ni {FDRE FDSE FDCE FDPE RAMB36E1 RAMB18E1 DSP48E1 RAMS64E RAMD64E RAMS32 RAMD32}} {
        error "IIgs native clock has an unreviewed negative-edge, nonclock or primitive consumer: $pin $ref"
    }
}
foreach clock [get_clocks -quiet -filter {IS_GENERATED == 1}] {
    if {[get_property MASTER_CLOCK $clock] eq $iigs_native_clock} {
        error "IIgs native pulse clock acquired an unreviewed derived clock: $clock"
    }
}
puts "APPLE_IIGS_NATIVE_GATE_LOGIC_PASS exact_phase3=1 modulo4_reset=1 positive_clock_pins=[llength $iigs_native_clock_pins] nonclock_consumers=0 derived_clocks=0"

set iigs_native_bridge_registers {}
foreach {name launch_phase} {native_prepare_q 1 native_prepared 0 native_commit_q 2} {
    set cell [get_cells -hierarchical -quiet -regexp \
        [format {.*g_apple_iigs_machine[/.]machine_adapter/%s_reg$} $name]]
    require_one_object "IIgs $name publication" $cell
    if {[get_property REF_NAME $cell] ne "FDRE"} {
        error "IIgs $name requires a clear-only publication primitive"
    }
    lappend iigs_native_bridge_registers $cell
    set q [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == Q}]
    set d [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == D}]
    set ce [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == CE}]
    require_one_object "IIgs $name data" $d
    require_one_object "IIgs $name feedback" $q
    require_one_object "IIgs $name enable" $ce
    # Clearing an obsolete request is permitted at any edge. A new asserted
    # publication must originate only at its reviewed phase, irrespective of
    # all other independent state inputs. Prove the actual synthesized cone.
    set iigs_phase_logic_fixed_drivers [dict create $q 0]
    foreach phase {0 1 2 3} {
        if {$phase!=$launch_phase && [iigs_phase_logic_value $ce $phase] ne "0" &&
            [iigs_phase_logic_value $d $phase] ne "0"} {
            error "IIgs $name can assert outside phase$launch_phase: phase$phase"
        }
    }
    if {$name eq "native_commit_q"} {
        # The primary phase3 edge creates the native rising edge. An armed
        # commit must remain high across that parent edge and BUFG delay.
        # Global/native reset is inactive in this partition; both independently
        # inhibit native effects when active. Do not waive any timing paths.
        set native_reset_q [get_pins -quiet -of_objects $iigs_native_reset_register -filter {REF_PIN_NAME == Q}]
        require_one_object "IIgs native reset level" $native_reset_q
        set iigs_phase_logic_fixed_drivers [dict create $q 1 \
            $iigs_global_reset_driver 0 $native_reset_q 0]
        if {[get_property REF_NAME $cell] ne "FDRE"} {
            error "IIgs native commit publication primitive changed"
        }
        set clear [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == R}]
        require_one_object "IIgs native commit clear" $clear
        if {[iigs_phase_logic_value $clear 3] ne "0" ||
            ([iigs_phase_logic_value $ce 3] ne "0" && [iigs_phase_logic_value $d 3] ne "1")} {
            error "IIgs native commit is not held across the phase3 native edge"
        }
    }
}
set iigs_phase_logic_fixed_drivers [dict create]
set iigs_native_track_ram [get_cells -hierarchical -quiet -filter \
    {NAME =~ *machine_adapter/g_woz*.floppy_i/*track_ram*/mem_reg* && REF_NAME =~ RAMB*}]
if {[llength $iigs_native_track_ram] < 32} {error "IIgs native WOZ track RAM census changed"}
foreach cell $iigs_native_track_ram {
    foreach pin [get_pins -quiet -of_objects $cell -filter \
        {REF_PIN_NAME == CLKARDCLK || REF_PIN_NAME == CLKBWRCLK}] {
        set clocks [get_clocks -quiet -of_objects $pin]
        if {[llength $clocks] != 1 || $clocks ne "apple_iigs_clk14"} {
            error "IIgs WOZ track RAM must retain native CLK_14M latency: $pin ($clocks)"
        }
    }
}
set iigs_woz_endpoints [get_pins -of_objects $iigs_woz_registers -filter {REF_PIN_NAME == D}]
set iigs_config_sources [get_pins -of_objects $iigs_config_registers -filter {REF_PIN_NAME == C}]
set iigs_publication_sources [get_pins -of_objects $iigs_phase0_registers -filter {REF_PIN_NAME == C}]
foreach {description from to periods} [list \
    WOZ [get_clocks apple_iigs_clk14] $iigs_woz_endpoints 2 \
    native_stall [get_clocks apple_iigs_clk14] $iigs_native_stall_endpoint 3 \
    configuration $iigs_config_sources [get_clocks apple_iigs_clk14] 3 \
    publication $iigs_publication_sources [get_clocks apple_iigs_clk14] 3] {
    set paths [get_timing_paths -quiet -from $from -to $to -delay_type max -max_paths 1]
    require_one_object "IIgs effective $description setup path" $paths
    if {abs([get_property REQUIREMENT $paths] - $periods * [get_property PERIOD $primary]) > 0.004} {
        error "IIgs $description setup constraint lost the reviewed phase requirement"
    }
    set holds [get_timing_paths -quiet -from $from -to $to -delay_type min -max_paths 1]
    require_one_object "IIgs effective $description hold path" $holds
    if {abs([get_property REQUIREMENT $holds]) > 0.004} {
        error "IIgs $description multicycle constraint changed normal hold timing"
    }
}
puts "APPLE_IIGS_AUX_PHASE_GATE_PASS config_registers=[llength $iigs_config_registers] eject_registers=[llength $iigs_woz_registers] publication_registers=[llength $iigs_phase0_registers] native_stall_registers=[llength $iigs_native_stall_register] native_track_ram=[llength $iigs_native_track_ram]"
puts "APPLE_IIGS_NATIVE_BRIDGE_PHASE_PASS registers=[llength $iigs_native_bridge_registers] prepare=1 prepared=0 commit=2"

# Dedicated SYNC CE setup/hold arcs must be present and normally timed.
# Deassertion launches on the passed primary edge; this hold check prevents
# a faster phase-to-CE branch from suppressing that native edge.
foreach delay_type {max min} {
    set path [get_timing_paths -quiet -from $iigs_gate_parent -to $iigs_gate_ce \
        -delay_type $delay_type -max_paths 1]
    require_one_object "IIgs native SYNC CE $delay_type timing arc" $path
    set expected [expr {$delay_type eq "max" ? $iigs_primary_period : 0.0}]
    if {abs([get_property REQUIREMENT $path]-$expected)>0.004 || [get_property SLACK $path]<0} {
        error "IIgs native SYNC CE $delay_type setup/hold is absent, excepted or failing"
    }
    report_timing -from $iigs_gate_parent -to $iigs_gate_ce -delay_type $delay_type \
        -max_paths 10 -file [file join $report_dir apple_iigs_native_ce_${delay_type}.rpt]
    puts "APPLE_IIGS_NATIVE_CE_TIMING_PASS type=$delay_type requirement=[get_property REQUIREMENT $path] slack=[get_property SLACK $path]"
}
