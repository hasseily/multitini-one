# Vectrex firmware integration

The Vectrex profile uses core ID `0x56454358` (`VECX`) and the common core
catalog, menu, USB service, and framed loader. The board entry point activates
the profile and exposes cartridge and Vectrex32 BASIC browsers. Host tests
exercise the portable services and real GSBasic interpreter. Physical SD,
USB, audio, display, and timing validation remain board tests.

## Content and boot

- Supply an 8192-byte BIOS at
  `0:/multitini/cores/vectrex/roms/bios.bin`. No BIOS is shipped in source.
- The selected core package's `content_root` is
  `0:/multitini/cores/vectrex/media`, supplying both browsers. BIOS files stay
  in the sibling `roms` directory. Cartridges and BASIC programs can be placed
  in subdirectories under `media`.
  `.vec` and core-local `.bin` files are native cartridges, 1 through 65536
  bytes. A `.bin` file remains ambiguous outside the Vectrex profile.
- Native boot asserts reset, selects native mode, loads BIOS and optional
  cartridge, releases reset, and waits for the core-ready flag. Eject uses
  explicit mode 2 and returns to the BIOS without a cartridge. Core reset
  retains loaded content. The last native cartridge selection is remembered.
- `.bas` files are NUL-free source up to 1 MiB. BASIC mode does not require a
  BIOS. A source/read validation failure happens before the core is reset.
  Stream failures are reported; incomplete native transfers keep reset held.

Both Vectrex browsers show folders before matching files. Enter or Right opens
a folder. Choose `..`, press Backspace or press Left to go up; Escape closes
the browser. Navigation cannot go above the media root. The displayed path
tracks the current folder. A BASIC program receives its own containing
directory as the asset root, so `media/Vecalabeth/vec.bas` reads `SPLASH` and
the other assets from `media/Vecalabeth/`. Reset preserves that relative path.
Media-relative paths may contain up to 255 bytes. The existing persistent
cartridge setting has a 95-byte limit; longer cartridge paths load normally
but clear the prior saved cartridge selection and report that they cannot
be remembered. BASIC Reset uses the full in-session path.
The shared browser retains its 95-byte individual-name and 144-entry limits
(including the parent entry). Larger directories should be split into folders.

BASIC runs synchronously in the existing menu action. Cooperative callbacks
service USB during computation, drawing, and frame waits. F12 stops the
program and returns to the menu. Selecting it again or choosing Reset core
reloads the last successfully run BASIC file and starts a fresh interpreter.
There is no suspended BASIC task. A failed source reload leaves the menu
available; an unsuccessful program selection must be selected again.
All three PSG volume registers are silenced on exit. A failed silence command
is reported as a core transfer failure.

## Input and pacing

Arrow keys control player one's axes. Z, X, C, V are buttons 1 through 4.
The first two connected gamepads supply players one and two. Generic HID
buttons 1 through 4 and Xbox A, B, X, Y remain four separate buttons. Keyboard
buttons merge with player one; pressed arrow keys override the corresponding
gamepad axis. Opposite arrow keys cancel. Disconnects release controls, and
failed input deliveries remain pending for retry.
Core-reset input deliveries are discarded by the FPGA; the firmware explicitly
invalidates its delivery cache and resends all held controls after reset.

Native input namespace `0x20` uses index 0 for eight active-high buttons
(player one in the low nibble), then X1, Y1, X2, Y2 as signed 16-bit values.
Positive Y points upward. The BASIC backend converts axes to -128 through
127 and leaves its runtime to apply requested analog/digital masks.

`GetTickCount` uses the platform's monotonic milliseconds, converted into
the Vectrex32 960 Hz counter with 32-bit wrapping.
The board callback accepts 1 through 150 frames per second, matching the
PiTrex backend's upper limit. It uses elapsed milliseconds with a fractional
remainder, independent of DVI refresh. Excessively late frames reset the
deadline instead of producing an unlimited catch-up burst. A blocked stream
uses the common continuation predicate to poll USB and cancel on F12.
Deferred USB polling cannot issue another core command while a stream command
is in progress; changed input is delivered by ordinary polling afterward.

`Stdin` and `Stdout` use UART0 byte input/output, including binary zero bytes.
Input waits service USB so F12 still exits. `BreakOnControlC` controls byte 3
from UART input; it does not disable F12. Output checks cancellation every
64 bytes and reports UART transfer failure. The menu owns UART again after
BASIC exits; USB keyboard input remains the game/menu control path.

## BASIC graphics, sound, and assets

