# Native Vectrex32 API coverage

This is a native GS BASIC runtime, built from the MIT
[PiTrex source at 457c5a98](https://github.com/malbanGit/pitrex-baremetal/tree/457c5a98f914a812c9341c2949e9d57473ccac4a/gsBasic).
The Cortex-A9 executes the language and high-level graphics; a separate software
6809 executes CodeSprite. It does not execute the original PIC32 firmware.
`Version()` reports 127, matching the imported language lineage. The
[Vectrex32 1.29 manual](https://vectrex32.com/wp-content/uploads/2023/07/Vectrex32.pdf)
is the public API inventory, not a claim of binary or peripheral compatibility.

All 75 function names in that manual are registered and have implementations.
The following table identifies their native paths. The compatibility limits
below remain part of their contract.

| Functions | Native implementation |
| --- | --- |
| ClearScreen, DrawingListSprite, RemoveSprite, PutSpriteAfter, PutSpriteBefore | Retained drawing list with reference-counted sprite handles and ordering. |
| DotsSprite, LinesSprite, MoveSprite, ReturnToOriginSprite, ScaleSprite, IntensitySprite | Vector/pen state. Coordinates are local to the starting pen; dots reset the pen after drawing. Scale includes the Vectrex timer's two-cycle overhead. |
| Lines25DSprite, Lines3DSprite, CameraGetFocalLength, CameraGetRotation, CameraRotate, CameraSetFocalLength, CameraSetRotation, CameraTranslate | Audited upstream transformation order, world conversion, camera translation/rotation, near-plane clipping and perspective; 2.5D billboards. |
| SpriteClip, SpriteEnable, SpriteGetMagnification, SpriteGetRotation, SpriteGetUserData, SpriteIntensity, SpriteMove, SpriteRotate, SpriteScale, SpriteSetData, SpriteSetMagnification, SpriteSetRotation, SpriteSetUserData, SpriteTranslate | Live data/translation/clip arrays, transforms, pseudo-sprite setters and user data. Mutated attributes are checked again when rendered. |
| TextSprite, TextListSprite, TextSizeSprite | Caller BIOS raster glyphs, or an explicitly reported vector fallback. |
| Text2Sprite, Text2ListSprite, TextToLines, GetFontMetrics | Imported MIT vector font and vector geometry. |
| Distance, Offset, PtInRect, RegularPolygon, VectorRotate | 2D/3D geometry functions. |
| ABC, Explosion, Music, MusicIsPlaying, Play, Sound | MIT music/explosion sequencer and AY register callbacks; buffered at WaitForFrame. |
| FClose, FEOF, FGetc, FGets, FOpen, FPuts, FRead, FSeek, FTell, FWrite, Remove, Rename | Binary-safe callback filesystem and Stdin/Stdout terminal streams. |
| CodeSprite, Peek | Persistent software 6809, shared RAM and memory readback; see the dedicated adapter contract. |
| GetCompiledSpriteCount, GetFrameRate, GetTickCount, SetFrameRate, WaitForFrame | Native drawing progress, frame cadence, monotonic clock and controller sampling. |
| BreakOnControlC, DumpSprite, DumpSprites, SpritePrintVectors, Version | Terminal cancellation policy, meaningful sprite/vector diagnostics and source version. Diagnostic text formatting is native. |

## Rendering and timing limits

Line callbacks use signed native coordinates, approximately +/-17688 horizontally
and +/-21384 vertically for a normal view. A coordinate delta is multiplied by
`scale + 2`. Intensity is 0..127; the board adapter converts to the FPGA's 8-bit
range and saturates endpoints to signed 16-bit values. High-level rendering uses
ideal line segments. It does not recreate beam acceleration, analogue settling,
dot dwell brightness or phosphor persistence through original BIOS instruction
timing. The FPGA renderer has its own intensity/persistence model.
The board approximates a patterned line with eight equal subsegments, emitting
the portions selected by its pattern byte. This preserves visible stippling but
does not match the original beam-timed dash spacing.

Raster text reads seven 80-byte rows beginning at BIOS address `$F9F4`. The glyph
bytes are original when a BIOS is supplied; span spacing follows idealized BIOS
shift/character timing. Analogue text shape is not cycle-exact. Without a BIOS,
`mt_v32_result.raster_font_fallback` is set and the MIT vector font substitutes for
TextSprite/TextListSprite. Text2 functions always use that vector font.

Frame rate defaults to 30 Hz. Positive requests above 150 Hz clamp to the audited
upstream limit. WaitForFrame submits the drawing list, completes queued Peeks,
steps music, flushes Sound register writes and obtains controls. The board callback
waits for the requested cadence. GetCompiledSpriteCount counts successfully
rendered enabled sprites; high-level vectors do not consume the original
SmartCart's code buffer or trigger its refill thresholds.

Controller callbacks supply two rows of X/Y in -128..127 and four buttons, with
positive Y upward. Digital sampling returns -64/0/64 using thresholds +/-64.
Analog resolution uses deterministic quantization; it does not simulate the
BIOS comparator's analogue successive-approximation errors. GetTickCount converts
a real monotonic millisecond source into wrapping signed 32-bit 960 Hz ticks.

Cancellation is checked every 256 BASIC statements, every 256 scanned render
items/rows/glyphs, during low-level execution and blocked terminal reads. Emitted
lines also reach the board's service callback. This bounds ordinary long loops
and invisible rendering work; one allocation, arithmetic operation or filesystem
callback is still synchronous. ARM frame deadlines and responsiveness require
physical-board measurement.

## Audio, files and source loading

Sound changes become PSG writes at the next WaitForFrame. Machine-code
PSG writes are forwarded in order during that frame, after pending Sound writes;
repeated envelope-register writes remain separate restart events. Music and built-in
pieces use the caller BIOS's frequency, ADSR and modulation tables. Built-in
pieces also read their tune data from the BIOS. Missing BIOS data is an explicit
runtime error. Explosions need no ROM data and accept durations 1..128. No BIOS
music, envelope, frequency or font bytes are shipped in this source tree.

The board backend supports read, write, append and update modes, binary data,
seeking, EOF, removal and renaming. Paths stay relative to the selected game's
directory. Roots, drive prefixes, parent/current-directory components, wildcard
characters, and components ending with a dot or space are rejected. Failures
produce the documented BASIC failure value where possible; failed flushes that
otherwise report a full write and failed implicit closes become visible errors.
Strings and FRead are limited to 65535 bytes. The host replay tool deliberately
provides read-only assets; it cannot change the supplied game.

INCLUDE loads confined relative source, preserves source locations, and suppresses
duplicate includes using the original eight-file limit. CHAIN unwinds the running
program before loading the next confined source, with optional `.bas` fallback.
Source lines retain the language's 200-character bound. Oversized input fails
before conversion to the interpreter's 16-bit string representation.

Stdin/Stdout use byte-oriented terminal callbacks. Ctrl-C normally stops a blocked
read; BreakOnControlC(0) permits byte 3 as data. The common menu cancellation path
remains active. The board uses UART0; a host embedding must supply its own console
callbacks. Missing terminal services fail explicitly.

## Low-level and unavailable hardware

See [`firmware/vectrex32/lowlevel.md`](../firmware/vectrex32/lowlevel.md) for the
memory map, CPU/VIA model and execution bounds. CodeSprite executes real 6809
instructions inline, preserving registers and RAM between sprites and frames.
It supports BIOS calls through externally supplied ROM and renders their vector
and PSG effects through callbacks. Each frame allocates code consecutively in a
2 KiB shared-RAM arena. This layout and the register effects of surrounding
high-level sprites differ from the SmartCart compiler's generated code.

Synchronous Peek reads offsets 0..2047 in that shared RAM. Deferred Peek reads
1..16 CPU-addressed bytes and updates the caller's live completion/data array at
WaitForFrame. PIC32 address-space reads are unavailable. Illegal opcodes, invalid
memory, missing BIOS and finite execution-budget exhaustion are explicit errors.
The adapter does not provide PIC32 USB, DMA, flash firmware updates, stereo
eyewear, external VIA interrupt sources or a PCM/DAC-audio callback.

Interactive monitor editing/debugging commands such as LOAD, RELOAD, DIR, MEM,
breakpoints and stepping are unavailable in this embedded runner; they fail
instead of pretending to change the program or hardware. INCLUDE and CHAIN are
the supported programmatic source-loading paths. PiTrex-only YM/sample extensions
and CameraSet3D eyewear support are outside the 75-function manual inventory.

## Evidence and remaining acceptance

Directed runtime tests cover language execution, callbacks, 2D/3D geometry and
clipping, live arrays, binary files, console cancellation, music/explosions,
synthetic BIOS raster glyphs, real CodeSprite/Peek, cleanup and string boundaries.
The separate low-level tests cover CPU/VIA state, budgets, reentrancy and optional
calls into a caller-supplied real BIOS. Board adapter tests exercise FatFs failures,
binary NUL data, terminal byte/error paths, cadence and input mapping.

`scripts/verify_vecalabeth.py` hashes all thirteen externally supplied files before
and after two deterministic replays. One runs 2000 frames through setup/outdoors;
the other enters/exits a dungeon, re-enters, damages a Skeleton and returns to the
surface. It records vector captures, food/health changes and runtime counters in
the chosen build output directory. The optional `--bios PATH` argument to both
the host and verifier loads an external 8192-byte BIOS, records its SHA-256 and
checks that both replays use its raster glyphs without changing it. Without that
argument the replay uses the vector font fallback. These routes do not demonstrate
a complete playthrough, all game branches,
audio correctness on a speaker, measured ARM timing or physical-board acceptance.

Replay determinism is local to the selected C runtime: the inherited Rand and
Randomize functions use `rand`/`srand`, whose sequences differ between Windows,
Linux and ARM libraries. The host replay explicitly supplies seed 1. No-argument
Randomize uses the embedding application's seed callback, otherwise folds its
64-bit monotonic milliseconds into 32 bits. The board supplies that clock. Only
embeddings without either callback fall back to seed 1. Explicit Randomize(seed)
passes that value through unchanged.
Counters, generated maps and captures are therefore not cross-platform golden
outputs. The verifier checks gameplay events and bounds instead of one image hash.
