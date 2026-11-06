# SPDX-License-Identifier: GPL-3.0-only

set mode [lindex $argv 0]
if {$mode eq "checkpoint"} {
    open_checkpoint [lindex $argv 1]
    set adapter [get_cells -quiet -hierarchical -filter {NAME =~ *machine_adapter && REF_NAME =~ multitini_zx_spectrum_adapter*}]
    if {[llength $adapter] != 1} {
        error "Expected one Spectrum adapter, got $adapter"
    }
    write_verilog -force -mode funcsim -cell $adapter -rename_top multitini_zx_spectrum_adapter mapped_spectrum.v
} elseif {$mode eq "synth"} {
    foreach source [lrange $argv 1 end] {
        switch -- [file extension $source] {
            .vhd { read_vhdl -vhdl2008 $source }
            .sv { read_verilog -sv $source }
            default { error "Unknown source extension: $source" }
        }
    }
    synth_design -top multitini_zx_spectrum_adapter -part xc7z020clg484-2 -mode out_of_context
    opt_design
    write_verilog -force -mode funcsim mapped_spectrum.v
} else {
    error "Expected synth or checkpoint"
}
