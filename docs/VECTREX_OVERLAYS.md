# Vectrex overlay sources and controls

The firmware supports user-supplied Vectrex artwork. This repository does not
include overlay images or the upstream artwork archive.

## Source record

The format reference is
[MiSTer Vectrex](https://github.com/MiSTer-devel/Vectrex_MiSTer/tree/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b),
pinned to commit `945088b8862c09aa556f3d0c3efa07eeaa1a5b5b`.
Its [overlay README](https://github.com/MiSTer-devel/Vectrex_MiSTer/blob/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/overlays/README.md)
describes a 540 by 720 image with four-bit RGBA channels, stored as two bytes
per pixel. The first byte contains green in its upper nibble and red in its
lower nibble; the second contains alpha above blue. A complete image is
777600 bytes. The files contain no header or dimensions.

The [pinned overlay archive](https://github.com/MiSTer-devel/Vectrex_MiSTer/raw/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/overlays/overlays.zip)
can be downloaded and extracted separately. It is 2015776 bytes, with SHA-256
`2c6f6e8a1e02dcaf1a522f91a2e380ea1c4597dfa0faac379d3984e07e1b6461`.
It contains 90 `.ovr` files and no license, attribution, or readme entries.
The archive has no comment. Eighty-six files have the complete image size;
the four exceptions are:

| File | Converter dimensions | Bytes |
| --- | --- | ---: |
| Eating_Fish.ovr | 540 by 711 | 767880 |
| Laser_Wars.ovr | 460 by 551 | 506920 |
| PongOvl.ovr | 540 by 716 | 773280 |
| Stunt_Man_Stories.ovr | 540 by 718 | 775440 |

All corresponding PNGs are 540 by 720. The upstream converter takes the
right and bottom coordinates of the all-channel image bounding box as output
width and height, while starting its loops at zero. It does not crop from
the bounding box's left and top. Reproducing the historical all-channel
`getbbox()` behavior matters: current Pillow versions default to checking
alpha alone, which produces different bounds for some of these files.
The shorter files must not be mistaken for complete 540 by 720 images.
Format support and asset validation are separate from artwork rights.

The overlay README attributes three collections:

- [libretro/overlay-borders](https://github.com/libretro/overlay-borders),
  which publishes an [MIT license](https://github.com/libretro/overlay-borders/blob/master/LICENSE).
- [The Bezel Project's GCE Vectrex pack](https://github.com/thebezelproject/bezelproject-GCEVectrex),
  whose inspected repository README does not supply a redistribution grant.
- [raphkoster/vectrex-overlays](https://github.com/raphkoster/vectrex-overlays),
  whose README describes collected internet artwork and cleanup, without
  specifying license terms.

This is collection-level attribution; MiSTer does not map each converted file
to its original source and applicable notice. The MIT statement for one
collection and the GPL header on `Vectrex.sv` do not establish a uniform license
for all 90 images. Record untraced artwork as `NOASSERTION`. A future distributed
subset needs individual source records, hashes, and applicable notices.
The project's [content policy](CONTENT_POLICY.md) distinguishes distribution
decisions from findings about underlying rights. Decoder code and original
calibration fixtures can be distributed independently of this artwork.

## Saved Video controls

Vectrex adds three rows to Video: Overlay enabled, Overlay brightness
(25%, 50%, 75%, 100%), and Beam brightness (1x, 2x, 3x, 4x).
The defaults are enabled, 50%, and 2x. These controls require the active
hardware to identify itself as Vectrex and expose ABI 0.13 or later.
An older package retains the saved preferences and shows unsupported controls.

Settings schema 4 stores 252 bytes per core profile, 4088 bytes overall.
It preserves schema 1/2 files of 3960 bytes and schema 3 files of 4024 bytes,
including pending journal recovery. Older profiles receive the new defaults.
The existing border bytes remain at offsets 244 through 247. New profile
offsets are 248 for overlay enabled, 249 for the beam-gain index, 250 for the
artwork-opacity index, and 251 reserved as zero. Both indices range from 0 to 3.

Changes save the complete settings before updating the live video register.
A failed save restores the in-memory settings and leaves the live display
unchanged. A completed journal may still be recovered on the next boot after
a later storage failure; this is the existing settings transaction behavior.

The video word uses bit 0 for overlay enable, bits 2:1 for the beam-gain index,
and bits 4:3 for the artwork-opacity index. Its default is `0x0000000B`.
Artwork loaded/error status is transient and is not saved with preferences.

## Decoder and transfer contract

Pixels are top-left, row-major little-endian `0xABGR` words. There is no
palette, header, compression, or orientation metadata. The portable decoder
expands each four-bit channel by repeating its nibble. It preserves RGB even
when alpha is zero, because the display can use that color to tint the beam.
The 90 archive files contain between 15 and 2654 distinct RGBA values; only
28 fit a 256-entry palette without loss. The implementation therefore retains
the original 16-bit pixels.

The [portable helper](../firmware/include/multitini_vectrex_overlay.h) accepts
positive, even file sizes up to 777600 bytes. Short files keep their raw linear
addresses and receive a transparent-black tail. In particular, the headerless
460-pixel stride in `Laser_Wars.ovr` cannot be inferred or repaired by the
loader; supplying a correctly exported 540 by 720 overlay avoids that issue.
Out-of-range coordinates and invalid decoder arguments leave the caller's
pixel output intact.

The caller provides one full-size image buffer. The loader validates its
capacity and payload size before changing it, zeroes any missing tail, and
streams exactly 777600 bytes. A failed transfer is not an installed overlay.
A separate one-zero-byte command explicitly clears the image. The caller
must check the active core and ABI before either command.

Sidecar names replace the final filename extension with `.ovr`, preserving
the nested directory, stem, and case. Extensionless names gain `.ovr`; a
leading dot is part of the stem. Confined forward-slash paths and their
results are limited to 255 bytes. Invalid paths or insufficient output
capacity leave the output intact, including when input and output overlap.

## Reproducible format evidence

The following inspected sources are pinned to the MiSTer commit above.
These hashes identify format evidence, not files imported into this project.

| Source | SHA-256 |
| --- | --- |
| [Overlay README](https://github.com/MiSTer-devel/Vectrex_MiSTer/blob/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/overlays/README.md) | `d2c0d97f4bafa5b236a3745af5ac256b786dbe8258754acb005b333df30761d2` |
| [PNG converter](https://github.com/MiSTer-devel/Vectrex_MiSTer/blob/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/overlays/convert_image_alpha.py) | `430fa6a08ac429157c2c1bf4483052f98b08b9fc472151b1068923672c4aecab` |
| [Vectrex wrapper](https://github.com/MiSTer-devel/Vectrex_MiSTer/blob/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/Vectrex.sv) | `abf9603a1c0b4363d32896d65fb21e97b8058d6e2bfe36728147e189ec581640` |
| [Alpha blend helper](https://github.com/MiSTer-devel/Vectrex_MiSTer/blob/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/rtl/alphablend.sv) | `b4f60e146c722b481f4163883118417302d9d410851a8db13d75bbeeee5ece42` |

Six PNG samples reproduced their archived OVR bytes exactly with the pinned
converter's historical bounding-box behavior. The sample OVR hashes are:

| File | SHA-256 |
| --- | --- |
| Mine_Storm.ovr | `d166809733a5455332bb1e535962b59326bd2f23e8cb847849cabb396e543c9e` |
| ArmorAttack.ovr | `f2fdd98f774e168d04584872003d8c095ab2aaee7effcce649d48dc63d63fb1d` |
| Eating_Fish.ovr | `090ed6eca009d6da594880d71a7789cb8dd412f8266f0eac82b576ac97797385` |
| Laser_Wars.ovr | `af783f7668f3b85d809d12388e8f951967f9ca5c7f2b465cd8aeb771df562801` |
| PongOvl.ovr | `d9bd27841cf70909937686490a8786c373745192f81cd6ed06ced2622239109e` |
| Stunt_Man_Stories.ovr | `bc26476caa3456655b768299b5657882043a683f57d5228e7ff58ae9877ceef3` |

The local research records are `build/vectrex-overlay-research/UPSTREAM.json`
and `validation.json`. They are generated evidence; no inspected PNG or OVR
is a source-tree asset. Decoder tests establish file handling, not physical
display fidelity.

## MiSTer compositing reference

The pinned wrapper treats a nonzero beam as foreground. Overlay color can tint
that beam even when the artwork's alpha is zero; black overlay RGB leaves
the original beam color. Background pixels use the alpha blend helper selected
by `files.qip`. That helper sums `C8 >> (4-k)` for each set alpha bit `k`, with
`C8 = 17 * C4`. Its maximum `C4=15, A4=15` result is 236 rather than 255.

The foreground color expression has a less obvious width effect: each half
of its concatenation is four bits. Its literal channel result is
`17 * (((C4 << 2) & 15) | (C4 & 1))`, repeating 0, 85, 136, and 221 as the
input nibble increases. An isolated SystemVerilog test confirmed this width
behavior. With the default white tint enabled, an original beam red value
above 108 adds `((255-C) >> 1) + ((255-C) >> 2)` to each resulting channel.
These are the pinned MiSTer implementation's operations, not a claim that
Multitini reproduces those quantization artifacts. Multitini's artwork
brightness and beam-gain controls are separate from file-format compatibility.
