# SPDX-License-Identifier: GPL-3.0-only
# A global BUFGCE can pause the chipset while the memory adapter resolves a
# miss. The AP68040 has a second gated clock from the same MMCM. Vivado
# propagates that MMCM clock through its gate for related-clock timing.
set amiga_gate_source [get_pins -quiet -hierarchical -regexp \
    {.*amiga_machine/machine_clock_gate/(I|I0)$}]
set amiga_gate_output [get_pins -quiet -hierarchical -regexp \
    {.*amiga_machine/machine_clock_gate/O$}]
create_generated_clock -name amiga_chip -source $amiga_gate_source \
    -divide_by 1 $amiga_gate_output
