# SPDX-License-Identifier: GPL-3.0-only
param(
    [Parameter(Mandatory=$true)][string]$Baseline,
    [Parameter(Mandatory=$true)][string]$Workspace,
    [Parameter(Mandatory=$true)][string]$Platform,
    [switch]$IncludeGolden
)

# Build fresh ARM applications on an explicitly selected retained platform.
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Item -LiteralPath $PSScriptRoot).Parent.FullName
Set-Location -LiteralPath $repoRoot
$baselinePath = [IO.Path]::GetFullPath((Join-Path $repoRoot $Baseline))
$workspacePath = [IO.Path]::GetFullPath((Join-Path $repoRoot $Workspace))
$platformPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $Platform))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $workspacePath.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Workspace must be inside this checkout build directory.'
}
if (Test-Path -LiteralPath $workspacePath) { throw 'Fresh Vitis workspace already exists.' }
$xsaPath = Join-Path $baselinePath 'inputs/supervisor.xsa'
foreach ($inputPath in @($xsaPath, $platformPath)) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Missing selected input: $inputPath" }
}
$env:MULTITINI_VITIS_WORKSPACE = $workspacePath
$env:MULTITINI_XSA = $xsaPath
$env:MULTITINI_PLATFORM_XPFM = $platformPath
$env:MULTITINI_CPU1_BASELINE = $baselinePath
if ($IncludeGolden) {
    if (Test-Path Env:MULTITINI_SKIP_GOLDEN) { Remove-Item Env:MULTITINI_SKIP_GOLDEN }
} else { $env:MULTITINI_SKIP_GOLDEN = '1' }
$env:MULTITINI_VITIS_APP = 'diagnostic'
if (Test-Path Env:MULTITINI_PLATFORM_ONLY) { Remove-Item Env:MULTITINI_PLATFORM_ONLY }
$env:XILINX_VITIS = 'C:/Xilinx/2025.2/Vitis'
$env:TEMP = "$workspacePath-temp"
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
$logPath = "$workspacePath.log"
python scripts/run_native_tool.py -- C:/Xilinx/2025.2/Vitis/bin/vitis.bat -s scripts/create_vitis_workspace.py *> $logPath
if ($LASTEXITCODE -ne 0) { throw "ARM firmware build failed; see $logPath" }
Write-Output "PASS fresh ARM applications; retained FPGA inputs: $baselinePath"
