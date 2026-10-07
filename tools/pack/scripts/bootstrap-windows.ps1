#Requires -RunAsAdministrator
<#
Builds the hermetic cooking environment for the Asset Pipeline (ADR-0030)
entirely under -AssetsRoot.

Cooking (always): a self-contained Python environment (uv-managed - no
system/global Python involved) with the tools/pack Python project
installed into it (pulling in usd-optimize/usd-validation-nvidia/pynacl/
blake3 as its own dependencies, plus the two small native _meshoptimizer/
_textconv extension modules built and placed into that same project -
ADR-0030's cooker is pure Python otherwise, including the pack format and
key generation), a signing keypair, and the authoring/packs/keys content
dirs (the cooker reads stages from authoring/, relative to it).
Everything the installed `augusta-pack` command needs to run lives under
this one root, so it never depends on what's on PATH in whatever shell it's
invoked from. Composer and the Adobe USD plugins have their own opt-in
bootstrap at `tools/composer/scripts/bootstrap-windows.ps1`.
#>

param(
  # Root of the hermetic environment this script builds. Everything below
  # (content dirs, fetched tool checkouts, the uv-managed Python venv) is
  # created under it - nothing here belongs in git (least of all
  # $AssetsRoot\keys).
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$AssetsRoot
)

$ErrorActionPreference = "Stop"

$packProject = Split-Path -Parent $PSScriptRoot

function Install-WingetPackage {
  param([string]$Id, [string[]]$Override)
  $wingetArgs = @("install", "--id", $Id, "--exact", "--silent", "--accept-package-agreements", "--accept-source-agreements")
  if ($Override) {
    $wingetArgs += @("--override", ($Override -join " "))
  }
  Write-Host "Installing $Id..."
  winget @wingetArgs
}

# uv manages its own Python interpreters (see the venv creation below) - no
# system-wide Python install needed. VCRedist is a genuine OS-level
# dependency uv can't provide: usd-optimize's USD runtime DLLs import
# MSVCP140.dll/VCRUNTIME140*.dll, not bundled in its wheel (see
# usd-optimize's own PyPI README).
Install-WingetPackage -Id "astral-sh.uv"
Install-WingetPackage -Id "Microsoft.VCRedist.2015+.x64"

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
  [System.Environment]::GetEnvironmentVariable("Path", "User")

$requiredCommands = @("git", "uv", "cmake", "ninja")
$missing = $requiredCommands | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($missing) {
  throw "Bootstrap installed packages but these commands still aren't on PATH: $($missing -join ', '). " +
    "cmake/ninja come from scripts/bootstrap-windows.ps1 (run that first if you haven't). " +
    "Otherwise try opening a new terminal and re-running this script."
}

# --- Content dirs ---
$authoringDir = Join-Path $AssetsRoot "authoring"
$packsDir = Join-Path $AssetsRoot "packs"
$keysDir = Join-Path $AssetsRoot "keys"
$pythonDir = Join-Path $AssetsRoot "python"
$binDir = Join-Path $AssetsRoot "bin"
New-Item -ItemType Directory -Force -Path $authoringDir, $packsDir, $keysDir, $binDir | Out-Null

# A small worked authoring/ tree (tools/composer/examples/authoring - committed,
# unlike everything else under $AssetsRoot) so a fresh environment has
# something to cook straight away (`augusta-pack firebase`): one map (ADR-0015),
# one character (ADR-0040), placeholder cue sounds (ADR-0020), a Parameters
# script (ADR-0039) and rules (ADR-0022), and the scenario's manifest
# (ADR-0041) composing them. Seeded piece by piece rather than as one tree,
# so each survives local edits independently - left alone once it exists,
# like the signing key below.
$exampleRoot = Join-Path (Split-Path -Parent $packProject) "composer\examples\authoring"
$examplePieces = @("maps\firebase.usda", "characters\soldier.usda") +
  ("gunshot", "hit_marker", "hit_taken", "death", "match_won", "match_lost" | ForEach-Object { "sounds\$_.wav" }) +
  @("scripts\parameters\rules_of_engagement.lua", "scripts\rules\last_man_standing.lua", "scenarios\firebase.yaml")
foreach ($piece in $examplePieces) {
  $source = Join-Path $exampleRoot $piece
  $dest = Join-Path $authoringDir $piece
  if (Test-Path $dest) {
    Write-Host "Example $piece already exists at $dest - leaving it as is."
  } else {
    Write-Host "Seeding example $piece at $dest..."
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
    Copy-Item $source $dest
  }
}

