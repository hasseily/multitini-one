# Doom GS compatibility diagnosis

The owner reported a frozen Doom GS title screen on IIgs FPGA `4D000609`.
F12 still opened the Multitini menu; game audio was silent or stuck. UART
reported ROM 3, ZipGS speed code 6 (57.27 MHz), no core/media fault and no
pending media request. Frame capture continued advancing. The mounted image
was named `doom-hd.hdv`; its bytes were not read back from the live board.
The serial connection was released after collecting these observations.

## Cause and correction

Doom GS enables `SHADOW.IOLC` and installs its native IRQ handler at `$00:DD00`
through the RAM vector at `$00:FFEE`. The imported MMU instead forced IRQ
vector reads to ROM regardless of IOLC. ROM 3 supplies `$C074`; with IOLC
inhibited, the CPU then fetches instructions from RAM at `$00:C074`, not the
ROM dispatch stub. The game can leave its own execution path on the first
sound or ADB interrupt.

The generated Multitini MMU port gates that ROM override with `!shadow[6]`.
Normal language-card mapping retains ROM vectors; IOLC inhibit exposes RAM
vectors. The pinned upstream source remains unchanged. Vivado and simulation
both select the generated port, reproduced by `port_apple_iigs_core.py`.

This agrees with MAME's
[Apple IIgs vector callback](https://github.com/mamedev/mame/blob/master/src/mame/apple/apple2gs.cpp#L608)
and GSSquared's `MMU_IIgs::vp_read`. Doom's interrupt setup is in
[irq65.s](https://github.com/Webifi/iigs-doom/blob/e645893f3d1ac6826e560e0ed3b28e0062c33995/src/iigs/irq65.s#L482).

## Empty keyboard-poll completion

Doom periodically issues a raw keyboard TALK after mouse reports, even when
no key is waiting. The imported ADB implementation sent no response for an
empty keyboard TALK. Doom retained its outstanding-request flag; a later
keyboard service request could then trap its polling loop.

An empty keyboard TALK now publishes completion header `$80`, with no payload.
The header participates in `$C027` data-valid and enabled data interrupts.
Acknowledgement happens after the `$C026` read strobe falls, keeping the reply
stable throughout the read and preserving it across unrelated status reads.
This follows [KEGS's keyboard TALK and host status implementation](https://github.com/a2kegs/kegs/blob/main/src/adb.c).

The isolated regression checks empty replies, held read strobes, nonempty
keyboard packets, absent device addresses, and Doom's polling decisions after
four mouse reports followed by a key. Its Doom polling loop is a model of the
game's relevant decisions, not a full game execution.

## Evidence

The original MMU reproduces the wrong vector in a synthetic program executed
by the production 65816, adapter and cache. The corrected port passes eight
cases: stock and maximum speed, native and emulation IRQ entry, and normal
language-card mapping versus IOLC inhibit. Both vector bytes and the executed
handler's completion marker are checked. The original MMU remains an explicit
negative control.

```powershell
python scripts/test_apple_iigs_iolc_vectors.py
python scripts/test_apple_iigs_iolc_vectors.py --upstream-negative-control --build-dir build/apple-iigs-iolc-vectors-original
python scripts/test_apple_iigs_adb_empty_talk.py
python scripts/test_apple_iigs_cpu_peek.py
python scripts/test_apple_iigs_logical_cache.py
python scripts/port_apple_iigs_core.py --check
python scripts/audit_apple_iigs_import.py
```

An isolated DOC test also reproduced Doom's ramp upload and consecutive
data-register reads through the real CPU at speed codes 0, 3 and 6. Oscillator
31 advanced at the expected game-clock rate. This excludes the basic DOC
timer path in that workload; it does not establish full game compatibility.

Full-game diagnosis uses the official v1.5 release image (2,658,304 bytes),
SHA-256 `917212c62acf08d4ff27752e44a97543b8e142a1587e26dc0ee3f22e0053a9e5`.
Private ROMs and the game image remain outside source control.

The correction requires an IIgs FPGA rebuild. ARM firmware, slot firmware
and the other machine images do not change. Physical-board verification of
the corrected image remains pending.
