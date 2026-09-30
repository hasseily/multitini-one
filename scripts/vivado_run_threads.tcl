# SPDX-License-Identifier: GPL-3.0-only
# Adapted from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0.
# Sourced in the parent and each child run; -jobs does not set worker threads.
set_param general.maxThreads 8
if {[get_param general.maxThreads] != 8} {
    error "Vivado did not accept eight worker threads."
}
puts "Vivado worker threads: [get_param general.maxThreads]"
