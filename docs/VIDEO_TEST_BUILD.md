# Video settings test update

This update adds border controls to **F12 → Video**.
There is no separate Borders tab. Borders never resize the game image:
off-screen parts are clipped. On Vectrex, the side borders
remain visible while the top and bottom lie outside the display.

The five styles are Single line, Double line, Corner brackets, Dashed and
Soft glow. Choose among 16 muted colors and 1–20 pixels of padding. Settings
are saved separately for each core. See [Video settings](VIDEO_SETTINGS.md)
for controls and defaults. The original game-window sizes are unchanged.

## Install

Use the normal firmware updater with `sd/FIRMWARE.BIN` (**VEC3**). Merge
`sd/multitini/cores/` into the SD card, replacing the supplied core binaries
and `core.cfg` files for C64, GBC and Vectrex. Preserve your ROMs, media,
saves and `multitini/settings.bin`. Existing settings migrate automatically.
The SD update tree supplies no game or BIOS files.

Both the firmware and the core images need updating to use the new controls.
The included C64 package has PAL and NTSC variants, and the GBC image enables
PSRAM. All included images use shell ABI 0.12. Older ABI 0.11 core packages
can still load, but the new controls explain that their core image needs an
update.

Existing Vectrex media fixes remain: use the core's `roms/` and `media/`
directories, enter subdirectories to load BASIC, and keep program assets next
to their BASIC files. Vectrex32 uses the same border settings as
the native Vectrex core.

## Evidence and limits

On 2026-09-15 the owner reported successful physical testing of this update:
"Tested on physical. Good." The tested bundle is
`video-settings-test-20260915.zip` (21,538,356 bytes), SHA-256
`439ed09606703a5d171f5b0e506382d9edcc19ec6bd806b79a9b352a4e16bbb2`.
Its VEC3 firmware SHA-256 is
`8b3768e0b20cd7a1d3847a4f21b025bebd16d3fcc8d72fb11ce6b7c139cffb41`.
This records the owner's acceptance of the update; it does not expand the
reported test into an exhaustive game or hardware qualification matrix.
Software previews and the calibration image in `previews/` are not board captures.

The package contains the exact source snapshot, explicit build inputs,
Vivado reports, firmware/package audits, and focused test evidence.
`manifest.json` records the timing results and payload hashes;
`SHA256SUMS.txt` covers all packaged files. Packaging rejects changed build
inputs, mismatched core bytes and failed setup, hold, pulse-width or bus-skew
checks. Each selected artifact must postdate its source capture and its build
log must report successful XSA export. The manifest binds those source
inventories, artifacts and reports by SHA-256. The five selected `fixed-*`
build directories were fresh for this build.

The final evidence includes 34 host tests, 148 Python tests (two explicit
skips), three sanitizer tests, 32 portable HDL benches plus shell compilation,
and the seven focused
video/register tests. The border pixel test is retained because its source
is unchanged. The parallel-cable XSim test is explicitly skipped in the
portable HDL run. `evidence/source-bindings.json` associates the selected logs
with the archived source hashes and checks that no associated source changed
after the log. The menu preview supplies its own source and image hashes.
These checks establish software evidence; they do not replace board testing.

The settings tests cover migration of both earlier schemas, interrupted SD
journals, corrupt files, and live rollback when saving fails. A journal that
was already made durable can recover the requested setting after reboot even
when a later part of the save reported an error.

## Reproduce

Use Vivado and Vitis 2025.2, Git, Python 3.12, and the MSYS2 UCRT64 runtime on
Windows. The guarded launcher expects `C:/msys64/ucrt64/bin`; set
`MULTITINI_UCRT_BIN` if that runtime is installed elsewhere. Use short paths
for the source checkout and fresh Vitis workspace. Extract the bundle to
`C:/mt-video-test` in this example. Clone the verified upstream repository at
the captured commit, initialize its submodule, then overlay the exact source
snapshot. Keep the clone's Git and submodule metadata: the FPGA input capture
script records the current Git commit.

```powershell
$bundle = 'C:/mt-video-test'
$repo = 'C:/mt-source'
$python = 'C:/Program Files/Python312/python.exe'
$guard = Join-Path $bundle 'evidence/run_native_tool.py'
$capture = Get-Content (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
& $python -u $guard -- git clone https://github.com/hasseily/multitini-one $repo
Set-Location $repo
& $python -u $guard -- git checkout --detach $capture.git_head
& $python -u $guard -- git submodule update --init --recursive
Expand-Archive -LiteralPath (Join-Path $bundle 'source.zip') -DestinationPath $repo -Force
$sources = Get-Content (Join-Path $bundle 'source-manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $sources.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $repo $entry.Name) -Algorithm SHA256).Hash -ne $entry.Value) {
        throw "Source mismatch: $($entry.Name)"
    }
}
New-Item -ItemType Directory -Path build -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $bundle 'evidence/build-matrix.py') -Destination build/build-borders-matrix.py
& $python -u scripts/run_native_tool.py -- $python build/build-borders-matrix.py fixed-supervisor fixed-c64-pal fixed-c64-ntsc fixed-gbc fixed-vectrex
```

The FPGA helper selects every variant explicitly. It refuses previously used
output directories. Retain the generated inventories and signoff reports.
The bundled package was built at the commit in `manifest.json` with the
archived source overlay; the commit alone does not contain this snapshot.

Regenerate the ARM platform and application from the bundled Vectrex XSA,
without reusing an external platform. All machine variants share the same PS
peripherals and shell address window. Run this from the overlaid checkout:

```powershell
$env:XILINX_VITIS = 'C:/Xilinx/2025.2/Vitis'
$env:MULTITINI_XSA = Join-Path $bundle 'inputs/vectrex.xsa'
$env:MULTITINI_VITIS_WORKSPACE = Join-Path $repo 'build/v-reproduce'
$env:MULTITINI_VITIS_APP = 'diagnostic'
Remove-Item Env:MULTITINI_PLATFORM_XPFM -ErrorAction SilentlyContinue
& $python -u scripts/run_native_tool.py -- C:\Windows\System32\cmd.exe /d /c C:\Xilinx\2025.2\Vitis\bin\vitis.bat -s scripts/create_vitis_workspace.py
```

The generator applies the required FatFs/BSP patches, C++ options, heap/stack
sizes and linker sections. It creates `diagnostic/build/diagnostic.elf` and
the platform's `export/multitini_platform/sw/boot/fsbl.elf` beneath that fresh
workspace. Rebuilt binaries need their own package audit and board acceptance;
vendor-generated metadata and paths can differ from the supplied binaries.

For provenance, `inputs/used-vitis-platform.zip` contains the actual VEC2
platform export reused by the supplied firmware, including its basis XSA,
headers and libraries. `evidence/vitis/` contains its file hashes, BSP
configuration, application linker script, UserConfig, compiler commands and
build logs. That archived platform is evidence of the supplied build; the
commands above deliberately create a new platform from the supplied XSA.

The assembly helper requires an absent or empty output directory and explicit
final inputs. It never merges an older bundle into a new one.
