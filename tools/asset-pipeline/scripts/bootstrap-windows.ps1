#Requires -RunAsAdministrator
<#
Builds a hermetic authoring/cooking environment for the Asset Pipeline
(ADR-0015, ADR-0016, ADR-0017, ROADMAP.md M2) entirely under -AssetsRoot.

Cooking (always): a self-contained Python environment (uv-managed - no
system/global Python involved) with the tools/asset-pipeline Python project
installed into it (pulling in usd-optimize/usd-validation-nvidia/pynacl/
blake3 as its own dependencies, plus the two small native _meshoptimizer/
_textconv extension modules built and placed into that same project -
ADR-0030's cooker is pure Python otherwise, including the pack format and
key generation), a signing keypair, and the authoring/packs/keys content
dirs (the cooker reads stages from authoring/, relative to it).
Everything the installed `cooker` command needs to run lives under
this one root, so it never depends on what's on PATH in whatever shell it's
invoked from.

Authoring (skipped with -SkipAuthoring): NVIDIA Omniverse USD Composer (via
kit-app-template) and Adobe's USD-Fileformat-plugins.

These are offline, authoring-time tools only (ARCHITECTURE.md §2's Content
tooling constraint) - never linked into the shipped client/server binaries -
so this is a separate, opt-in script from bootstrap-windows.ps1 (the
mandatory dev-environment one, which this one still relies on for
git/cmake/ninja), run only by whoever is actually authoring content.
#>

param(
  # Root of the hermetic environment this script builds. Everything below
  # (content dirs, fetched tool checkouts, the uv-managed Python venv) is
  # created under it - nothing here belongs in git (least of all
  # $AssetsRoot\keys).
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$AssetsRoot,

  # Set up only the cooking environment, without the authoring tools
  # (Composer, USD-Fileformat-plugins) - e.g. on a machine that only cooks
  # stages someone else authored.
  [switch]$SkipAuthoring,

  # Accept NVIDIA's Omniverse license terms without the interactive prompt
  # (only relevant without -SkipAuthoring) - for unattended runs by someone
  # who has already read them.
  [switch]$AcceptOmniverseEula
)

$ErrorActionPreference = "Stop"

$assetPipelineProject = Split-Path -Parent $PSScriptRoot

