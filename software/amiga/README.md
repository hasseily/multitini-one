# Multitini Amiga desktop helper

Original GPL-3.0-only guest software. It contains no ROM, AmigaOS executable,
SDK header, or disk image. Offline checks validate the generated code and files;
board acceptance is recorded separately in docs/AMIGA_DISPLAY_OPTIONS.md.

The intended modes are PAL HiRes 640 by 256, PAL HiRes Interlace 640 by 512,
and PAL HiRes Interlace 724 by 566, each with four bitplanes / 16 colours.
The fourth choice is PAL Super-HiRes Interlace 1280 by 512, with two bitplanes /
four colours from the ECS 64-colour palette. Its ModeID is 0x00029024.
ScreenMode preference files describe real Workbench dimensions. They do not
change HDMI resolution or manufacture extra pixels through capture scaling.

`m68k.py` assembles only the documented 68000 instructions used by `helper.py`.
It checks named PC-relative references and writes a single HUNK_CODE segment
without relocations. `build.py --output <new-folder>` produces the original
helper, preferences, shell jobs, installer, readable instruction listing and
source/output hashes. All output should go into the repository's ignored
`build/` directory. No cross compiler, SDK, download or installation is needed
to build it. The helper opens dos.library, intuition.library and graphics.library
version 37; Workbench's ScreenMode and Overscan tools must already be installed.

The helper uses Exec's public-port list to prevent duplicate daemons. It polls
once per second using DOS Delay(50), updates a heartbeat, and queries the locked
Workbench public screen plus graphics display information. Before locking the
screen it scans the locked public-screen list for an existing, non-private
Workbench entry, then releases the list. When absent it keeps its heartbeat
and defers requests without opening Workbench from User-Startup. No screen
pointer is read from that list. Screen closure after the list is released is
still possible; the subsequent screen lock follows the normal Intuition API.
Preferences run through synchronous DOS Execute with documented zero input
and a fresh owned NIL: output handle per command. The helper publishes BUSY
before Execute, closes its output once on success or failure, and never
transfers its own input/output streams. Heartbeat can pause while Execute
waits for a preferences tool or requester; ARM812 keeps an accepted request's
separate 120-second lifetime. While a command has no terminal receipt, the
helper makes no Intuition or graphics calls that could wait on its GUI locks.
After a successful receipt it queries Workbench
afresh and allows up to ten one-second polls for the requested screen and
visible viewport to match. It starts SAVE only after USE matches and freshly
verifies SAVE with the same bound. BUSY and the previous ACK remain until the
transaction succeeds or reports its final error.
Preferences tools run one shell command at a time. A RAM `T:` receipt records
when the preferences utilities have returned; Execute's BOOL reports launch
success rather than the commands' exit status, so the daemon still requires
a complete two-byte terminal receipt. A failed command, cancelled screen change or screen mismatch cannot
produce a successful acknowledgement. A changed request cannot receive an old
request's acknowledgement. USE is verified before SAVE; SAVE is verified again.

The draft installer copies only original files to C: and
SYS:Prefs/Presets/Multitini and appends one guarded startup line to S:User-Startup.
It does not replace Startup-Sequence. Installing it changes the user's guest
volume and belongs to a separate, explicit installation step. Building never
opens or edits an HDF or accesses the card. Test offline first.

## Mailbox

The word registers at DE0F00 are shared with the board implementation. Signature
4D54, version 1 and request are read-only to the guest. Request bits 15:2 contain
a nonzero 14-bit sequence and bits 1:0 the mode (0..3). The daemon writes result
and observations before writing the exact acknowledged request last. Result
values are 0 success, 1 busy, 2 unsupported, 3 preferences failed, 4 screen
mismatch. Flags bits 0..4 mean alive/libraries open, screen valid, maximum
overscan query valid, monitor query valid, asynchronous transaction busy.
Bit 5 means visible ViewPort width/height equal both the requested dimensions and
Screen width/height, and the actual ViewPortExtra.DisplayClip has those dimensions
with the same origin as Screen.LeftEdge/TopEdge. The helper obtains that OS-owned
clip through graphics VideoControl while holding the screen lock, rearms its
consumed GET tag on each query, and copies the rectangle before unlocking. An
unknown clip, panned screen or partly clipped bitmap cannot pass. Mailbox
width/height describe the visible ViewPort.
Result/observations stay immutable after each success or error ACK until a new
nonzero request; only heartbeat changes while idle. Startup observations remain
live before the first ACK, and a request waits for Workbench to exist. A
request already present when the daemon starts is ignored; ARM publishes a
fresh sequence after observing this incarnation advance its heartbeat. Mailbox
metadata is observational; there is no direct guest-memory editing.

