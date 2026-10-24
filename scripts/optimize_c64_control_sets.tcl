# SPDX-License-Identifier: GPL-3.0-only
# C64-only post-opt hook, selected from ACTIVE_MACHINE by the shared profile.
# Merge compatible control sets after sequential area optimization so the
# complete eight-SID design can pack without changing its cycle semantics.
opt_design -control_set_merge
