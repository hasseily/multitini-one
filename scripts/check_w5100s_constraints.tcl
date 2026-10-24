# SPDX-License-Identifier: GPL-3.0-only
# Call after route_design. External paths are bounded, never false-pathed.
proc check_w5100s_constraints {} {
    set bridge [get_cells -quiet -hierarchical -filter {NAME =~ *w5100s_bridge_i}]
    if {![llength $bridge]} {return [dict create present false]}
    set launch [get_cells -hierarchical -filter {REF_NAME =~ FD* && (NAME =~ *w5100s*eth_a_reg* || NAME =~ *w5100s*eth_d_o_reg* || NAME =~ *w5100s*eth_d_t_reg* || NAME =~ *w5100s*eth_rd_n_reg* || NAME =~ *w5100s*eth_wr_n_reg* || NAME =~ *w5100s*eth_cs_n_reg* || NAME =~ *w5100s*eth_rst_n_reg*)}]
    set capture [get_cells -hierarchical -filter {REF_NAME =~ FD* && NAME =~ *w5100s*captured_data_reg*}]
    if {[llength $launch] != 22 || [llength $capture] != 8} {
        error "W5100S expected 22 output and 8 input IOB registers"
    }
    set out [get_ports {eth_a[*] eth_rd_n eth_wr_n eth_cs_n eth_rst_n eth_d[*]}]
    set in [get_ports {eth_d[*]}]
    set output_max 0.0
    set input_max 0.0
    foreach cell $launch {
        if {![string match OLOGIC_* [get_property LOC $cell]]} {
            error "W5100S launch register is not packed in OLOGIC: $cell"
        }
        set pin [get_pins -of_objects $cell -filter {REF_PIN_NAME == C}]
        set paths [get_timing_paths -quiet -from $pin -to $out -max_paths 1]
        if {[llength $paths] != 1} {error "W5100S output timing path absent: $cell"}
        set path [lindex $paths 0]
        set delay [get_property DATAPATH_DELAY $path]
        if {[get_property REQUIREMENT $path] != 4.000 || $delay > 4.000 || [get_property SLACK $path] < 0} {
            error "W5100S output does not meet the exact 4ns external bound: $cell"
        }
        if {$delay > $output_max} {set output_max $delay}
    }
    foreach cell $capture {
        if {![string match ILOGIC_* [get_property LOC $cell]]} {
            error "W5100S capture register is not packed in ILOGIC: $cell"
        }
        set pin [get_pins -of_objects $cell -filter {REF_PIN_NAME == D}]
        set paths [get_timing_paths -quiet -from $in -to $pin -max_paths 1]
        if {[llength $paths] != 1} {error "W5100S input timing path absent: $cell"}
        set path [lindex $paths 0]
        set delay [get_property DATAPATH_DELAY $path]
        if {[get_property REQUIREMENT $path] != 9.000 || $delay > 9.000 || [get_property SLACK $path] < 0} {
            error "W5100S input does not meet the exact 9ns external bound: $cell"
        }
        if {$delay > $input_max} {set input_max $delay}
    }
    set result [dict create present true output_paths 22 input_paths 8 output_max_ns $output_max input_max_ns $input_max]
    puts "W5100S external timing PASS: $result"
    return $result
}
