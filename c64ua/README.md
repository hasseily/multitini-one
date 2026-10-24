# C64 audio board probes

These original GPL-3.0-only probes use synthetic audio. They contain no ROM,
game, demo, or third-party program bytes. Assemble with ACME 0.97:

```
python c64ua/build.py
python c64ua/build.py --check
python -m unittest tests.test_c64ua
```

Outputs and their source/output hashes are in `build/c64ua/`. Assembly is
automated; board execution remains a separate acceptance step.

- `detect.prg` uses the common 128-status-read detection rule. After resetting
  the C64, run with UA on and off, Turbo Control Off and all manual indices,
  badline timing on/off, and the screen on/off. Its stale-length behavior is
  intentional: `POKE 57131,255` before rerunning without reset exposes the
  shorter turbo polling budget. Reset clears this byte.
- `background-48k.prg` stashes seven generated tones at REU offsets
  `$F00000-$F06FFF`, starts seven 16-bit interleaved looping voices at rate 130,
  and returns to BASIC. `background-cap.prg` uses rate 0 to stress the fetch cap;
  its underruns are expected. Both overwrite C64 RAM `$3000-$9FFF` and that REU
  range. Choose the 16 MB REU and turn Ultimate Audio on before loading them.
- `period.prg` and `period16.prg` install an IRQ handler chained through the KERNAL and print
  six hexadecimal CIA2 timer-B intervals for rates 1, 130, 281, 283, 781 and
  65535. Values include I/O/IRQ latency; compare native and turbo runs. It stops
  timer B and restores the previous IRQ vector afterward. Reset before each
  run and use ordinary BASIC, with no competing CIA2 timer-B user.
  The first probe uses 8-bit samples and the second uses 16-bit samples.
- `stress.prg` starts the same seven voices and runs 256 rounds of REU stash,
  fetch, swap and verify, followed by fixed-address stash/fetch checks. It
  overwrites RAM `$2000-$21FF` and REU `$E00000-$E000FF` in addition to the
  background program ranges. PASS means data comparisons passed; compare
  the UART readiness and audio counters separately. Voices remain running.

For Expand with audio traffic, load the background program first and run it.
Use `:debug on`, `:c64diag on`, and `:uastat` to record a baseline once all seven
voices play. Insert the demo using Drive 8, then LOAD/RUN from BASIC. Multitini
PRG loading or Mount & Run resets the C64 and would stop the voices. Record
`:uastat` afterward: playing must remain `$7F`, fetches must advance, fault must
remain clear, and rate-130 underruns must not advance after the baseline.

For preload, use `synthetic-1m.reu` with Load into REU or
`:reuload "path/synthetic-1m.reu"`. Repeat with audio on and off. After the load,
voices must be idle and fault clear. Compare `:reudump` with the source bytes.
For adjacent-byte coverage, first fill the destination and neighboring bytes,
then preload an odd-length slice at an unaligned offset; only that range may
change. This procedure requires physical board execution and is not claimed
by the assembly receipt.
