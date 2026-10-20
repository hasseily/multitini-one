# SPDX-License-Identifier: GPL-3.0-only
# The native peripheral clock is an integer division of the 57.272728 MHz machine
# clock. CLK28 is a reserved, unused VGC input in this imported revision.
# Its declaration records frequency only: its real rise is one primary
# period after CLK14. The GS helper rejects any sequential CLK28 consumer;
# activating it requires reviewing that phase before adding timing paths.
# Build export checks the exact hierarchy, ratios and zero-consumer gate.
set_property DONT_TOUCH TRUE [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk28_divider_reg$}]
set_property DONT_TOUCH TRUE [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk28_buffer_i$}]
set apple_iigs_clk28_source [get_pins -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk28_divider_reg/C$}]
set apple_iigs_clk28_output [get_pins -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk28_divider_reg/Q$}]
create_generated_clock -name apple_iigs_clk28 \
    -source $apple_iigs_clk28_source -divide_by 2 $apple_iigs_clk28_output
# SYNC BUFGCE passes the positive primary edge selected by phase3.
# The high pulse is one primary half-cycle, repeated every four periods.
set apple_iigs_clk14_source [get_pins -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk14_buffer_i/(I|I0)$}]
set apple_iigs_clk14_output [get_pins -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/clk14_buffer_i/O$}]
create_generated_clock -name apple_iigs_clk14 \
    -source $apple_iigs_clk14_source -edges {1 2 9} $apple_iigs_clk14_output

# The accelerated CPU and both cache levels run on the primary clock.
# Every CPU/cache address, tag, data and retirement path keeps ordinary
# single-cycle timing. Native-only phase exceptions below do not select it.

# The IWM, flux engines and WOZ buffers use the primary clock with explicit
# native/fast enables. Their RAM, CPU and eject paths keep ordinary one-cycle
# primary setup/hold timing in both modes. The exporter checks clock identity
# and Normal-mode eject sampling; regressions check native edge equivalence.
# No floppy multicycle or generated-clock exception is introduced for Fast.

# Configuration publishes only on loader phase0. The following native phase3
# edge is three primary clocks later. Select only those launch registers;
# primary-to-CPU paths retain single-cycle setup and normal hold.
set apple_iigs_config_registers [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/loader_i/config_bytes_reg(\[[0-9]+\])?(_rep[^/]*)?$}]
set_multicycle_path 3 -setup -start \
    -from [get_pins -of_objects $apple_iigs_config_registers -filter {REF_PIN_NAME == C}] \
    -to [get_clocks apple_iigs_clk14]
set_multicycle_path 2 -hold -start \
    -from [get_pins -of_objects $apple_iigs_config_registers -filter {REF_PIN_NAME == C}] \
    -to [get_clocks apple_iigs_clk14]

# Complete file-service tuples and native reset release publish on phase0.
# Native reset's global assertion uses its asynchronous PRE pin; the clocked
# release/local-update path is phase0-qualified. ACK/image fields are still
# timed through these exact publication registers, without clock-group cuts.
set apple_iigs_phase0_registers [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/(native_(reset|cold)_q|woz_(sd_(addr|data|ack|write)|mount|readonly|size)_q|hdd_(sd_(addr|data|ack|write|error)|mount|readonly|size)_q)_reg(\[[0-9]+\])*(_rep[^/]*)?$}]
set_multicycle_path 3 -setup -start \
    -from [get_pins -of_objects $apple_iigs_phase0_registers -filter {REF_PIN_NAME == C}] \
    -to [get_clocks apple_iigs_clk14]
set_multicycle_path 2 -hold -start \
    -from [get_pins -of_objects $apple_iigs_phase0_registers -filter {REF_PIN_NAME == C}] \
    -to [get_clocks apple_iigs_clk14]

# The entire native hold tuple is sampled only on phase2, three primary
# periods after native state changes on phase3. Its primary-to-native output
# keeps ordinary single-cycle timing and cannot change on native phase3.
set apple_iigs_native_stall_register [get_cells -hierarchical -quiet -regexp \
    {.*g_apple_iigs_machine[/.]machine_adapter/native_stall_q_reg$}]
set_multicycle_path 3 -setup -end -from [get_clocks apple_iigs_clk14] \
    -to [get_pins -of_objects $apple_iigs_native_stall_register -filter {REF_PIN_NAME == D}]
set_multicycle_path 2 -hold -end -from [get_clocks apple_iigs_clk14] \
    -to [get_pins -of_objects $apple_iigs_native_stall_register -filter {REF_PIN_NAME == D}]
