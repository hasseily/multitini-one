# Content policy

System ROMs supplied by the user are external-only inputs. Do not add their
bytes to Git or source archives. Private local delivery packages may copy the
validated files into `sd/multitini/cores/<core>/roms/` from an explicit private
ROM collection. Record every input and output hash and any conversion. No games
or operating-system disks are included by the boot-ROM collection step.

The earlier owner-directed C64 inclusion decision is retired as of 2026-10-15.
Historical hashes, credits and hardware acceptance remain in
`provenance/c64_roms.json`, `provenance/current_release.json` and
`licenses/files.csv`; those records do not grant a new redistribution license.
The original C64 ROM bytes and fast derivatives are no longer source inputs.

The pinned MIT SameBoy bootstrap, its deterministic memory representation, and
the licensed overlay font remain redistributable source-built assets. The IIgs
HDD bootstrap is built from its GPL source. Keep their exact licenses and
provenance. These open assets are distinct from user-supplied system ROMs.

ROM preparation, fast-ROM patching and simulator tools read the private
`MULTITINI_ROM_SOURCE` collection (or explicit file arguments). Generated
private outputs stay under `build/`. `scripts/check_repo.py` rejects source
ROM binaries; there is no C64 ROM inclusion exception.