Mode 3 uses the existing two-bit request field. Flags bit 6 advertises support
for this fourth choice; mailbox signature/version and bits 0..5 keep their
existing meanings. ARM must require both the FPGA Super-HiRes capability and
the healthy helper's bit 6 before offering mode 3. Older helpers lack that bit.
The helper checks the requested depth (four planes for modes 0..2, two for 3)
after USE and SAVE, in addition to exact Screen/visible ViewPort dimensions and
ModeID. Every mode applies an OSCN record before ScreenMode. Mode 2 uses the
normalized maximum record; modes 0, 1 and 3 restore the nominal PAL HiRes
overscan record. The four SCRM preference files retain their previous bytes;
the apply/save jobs for modes 0, 1 and 3 now include that Overscan step.

Upgrading the guest files does not replace a running daemon: the version 1
singleton port still prevents duplicates. Reboot Workbench after installing
an upgrade. A retained ACK snapshot, including its older flags, stays frozen
until a new request; capability discovery does not rewrite that snapshot.

## Interface references

Only public interface facts and original encodings are used. No SDK source is
included. Library vectors were independently checked against the already-local
amitools FD descriptions; m68k structure offsets were checked against the
already-local AROS public interface definitions. Those tools are verification
references, not production build dependencies.

PAL/NTSC Overscan preferences use the group standard HiRes NONLACE units.
The helper queries mode 29000 for its 724 by 283 maximum rectangle and the
current MonitorInfo.ViewPosition. For mode 2 it normalizes StandardRectangle
to (0,0)..(723,282), sets Text to 724 by 283, and shifts ViewPosition by
(MAX.MinX/2, MAX.MinY). HiRes X has two pixels per View clock; nonlace Y uses
the same unit. Odd X origins, arithmetic overflow or positions outside the
reported ViewPositionRange refuse the transaction. With the observed stock
position (129,44) and maximum origin (-72,-15), this produces (93,29).
Modes 0, 1 and 3 restore fixed nominal 640 by 256 bounds and the immutable
MonitorInfo.DefaultViewPosition, requiring at least 84 bytes of monitor data.
Mutable TEXT/STANDARD queries cannot supply that restore baseline.

Every OSCN record uses full PAL HiRes mode ID 29000 and zero reserved words.
A Workbench 3.1 archive captured during board testing uses that ID; the earlier
21000 record left stock bounds unchanged. A later capture confirmed that
negative Standard origins were clamped to bitmap dimensions of 652 by 268,
despite logical Screen/ViewPort dimensions of 724 by 566. Normalization and
the physical-clip/origin check address these observed failures. Board acceptance
is recorded separately from this source and its offline test receipts.

- [Preferences: SCRM and OSCN layouts](https://wiki.amigaos.net/wiki/Preferences)
- [Workbench monitor mode limits](https://wiki.amigaos.net/wiki/AmigaOS_Manual:_Workbench_Monitors)
- [Display ModeID constants](https://wiki.amigaos.net/wiki/Display_Database)
- [Overscan queries](https://wiki.amigaos.net/wiki/Intuition_Screens#Preset_Overscan_Values)
- [Graphics display queries](https://wiki.amigaos.net/wiki/Classic_Graphics_Primitives)
- [Overscan units](https://wiki.amigaos.net/wiki/AmigaOS_Manual:_Workbench_Preferences)
- [Preferences commands](https://wiki.amigaos.net/wiki/AmigaOS_Manual:_AmigaDOS_Workbench-Related_Command_Reference)
- [DOS Execute and zero-input semantics](https://developer.amigaos3.net/autodocs/dos.library/Execute.html)
- [MonitorInfo layout and immutable default position](https://d0.se/include/graphics/displayinfo.h)
- [VideoControl clip query](https://d0.se/autodocs/graphics.library/VideoControl)
- [ViewPortExtra layout](https://d0.se/include/graphics/view.h)
- [Screen position and display clip](https://d0.se/autodocs/intuition.library/OpenScreen)

All instruction encodings are for the original 68000. The helper does not
require a 68020, FPU, AGA, replacement ROM or proprietary development kit.


## Offline checks

Run `python -m unittest discover -s software/amiga/tests -v` for standard-library
HUNK/preset/OFS tests. If Capstone is available, the same suite disassembles every
instruction as an original 68000 instruction. It is optional and is not a build
dependency.

`python software/amiga/test_native.py --output build/<fresh-directory>` runs
Xilinx XSim against the real imported FX68K and production guest mailbox. All
Exec/DOS/Intuition/Graphics calls use original test stubs. The fixture verifies
emitted control flow, interface registers, asynchronous heartbeat, stale
sequences, signed queried overscan bytes, preferences errors and virtual-screen
rejection. This does not establish behavior of the real AmigaOS utilities.

The generated data ADF has no boot code and must be mounted after Workbench has
booted. It contains the original executable/presets/installer and Source.zip
with the complete standalone builder source and GPL licence. The OFS writer is
standard-library-only; existing amitools can independently validate the image.
