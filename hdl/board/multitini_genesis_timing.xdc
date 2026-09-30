# SPDX-License-Identifier: GPL-3.0-only
# Genesis-only generated clock. The chip BUFGCE retains the parent frequency;
# only the continuously running audio conditioner divides that clock by two.
# Both remain related and fully timed, including chip-data to audio paths.
set genesis_audio_divider_c [get_pins -hierarchical -quiet \
    -filter {NAME =~ *g_genesis_machine/machine_adapter/machine_i/audio_divider_reg/C}]
set genesis_audio_divider_q [get_pins -hierarchical -quiet \
    -filter {NAME =~ *g_genesis_machine/machine_adapter/machine_i/audio_divider_reg/Q}]
create_generated_clock -name genesis_audio_clk \
    -source $genesis_audio_divider_c -divide_by 2 $genesis_audio_divider_q
