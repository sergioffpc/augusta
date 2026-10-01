<#
Sets up the optional NVIDIA Omniverse USD Composer authoring tools under a
caller-chosen assets root. Run after the repository's scripts/bootstrap-windows.ps1
has installed Git, CMake and Ninja. Composer is an authoring-time tool only;
it is never linked into the shipped client/server binaries.
#>

param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$AssetsRoot,

  # Accept NVIDIA's Omniverse license terms without the interactive prompt
  # (for unattended runs by someone who has already read them).
  [switch]$AcceptOmniverseEula
)

$ErrorActionPreference = "Stop"

$toolsDir = Join-Path $AssetsRoot "tools"
$binDir = Join-Path $AssetsRoot "bin"
$composerProject = Split-Path -Parent $PSScriptRoot
$kitAppTemplateDir = Join-Path $toolsDir "kit-app-template"
$adobePluginsDir = Join-Path $toolsDir "USD-Fileformat-plugins"

$requiredCommands = @("git", "cmake", "ninja")
$missing = $requiredCommands | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($missing) {
  throw "These commands are required but aren't on PATH: $($missing -join ', '). " +
    "Run scripts\bootstrap-windows.ps1 first, then re-run this script."
}

New-Item -ItemType Directory -Force -Path $toolsDir, $binDir | Out-Null

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

# The old Omniverse Launcher was deprecated (Oct 2025); Composer is now
# built from the Kit App Template repo.
Sync-GitRepo -Url "https://github.com/NVIDIA-Omniverse/kit-app-template.git" -Path $kitAppTemplateDir

# Replay the checked-in app definition instead of the interactive template
# wizard, so its name/version/setup extension stay fixed. Existing apps may
# have been customized, so only replay when the app is absent.
Push-Location $kitAppTemplateDir
try {
  $augustaApp = Get-ChildItem "source\apps" -Filter "augusta.kit" -ErrorAction SilentlyContinue
  if (-not $augustaApp) {
    $eulaBreadcrumb = ".omniverse_eula_accepted.txt"
    if (-not (Test-Path $eulaBreadcrumb)) {
      Write-Host "The Omniverse Kit App Template is governed by the NVIDIA Software License Agreement and the"
      Write-Host "Product-Specific Terms for NVIDIA Omniverse:"
      Write-Host "  https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-software-license-agreement/"
      Write-Host "  https://www.nvidia.com/en-us/agreements/enterprise-software/product-specific-terms-for-omniverse/"
      if (-not $AcceptOmniverseEula) {
        $answer = Read-Host "Do you accept the governing terms? (yes/no)"
        if ($answer -notmatch "^(y|yes)$") {
          throw "Omniverse terms not accepted - re-run with -AcceptOmniverseEula after accepting them."
        }
      }
      New-Item -ItemType File -Path $eulaBreadcrumb | Out-Null
    }

    Write-Host "Scaffolding the Augusta USD Composer app (repo template replay)..."
    .\repo.bat template replay (Join-Path $composerProject "augusta.playback.toml")
    if ($LASTEXITCODE -ne 0) {
      throw "repo template replay failed (exit $LASTEXITCODE)."
    }
  }

  # `repo.bat build` fetches the Kit SDK on its first run and is not interactive.
  Write-Host "Building USD Composer (augusta.kit)..."
  .\repo.bat build
  if ($LASTEXITCODE -ne 0) {
    throw "repo build failed (exit $LASTEXITCODE)."
  }
} finally {
  Pop-Location
}

Copy-Item -Force (Join-Path $composerProject "augusta-composer.ps1") (Join-Path $binDir "augusta-composer.ps1")

# glTF/FBX/OBJ ingestion (ADR-0016) is kept as a separate checkout; the cooker
# does not consume it yet.
Sync-GitRepo -Url "https://github.com/adobe/USD-Fileformat-plugins.git" -Path $adobePluginsDir

Write-Host ""
Write-Host "USD Composer ready under $AssetsRoot :"
Write-Host "  - $kitAppTemplateDir : Composer app source and build"
Write-Host "  - $adobePluginsDir : Adobe USD-Fileformat-plugins"
Write-Host "  - $(Join-Path $binDir 'augusta-composer.ps1') : launch Composer"
