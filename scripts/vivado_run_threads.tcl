# SPDX-License-Identifier: GPL-3.0-only
# Adapted from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0.
# Sourced in the parent and each child run; -jobs does not set worker threads.
set_param general.maxThreads 8
if {[get_param general.maxThreads] != 8} {
    error "Vivado did not accept eight worker threads."
}
puts "Vivado worker threads: [get_param general.maxThreads]"
if {[info exists ::env(MULTITINI_MACHINE)] &&
    [string toupper [string trim $::env(MULTITINI_MACHINE)]] eq "NES"} {
    # A native Windows parallel synthesis task stopped making CPU/file progress.
    # Serialize only synthesis; place and route retain eight worker threads.
    set_param synth.maxThreads 1
    if {[get_param synth.maxThreads] != 1} {
        error "Vivado did not accept serial NES synthesis."
    }
    puts "NES synthesis worker threads: [get_param synth.maxThreads]"
}
