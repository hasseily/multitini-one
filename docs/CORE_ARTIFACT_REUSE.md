# Selective FPGA builds and artifact reuse

An ARM or packaging change can retain validated FPGA images. A release build ID
applies to newly built images; reused images keep their original ID, ABI, source
archives, settings and routed reports. Never rewrite an old capture to describe
the current checkout.

M0.4.0 rebuilds only `apple-iigs` for accelerated CPU execution and its SHR
edge correction. The six other qualified M0.3.0 images remain supported at
ABI 0.21; the new IIgs image advertises ABI 0.22. New firmware reads the IIgs
speed capability only when that image implements it. USB SD export and folder
navigation are ARM changes. They do not require replacement machine images.
The new-image default is `4D000400`, under `build/core-gs3/hardware`.

The automatic builder conservatively compares all effective sources, including
shared register specifications. When deliberately retaining an earlier supported
interface after a backward-compatible firmware change, build only the changed
image and pass each other qualified artifact explicitly to the packager:

```powershell
python scripts/build_core_gs.py --image apple-iigs --output build/core-gs3/hardware
python scripts/package_core_gs.py --hardware build/core-gs/hardware --selection-manifest build/core-gs3/hardware/core-selection.json --reuse-image supervisor=build/core-gs/hardware/supervisor --reuse-image c64-pal=build/core-gs/hardware/c64-pal --reuse-image gbc=build/core-gs/hardware/gbc --reuse-image vectrex=build/core-gs/hardware/vectrex --reuse-image genesis-pal=build/core-gs/hardware/genesis-pal --reuse-image genesis-ntsc=build/core-gs/hardware/genesis-ntsc --workspace build/core-gs/v4 --suffix core-gs3 --output build/firmware-core-gs3
```

These explicit package selections preserve their source differences in the
image inventory. Artifact integrity, original timing, ABI support and exact PS7
initialization checks remain mandatory. A different PS setup is not assumed
compatible. The earlier M0.3.1 example below records its original release and
should be reproduced with that release's source when identical output is needed.

For an M0.3.1 firmware build, inspect the seven M0.3.0 artifacts without running
Vivado:

```powershell
python scripts/build_core_gs.py --reuse-from build/core-gs/hardware --plan
```

The result lists `PLAN REUSE` or `PLAN BUILD` for every selected image and a total.
`--check-only` is an alias for `--plan`. Planning writes no files unless an explicit
`--selection-manifest PATH` is supplied. For that M0.3.1 ARM-only follow-up, all
seven entries reused FPGA ID `0x4D000300` and ABI `0.21`; that release's new-image
default was `4D000301`.

Run the same selection to create the record used by packaging:

```powershell
python scripts/build_core_gs.py --output build/core-gs2/hardware --reuse-from build/core-gs/hardware
```

This writes `build/core-gs2/hardware/core-selection.json`. Reused entries point to
their original directories. The builder does not copy their outputs or launch
Vivado for them. Only `PLAN BUILD` entries run capture, project generation,
synthesis, implementation and the existing strict export gates. New build
directories and selection manifests must be fresh; existing outputs are preserved.
The manifest records whether each action completed or failed.

Choose a subset or override one directory explicitly:

```powershell
python scripts/build_core_gs.py --image apple-iigs --reuse-from build/core-gs/hardware --plan
python scripts/build_core_gs.py --image genesis-pal --reuse-image genesis-pal=build/core-gs/hardware/genesis-pal --plan
```

`--image apple-iigs` selects only IIgs. Without `--image`, the complete seven-image
matrix is selected. An explicit `--reuse-image NAME=DIR` overrides `--reuse-from`
for that image and must name a selected entry. Duplicate and unknown names fail.
Without a reuse option, selected entries are new builds.

Every candidate must pass the shared artifact audit: original machine and variant,
source ZIP and TAR membership and hashes, original generator/export settings,
image kind/build ID/ABI, routed timing and CDC/coverage gates, and matching bitstream
inside the XSA. Reuse also compares the current effective FPGA input inventory and
hashes with the archived capture. Changes to HDL, constraints, imported source,
generation specifications or build/signoff settings require a new image. Ancillary
plan, documentation, license and provenance edits retain the original archived
versions and are reported separately.

An invalid or incompatible explicit `--reuse-image` fails before any build starts.
For `--reuse-from`, a missing or rejected candidate becomes a new build with the
reason shown in the plan and recorded in the manifest. There is no search for a
substitute bitstream. Review `--plan` before executing when a rebuild is unexpected.

Pass `--selection-manifest` to the packaging command to consume the same selections.
Packaging revalidates the selected artifacts and pins their identities and hashes;
it also checks the selected firmware's ABI and PS7/FSBL configuration. Matching
effective source inputs alone do not establish platform compatibility. Different
PS7 settings require explicit qualification rather than an assumed superset.

Reproduce the orchestration checks with:

```powershell
python scripts/test_core_build_reuse.py
```

These tests launch no FPGA tools. Build, source and package audits do not replace
physical board qualification.
