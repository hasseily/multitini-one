# Shared UI and board services

Firmware `0x55494631` (`UIF1`) adds USB recovery fixes, a TXT reader, a
System page, and reusable checkbox and slider controls. The changes run on
the ARM and use the existing dense text overlay. They require no new FPGA
registers or image. Configurable menus and named presets are outside this work.

## USB

The host wakes a suspended USB3300 PHY before resetting the controller,
bounds reset waits, and preserves PHY and host initialization failures.
Root-hub startup failures now propagate even when CherryUSB's public
initializer returns success. Failed hub startup cleans up its message queue.
Late controller-stop failures also remain visible. Reading status uses cached
PHY diagnostics and does not issue ULPI transactions.

System > Restart USB releases held inputs and restarts device discovery.
A failed stop is reported without attempting another start. USB errors on
the System page show cumulative submit, transfer, and core-input counters.

## Documents

TXT files appear beside supported media in file browsers. System > Read
documents also browses folders below `0:/multitini`, including in standby.
Directories and usable media take precedence when a listing reaches its
fixed capacity. Opening a document never mounts or ejects game media.

The reader shows 95 columns and 40 lines. Up/Down scrolls one line;
Left/Right and Page Up/Down scroll one page; Home/End jump to either end.
Escape or Backspace returns to the same selected file and scroll position.
F12 closes the reader and menu. The active core stays paused while the
menu is visible.

Input is capped at 256 KiB with a visible truncation notice. Reads use at
most 4 KiB per step, poll deferred USB work between steps, and close the
file before decoding it. SD errors close the reader and retain the browser.
CR, LF, CRLF, tabs, and UTF-8 BOMs are handled. Common typographic quotes,
dashes, and nonbreaking spaces map to ASCII; unsupported glyphs become `?`.
UTF-16 BOMs are rejected. This is a plain-text reader, not a rich document
renderer.

## System

The page provides restart, autoboot, documents, clock setting, USB restart,
and the golden updater. It displays firmware and FPGA build IDs, interface
version, uptime, board temperature, USB state, the last SD catalog scan,
save/settings warnings, and audio state. Core-dependent actions are disabled
in standby. The catalog row describes the last scan, not live SD health.
Temperature refreshes once per second while the System page is visible.
The existing changed-word presenter avoids rewriting unchanged text cells.

The PCF8563 clock editor supports local dates from 2000 through 2099.
Up/Down or Tab selects a field, Left/Right adjusts it, and Enter advances
or applies Save time. Escape cancels. Month/year changes clamp the day to
the valid calendar range. Writes are checked by reading the clock back;
the requested time or one second later counts as success. Stopped clocks,
low-voltage flags, invalid dates, and I2C failures are reported. Successful
edits also refresh the cached FAT timestamp.

## Controls and source

Menu descriptors now carry an optional checkbox or slider with bounds.
Existing C64 options, autoboot, video border, and Vectrex overlay settings
use these controls. Unavailable actions show a disabled cue; informational
rows remain readable. The UI helpers also support a locked appearance.
Descriptors and System snapshots perform no hardware or storage I/O.

USB fixes and text normalization/wrapping were adapted from Appletini ONE
commit `1fd4f371e8f4ef08bc63875550564590609fcfeb`. Per-file licenses and
the source pin are recorded in `licenses/files.csv` and
`provenance/sources.lock.json`. The sibling checkout is unchanged.

## Validation

Host tests cover USB reset/start/stop failures, SD prefix reads and errors,
text normalization and bounded navigation, modal input and buffer cleanup,
clock calendar/BCD/readback behavior, System descriptors, and UI clipping.
The production renderer supplies the `system`, `clock`, and `text-reader`
scenarios in `scripts/preview_menu.py`. These renders are software previews.

Use `python scripts/run_native_tool.py -- TOOL ...` for native build/test
commands on Windows. It adds the local UCRT runtime to child-process PATH
and converts loader failures into command errors without modal DLL alerts.

Physical USB reconnect/restart, SD removal during a read, battery-backed
clock setting, and display checks remain board acceptance work. This change
does not claim that those hardware checks have passed.