The ARM backend submits four signed little-endian 16-bit native coordinates
and an intensity byte per line. The FPGA projects and queues those lines.
Solid patterns submit one segment; other eight-bit patterns submit the lit
parts of eight equal subdivisions. This subdivision approximates stippling.
Coordinates are saturated at the transport's signed 16-bit boundary.
The seven-bit BASIC intensity expands to eight bits as `(value << 1) |
(value >> 6)`, matching the native beam's renderer scale.
Sound commands submit an AY register number and value.

File access is rooted at the selected BASIC program's content directory.
Absolute paths, drive prefixes, `..`, wildcards, and components ending in a
dot or space are rejected. Separators are normalized before accessing FatFs.
The shared SD owner supports 16 managed handles and closes remaining BASIC
handles on exit. A close failure remains an error and is retried before the
next program starts. Files are streamed; there is no former 1 MiB asset cap.
The filesystem's offset range and BASIC's signed integer range still apply.

| BASIC operation | Board backend behavior |
| --- | --- |
| `FOpen` | `r`, `w`, `a`, optional `b` and `+`; existing, truncate/create, or append/create |
| `FGets`, `FGetc`, `FRead` | Read bytes, preserve newlines, report EOF and backend errors |
| `FPuts`, `FWrite` | Write bytes and sync; partial or failed writes return failure/count |
| `FSeek`, `FTell`, `FEof` | Absolute/current/end positions and a persistent EOF indicator |
| `FClose` | Sync writers, close, and report either failure |
| `Remove`, `Rename` | Confined source/destination paths; return backend result |
| `Include`, `Chain` | Read program source through the same confined file callbacks |

Seeking past EOF changes the logical position; a subsequent write fills the
gap with zero bytes. Append writes always use the then-current file end,
including after a seek. Text and binary modes preserve the same on-disk bytes.
Conflicting duplicate opens involving a writer, removal/rename of an open
file, and unmount with managed handles open are rejected. Ordinary BASIC file
writes are synced but are not journaled atomic replacements. Existing C64
and supervisor journal APIs retain their behavior.
An individual `FRead` is limited to 65535 bytes by the interpreter's string
representation; larger files can be processed in successive reads. Binary
reads retain embedded NUL bytes and their explicit lengths. A sync failure
remains visible even when the backend accepted every requested write byte.

ROM-based sound helpers can read the user-supplied BIOS through a checked
`E000`–`FFFF` callback. BASIC programs that do not require these helpers can
still run without a BIOS. No BIOS tune data is embedded in the adapter.
Unsupported interpreter operations return errors; see the runtime's own
compatibility documentation for its language subset.

## Build and validation

The C sources are `firmware/common/vectrex_boot.c`,
`firmware/common/vectrex_hid_input.c`,
`firmware/platform/xilinx/vectrex_content_service.c`, and
`firmware/platform/xilinx/vectrex32_service.c`. GSBasic sources are listed in
`firmware/vectrex32/gsbasic_sources.txt`, with `runtime.cpp` and the compatibility
includes in that directory. The interpreter requires C++17, exceptions,
RTTI, `-fwrapv`, C++ runtime linkage, and normal static constructor startup.

The native content buffer occupies 1 MiB plus one byte of static storage,
and the BIOS buffer occupies 8 KiB. Managed files use 16 static FatFs objects
and path buffers instead of loading whole assets into heap memory.
The Vitis configuration allocates a 64 MiB heap and 2 MiB stack. These are
engineering allocations; they are not measured maximum program requirements.

The main CMake host suite includes `vectrex_firmware` and `vectrex32_runtime`.
Firmware tests cover BIOS absence, native boot, odd and oversized cartridges,
mid-transfer failure, ejection, four-button input, two pads, release retry,
real BASIC line/PSG/asset callbacks, frame pacing, invalid cadence, runtime
failure, normal cooperative exit, and cancellation during a blocked stream.
Compiled BASIC programs also exercise the full file callback roundtrip,
path confinement, failure values, and implicit close errors. SD-owner tests
cover all open modes, append/update, seek gaps, handle conflicts and limits,
short writes, and injected seek/read/sync/close/rename/remove failures.
Main and new platform services also compile under the strict warning checks.

`scripts/test_vectrex_transport.py` tests the actual machine HDL with an
original synthetic BIOS program and generated cartridge bytes. It covers
BIOS readiness, CPU-controlled PB6 bank switching, odd-size bounds, malformed
frames, mode changes, line projection, PSG, input, pause, queue backpressure,
and early/delayed/simultaneous abort epochs. The byte channel is ordered, as
in the board mailbox; the independent abort channel may arrive earlier or
later. This test does not prove asynchronous mailbox timing or board clocks.

Run Windows native test tools through `scripts/run_native_tool.py`. The test
transport wrapper does this automatically. Build and test logs and generated
executables stay under `build/`.
