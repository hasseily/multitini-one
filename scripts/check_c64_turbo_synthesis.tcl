# SPDX-License-Identifier: GPL-3.0-only
# Fail-closed gates for the CPU-island storage and related clock paths.
# Source after opening the synthesized/routed design. The exporter owns the
# reviewed MMCM plan and image-wide timing gates; these procedures do not
# replace either of them or establish physical board compatibility.

proc c64_turbo_require {condition message} {
    if {![uplevel 1 [list expr $condition]]} {error "C64 turbo: $message"}
}

proc c64_turbo_pin_inverted {cell port} {
    set value [get_property IS_${port}_INVERTED $cell]
    # Vivado represents the default false property as an empty value and the
    # true primitive configuration as 1'b1, depending on netlist stage.
    return [expr {$value in {1 true TRUE 1'b1}}]
}

proc check_c64_turbo_memory {stage {strict_rom 1} {island_cycle 1}} {
    set storage [get_cells -quiet -hier -filter {NAME =~ *main_ram_i/*}]
    set blocks [filter $storage {REF_NAME =~ RAMB*}]
    set distributed [filter $storage {REF_NAME =~ RAMD* || REF_NAME =~ RAMS* || REF_NAME =~ RAM32* || REF_NAME =~ RAM64* || REF_NAME =~ RAM128* || REF_NAME =~ RAM256*}]
    set main [filter $blocks {NAME =~ *main_low* || NAME =~ *main_high*}]
    set rom [filter $blocks {NAME =~ *basic_kernal* || NAME =~ *character*}]
    set colour [filter $blocks {NAME =~ *colour*}]
    c64_turbo_require {[llength $main]==16 && [llength [filter $main {REF_NAME =~ RAMB36*}]]==16} "$stage main RAM must use exactly16 RAMB36"
    c64_turbo_require {[llength $distributed]==0} "$stage island memory contains distributed RAM"
    if {$strict_rom} {
        c64_turbo_require {[llength $rom]==5 && [llength [filter $rom {REF_NAME =~ RAMB36*}]]==5} "$stage live ROM copies must use exactly5 RAMB36"
        c64_turbo_require {[llength $colour]==1 && [llength [filter $colour {REF_NAME =~ RAMB18*}]]==1} "$stage colour RAM must use one RAMB18"
        c64_turbo_require {[llength $blocks]==22} "$stage unexpected island memory block count"
    }
    foreach cell $blocks {
        foreach port {A B} {
            c64_turbo_require {[get_property DO${port}_REG $cell]==0} "$stage $cell port$port added an output register"
            set mode [get_property WRITE_MODE_$port $cell]
            if {$mode eq "READ_FIRST"} {
                c64_turbo_require {[get_property RDADDR_COLLISION_HWCONFIG $cell] eq "DELAYED_WRITE"} "$stage $cell READ_FIRST lacks DELAYED_WRITE"
            }
        }
        if {[lsearch -exact $main $cell]>=0} {
            foreach port {A B} {
                c64_turbo_require {[get_property RAM_EXTENSION_$port $cell] eq "NONE"} "$stage $cell has a main-memory cascade"
            }
        }
    }
    puts "C64_ISLAND_MEMORY_PASS stage=$stage main36=[llength $main] rom36=[llength $rom] colour18=[llength $colour] lutram=0"
}

proc check_c64_turbo_timing {stage cpu_clock native_clock {minimum_slack 0.100} {island_cycle 1} {duty_user_minimum 0.200}} {
    set fast [get_clocks -quiet $cpu_clock]
    set native [get_clocks -quiet $native_clock]
    c64_turbo_require {[llength $fast]==1 && [llength $native]==1} "$stage missing or ambiguous clock pair"
    set fp [get_property PERIOD $fast]
    set np [get_property PERIOD $native]
    c64_turbo_require {round(1000*$np)==2*round(1000*$fp)} "$stage native/CPU clock periods are not exactly2:1 at1ps"
    set master [get_property MASTER_CLOCK $fast]
    c64_turbo_require {$master ne "" && $master eq [get_property MASTER_CLOCK $native]} "$stage clocks do not share a generated-clock master"
    set blocks [get_cells -quiet -hier -filter {NAME =~ *main_ram_i/* && REF_NAME =~ RAMB*}]
    set fast_address_pins {}
    set fast_ports 0
    foreach cell $blocks {
        foreach tuple {{CLKARDCLK ADDRARDADDR A} {CLKBWRCLK ADDRBWRADDR B}} {
            lassign $tuple clock_pin address_pin port
            set clocks [get_clocks -quiet -of_objects [get_pins $cell/$clock_pin]]
            if {[lsearch -exact $clocks $fast]>=0} {
                incr fast_ports
                c64_turbo_require {[c64_turbo_pin_inverted $cell $clock_pin]==($island_cycle==1)} "$stage $cell CPU memory clock has wrong polarity"
                set fast_address_pins [concat $fast_address_pins [get_pins -quiet $cell/${address_pin}*]]
            } elseif {[lsearch -exact $clocks $native]>=0} {
                c64_turbo_require {![c64_turbo_pin_inverted $cell $clock_pin]} "$stage $cell native memory clock is inverted"
            } else {error "C64 turbo: $stage $cell/$clock_pin is outside the related clock pair ($clocks)"}
        }
    }
    c64_turbo_require {$fast_ports>=22} "$stage not all island RAM CPU ports retain a timed clock"
    set forbidden [get_timing_paths -quiet -rise_from $native -fall_to $fast -max_paths 1]
    c64_turbo_require {[llength $forbidden]==0} "$stage a native-rise to CPU-fall data path exists"
    foreach pair [list [list $fast $fast] [list $native $fast] [list $fast $native]] {
        lassign $pair source destination
        set path [get_timing_paths -from $source -to $destination -max_paths 1]
        c64_turbo_require {[llength $path]==1} "$stage missing timed path $pair"
        set slack [get_property SLACK $path]
        c64_turbo_require {$slack>=$minimum_slack} "$stage island setup slack $slack below $minimum_slack on $pair"
        if {$source ne $destination} {
            set requirement [get_property REQUIREMENT $path]
            c64_turbo_require {abs($requirement-$fp)<0.001} "$stage cross-clock requirement $requirement is not one fast period $fp"
        }
        puts "C64_ISLAND_TIMING stage=$stage from=$source to=$destination slack_ns=$slack requirement_ns=[get_property REQUIREMENT $path]"
    }
    if {$island_cycle==1} {
        set half [get_timing_paths -from $fast -to $fast_address_pins -max_paths 1]
        c64_turbo_require {[llength $half]==1} "$stage missing CPU-to-BRAM half-period path"
        set requirement [get_property REQUIREMENT $half]
        c64_turbo_require {abs($requirement-$fp/2.0)<0.002} "$stage CPU memory path lost its half-period requirement"
        set uncertainty [get_property UNCERTAINTY $half]
        set user_uncertainty [get_property USER_UNCERTAINTY $half]
        c64_turbo_require {$user_uncertainty>=$duty_user_minimum-0.0001} "$stage memory probe lost the explicit duty uncertainty term"
        puts "C64_ISLAND_HALF_PERIOD_PASS stage=$stage requirement_ns=$requirement reported_uncertainty_ns=$uncertainty user_uncertainty_ns=$user_uncertainty"
    }
    puts "C64_CLOCK_PAIR_PASS stage=$stage fast=$fast native=$native period_ps=[expr {round($fp*1000)}]"
}
