# Fast disk ROM pair

Patched C64 KERNAL 901227-03 and 1541-II DOS 251968-03 that load through the
parallel drive cable of the fast ROM profile. Design, protocol and measured
results: [docs/C64_FAST_DISK.md](../docs/C64_FAST_DISK.md).

| File | Content |
|---|---|
| `protocol.inc` | constants shared by all three sources |
| `kernal_fast.a` | KERNAL overlay: tape/RS-232 stubs, cable transfer code, engine image |
| `dos_fast.a` | DOS overlay: detection handshake, plain-mode hooks |
| `engine.a` | drive engine (track-at-once reader, framed sender), runs from the drive RAM expansion |
| `build.py` | assembles the three with ACME, checks the changed byte ranges, writes `bin/` |
| `bin/kernal.bin`, `bin/1541.bin` | the images to copy to `roms/fast/` on the card |
| `bin/engine.bin`, `bin/PROVENANCE.json` | engine image and build record |

```
python c64fast/build.py            # rebuild bin/
python c64fast/build.py --check    # compare a fresh build with bin/
python -m unittest tests.test_c64fast
```

ACME 0.97 must be on the PATH (or in `~/tools/acme`). Intermediate files go
to `build/`, which is ignored by git.
