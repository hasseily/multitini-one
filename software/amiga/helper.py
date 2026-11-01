"""Build the original 68000 MultitiniScreen cooperative guest daemon.

Library vector numbers and public structure offsets are interface facts, not
imported SDK source. See README.md for their primary references.
SPDX-License-Identifier: GPL-3.0-only
"""
import struct
from m68k import Assembler, hunk

MAILBOX = 0xDE0F00
PRESET_ROOT = "SYS:Prefs/Presets/Multitini/"
RESULT_FILE = "T:MultitiniScreen.result"
OSCAN_FILE = "T:MultitiniScreen.overscan"
MODES = ((640, 256, 0x29000, 4), (640, 512, 0x29004, 4),
         (724, 566, 0x29004, 4), (1280, 512, 0x29024, 2))
FLAGS = {"alive": 1, "screen": 2, "maximum": 4, "monitor": 8, "busy": 16,
         "viewport_match": 32, "superhires": 64}
S = dict(request=0, phase=2, registered=4, flags=6, receipt=8, screen=12,
         width=16, height=18, depth=20, mode=24, rect=28, monitor=36,
         want_width=132, want_height=134, want_mode=136, port=140,
         screen_width=176, screen_height=178, frozen=180, want_depth=182,
         settle=184, screen_left=186, screen_top=188, clip_left=190,
         clip_top=192, clip_width=194, clip_height=196, video_tags=200)
STATE_BYTES = 216


def chunk(name, payload):
    return name + struct.pack(">I", len(payload)) + payload + bytes(len(payload) & 1)


def prefs(name, payload):
    body = b"PREF" + chunk(b"PRHD", bytes(6)) + chunk(name, payload)
    return b"FORM" + struct.pack(">I", len(body)) + body


def screenmode(width, height, mode, depth=4):
    return prefs(b"SCRM", bytes(16) + struct.pack(">IHHHH", mode, width, height, depth, 0))


def job(mode, save):
    """Shell command reports completion after the preferences utility returns.

    The daemon is the only owner of this RAM receipt, and never starts another
    command until it consumes the complete terminal two-byte record. No stale command can complete a later request.
    """
    verb = "SAVE" if save else "USE"
    tools = [f'SYS:Prefs/Overscan FROM "{OSCAN_FILE}" {verb}']
    tools.append(f'SYS:Prefs/ScreenMode FROM "{PRESET_ROOT}{mode}.prefs" {verb}')
    lines = ["; Original Multitini preferences transaction; SPDX-License-Identifier: GPL-3.0-only"]
    for command in tools:
        lines += [command, "IF WARN", f'Echo "3" >"{RESULT_FILE}"', "QUIT 20", "ENDIF"]
    lines += [f'Echo "0" >"{RESULT_FILE}"', "QUIT 0", ""]
    return "\n".join(lines).encode("ascii")


