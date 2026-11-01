# Amiga Workbench display options

## Scope

The working ARM 805 / FPGA 806 port is committed and pushed as bcd0091.
This change adds display options only. CPU implementation, speed profiles,
native clocks and memory configuration remain unchanged.

The first three choices retain 16 colours (four bitplanes). SuperHiRes uses
four colours (two bitplanes):

| Option | AmigaOS display ID | Guest pixels | Capture pixels |
| --- | --- | --- | --- |
| Standard | 00029000 | 640 by 256 | 640 by 512, line doubled |
| Interlaced | 00029004 | 640 by 512 | 640 by 512, woven fields |
| Maximum PAL overscan | 00029004 | 724 by 566 | 724 by 566, woven fields |
| SuperHiRes interlaced | 00029024 | 1280 by 512 | 1280 by 512, woven fields |

The maximum is the PAL HiRes interlaced overscan size documented by the
[Workbench monitor manual](https://wiki.amigaos.net/wiki/AmigaOS_Manual:_Workbench_Monitors).
Larger overscan needs native DIW-derived positioning; width alone does not
define a crop. Native PAL interlace also uses woven fields when the capture
choice is Standard, so a manual Workbench ScreenMode change does not bob
alternating text lines. Native noninterlaced screens retain line doubling.

The standard option retains its accepted alignment and Auto 2x at 1080p.
Maximum overscan cannot fit integer 2x inside 1080p, so Auto selects 1x.
Its padded stride is 1536 bytes; a 566-row frame uses 869376 bytes and fits
the existing 1 MiB slots. SuperHiRes has a 2560-byte stride and needs 1310720
bytes per frame, so its slots are 2 MiB. Only an explicit SuperHiRes commit
selects the larger pools; reset and the first three choices keep the original
addresses. Machine RAM stays at 24000000 through 24FFFFFF.

| Pool | Standard / interlaced / overscan | SuperHiRes |
| --- | --- | --- |
| Shared capture | 22000000, four 1 MiB slots | 22000000, four 2 MiB slots |
| Processed output | 22400000, four 1 MiB slots | 22800000, four 2 MiB slots |
| Persistence history | 22800000, 2 MiB | 23000000, 4 MiB |
| Native capture | 23000000, four 1 MiB slots | 23800000, four 2 MiB slots |

Frame and job metadata latch the physical profile before any transfer.
Software validates the core, capability, geometry and stride before selecting
wide addresses. Reference frame jobs retain ABI 2 for legacy layouts and use
ABI 3 for wide jobs; the runtime core ABI remains 0.27.

On 1024- and 1200-pixel HDMI outputs, intermediate capture renders all 1280
samples into the existing horizontal blanking interval. Every supported
raster total has at least 1344 pixel clocks per line. The physical HDMI timing
stays unchanged; only final scanout crops the centered image. Raw warm-up
fallback is suppressed in this case until the processed image is ready.

## Guest participation

Resizing capture does not change Workbench. A small original 68000 helper
inside AmigaOS applies original ScreenMode/Overscan presets through the OS
and reports the actual Workbench mode. It is delivered separately with an
installer; no ROM or installed operating-system image is distributed.
The menu calls this the MTDisplay guest helper; its installed executable is
`C:MultitiniScreen` on the original helper data disk.

The helper polls cooperatively, verifies the public Workbench screen and
acknowledges a request only after it has applied. Helper07 runs
each original USE and SAVE shell job through synchronous DOS `Execute`, with
zero input and a fresh, owned `NIL:` output handle. It closes that handle once
whether launch succeeds or fails. It does not transfer the daemon's own streams.
The shell job writes a terminal receipt after the preferences tools return;
`Execute`'s Boolean result alone does not prove that those tools succeeded.

Heartbeat can pause while synchronous preferences are running. ARM 812 retains
an accepted request for its full 120-second deadline despite the three-second
heartbeat availability limit. A stale heartbeat blocks new requests; it does
not cancel an accepted request or prevent a valid matching terminal reply.
ScreenMode may ask the user to close application windows. A failed or
cancelled request retains the previous applied capture choice and ARM
sidecar. Guest preferences finish first, so a later capture-apply failure
can leave Workbench using the requested size. Select a resolution again
to recover; guest preferences and capture are not one atomic transaction.
The ARM service does not edit a mounted Amiga filesystem.
The menu closes before publishing a request so guest input and any ScreenMode
requester are accessible. It stays closed while ScreenMode is running. The
display service suspends polling and deadlines while the overlay is shown,
preserving cached helper health. The fourth
choice requires the updated guest helper; older helpers still expose the
first three choices.

## Guest origins and visible clipping

Logical Screen and ViewPort dimensions alone do not prove that every requested
pixel is visible. The ownership-only helper07 candidate still clips about 72
pixels from the right of Maximum PAL overscan on the tested board. Its passing
ownership tests do not qualify that display mode. The combined normalized
helper07 correction below passes 57 native cases, 16 host checks and the
on-card four-mode checks described under Qualification.

Overscan preferences use PAL HiRes noninterlaced coordinates, including for
interlaced and SuperHiRes ScreenMode requests. The helper prepares an Overscan
record for every mode and runs Overscan before ScreenMode in both USE and SAVE:

| Choice | Overscan rectangle | Monitor view position |
| --- | --- | --- |
| Standard, interlaced and SuperHiRes | 0 through 639, 0 through 255 | Restore `MonitorInfo.DefaultViewPosition` |
| Maximum PAL overscan | 0 through 723, 0 through 282 | Live view position plus the original signed MaxOScan origin, with X converted to view units |

For Maximum, X compensation is an arithmetic signed half of
`MaxOScan.MinX`; Y compensation is `MaxOScan.MinY`. Normalizing the rectangle
and compensating the view position must be applied together. Returning to
another mode restores its default view position instead of retaining the
Maximum shift. MonitorInfo queries must return at least 84 bytes before the
helper reads the default position.

The helper also checks the full AmigaOS `DisplayClip` rectangle returned
through `VideoControl`. Success and helper flag bit 5 (`VIEWPORT_MATCH`) require
the requested dimensions to match the Screen, ViewPort and inclusive clip width
and height, together with the exact display ID and depth. `Screen.LeftEdge` and
`Screen.TopEdge` must also equal `DisplayClip.MinX` and `DisplayClip.MinY`.
A 724-pixel clip spanning -72 through 651 still cuts a screen spanning 0
through 723. Matching dimensions with a shifted origin must report a mismatch
and cannot acknowledge success.
The existing mailbox words and ARM/FPGA binaries stay unchanged; this is a
guest-software correction.

## Optional mailbox contract

D000 status bit 27 advertises this display extension; bit 26 advertises the
optional SuperHiRes profile, and RTC bit 28 retains its meaning. Helper flag
bit 6 advertises its SuperHiRes preset. Both flags gate the fourth choice.
Runtime ABI 0.27 stays compatible because existing ARM never requests mode 3
and all other modes preserve the legacy memory map.

| Host opcode | Payload | Meaning |
| --- | --- | --- |
| 8 | Low 16 bits: sequence14 shifted left two, OR mode2 | Publish guest request; sequence zero is reserved |
| 9 | Word index 0 through 17 | Read a coherent guest-mailbox word |
| 10 | Mode 0 through 3 | Apply capture profile after the guest acknowledges |

Reserved payload bits must be zero. Request and capture commits publish
on the inactive service-clock edge and have bounded completion. Capture
changes wait for the source bridge to drain. Shared output processing is
quiesced before a capture-profile commit and resumed with fresh geometry.
The commit holds pixel consumers in reset for sixty-four service cycles after
publishing the new profile, allowing both clock crossings and the geometry
snapshots to settle. HDMI timing and mailbox resets remain live while every
accepted AXI transaction drains.

The guest mailbox occupies the unused DE0F00 through DE0F23 range. Normal
Gayle identification at DE1xxx remains unchanged.

| Word index | Meaning |
| --- | --- |
| 0 | Read-only signature 4D54 |
| 1 | Read-only mailbox version 1 |
| 2 | Read-only request word |
| 3 | Acknowledged request, written last |
| 4 | Result: 0 success, 1 busy, 2 unsupported, 3 preference failure, 4 actual screen mismatch |
| 5 | Helper heartbeat |
| 6, 7, 8 | Actual guest width, height and bitplane depth |
| 9, 10 | Actual display ID, low and high word |
| 11 through 14 | Signed MaxOScan min X, min Y, max X, max Y |
| 15 | Helper flags |
| 16, 17 | Signed monitor view X and Y |

Metadata remains stable after acknowledgement until the next request.
The host compares acknowledgement before and after reading metadata.
Malformed, stale or unsuccessful responses cannot publish a new applied
mode or overwrite the last committed display preference.

Display preferences use a separate versioned, journalled sidecar. Existing
AM01 configuration, MTAMSET1 settings and mounted paths are preserved.
The option reports requested and applied state, and is unavailable when
the core capability or a live guest helper is absent.

## Qualification

Native tests must preserve distinct odd/even field content and all four
corners, exercise actual maximum DIW positioning, and reject partial or
mismatched field pairs. Source/output slot ownership and AXI responses
must drain before geometry changes. Standard 804 alignment and the 806
RTC/SysInfo fix remain regression gates.

The guest HUNK, preset formats, mailbox state machine, sidecar recovery,
old-core compatibility and option rendering need focused tests before a
full build. ARM 807 / FPGA 809 is the sealed three-choice package. FPGA 809
passed strict timing and CDC gates and was inspected on the board. Its
640 by 512 source screenshot contains repeated even/odd rows while Workbench
has an oversized noninterlaced virtual desktop. This observation does not
establish the result of the new weave path on hardware.

FPGA 811 passed its strict routed timing and CDC gates. ARM 812 retains an
accepted request while heartbeat pauses during preferences. Those exact
binaries and their original source archives are retained for the guest-only
helper07 correction; no ARM or FPGA rebuild is required for this helper work.

The ownership-only helper07 candidate passed 45 native cases. Maximum PAL
overscan still has the physical clipping failure described above, so the
four-choice feature is not accepted on that evidence. The combined normalized
origin and physical-clip helper passed all 57 native cases, including the 45
prior cases, 16 host/disassembly checks and exact standalone reproduction of
all 19 delivery files. Its source and component package are sealed. Native
library stubs remain distinct from the real preferences utilities. Results
for an earlier helper pin must not be attributed to the next helper binary.

On-card checks pass for this exact normalized helper07 with ARM 812 and FPGA
811. All four modes acknowledge, apply and save with matching guest clip and
screen origins. The full title bars start at the guest image's left edge in
Standard, Maximum and SuperHiRes source captures. Maximum-to-Standard,
Maximum-to-interlaced and Maximum-to-SuperHiRes round trips pass. Repeated
Maximum requests retain view position 93,29; a cold core load restores saved
Maximum. The final saved choice is interlaced 640 by 512, depth four, with
default view position 129,44. Sampled final video error counters are zero.

Two internal interlaced source captures are byte-identical, SHA-256
`e65186f1794d6fddfc3e5e4eba7544bfaa4aef366a7b61410619c38b154bd69f`.
These observations cover the live card, AmigaOS clip queries and internal
capture path. External physical DVI output was not separately measured.
Existing Scalos windows may need maximizing once after increasing screen size.
The sanitized `helper07-board-acceptance.json` receipt is retained with the
paired package. Original ARM/FPGA source archives remain unchanged; the new
guest component and source-only metadata have separate provenance.

Stop further builds if final routed timing fails. Other build or
qualification failures are repaired and checked without weakening gates.
The user authorized card inspection and the fourth resolution on this turn.
