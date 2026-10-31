# SPDX-License-Identifier: GPL-3.0-only
# The ECS machine has one timeline. A global BUFGCE can pause it while the
# memory adapter resolves a miss. All chipset and CPU state stays synchronous.
# No multicycle, false-path or asynchronous-clock exceptions are applied here.
set amiga_gate_source [get_pins -quiet -hierarchical -regexp \
    {.*amiga_machine/machine_clock_gate/(I|I0)$}]
set amiga_gate_output [get_pins -quiet -hierarchical -regexp \
    {.*amiga_machine/machine_clock_gate/O$}]
create_generated_clock -name amiga_chip -source $amiga_gate_source \
    -divide_by 1 $amiga_gate_output
