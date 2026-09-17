#Requires -RunAsAdministrator
<#
Fetches the authoring/debug/cooking tools for the Asset Pipeline (ADR-0015,
ADR-0016, ADR-0017, ROADMAP.md M2). These are offline, authoring-time tools
only (ARCHITECTURE.md §2's Content tooling constraint) - never linked into
the shipped client/server binaries - so this is a separate, opt-in script
from bootstrap-windows.ps1 (the mandatory dev-environment one), run only by
whoever is actually authoring content.
#>

$ErrorActionPreference = "Stop"

function Install-WingetPackage {
  param([string]$Id, [string[]]$Override)
  $args = @("install", "--id", $Id, "--exact", "--silent", "--accept-package-agreements", "--accept-source-agreements")
  if ($Override) {
    $args += @("--override", ($Override -join " "))
  }
  Write-Host "Installing $Id..."
  winget @args
}

# usd-optimize and usd-validation-nvidia (below) publish wheels for Python
# 3.10-3.12 only - not the latest 3.13+ that winget's plain "Python.Python.3"
# id would resolve to.
Install-WingetPackage -Id "Python.Python.3.12"

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
  [System.Environment]::GetEnvironmentVariable("Path", "User")

$requiredCommands = @("git", "python", "pip")
$missing = $requiredCommands | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($missing) {
  throw "Bootstrap installed packages but these commands still aren't on PATH: $($missing -join ', '). " +
    "Try opening a new terminal and re-running this script."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsDir = Join-Path $repoRoot "tools\asset-authoring"
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

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
# built from the Kit App Template repo instead. Not driven further than
# the clone here: `repo.bat template new` is an interactive wizard (app
# name/template selection), and the build itself is a GPU-dependent,
# 5-8-minute first run - both are a conscious step for whoever's about to
# author content, not something to run unattended from a bootstrap script.
$kitAppTemplateDir = Join-Path $toolsDir "kit-app-template"
Sync-GitRepo -Url "https://github.com/NVIDIA-Omniverse/kit-app-template.git" -Path $kitAppTemplateDir

# --- Cooking (ADR-0015/ADR-0016): usd-optimize, usd-validation-nvidia ---
# Both standalone (no Omniverse Kit needed) - see ADR-0015. Installed into
# the current Python environment, same as any other pip package; augusta's
# own cooker CLI (M2, not yet implemented) will invoke them as libraries or
# subprocesses once it exists.
python -m pip install --upgrade usd-optimize usd-validation-nvidia

# --- Cooking (ADR-0016): Adobe USD-Fileformat-plugins (glTF/FBX/OBJ ingestion) ---
# Cloned, not built here - it's a CMake project (own README covers the
# build), and augusta's cooker doesn't consume it yet.
$adobePluginsDir = Join-Path $toolsDir "USD-Fileformat-plugins"
Sync-GitRepo -Url "https://github.com/adobe/USD-Fileformat-plugins.git" -Path $adobePluginsDir

Write-Host ""
Write-Host "Asset pipeline tooling fetched under $toolsDir (gitignored - these are dev-machine tools, not vendored dependencies):"
Write-Host "  - USD Composer (authoring/assembly, PhysX debug via omni.physx.pvd/OmniPVD):"
Write-Host "      cd `"$kitAppTemplateDir`"; .\repo.bat template new   (Application > USD Composer)"
Write-Host "      .\repo.bat build; .\repo.bat launch"
Write-Host "  - usd-optimize / usd-validation-nvidia: installed into the current Python environment (pip)"
Write-Host "  - Adobe USD-Fileformat-plugins: cloned to `"$adobePluginsDir`" - see its README to build"
Write-Host ""
Write-Host ("Not fetched by this script: meshoptimizer and DirectXTex (ADR-0016/ADR-0017) - those are C++ build " +
  "dependencies for augusta's own cooker CLI, added via vcpkg.json once the cooker (ROADMAP.md M2) is implemented.")
