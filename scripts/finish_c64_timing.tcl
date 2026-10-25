# SPDX-License-Identifier: GPL-3.0-only
# Restore the C64 placement margin, preserve the common video/fabric repair,
# then apply the measured CPU-only pass. Each helper restores its own
# temporary constraints before returning.
source [file join [file dirname [info script]] c64_placement_margin.tcl]
c64_placement_margin::restore
source [file join [file dirname [info script]] finish_video_timing.tcl]
source [file join [file dirname [info script]] optimize_c64_cpu_margin.tcl]
