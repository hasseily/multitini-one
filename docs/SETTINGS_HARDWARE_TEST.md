# Supervisor Settings Hardware Test

This procedure qualifies the settings and supervisor-only boot source. It
requires a fresh supervisor PL implementation and matching firmware package;
the existing C64 and GBC SD packages remain independently loadable.

## Default-off gate

1. Remove `multitini/settings.bin` and `multitini/settings.bin.mtj` from a test
   card.
2. Boot the normal `FIRMWARE.BIN` and confirm no core is active and the
   `MULTITINI` / `Press F12` standby screen appears.
3. Open F12, select another core, and reboot through the golden updater.
4. Confirm no core loads and the standby screen returns. Selecting a core must
   not enable Autoboot.

## Explicit auto-boot gate

1. Select the desired core and timing variant.
2. Open System and set `Autoboot current core` to `On`.
3. Reboot through the golden updater and confirm that core and variant load.
4. Switch manually to another core without changing its System setting, then
   reboot and confirm the original explicit auto-boot target still loads.
5. Set `Autoboot current core` to `Off` on the target, reboot again, and confirm
   no core loads and the standby screen appears.

## Per-core restore gate

1. On C64, choose a PAL or NTSC variant, mount a D64/G64 or CRT, select the
   keyboard map and joystick port, then switch to GBC.
2. Load a GBC cartridge and switch back to C64. Confirm the C64 variant, media,
   keyboard map, and joystick port return.
3. Switch back to GBC and confirm its cartridge and battery save return.
4. Eject or detach each medium, switch away and back, and confirm it remains
   detached.

## C64 fast serial ROM gate

Use only a properly licensed matching C64 KERNAL and 1541 fast serial ROM pair.
Place the exact 8 KiB KERNAL at
`multitini/cores/c64/roms/fast/kernal.bin` and the exact 16 KiB drive ROM at
`multitini/cores/c64/roms/fast/1541.bin`. Multitini does not provide these
files.

1. Boot C64 with `Fast serial ROMs` Off and confirm the unchanged stock KERNAL
   and drive ROM paths load.
2. Turn `Fast serial ROMs` On. Confirm the complete pair is validated before
   reset, both ROMs load while the core is held in reset, and the C64 returns
   with the menu showing the fast profile active.
3. Reboot and confirm the saved fast profile is restored. Turn it Off and
   confirm the complete stock profile is safely restored and remembered.
4. Save the fast profile, remove or invalidate either fast file, and reboot.
   Confirm UART and the menu report that the pair is unavailable and the stock
   profile runs. The firmware must not mix one stock ROM with one fast ROM.
5. Force a stream/start failure during a runtime profile switch. Confirm the
   previous active pair is restored and the failed selection is not persisted.
6. Exercise `CPU turbo` independently in both profiles and confirm changing one
   option does not change the other. No test should expect a faster PL drive
   clock; whole-drive warp has been removed.

## Failure gates

1. Keep a known-good copy of `settings.bin`, corrupt one byte in the card copy,
   and reboot. Confirm no core loads, Autoboot does not run, and
   the System page reports a settings read failure.
2. Restore the good file. Interrupt one settings update while the journal is
   being written, then reboot. Confirm the system either recovers the complete
   new file or uses safe defaults; it must not load an unvalidated core or
   content path.
3. Make the volume unwritable and change a non-ROM setting. Confirm the active
   core keeps running while the menu reports that the settings write failed.
4. With the volume still unwritable, attempt a C64 ROM-profile change. Confirm
   the menu reports the persistence failure and the previously active complete
   ROM pair is restored.

Capture the UART lines beginning with `[settings]` for every case. Do not call
the feature board-qualified until both default-off and explicit opt-in behavior
have passed across a complete golden reboot.
