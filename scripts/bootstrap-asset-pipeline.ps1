#Requires -RunAsAdministrator
<#
Builds a hermetic authoring/cooking environment for the Asset Pipeline
(ADR-0015, ADR-0016, ADR-0017, ROADMAP.md M2) entirely under -AssetsRoot:
NVIDIA Omniverse USD Composer (via kit-app-template), a self-contained
Python environment (uv-managed - no system/global Python involved) with the
tools/asset-pipeline Python project installed into it (pulling in usd-
optimize/usd-validation-nvidia/pynacl/blake3 as its own dependencies, plus
the two small native _meshoptimizer/_textconv extension modules built and
placed into that same project - ADR-0030's cooker is pure Python otherwise,
including the pack format and key generation), and the authoring/packs/keys
content dirs. Everything the installed `asset-pipeline` command needs to
run lives under this one root, so it never depends on what's on PATH in
whatever shell it's invoked from.

These are offline, authoring-time tools only (ARCHITECTURE.md §2's Content
tooling constraint) - never linked into the shipped client/server binaries -
so this is a separate, opt-in script from bootstrap-windows.ps1 (the
mandatory dev-environment one, which this one still relies on for
git/cmake/ninja), run only by whoever is actually authoring content.
#>

param(
  # Root of the hermetic environment this script builds. Everything below
  # (authoring/packs/keys content, fetched tool checkouts, the uv-managed
  # Python venv) is created under it - nothing here belongs in git (least
  # of all $AssetsRoot\keys).
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$AssetsRoot
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

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
# system-wide Python install needed, unlike an earlier version of this
# script. VCRedist is a genuine OS-level dependency uv can't provide: usd-
# optimize's USD runtime DLLs import MSVCP140.dll/VCRUNTIME140*.dll, not
# bundled in its wheel (see usd-optimize's own PyPI README).
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
New-Item -ItemType Directory -Force -Path $authoringDir, $packsDir, $keysDir, $toolsDir | Out-Null

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

# --- Authoring + debug (ADR-0015): NVIDIA Omniverse USD Composer ---
# The old Omniverse Launcher was deprecated (Oct 2025); Composer is now
# built from the Kit App Template repo instead.
$kitAppTemplateDir = Join-Path $toolsDir "kit-app-template"
Sync-GitRepo -Url "https://github.com/NVIDIA-Omniverse/kit-app-template.git" -Path $kitAppTemplateDir

# `repo.bat template new` (app name/template selection) is an interactive
# wizard that can't be scripted - if no app has been scaffolded yet (neither
# from a previous run of this script against this same -AssetsRoot), that
# one step is still left to whoever's about to author content. Once an app
# exists, `repo.bat build` itself is not interactive (just slow, 5-8 minutes
# first run - GPU-dependent shader/Kit SDK fetch), so it's safe to automate
# here on every subsequent run.
$augustaApp = Get-ChildItem (Join-Path $kitAppTemplateDir "source\apps") -Filter "*augusta*.kit" -ErrorAction SilentlyContinue |
  Select-Object -First 1
if ($augustaApp) {
  Write-Host "Building USD Composer ($($augustaApp.Name))..."
  Push-Location $kitAppTemplateDir
  try {
    .\repo.bat build
  } finally {
    Pop-Location
  }
} else {
  Write-Host ""
  Write-Host "No USD Composer app scaffolded yet - one-time manual step:"
  Write-Host "  cd `"$kitAppTemplateDir`"; .\repo.bat template new   (Application > USD Composer)"
  Write-Host "  .\repo.bat build; .\repo.bat launch"
  Write-Host ""
}

# --- Hermetic Python (ADR-0015/ADR-0016): the asset-pipeline project ---
# uv-managed venv under $AssetsRoot - self-contained, no system Python
# involved. tools/asset-pipeline (this repo's own Python project - see its
# pyproject.toml) is installed into it editable, pulling in usd-optimize
# (Python API only, no CLI) and usd-validation-nvidia (CLI) as its
# dependencies. This produces $pythonDir\Scripts\asset-pipeline.exe, the
# actual pipeline entry point - editable so local edits to tools/asset-
# pipeline take effect without rerunning this script.
$venvPython = Join-Path $pythonDir "Scripts\python.exe"
if (-not (Test-Path $venvPython)) {
  Write-Host "Creating hermetic Python environment at $pythonDir (uv)..."
  uv venv --python 3.12 $pythonDir
  if ($LASTEXITCODE -ne 0) {
    throw "uv venv failed (exit $LASTEXITCODE)."
  }
}
$assetPipelineProject = Join-Path $repoRoot "tools\asset-pipeline"
Write-Host "Installing asset-pipeline (from $assetPipelineProject) into $pythonDir..."
uv pip install --python $venvPython --upgrade --editable $assetPipelineProject
if ($LASTEXITCODE -ne 0) {
  throw "uv pip install failed (exit $LASTEXITCODE)."
}

# --- Cooking (ADR-0016): Adobe USD-Fileformat-plugins (glTF/FBX/OBJ ingestion) ---
# Cloned, not built here - it's a CMake project (own README covers the
# build), and augusta's cooker doesn't consume it yet.
$adobePluginsDir = Join-Path $toolsDir "USD-Fileformat-plugins"
Sync-GitRepo -Url "https://github.com/adobe/USD-Fileformat-plugins.git" -Path $adobePluginsDir

# --- Native cooking-support modules build (ADR-0030) ---
# augusta_meshoptimizer_py/augusta_textconv_py (tools/asset-pipeline/
# cooking/*_bindings.cpp) are vcpkg-vendored C++ targets of the augusta
# repo itself (CMakePresets.json's windows-tools preset) - they still
# configure/build through the repo's own build/ tree (that's where the
# top-level CMakeLists.txt/vcpkg.json live), but that directory's
# CMakeLists.txt points RUNTIME_OUTPUT_DIRECTORY straight at
# ../src/asset_pipeline, so `import asset_pipeline._meshoptimizer`/
# `_textconv` just work with no separate copy step. Neither links USD (see
# that CMakeLists.txt's own comment on why) - cook.py walks the stage via
# usd-optimize's pip-installed pxr build instead, and everything else
# (the pack format, key generation) is pure Python (pack.py/keys.py).
# -DPython3_EXECUTABLE points cmake's Python discovery at this venv, so
# the built extensions' ABI matches the interpreter that imports them.
# pybind11 tags the actual filename with the Python ABI (e.g.
# _meshoptimizer.cp312-win_amd64.pyd) - Python's import machinery resolves
# that back to the plain module name regardless, so check by glob.
$assetPipelinePackageDir = Join-Path $assetPipelineProject "src\asset_pipeline"
$cookingModulesBuilt = (Get-ChildItem $assetPipelinePackageDir -Filter "_meshoptimizer*.pyd" -ErrorAction SilentlyContinue) -and
  (Get-ChildItem $assetPipelinePackageDir -Filter "_textconv*.pyd" -ErrorAction SilentlyContinue)
if (-not $cookingModulesBuilt) {
  Write-Host "Building augusta_meshoptimizer_py/augusta_textconv_py (windows-tools preset)..."
  cmake --preset windows-tools -S $repoRoot "-DPython3_EXECUTABLE=$venvPython"
  cmake --build --preset windows-tools --target augusta_meshoptimizer_py augusta_textconv_py
  $cookingModulesBuilt = (Get-ChildItem $assetPipelinePackageDir -Filter "_meshoptimizer*.pyd" -ErrorAction SilentlyContinue) -and
    (Get-ChildItem $assetPipelinePackageDir -Filter "_textconv*.pyd" -ErrorAction SilentlyContinue)
  if (-not $cookingModulesBuilt) {
    throw "augusta_meshoptimizer_py/augusta_textconv_py build did not produce both modules in $assetPipelinePackageDir - see the cmake output above."
  }
}

# Not regenerated on a re-run: overwriting it would silently invalidate every
# pack already signed with the old key and the public key already deployed
# for verification (main.cpp's <public_key_path> argument, ADR-0018). Goes
# through the asset-pipeline-gen-keypair entry point (asset_pipeline/keys.py,
# pynacl - pure Python, no native module or CLI binary involved).
$signingKeyPrefix = Join-Path $keysDir "augusta"
$signingKeyPath = "$signingKeyPrefix.key"
if (Test-Path $signingKeyPath) {
  Write-Host "Signing keypair already exists at $signingKeyPrefix.key/.pub - leaving it as is."
} else {
  Write-Host "Generating Ed25519 signing keypair at $signingKeyPrefix.key/.pub..."
  $genKeypairExe = Join-Path $pythonDir "Scripts\asset-pipeline-gen-keypair.exe"
  & $genKeypairExe $signingKeyPrefix
  if ($LASTEXITCODE -ne 0) {
    throw "asset-pipeline-gen-keypair failed (exit $LASTEXITCODE)."
  }
}

Write-Host ""
Write-Host "Hermetic environment ready at $AssetsRoot (never commit any of it, especially $keysDir):"
Write-Host "  - $authoringDir  : raw USD stages exported/saved from Composer"
Write-Host "  - $packsDir      : signed packs cooked via asset-pipeline"
Write-Host "  - $keysDir       : Ed25519 signing keypair (augusta.key/augusta.pub)"
Write-Host "  - $pythonDir     : hermetic Python venv (uv), asset-pipeline installed editable from tools\asset-pipeline"
Write-Host "                     (includes the native _meshoptimizer/_textconv modules - $assetPipelinePackageDir)"
Write-Host "  - $kitAppTemplateDir : USD Composer (kit-app-template)"
Write-Host "  - $adobePluginsDir : Adobe USD-Fileformat-plugins (see its README to build)"
Write-Host ""
$assetPipelineExe = Join-Path $pythonDir "Scripts\asset-pipeline.exe"
Write-Host "Run the full pipeline with e.g.: $assetPipelineExe $authoringDir\Example.usda"