function Install-WingetPackage {
  param([string]$Id, [string[]]$Override)
  $args = @("install", "--id", $Id, "--exact", "--silent", "--accept-package-agreements", "--accept-source-agreements")
  if ($Override) {
    $args += @("--override", ($Override -join " "))
  }
  Write-Host "Installing $Id..."
  winget @args
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
$toolsDir = Join-Path $AssetsRoot "tools"
$pythonDir = Join-Path $AssetsRoot "python"
$binDir = Join-Path $AssetsRoot "bin"
New-Item -ItemType Directory -Force -Path $authoringDir, $packsDir, $keysDir, $binDir | Out-Null
if (-not $SkipAuthoring) {
  New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
}

function Sync-GitRepo {
  param([string]$Url, [string]$Path, [string]$Ref)
  if (Test-Path $Path) {
    Write-Host "Updating $Path..."
    git -C $Path fetch --tags --quiet
  } else {
    Write-Host "Cloning $Url..."
    git clone --quiet $Url $Path
  }
  if ($Ref) {
    git -C $Path checkout --quiet $Ref
  }
}

# --- Authoring + debug (ADR-0015): NVIDIA Omniverse USD Composer, Adobe USD-Fileformat-plugins ---
$kitAppTemplateDir = Join-Path $toolsDir "kit-app-template"
$adobePluginsDir = Join-Path $toolsDir "USD-Fileformat-plugins"
if (-not $SkipAuthoring) {
  # The old Omniverse Launcher was deprecated (Oct 2025); Composer is now
  # built from the Kit App Template repo instead.
  Sync-GitRepo -Url "https://github.com/NVIDIA-Omniverse/kit-app-template.git" -Path $kitAppTemplateDir

  # The Augusta USD Composer app is scaffolded by replaying a checked-in
  # playback file (`repo.bat template replay`) instead of the interactive
  # `template new` wizard, so its name/version/setup extension are fixed
  # rather than typed in. Replay is skipped once the app exists, so re-runs
  # don't touch a scaffold that may have been customized since.
  Push-Location $kitAppTemplateDir
  try {
    $augustaApp = Get-ChildItem "source\apps" -Filter "augusta.kit" -ErrorAction SilentlyContinue
    if (-not $augustaApp) {
      # The template tool asks for this acceptance itself (and blocks on it
      # if the breadcrumb file it looks for is missing), so ask here instead
      # of letting a non-interactive replay fail. Never accepted on the
      # user's behalf: the breadcrumb is only written after an explicit yes
      # (or -AcceptOmniverseEula).
      $eulaBreadcrumb = ".omniverse_eula_accepted.txt"
      if (-not (Test-Path $eulaBreadcrumb)) {
        Write-Host "The Omniverse Kit App Template is governed by the NVIDIA Software License Agreement and the"
        Write-Host "Product-Specific Terms for NVIDIA Omniverse:"
        Write-Host "  https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-software-license-agreement/"
        Write-Host "  https://www.nvidia.com/en-us/agreements/enterprise-software/product-specific-terms-for-omniverse/"
        if (-not $AcceptOmniverseEula) {
          $answer = Read-Host "Do you accept the governing terms? (yes/no)"
          if ($answer -notmatch "^(y|yes)$") {
            throw "Omniverse terms not accepted - re-run with -SkipAuthoring to set up cooking only."
          }
        }
        New-Item -ItemType File -Path $eulaBreadcrumb | Out-Null
      }

      Write-Host "Scaffolding the Augusta USD Composer app (repo template replay)..."
      .\repo.bat template replay (Join-Path $assetPipelineProject "composer\augusta.playback.toml")
      if ($LASTEXITCODE -ne 0) {
        throw "repo template replay failed (exit $LASTEXITCODE)."
      }
    }

    # `repo.bat build` is slow (5-8 minutes first run - GPU-dependent
    # shader/Kit SDK fetch) but not interactive, so it runs on every re-run.
    Write-Host "Building USD Composer (augusta.kit)..."
    .\repo.bat build
    if ($LASTEXITCODE -ne 0) {
      throw "repo build failed (exit $LASTEXITCODE)."
    }
  } finally {
    Pop-Location
  }

  # glTF/FBX/OBJ ingestion (ADR-0016). Cloned, not built here - it's a CMake
  # project (own README covers the build), and augusta's cooker doesn't
  # consume it yet.
  Sync-GitRepo -Url "https://github.com/adobe/USD-Fileformat-plugins.git" -Path $adobePluginsDir
}

# --- Hermetic Python (ADR-0015/ADR-0016): the asset-pipeline project ---
# uv-managed venv under $AssetsRoot - self-contained, no system Python
# involved. tools/asset-pipeline (this repo's own Python project - see its
# pyproject.toml) is installed into it editable, pulling in usd-optimize
# (Python API only, no CLI) and usd-validation-nvidia (CLI) as its
# dependencies. This produces $pythonDir\Scripts\cooker.exe, the
# actual pipeline entry point (copied to $binDir below) - editable so local
# edits to tools/asset-pipeline take effect without rerunning this script.
$venvPython = Join-Path $pythonDir "Scripts\python.exe"
if (-not (Test-Path $venvPython)) {
  Write-Host "Creating hermetic Python environment at $pythonDir (uv)..."
  uv venv --python 3.12 $pythonDir
  if ($LASTEXITCODE -ne 0) {
    throw "uv venv failed (exit $LASTEXITCODE)."
  }
}
Write-Host "Installing asset-pipeline (from $assetPipelineProject) into $pythonDir..."
uv pip install --python $venvPython --upgrade --editable $assetPipelineProject
if ($LASTEXITCODE -ne 0) {
  throw "uv pip install failed (exit $LASTEXITCODE)."
}

# The user-facing commands live in $binDir, not buried in the venv. The
# console-script launchers pip/uv generate embed the absolute path of the
# venv's python.exe, so a plain copy runs from anywhere; refreshed on every
# run so it never goes stale after a reinstall.
foreach ($command in "cooker", "cooker-keygen") {
  Copy-Item (Join-Path $pythonDir "Scripts\$command.exe") $binDir -Force
}

# --- Native modules build (ADR-0030) ---
# tools/asset-pipeline/cpp is its own standalone CMake project (own
# vcpkg.json/CMakePresets.json, independent of the client/server build) that
# builds the two pybind11 modules straight into ../src/asset_pipeline, so
# `import asset_pipeline._meshoptimizer`/`_textconv` just work with no
# separate copy step. PYTHON_EXECUTABLE (the variable pybind11's vcpkg port's
# legacy FindPythonInterp reads) points cmake at this venv, so the built
# extensions' ABI matches the interpreter that imports them. pybind11 tags
# the actual filename with the Python ABI (e.g.
# _meshoptimizer.cp312-win_amd64.pyd) - Python's import machinery resolves
# that back to the plain module name regardless, so check by glob.
$nativeSourceDir = Join-Path $assetPipelineProject "cpp"
$assetPipelinePackageDir = Join-Path $assetPipelineProject "src\asset_pipeline"
$nativeModulesBuilt = (Get-ChildItem $assetPipelinePackageDir -Filter "_meshoptimizer*.pyd" -ErrorAction SilentlyContinue) -and
  (Get-ChildItem $assetPipelinePackageDir -Filter "_textconv*.pyd" -ErrorAction SilentlyContinue)
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
  $nativeModulesBuilt = (Get-ChildItem $assetPipelinePackageDir -Filter "_meshoptimizer*.pyd" -ErrorAction SilentlyContinue) -and
    (Get-ChildItem $assetPipelinePackageDir -Filter "_textconv*.pyd" -ErrorAction SilentlyContinue)
  if (-not $nativeModulesBuilt) {
    throw "The native modules build did not produce both _meshoptimizer and _textconv in $assetPipelinePackageDir - see the cmake output above."
  }
}

