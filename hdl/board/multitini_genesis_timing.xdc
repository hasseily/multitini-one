# SPDX-License-Identifier: GPL-3.0-only
# Related /2 audio clock; accept elaborated slash and synthesized dot generate hierarchy.
set genesis_audio_divider_c [get_pins -hierarchical -quiet -regexp \
    {.*g_genesis_machine[/.]machine_adapter/machine_i/audio_divider_reg/C$}]
set genesis_audio_divider_q [get_pins -hierarchical -quiet -regexp \
    {.*g_genesis_machine[/.]machine_adapter/machine_i/audio_divider_reg/Q$}]
create_generated_clock -name genesis_audio_clk \
    -source $genesis_audio_divider_c -divide_by 2 $genesis_audio_divider_q
