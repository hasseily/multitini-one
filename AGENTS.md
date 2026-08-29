# Multitini One Repository Guide

## Start Here

Read `MULTITINI_ONE_PLAN.md` before making plans or changing code. It records
the product goal, board facts, license rules, system design, work order, and
test gates agreed before this repository was made.

## Project Scope

Multitini One turns the Appletini ONE card into a stand-alone FPGA system. It
does not use an Apple II host. Assume the board's 5 V jumper fix is present.
The first target is a working C64 based on the GPL MiSTer C64 core. The wider
goal is a shared run-time and FPGA shell that can support more licensed MiSTer
cores with little board-specific work.

## Source Repository

The board support source starts in the sibling repository `../appletini-one`.
Do not change that repository as part of Multitini work unless the user asks.
Copy or port only what Multitini needs, preserve source history where useful,
and keep all license and credit files.

## Working Rules

- Keep generated Vivado and Vitis files out of source control.
- Generate the Vivado project from source and scripts.
- Pin every imported core and helper to a source commit.
- Keep a per-file license list for all imported HDL and firmware.
- Do not add system ROMs, game images, disk images, or logos without clear
  rights to ship them.
- Publish the exact source, scripts, constraints, and settings used to build
  each public FPGA image.
- Run small tests before a full Vivado build.
- Review every new clock, reset, crossing, and timing rule.
- Prefer a thin board shell and keep each machine core close to its upstream
  source.

## Writing Style

Use short, direct prose. State known facts, open questions, and test results
separately. Do not claim hardware support until it passes on the board.