# Not regenerated on a re-run: overwriting it would silently invalidate every
# pack already signed with the old key and the public key already deployed
# for verification (main.cpp's <public_key_path> argument, ADR-0018). Goes
# through the cooker-keygen entry point (asset_pipeline/keys.py,
# pynacl - pure Python, no native module or CLI binary involved).
$signingKeyPrefix = Join-Path $keysDir "augusta"
$signingKeyPath = "$signingKeyPrefix.key"
if (Test-Path $signingKeyPath) {
  Write-Host "Signing keypair already exists at $signingKeyPrefix.key/.pub - leaving it as is."
} else {
  Write-Host "Generating Ed25519 signing keypair at $signingKeyPrefix.key/.pub..."
  $genKeypairExe = Join-Path $binDir "cooker-keygen.exe"
  & $genKeypairExe $signingKeyPrefix
  if ($LASTEXITCODE -ne 0) {
    throw "cooker-keygen failed (exit $LASTEXITCODE)."
  }
}

Write-Host ""
Write-Host "Hermetic environment ready at $AssetsRoot (never commit any of it, especially $keysDir):"
Write-Host "  - $authoringDir  : raw USD stages - the cooker's input root"
Write-Host "  - $packsDir      : signed packs cooked via the cooker"
Write-Host "  - $keysDir       : Ed25519 signing keypair (augusta.key/augusta.pub)"
Write-Host "  - $binDir        : the cooker and cooker-keygen commands"
Write-Host "  - $pythonDir     : hermetic Python venv (uv), asset-pipeline installed editable from tools\asset-pipeline"
Write-Host "                     (includes the native _meshoptimizer/_textconv modules - $assetPipelinePackageDir)"
if (-not $SkipAuthoring) {
  Write-Host "  - $kitAppTemplateDir : USD Composer (kit-app-template)"
  Write-Host "  - $adobePluginsDir : Adobe USD-Fileformat-plugins (see its README to build)"
}
Write-Host ""
$cookerExe = Join-Path $binDir "cooker.exe"
Write-Host "Cook a stage saved under $authoringDir, e.g.: $cookerExe Example"
