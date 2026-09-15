# Vectrex CPU source

The only synthesis input here is `mc6809i.v`. It is copied without changes
from `modules/jtframe/hdl/cpu/mc6809i.v` in
<https://github.com/jotego/jtcores> at commit
`24761ad59f2abc0718a009dede5c18fec6d0e266`.

This is Greg Miller's cycle-accurate 6809 with JTFRAME clock enables and
fixes. There is no separately imported JT6809 core. JTFRAME's GPLv3 license
is preserved in `LICENSE-JTFRAME`. Greg Miller's original grant is preserved
in `LICENSE-Greg-Miller.md`, copied from `documentation/LICENSE.md` in
<https://github.com/cavnex/mc6809> at commit
`17e94a6ef163be8b79a9b15b2e814847b6062f0f`. Multitini selects that grant's
**Standard BSD license**, which permits source redistribution. Its alternative
binary-only grant is not selected. Original work carries BSD-3-Clause terms;
the JTFRAME derivative is distributed under GPL-3.0 with the BSD notice kept.

`README-Greg-Miller.md` is the original README at the same cavnex commit.
It records the author's cycle-count and physical-system validation, including
a Vectrex. That historical evidence is not a claim that Multitini has passed
on hardware. No system, game, or test ROM is imported.

| Local file | Upstream path | License / role | SHA-256 |
| --- | --- | --- | --- |
| `mc6809i.v` | JTFRAME `modules/jtframe/hdl/cpu/mc6809i.v` | GPL-3.0 derivative; underlying BSD-3-Clause | `a0d0c57b322ea3cb757d82a9945475e2f0df047a2b472c9481b67dc251e5fb78` |
| `LICENSE-JTFRAME` | JTFRAME `modules/jtframe/LICENSE` | GPLv3 license text | `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986` |
| `LICENSE-Greg-Miller.md` | cavnex `documentation/LICENSE.md` | Original BSD-3-Clause grant and unselected alternative | `11d10d396eb75ce78dd49eccc4f575379106f2c5291e863eb653842532694d0a` |
| `README-Greg-Miller.md` | cavnex `README.md` | BSD-3-Clause documentation | `7e181a57e4aae75383aa47defb2ea69fe6a4da0b387073e2bc02f37d8c86823d` |

## Multitini wrapper contract

`hdl/core/vectrex/multitini_vectrex_cpu.sv` uses only rising edges of `clk`.
`CLOCKS_PER_CPU` must be divisible by four; 16 gives 1.5 MHz from 24 MHz,
and 64 gives 1.5 MHz from 96 MHz. There are no generated clock nets.

Phase zero produces `e_tick`, the emulated E falling edge. The CPU advances
and AVMA is captured for the new bus cycle. At three quarters of the cycle,
`cpu_tick` represents Q falling, samples CPU interrupt inputs, and issues
exactly one `bus_read` or `bus_write` pulse when the latched AVMA is true.
Address and write data are stable before that transfer. `data_in` must be
valid before `cpu_tick`; a continuously clocked synchronous RAM read or a
combinational peripheral mux has most of a CPU cycle to respond. The wrapper
captures the read value before peripheral read side effects, then presents
it to the CPU until the next E falling edge. Use `cpu_tick` for VIA and PSG
machine-cycle advancement. It still pulses during CPU-internal dead cycles.

`pause_request` is synchronous to `clk`. It takes effect at phase zero after
the current bus transfer; `paused` confirms the boundary. No bus strobe or
machine tick repeats while paused. Interrupt inputs must be synchronous to
`clk`; the CPU applies its own documented 6809 interrupt sampling pipeline.
Assert `reset` for at least one fabric edge and release it synchronously.
Reset suppresses all external bus strobes. Reset and pause do not use HALT
or DMA bus arbitration, which this machine does not require.

## Validation

Run `python scripts/test_vectrex_cpu.py` with Icarus installed. The runner
uses `run_native_tool.py` to supply Windows runtime DLL search paths and
suppress modal loader dialogs. The test passes at divider 16 and divider 64:
reset vector reads, immediate arithmetic and exact 2/2/5-cycle instruction
spacing, indexed postincrement and taken branches, subroutine stack/return,
read-to-clear data capture, IRQ/FIRQ/NMI handlers and RTI stack restoration,
and boundary-safe pause. Memory reads in this test have synchronous latency.
The only compiler warning is the unannotated timescale on synthesis modules.
No FPGA resource, timing closure, or board validation is implied by this test.
