# Vectrex overlays

Implementation baseline: 2026-09-15. The user accepted VEC3 on the board;
overlay changes require separate validation and a new Vectrex image.

## Format and content

Support the native MiSTer `.ovr` format: headerless 540 by 720 pixels,
top-left row order, two bytes per pixel with R/G/B/A four-bit channels
(little-endian word `0xABGR`). Preserve the full artwork resolution and the
existing 810 by 1080 game viewport. Load a same-stem sidecar beside a cartridge
or BASIC program, including nested directories. Permit manual overlay loading.
Clear previous artwork when replacing content without a usable sidecar.
Overlay failures must not prevent the selected game from running.

Four files in the pinned MiSTer collection are short. Accept even, nonempty
payloads up to 777600 bytes and fill the unused tail with transparent black.
The headerless format cannot express alternative dimensions. Reject larger or
odd-sized files. Keep external artwork out of the source and firmware package
until per-file redistribution rights are established.

## Storage and display

Use the existing dual PSRAM hardware with a Vectrex-specific burst reader.
Read 64 logical bytes (32 bytes per chip) without crossing a wrap boundary;
pad each stored row to 1088 bytes. Three row buffers absorb the alternating
one/two scan-line intervals of the existing 3:2 scaler. Validate sustained
bandwidth with a pin-level memory model, then synthesize for resource fit.
Keep the native vector intensity framebuffer and beam timing unchanged.

Composite dim artwork with brighter colored vectors. Provide Vectrex-only
Video controls for overlay enable, artwork opacity (25/50/75/100 percent),
and beam gain (1/2/3/4 times, saturating). Default to overlays enabled,
50 percent artwork opacity, and twice the beam intensity. Latch settings at
vertical blank through a coherent clock crossing.

## Interface and persistence

ABI 0.13 adds `VECTREX_VIDEO_CONFIG` at 0xC4 and read-only
`VECTREX_OVERLAY_STATUS` at 0xC8. Stream kind 0x45 carries a complete padded
overlay or one zero byte to clear it. Validate framing, epochs and aborts;
display artwork only after successful completion. Preserve it across a soft
machine reset. Older cores must never receive these operations.

Settings schema 4 adds three per-profile fields and preserves schema 1, 2,
and deployed schema 3 settings. Save settings before writing hardware.

## Validation and delivery gates

1. Prove channel order, sidecar paths, malformed files, replacement semantics,
   settings migration, register strobes and old-core capability guards.
2. Simulate PSRAM commands, row deadlines, clock crossings, aborts and frame
   boundaries; compare actual artwork compositing captures with source data.
3. Run existing Vectrex transport, renderer, game and Vectrex32 checks.
4. Build and inspect FPGA resource, clock, reset, CDC, setup, hold, pulse-width,
   routing and bus-skew reports. Do not weaken existing signoff requirements.
5. Build firmware, package only explicitly selected images, verify source and
   binary provenance, and deliver a test update. Physical overlay validation
   remains pending until tested on the board.

Reference: [MiSTer Vectrex at 945088b](https://github.com/MiSTer-devel/Vectrex_MiSTer/tree/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b).
