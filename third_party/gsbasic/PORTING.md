# GS BASIC source import

This directory contains the source and documentation from `gsBasic/` in
[malbanGit/pitrex-baremetal](https://github.com/malbanGit/pitrex-baremetal/tree/457c5a98f914a812c9341c2949e9d57473ccac4a/gsBasic),
pinned to commit `457c5a98f914a812c9341c2949e9d57473ccac4a`.
`licence.txt` is the upstream MIT license, copyright Robert E. Alexander.
`UPSTREAM.json` records every imported path and its original Git blob SHA-1.
Generated PiTrex images and build directories were excluded. No game or system
ROM is included. The upstream Makefile is retained for provenance; Multitini
builds the selected language files through `firmware/vectrex32/CMakeLists.txt`.

The following files have local changes. All other imported files retain their
original bytes, including original line endings and text encoding.

| File | Change |
| --- | --- |
| BuiltInFunctions.cpp | Remove directory enumeration that bypasses the confined asset callbacks. |
| CallSpec.cpp | Read the procedure/builtin/structure union through its type discriminator when resolving a call name. |
| Executive.cpp / Executive.h | Add memory source loading and confined INCLUDE/CHAIN callbacks; bound source lines before string construction; retain include locations and duplicate suppression; poll cancellation; preserve runtime errors; clear chain state correctly; reject embedded monitor commands; avoid empty-line underflow and unsafe trace formatting. |
| Expression.cpp / Expression.h | Use pointer-width integer tags on 64-bit hosts and explicit signed 32-bit numeric encoding. |
| GSBCommon.h | Replace the MIPS breakpoint instruction with a portable compiler trap; keep MinGW's existing intrinsic. |
| GSBString.cpp / GSBString.h | Use pointer-width tags and a compiler byte-order check for little-endian builds. Check the 65535-byte string limit before allocation/copy, retain explicit lengths for short binary strings, return a valid empty C string, bound substring arithmetic, and format large floating-point values safely. |
| Statements.cpp / Statements.h | Preserve pointer width; give FOR loops unique internal names; remove an invalid indexed-variable downcast for scalar DIM declarations; explicitly reject unavailable DIR/MEM operations. |
| Token.cpp | Preserve the full built-in constant pointer when copying tokens on 64-bit hosts. |
| Value.h | Qualify the nested String member type for GCC 13. |
| vio.h | Bound console formatting and print strings with a fixed format. Use a variadic helper so macro-local buffers cannot shadow caller arguments. |
| VectrexSound.cpp / VectrexSound.h | Keep the MIT music/explosion sequencer; remove copied BIOS tune, envelope, twang and frequency assets; read caller-supplied BIOS data instead. Remove PiTrex sample/YM dependencies, fix scalar deletion and ADSR nibble order, initialize modulation state, and stop completed explosions. |

The replacement backend is original Multitini code in `firmware/vectrex32`.
The original PiTrex hardware backend is retained as reference but is not linked.
The port implements all 75 public function names listed in the Vectrex32 1.29
manual, using the pinned 1.27 language source as its base. This includes retained
2D/2.5D/3D sprites and camera transforms, live attributes and clipping, vector and
raster text, confined file operations, terminal input/output, buffered PSG writes,
music and explosions, a monotonic clock, and cooperative cancellation.
Built-in music and the compatible default music envelope/frequency table require
a caller-supplied Vectrex BIOS. No ROM tune or envelope bytes are embedded.
Raster text uses glyph bytes from that BIOS when available; otherwise the result
explicitly reports a vector-font substitution. Text2 uses the upstream MIT vector
font directly.

CodeSprite and both Peek forms use a separate software 6809 adapter with persistent
2 KiB shared RAM, machine RAM and VIA state. Its imported CPU has separate GPL
provenance under `third_party/vectrex32/e6809`; it is not an MIT dependency of
this subtree. The native drawing layout and analogue model differ from the
SmartCart. The PIC32 firmware and its peripherals are not emulated. See
[`docs/VECTREX32_API.md`](../../docs/VECTREX32_API.md) and
[`lowlevel.md`](../../firmware/vectrex32/lowlevel.md) for the exact compatibility
scope, external BIOS requirements, and unsupported monitor/peripheral behavior.

The selected compilation units are listed once in
`firmware/vectrex32/gsbasic_sources.txt`, shared by CMake and the Vitis generator.
Build with C++17, exceptions and RTTI; GNU/Clang builds use `-fwrapv` and
`-fno-strict-aliasing` for the upstream 32-bit language arithmetic and tagged pool
storage. `scripts/check_gsbasic_provenance.py` verifies all unchanged imports.