def build():
    a = Assembler()
    i = a.ins
    lab = a.label
    st = lambda name: f"{S[name]}(a5)"
    mb = lambda offset: f"0x{MAILBOX + offset:x}.l"
    call = lambda bias: i("jsr", f"{-bias}(a6)")

    lab("entry")
    i("save")
    i("lea", "state(pc)", "a5")
    for reg in ("a2", "a3", "a4"):
        i("move.l", "#0", reg)
    i("cmpi.w", "#0x4d54", mb(0))
    i("bne", "unavailable")
    i("cmpi.w", "#1", mb(2))
    i("bne", "unavailable")
    # A public ignored-message port provides an atomic singleton. No signal
    # bit is allocated, and Forbid covers only the find/add pair.
    i("move.l", "4.w", "a6")
    call(132)
    i("lea", "portname(pc)", "a1")
    call(390)
    i("tst.l", "d0")
    i("bne", "already_running")
    i("lea", f"{S['port']}(a5)", "a1")
    i("move.b", "#4", "8(a1)")  # NT_MSGPORT
    i("lea", "portname(pc)", "a0")
    i("move.l", "a0", "10(a1)")
    i("move.b", "#2", "14(a1)")  # PA_IGNORE
    i("lea", f"{S['port'] + 24}(a5)", "a0")
    i("move.l", "a0", "20(a1)")
    i("lea", f"{S['port'] + 20}(a5)", "a0")
    i("move.l", "a0", "28(a1)")
    call(354)
    i("move.w", "#1", st("registered"))
    call(138)
    for name, reg in (("dosname", "a4"), ("intname", "a3"), ("gfxname", "a2")):
        i("lea", name + "(pc)", "a1")
        i("moveq", "#37", "d0")
        call(552)
        i("move.l", "d0", reg)
        i("tst.l", "d0")
        i("beq", "unavailable")
    # Ignore a request left by an earlier guest/daemon incarnation. ARM
    # sends fresh sequences only after observing our advancing heartbeat.
    i("move.w", mb(4), st("request"))
    i("move.w", mb(6), "d0")
    i("andi.w", "#0xfffc", "d0")
    i("beq", "poll")
    i("move.w", "#1", st("frozen"))
    lab("poll")
    i("cmpi.w", "#0x4d54", mb(0))
    i("bne", "unavailable")
    i("cmpi.w", "#1", mb(2))
    i("bne", "unavailable")
    i("addq.w", "#1", mb(0x0a))
    # Preferences can hold Intuition locks while displaying a requester.
    # A child with no terminal receipt needs only DOS polling and heartbeat;
    # never query a GUI library until that child has returned.
    i("move.w", st("phase"), "d0")
    for phase in (1, 2):
        i("cmpi.w", f"#{phase}", "d0")
        i("beq", "child_pending")
    for phase in (3, 4):
        i("cmpi.w", f"#{phase}", "d0")
        i("beq", "verify_pending")
    # Once ACK commits a result, its observations are an immutable snapshot.
    # Startup and in-flight transactions may still query the changing screen.
    i("tst.w", st("frozen"))
    i("beq", "observe")
    i("tst.w", st("phase"))
    i("bne", "observe")
    i("move.w", mb(4), "d0")
    i("move.w", "d0", "d1")
    i("andi.w", "#0xfffc", "d1")
    i("beq", "observed")
    i("cmp.w", st("request"), "d0")
    i("beq", "observed")
    lab("observe")
    i("bsr", "query_screen")
    lab("observed")
    i("tst.w", st("phase"))
    i("bne", "child_pending")
    # Ctrl-C is honored only when no child remains able to write a receipt.
    i("move.l", "4.w", "a6")
    i("moveq", "#0", "d0")
    i("move.l", "#0x1000", "d1")
    call(306)
    i("andi.l", "#0x1000", "d0")
    i("bne", "clean_exit")
    i("move.w", mb(4), "d6")
    i("move.w", "d6", "d0")
    i("andi.w", "#0xfffc", "d0")
    i("beq", "sleep")
    i("cmp.w", st("request"), "d6")
    i("beq", "sleep")
    # User-Startup can launch us before LoadWB. Do not consume its first
    # request until a real Workbench public screen exists; retry next poll.
    i("move.w", st("flags"), "d0")
    i("andi.w", "#2", "d0")
    i("beq", "sleep")
    i("move.w", "d6", st("request"))
    i("clr.w", st("frozen"))
    i("move.w", "d6", "d7")
    i("andi.w", "#3", "d7")
    for mode, (width, height, mode_id, depth) in enumerate(MODES):
        i("cmpi.w", f"#{mode}", "d7")
        i("beq", f"select_{mode}")
    i("bra", "unsupported")
    for mode, (width, height, mode_id, depth) in enumerate(MODES):
        lab(f"select_{mode}")
        i("move.w", f"#{width}", st("want_width"))
        i("move.w", f"#{height}", st("want_height"))
        i("move.l", f"#{mode_id}", st("want_mode"))
        i("move.w", f"#{depth}", st("want_depth"))
        i("bra", "selected")
    lab("selected")
    i("move.l", "a2", "a6")
    i("move.l", st("want_mode"), "d0")
    call(798)  # ModeNotAvailable
    i("tst.l", "d0")
    i("bne", "unsupported")
    i("bsr", "write_overscan")
    i("cmpi.l", "#2", "d0")
    i("beq", "unsupported")
    i("tst.l", "d0")
    i("bne", "prefs_failed")
    lab("apply")
    i("move.w", "#1", st("phase"))
    i("bsr", "launch_child")
    i("tst.l", "d0")
    i("bne", "prefs_failed")
    i("bra", "sleep")

    lab("child_pending")
    i("bsr", "read_receipt")
    i("cmpi.l", "#-1", "d0")
    i("beq", "sleep")
    # A newer request never receives the old child's acknowledgement or save.
    i("move.w", st("request"), "d6")
    i("cmp.w", mb(4), "d6")
    i("bne", "abandon")
    i("tst.l", "d0")
    i("bne", "prefs_failed")
    # The receipt can arrive after an earlier screen observation. Separate
    # child completion from fresh verification, including asynchronous GUI
    # publication after the preferences command has returned.
    i("addq.w", "#2", st("phase"))  # 1/2 -> 3/4: verify USE/SAVE.
    i("clr.w", st("settle"))
    lab("verify_pending")
    i("move.w", st("request"), "d6")
    i("cmp.w", mb(4), "d6")
    i("bne", "abandon")
    i("bsr", "query_screen")
    i("bsr", "verify_screen")
    i("tst.l", "d0")
    i("bne", "verify_settling")
    # A GUI query may block while ARM supersedes the request. Recheck after
    # returning, before allowing the old transaction to persist anything.
    i("move.w", st("request"), "d6")
    i("cmp.w", mb(4), "d6")
    i("bne", "abandon")
    i("cmpi.w", "#4", st("phase"))
    i("beq", "success")
    i("move.w", "#2", st("phase"))
    i("bsr", "launch_child")
    i("tst.l", "d0")
    i("bne", "prefs_failed")
    i("bra", "sleep")
    lab("verify_settling")
    i("addq.w", "#1", st("settle"))
    i("cmpi.w", "#10", st("settle"))
    i("bcs", "sleep")
    i("bra", "mismatch")
    lab("success")
    i("moveq", "#0", "d0")
    i("bra", "acknowledge")
    lab("unsupported")
    i("moveq", "#2", "d0")
    i("bra", "acknowledge")
    lab("prefs_failed")
    i("moveq", "#3", "d0")
    i("bra", "acknowledge")
    lab("mismatch")
    i("moveq", "#4", "d0")
    lab("acknowledge")
    i("move.w", st("request"), "d6")
    i("cmp.w", mb(4), "d6")
    i("bne", "abandon")
    i("move.w", "d0", mb(8))
    i("clr.w", st("phase"))
    i("andi.w", "#0xffef", st("flags"))
    i("move.w", st("flags"), mb(0x1e))
    i("move.w", "#1", st("frozen"))
    i("move.w", "d6", mb(6))  # Commit LAST, after observations/result/flags.
    i("bra", "sleep")
    lab("abandon")
    i("clr.w", st("phase"))
    lab("sleep")
    i("move.l", "a4", "a6")
    i("moveq", "#50", "d1")
    call(198)
    i("bra", "poll")

    lab("query_screen")
    i("clr.w", st("clip_width"))
    i("clr.w", st("clip_height"))
    i("move.w", f"#{FLAGS['alive'] | FLAGS['superhires']}", st("flags"))
    i("tst.w", st("phase"))
    i("beq", "query_lock")
    i("ori.w", "#16", st("flags"))
    lab("query_lock")
    i("move.l", "a3", "a6")
    # LockPubScreen("Workbench") creates Workbench when it is absent. A
    # User-Startup daemon must first observe an existing public entry so it
    # cannot open the screen before LoadWB. Copy only a presence boolean;
    # release the list before calling any screen function or DOS wait.
    i("moveq", "#0", "d2")
    call(522)  # LockPubScreenList
    i("tst.l", "d0")
    i("beq", "query_done")
    i("move.l", "d0", "a1")
    i("move.l", "(a1)", "a1")  # List.lh_Head
    lab("query_public_node")
    i("move.l", "(a1)", "d0")  # A zero successor identifies the tail sentinel.
    i("beq", "query_public_done")
    i("move.l", "10(a1)", "a0")  # Node.ln_Name, not PubScreenNode.psn_Screen.
    i("move.l", "a0", "d0")
    i("beq", "query_public_next")
    for offset, value in enumerate(b"Workbench\0"):
        i("cmpi.b", f"#{value}", f"{offset}(a0)")
        i("bne", "query_public_next")
    i("move.w", "18(a1)", "d0")  # PubScreenNode.psn_Flags
    i("andi.w", "#1", "d0")  # PSNF_PRIVATE is not ready for visitors.
    i("bne", "query_public_next")
    i("moveq", "#1", "d2")
    i("bra", "query_public_done")
    lab("query_public_next")
    i("move.l", "(a1)", "a1")
    i("bra", "query_public_node")
    lab("query_public_done")
    call(528)  # UnlockPubScreenList; never hold it across LockPubScreen.
    i("tst.l", "d2")
    i("beq", "query_done")
    i("lea", "wbname(pc)", "a0")
    call(510)
    i("move.l", "d0", st("screen"))
    i("tst.l", "d0")
    i("beq", "query_done")
    i("move.l", "d0", "a0")
    i("move.w", "8(a0)", st("screen_left"))
    i("move.w", "10(a0)", st("screen_top"))
    i("move.w", "12(a0)", st("screen_width"))
    i("move.w", "14(a0)", st("screen_height"))
    # Public Screen viewport dimensions exclude panned virtual bitmap space.
    for offset, name, target in ((68, "width", 0x0c), (70, "height", 0x0e)):
        i("move.w", f"{offset}(a0)", st(name))
        i("move.w", st(name), mb(target))
    i("move.l", "88(a0)", "a1")  # Screen.RastPort.BitMap
    i("moveq", "#0", "d0")
    i("move.b", "5(a1)", "d0")
    i("move.w", "d0", st("depth"))
    i("move.w", "d0", mb(0x10))
    i("move.l", st("screen"), "a0")
    i("lea", "44(a0)", "a0")  # Screen.ViewPort
    i("move.l", "a2", "a6")
    call(792)
    i("move.l", "d0", st("mode"))
    i("move.w", "d0", mb(0x12))
    i("swap", "d0")
    i("move.w", "d0", mb(0x14))
    # VideoControl returns the actual ViewPortExtra display clip. The GET
    # tag is rewritten to SET by the API, so rearm every query. Copy only
    # while the public screen is locked; the returned VPE remains OS-owned.
    i("move.l", st("screen"), "a0")
    i("move.l", "48(a0)", "a0")  # Screen.ViewPort.ColorMap
    i("move.l", "a0", "d0")
    i("beq", "query_clip_done")
    i("lea", st("video_tags"), "a1")
    i("move.l", "#0x80000013", "(a1)")  # VTAG_VIEWPORTEXTRA_GET
    for offset in (4, 8, 12):
        i("clr.l", f"{offset}(a1)")
    i("move.l", "a2", "a6")
    call(708)
    i("tst.l", "d0")
    i("bne", "query_clip_done")
    i("move.l", f"{S['video_tags'] + 4}(a5)", "a0")
    i("move.l", "a0", "d0")
    i("beq", "query_clip_done")
    for offset, name in ((28, "clip_left"), (30, "clip_top")):
        i("move.w", f"{offset}(a0)", st(name))
    for offset, minimum, size in ((32, "clip_left", "clip_width"),
                                  (34, "clip_top", "clip_height")):
        i("move.w", f"{offset}(a0)", "d0")
        i("cmp.w", st(minimum), "d0")
        i("blt", "query_clip_done")
        i("sub.w", st(minimum), "d0")
        i("addq.w", "#1", "d0")
        i("move.w", "d0", st(size))
    lab("query_clip_done")
    i("move.l", "a3", "a6")
    i("lea", "wbname(pc)", "a0")
    i("move.l", st("screen"), "a1")
    call(516)
    i("ori.w", "#2", st("flags"))
    for actual, other in (("width", "screen_width"), ("height", "screen_height"),
                          ("width", "want_width"), ("height", "want_height"),
                          ("width", "clip_width"), ("height", "clip_height"),
                          ("screen_left", "clip_left"), ("screen_top", "clip_top")):
        i("move.w", st(actual), "d0")
        i("cmp.w", st(other), "d0")
        i("bne", "viewport_checked")
    i("ori.w", "#32", st("flags"))
    lab("viewport_checked")
    i("move.l", st("mode"), "d2")
    i("bsr", "query_limits")
    lab("query_done")
    i("move.w", st("flags"), mb(0x1e))
    i("rts")

    lab("query_limits")
    i("andi.w", "#0xfff3", st("flags"))
    i("move.l", "a3", "a6")
    i("move.l", "d2", "a0")
    i("lea", st("rect"), "a1")
    i("moveq", "#3", "d0")
    call(474)  # QueryOverscan OSCAN_MAX in the selected mode's units.
    i("tst.l", "d0")
    i("beq", "limits_monitor")
    for offset in range(0, 8, 2):
        i("move.w", f"{S['rect'] + offset}(a5)", mb(0x16 + offset))
    i("ori.w", "#4", st("flags"))
    lab("limits_monitor")
    i("move.l", "a2", "a6")
    i("move.l", "#0", "a0")
    i("lea", st("monitor"), "a1")
    i("moveq", "#96", "d0")
    i("move.l", "#0x80002000", "d1")  # DTAG_MNTR
    call(756)
    i("cmpi.l", "#24", "d0")
    i("bcs", "limits_done")
    i("move.w", f"{S['monitor'] + 20}(a5)", mb(0x20))
    i("move.w", f"{S['monitor'] + 22}(a5)", mb(0x22))
    i("ori.w", "#8", st("flags"))
    lab("limits_done")
    i("rts")

    lab("verify_screen")
    i("move.w", st("flags"), "d0")
    i("andi.w", "#47", "d0")
    i("cmpi.w", "#47", "d0")
    i("bne", "verify_bad")
    for name, wanted in (("width", "want_width"), ("height", "want_height"),
                         ("mode", "want_mode"), ("depth", "want_depth")):
        size = "l" if name == "mode" else "w"
        i("move." + size, st(name), "d0")
        i("cmp." + size, st(wanted), "d0")
        i("bne", "verify_bad")
    i("moveq", "#0", "d0")
    i("rts")
    lab("verify_bad")
    i("moveq", "#1", "d0")
    i("rts")

    lab("launch_child")
    i("move.l", "a4", "a6")
    i("lea", "receiptname(pc)", "a0")
    i("move.l", "a0", "d1")
    call(72)
    i("tst.l", "d0")
    i("bne", "receipt_removed")
    call(132)  # IoErr: absence is safe, other deletion failures are not.
    i("cmpi.l", "#205", "d0")
    i("bne", "launch_failed")
    lab("receipt_removed")
    for mode in range(len(MODES)):
        i("cmpi.w", f"#{mode}", "d7")
        i("beq", f"launch_{mode}")
    i("bra", "launch_failed")
    for mode in range(len(MODES)):
        lab(f"launch_{mode}")
        i("lea", f"apply{mode}(pc)", "a0")
        i("cmpi.w", "#1", st("phase"))
        i("beq", "launch_selected")
        i("lea", f"save{mode}(pc)", "a0")
        i("bra", "launch_selected")
    lab("launch_selected")
    i("move.l", "a0", "d5")  # Command survives Open's scratch registers.
    i("lea", "nilname(pc)", "a0")
    i("move.l", "a0", "d1")
    i("move.l", "#1006", "d2")  # MODE_NEWFILE: this launch owns the NIL output.
    call(30)
    i("tst.l", "d0")
    i("beq", "launch_failed")
    i("move.l", "d0", "d4")
    # Execute waits, so publish BUSY before it can display a GUI requester.
    # Zero input is documented by Execute; no parent streams are transferred.
    i("move.w", "#1", mb(8))
    i("ori.w", "#16", st("flags"))
    i("move.w", st("flags"), mb(0x1e))
    i("move.l", "d5", "d1")
    i("moveq", "#0", "d2")
    i("move.l", "d4", "d3")
    call(222)  # Execute(command, 0, owned_output), BOOL, not command return code.
    i("move.l", "d0", "d5")
    i("move.l", "d4", "d1")
    call(36)  # Close exactly once, whether Execute returned TRUE or FALSE.
    i("tst.l", "d0")
    i("beq", "launch_failed")
    i("tst.l", "d5")
    i("beq", "launch_failed")
    i("moveq", "#0", "d0")
    i("rts")
    lab("launch_failed")
    i("moveq", "#3", "d0")
    i("rts")

    lab("read_receipt")
    i("move.l", "a4", "a6")
    i("lea", "receiptname(pc)", "a0")
    i("move.l", "a0", "d1")
    i("move.l", "#1005", "d2")  # MODE_OLDFILE
    call(30)
    i("tst.l", "d0")
    i("beq", "receipt_pending")
    i("move.l", "d0", "d4")
    i("move.l", "d4", "d1")
    i("lea", st("receipt"), "a0")
    i("move.l", "a0", "d2")
    i("moveq", "#4", "d3")
    call(42)
    i("move.l", "d0", "d5")
    i("move.l", "d4", "d1")
    call(36)
    i("tst.l", "d5")
    i("bmi", "receipt_failed")
    i("cmpi.l", "#2", "d5")
    i("bcs", "receipt_pending")
    i("bne", "receipt_failed")
    i("cmpi.b", "#10", f"{S['receipt'] + 1}(a5)")
    i("bne", "receipt_failed")
    i("cmpi.b", "#48", st("receipt"))
    i("bne", "receipt_failed")
    i("moveq", "#0", "d0")
    i("rts")
    lab("receipt_failed")
    i("moveq", "#3", "d0")
    i("rts")
    lab("receipt_pending")
    i("moveq", "#-1", "d0")
    i("rts")

    lab("write_overscan")
    # PAL/NTSC OSCN preferences use standard HiRes NONLACE coordinates.
    # Query that mode directly, then explicitly convert even HiRes X origins
    # to whole View clocks; nonlace Y uses the same unit as ViewPosition.
    i("move.l", "#0x29000", "d2")
    i("bsr", "query_limits")
    i("cmpi.l", "#84", "d0")  # DefaultViewPosition ends at byte83.
    i("bcs", "overscan_unsupported")
    i("move.w", st("flags"), "d0")
    i("andi.w", "#12", "d0")
    i("cmpi.w", "#12", "d0")
    i("bne", "overscan_unsupported")
    i("lea", "oscn(pc)", "a0")
    i("move.l", "#0x29000", "50(a0)")
    i("cmpi.w", "#2", "d7")
    i("beq", "overscan_maximum")
    # Standard PAL HiRes is our fixed640x256 profile. TEXT/STANDARD queries
    # are mutable after a maximum request, so restore the original monitor
    # position, not the currently applied maximum position.
    for axis in (0, 2):
        i("move.w", f"{S['monitor'] + 80 + axis}(a5)", "d0")
        i("cmp.w", f"{S['monitor'] + 28 + axis}(a5)", "d0")
        i("blt", "overscan_unsupported")
        i("cmp.w", f"{S['monitor'] + 32 + axis}(a5)", "d0")
        i("bgt", "overscan_unsupported")
        i("move.w", "d0", f"{54 + axis}(a0)")
    for offset, value in ((58, 640), (60, 256), (62, 0), (64, 0), (66, 639), (68, 255)):
        i("move.w", f"#{value}", f"{offset}(a0)")
    i("bra", "overscan_record_ready")
    lab("overscan_maximum")
    for hi, lo, expected in ((4, 0, 724), (6, 2, 283)):
        i("move.w", f"{S['rect'] + hi}(a5)", "d0")
        i("sub.w", f"{S['rect'] + lo}(a5)", "d0")
        i("addq.w", "#1", "d0")
        i("cmpi.w", f"#{expected}", "d0")
        i("bne", "overscan_unsupported")
    i("move.w", st("rect"), "d0")
    i("andi.w", "#1", "d0")
    i("bne", "overscan_unsupported")  # HiRes X must map to whole View clocks.
    for axis in (0, 2):
        i("move.w", f"{S['monitor'] + 20 + axis}(a5)", "d0")
        i("move.w", f"{S['rect'] + axis}(a5)", "d1")
        if axis == 0:
            i("asr.w", "#1", "d1")
        i("add.w", "d1", "d0")
        i("bvs", "overscan_unsupported")
        i("cmp.w", f"{S['monitor'] + 28 + axis}(a5)", "d0")
        i("blt", "overscan_unsupported")
        i("cmp.w", f"{S['monitor'] + 32 + axis}(a5)", "d0")
        i("bgt", "overscan_unsupported")
        i("move.w", "d0", f"{54 + axis}(a0)")
    # A negative Standard origin clips the bitmap at MaxX/MaxY, even when
    # its rectangle's inclusive dimensions are correct. Normalize the bitmap
    # clip and compensate ViewPosition using the queried origin above.
    for offset, value in ((58, 724), (60, 283), (62, 0), (64, 0), (66, 723), (68, 282)):
        i("move.w", f"#{value}", f"{offset}(a0)")
    lab("overscan_record_ready")
    i("move.l", "a4", "a6")
    i("lea", "overscanname(pc)", "a0")
    i("move.l", "a0", "d1")
    i("move.l", "#1006", "d2")
    call(30)
    i("tst.l", "d0")
    i("beq", "overscan_failed")
    i("move.l", "d0", "d4")
    i("move.l", "d4", "d1")
    i("lea", "oscn(pc)", "a0")
    i("move.l", "a0", "d2")
    i("moveq", "#70", "d3")
    call(48)
    i("move.l", "d0", "d5")
    i("move.l", "d4", "d1")
    call(36)
    i("tst.l", "d0")
    i("beq", "overscan_failed")
    i("cmpi.l", "#70", "d5")
    i("bne", "overscan_failed")
    i("moveq", "#0", "d0")
    i("rts")
    lab("overscan_unsupported")
    i("moveq", "#2", "d0")
    i("rts")
    lab("overscan_failed")
    i("moveq", "#3", "d0")
    i("rts")

    lab("already_running")
    call(138)
    i("moveq", "#0", "d0")
    i("restore")
    i("rts")
    lab("unavailable")
    i("moveq", "#20", "d7")
    i("bra", "close_libraries")
    lab("clean_exit")
    i("moveq", "#0", "d7")
    lab("close_libraries")
    i("move.l", "4.w", "a6")
    for reg in ("a2", "a3", "a4"):
        i("move.l", reg, "d0")
        i("beq", "close_" + reg)
        i("move.l", reg, "a1")
        call(414)
        lab("close_" + reg)
    i("tst.w", st("registered"))
    i("beq", "return")
    i("lea", st("port"), "a1")
    call(360)
    lab("return")
    i("move.l", "d7", "d0")
    i("restore")
    i("rts")
    instruction_end = a.pc
    for name, text in (("portname", "MultitiniScreen.v1"), ("dosname", "dos.library"),
                       ("intname", "intuition.library"), ("gfxname", "graphics.library"),
                       ("wbname", "Workbench"), ("nilname", "NIL:"), ("receiptname", RESULT_FILE),
                       ("overscanname", OSCAN_FILE)):
        lab(name)
        a.data(text.encode("ascii") + b"\0")
    for mode in range(len(MODES)):
        for action in ("apply", "save"):
            lab(action + str(mode))
            a.data(f'Execute "{PRESET_ROOT}{action}{mode}"'.encode("ascii") + b"\0")
    a.align()
    lab("oscn")
    a.data(prefs(b"OSCN", bytes(36)))
    a.align()
    lab("state")
    a.data(bytes(STATE_BYTES))
    code = a.finish()
    return dict(hunk=hunk(code), code=code, assembler=a, instruction_end=instruction_end)