# --- Hermetic Python (ADR-0015/ADR-0016): the pack project ---
# Installed with `uv tool install`, the mechanism uv provides for exactly this:
# an isolated, uv-managed venv per tool (no system Python involved) plus the
# tool's own console scripts placed in a bin directory. Both are redirected
# under $AssetsRoot - the venv to $pythonDir\pack, the commands
# (augusta-pack, augusta-keygen, augusta-inspect, augusta-verify,
# augusta-publish) to $binDir.
# tools/pack (this repo's
# own Python project - see its pyproject.toml) is installed editable, pulling
# in usd-optimize (Python API only, no CLI) and usd-validation-nvidia (CLI) as
# its dependencies, so local edits to it take effect without rerunning this
# script. The two variables are scoped to this process.
if (Test-Path (Join-Path $pythonDir "pyvenv.cfg")) {
  throw "$pythonDir is a plain venv from an earlier version of this script; delete it and re-run (it is fully regenerated)."
}
$env:UV_TOOL_DIR = $pythonDir
$env:UV_TOOL_BIN_DIR = $binDir
$venvPython = Join-Path $pythonDir "pack\Scripts\python.exe"
Write-Host "Installing pack (from $packProject) into $pythonDir, commands into $binDir (uv tool)..."
uv tool install --python 3.12 --force --editable $packProject
if ($LASTEXITCODE -ne 0) {
  throw "uv tool install failed (exit $LASTEXITCODE)."
}

# --- Native modules build (ADR-0030) ---
# tools/pack/cpp is its own standalone CMake project (own
# vcpkg.json/CMakePresets.json, independent of the client/server build) that
# builds the two pybind11 modules straight into ../src/pack, so
# `import pack._meshoptimizer`/`_textconv` just work with no
# separate copy step. PYTHON_EXECUTABLE (the variable pybind11's vcpkg port's
# legacy FindPythonInterp reads) points cmake at this venv, so the built
# extensions' ABI matches the interpreter that imports them. pybind11 tags
# the actual filename with the Python ABI (e.g.
# _meshoptimizer.cp312-win_amd64.pyd) - Python's import machinery resolves
# that back to the plain module name regardless, so check by glob.
$nativeSourceDir = Join-Path $packProject "cpp"
$packPackageDir = Join-Path $packProject "src\pack"
$nativeModulesBuilt = (Get-ChildItem $packPackageDir -Filter "_meshoptimizer*.pyd" -ErrorAction SilentlyContinue) -and
  (Get-ChildItem $packPackageDir -Filter "_textconv*.pyd" -ErrorAction SilentlyContinue)
if (-not $nativeModulesBuilt) {
  Write-Host "Building the native modules ($nativeSourceDir)..."
  cmake --preset windows -S $nativeSourceDir "-DPYTHON_EXECUTABLE=$venvPython"
  if ($LASTEXITCODE -ne 0) {
    throw "cmake configure failed (exit $LASTEXITCODE)."
  }
  cmake --build (Join-Path $nativeSourceDir "build\x64-windows")
  if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed (exit $LASTEXITCODE)."
  }
  $nativeModulesBuilt = (Get-ChildItem $packPackageDir -Filter "_meshoptimizer*.pyd" -ErrorAction SilentlyContinue) -and
    (Get-ChildItem $packPackageDir -Filter "_textconv*.pyd" -ErrorAction SilentlyContinue)
  if (-not $nativeModulesBuilt) {
    throw "The native modules build did not produce both _meshoptimizer and _textconv in $packPackageDir - see the cmake output above."
  }
}

# Not regenerated on a re-run: overwriting it would silently invalidate every
# pack already signed with the old key and the public key already deployed
# for verification (main.cpp's <public_key_path> argument, ADR-0018). Goes
# through the augusta-keygen entry point (pack/keys.py,
# pynacl - pure Python, no native module or CLI binary involved).
$signingKeyPrefix = Join-Path $keysDir "augusta"
$signingKeyPath = "$signingKeyPrefix.key"
if (Test-Path $signingKeyPath) {
  Write-Host "Signing keypair already exists at $signingKeyPrefix.key/.pub - leaving it as is."
} else {
  Write-Host "Generating Ed25519 signing keypair at $signingKeyPrefix.key/.pub..."
  $genKeypairExe = Join-Path $binDir "augusta-keygen.exe"
  & $genKeypairExe $signingKeyPrefix
  if ($LASTEXITCODE -ne 0) {
    throw "augusta-keygen failed (exit $LASTEXITCODE)."
  }
}

Write-Host ""
Write-Host "Hermetic environment ready at $AssetsRoot (never commit any of it, especially $keysDir):"
Write-Host "  - $authoringDir  : scenario folders (a stage and its Lua scripts each) - a convenient place to keep them, not a boundary the cooker enforces"
Write-Host "  - $packsDir      : signed packs cooked via the cooker"
Write-Host "  - $keysDir       : Ed25519 signing keypair (augusta.key/augusta.pub)"
Write-Host "  - $binDir        : augusta-pack, augusta-keygen, augusta-inspect, augusta-verify, augusta-publish"
Write-Host "  - $pythonDir     : hermetic Python venv (uv tool), pack installed editable from tools\pack"
Write-Host "                     (includes the native _meshoptimizer/_textconv modules - $packPackageDir)"
Write-Host ""
$augustaPackExe = Join-Path $binDir "augusta-pack.exe"
Write-Host "Cook the example scenario: $augustaPackExe $exampleDest"
