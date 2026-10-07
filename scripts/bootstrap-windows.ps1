#Requires -RunAsAdministrator
<#
Bootstraps the Windows client development environment (docs/ENGINEERING.md,
Developer Environment). Installs VS Build Tools system-wide (default
install location, no --installPath) plus the rest of the client toolchain.
#>

$ErrorActionPreference = "Stop"

function Install-WingetPackage {
  param([string]$Id, [string[]]$Override, [string]$Version)
  $wingetArgs = @("install", "--id", $Id, "--exact", "--silent", "--accept-package-agreements", "--accept-source-agreements")
  if ($Version) {
    # --force so a different installed version (e.g. a newer one) is replaced.
    $wingetArgs += @("--version", $Version, "--force")
  } else {
    # A bootstrap installs prerequisites; it should not update unrelated
    # existing installations as a side effect.
    $wingetArgs += "--no-upgrade"
  }
  if ($Override) {
    $wingetArgs += @("--override", ($Override -join " "))
  }
  Write-Host "Installing $Id..."
  winget @wingetArgs
  if ($LASTEXITCODE -eq -1978335135) {
    Write-Host "$Id is already installed; skipping."
    return
  }
  if ($LASTEXITCODE -ne 0) {
    throw "winget failed to install $Id (exit $LASTEXITCODE)."
  }
}

Install-WingetPackage -Id "Microsoft.VisualStudio.BuildTools" `
  -Override @("--wait", "--quiet", "--add", "Microsoft.VisualStudio.Workload.VCTools", "--includeRecommended")
Install-WingetPackage -Id "Microsoft.WindowsSDK.10.0.26100"
Install-WingetPackage -Id "Kitware.CMake"
Install-WingetPackage -Id "Ninja-build.Ninja"
# GNU make, for the top-level Makefile (a wrapper over the CMake presets).
Install-WingetPackage -Id "ezwinports.make"
Install-WingetPackage -Id "Git.Git"
Install-WingetPackage -Id "Mozilla.sccache"
Install-WingetPackage -Id "astral-sh.uv"
# StyLua formats the scenarios' Lua scripts and taplo formats and lints TOML,
# in the hooks below and in CI, which pins the same versions. luacheck, which
# lints the Lua, is not on winget: it is installed further down.
Install-WingetPackage -Id "JohnnyMorganz.StyLua" -Version "2.5.2"
Install-WingetPackage -Id "tamasfe.taplo" -Version "0.10.0"
# Doxygen builds the documentation site's C++ API reference (`make docs`,
# ADR-0046), as the docs workflow does with the runner's own.
Install-WingetPackage -Id "DimitriVanHeesch.Doxygen"
# clang-format and clang-tidy, see docs/ENGINEERING.md, Code Quality. Back
# the .githooks/pre-commit and .githooks/pre-push hooks below. Pinned to the
# clang CI runs (the ubuntu-26.04 runner's distro package): another major
# formats and lints differently, so the hooks would disagree with CI's gates.
# Bump it together with the runner image. The pin keeps `winget upgrade` on
# that major.
Install-WingetPackage -Id "LLVM.LLVM" -Version "21.1.8"
winget pin add --id LLVM.LLVM --exact --version "21.*" --force

# Unlike the other packages here, LLVM's and Doxygen's installers don't add
# themselves to PATH under winget's --silent flag (that's an
# interactive-installer checkbox, unchecked by default in silent mode) - add
# their default install locations explicitly rather than relying on it.
foreach ($bin in @("$env:ProgramFiles\LLVM\bin", "$env:ProgramFiles\doxygen\bin")) {
  if ((Test-Path $bin) -and
      ([System.Environment]::GetEnvironmentVariable("Path", "Machine") -notlike "*$bin*")) {
    $machinePath = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    [System.Environment]::SetEnvironmentVariable("Path", "$machinePath;$bin", "Machine")
  }
}

# winget/MSI installers update the Machine/User PATH in the registry, but
# this process's own $env:PATH was captured at shell startup and won't see
# that change without this - without it, every check below would report a
# false failure even on a fully successful install (this is also why a
# fresh terminal is needed afterward to actually use these tools).
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
  [System.Environment]::GetEnvironmentVariable("Path", "User")

# A silently-missing tool here doesn't fail loud until much later - e.g.
# clang-format's absence only shows up as ".githooks/pre-commit: not
# found on PATH - skipping" at commit time, which is easy to miss and
# leaves every local commit unformatted. Check now, once, instead.
$requiredCommands = @("cmake", "ninja", "make", "git", "sccache", "uv", "clang-format", "clang-tidy",
  "stylua", "taplo", "doxygen")
$missing = $requiredCommands | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($missing) {
  throw "Bootstrap installed packages but these commands still aren't on PATH: $($missing -join ', '). " +
    "Try opening a new terminal and re-running this script; if a specific package failed to install, " +
    "check 'winget list' and re-run 'winget install' for it directly."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstall = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsInstall) {
  throw "Visual Studio Build Tools is installed, but its VC C++ toolset is missing. Add the Microsoft.VisualStudio.Workload.VCTools workload and rerun this script."
}
$windowsSdkInclude = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include\10.0.26100.0"
if (-not (Test-Path $windowsSdkInclude)) {
  throw "Windows SDK 10.0.26100 headers are missing at $windowsSdkInclude."
}

# Install the standalone yamlfmt release in a user-local bin directory.
$yamlfmtBin = Join-Path $env:LOCALAPPDATA "Programs\yamlfmt"
New-Item -ItemType Directory -Force -Path $yamlfmtBin | Out-Null
$yamlfmtVersion = "0.21.0"
$yamlfmtAsset = "yamlfmt_${yamlfmtVersion}_Windows_x86_64.tar.gz"
$yamlfmtTemp = Join-Path $env:TEMP ("yamlfmt-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $yamlfmtTemp | Out-Null
try {
  $releaseUrl = "https://github.com/google/yamlfmt/releases/download/v$yamlfmtVersion"
  Invoke-WebRequest -Uri "$releaseUrl/$yamlfmtAsset" -OutFile (Join-Path $yamlfmtTemp $yamlfmtAsset)
  $expectedHash = "07f80ce5d741eb4b0a9380ac78a19c7cb5bd44e2a9a47a5a04839e3ba54dd463"
  $archive = Join-Path $yamlfmtTemp $yamlfmtAsset
  $actualHash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actualHash -ne $expectedHash) {
    throw "SHA-256 mismatch for $yamlfmtAsset."
  }
  tar.exe -xzf $archive -C $yamlfmtTemp
  if ($LASTEXITCODE -ne 0) {
    throw "Extracting $yamlfmtAsset failed (exit $LASTEXITCODE)."
  }
  $yamlfmtSource = Join-Path $yamlfmtTemp "yamlfmt.exe"
  if (-not (Test-Path $yamlfmtSource)) {
    throw "yamlfmt.exe missing from $yamlfmtAsset."
  }
  Copy-Item -Force $yamlfmtSource (Join-Path $yamlfmtBin "yamlfmt.exe")
} finally {
  Remove-Item -LiteralPath $yamlfmtTemp -Recurse -Force
}
$userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$yamlfmtBin*") {
  $userPath = "$userPath;$yamlfmtBin"
  [System.Environment]::SetEnvironmentVariable("Path", $userPath, "User")
}
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + $userPath
if (-not (Get-Command yamlfmt -ErrorAction SilentlyContinue)) {
  throw "yamlfmt was installed but is not on PATH: $yamlfmtBin"
}

# Install luacheck's standalone release (it bundles its own Lua) in a
# user-local bin directory, pinned as CI pins it.
$luacheckBin = Join-Path $env:LOCALAPPDATA "Programs\luacheck"
New-Item -ItemType Directory -Force -Path $luacheckBin | Out-Null
$luacheckTemp = Join-Path $env:TEMP ("luacheck-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $luacheckTemp | Out-Null
try {
  $luacheckDownload = Join-Path $luacheckTemp "luacheck.exe"
  Invoke-WebRequest -Uri "https://github.com/lunarmodules/luacheck/releases/download/v1.2.0/luacheck.exe" `
    -OutFile $luacheckDownload
  $expectedHash = "0f1c69c4d09f1ebb4d8df14c215e4553e2e639bd4cb7bf3c639b0daa6198317b"
  $actualHash = (Get-FileHash $luacheckDownload -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actualHash -ne $expectedHash) {
    throw "SHA-256 mismatch for luacheck.exe."
  }
  Copy-Item -Force $luacheckDownload (Join-Path $luacheckBin "luacheck.exe")
} finally {
  Remove-Item -LiteralPath $luacheckTemp -Recurse -Force
}
$userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$luacheckBin*") {
  $userPath = "$userPath;$luacheckBin"
  [System.Environment]::SetEnvironmentVariable("Path", $userPath, "User")
}
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + $userPath
if (-not (Get-Command luacheck -ErrorAction SilentlyContinue)) {
  throw "luacheck was installed but is not on PATH: $luacheckBin"
}

# Install the PSScriptAnalyzer module, which lints and formats the PowerShell
# scripts (scripts\psscriptanalyzer.ps1 imports it from here), pinned as CI
# pins it. Unpacked from the gallery's package rather than Install-Module,
# which needs the NuGet provider and a trusted gallery first.
$psScriptAnalyzerVersion = "1.25.0"
$psScriptAnalyzerDir = Join-Path $env:LOCALAPPDATA "Programs\PSScriptAnalyzer"
$psScriptAnalyzerManifest = Join-Path $psScriptAnalyzerDir "PSScriptAnalyzer.psd1"
if ((Test-Path $psScriptAnalyzerManifest) -and
    (Import-PowerShellDataFile $psScriptAnalyzerManifest).ModuleVersion -eq $psScriptAnalyzerVersion) {
  Write-Host "PSScriptAnalyzer $psScriptAnalyzerVersion is already installed; skipping."
} else {
  $psScriptAnalyzerTemp = Join-Path $env:TEMP ("PSScriptAnalyzer-" + [guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Path $psScriptAnalyzerTemp | Out-Null
  try {
    $psScriptAnalyzerPackage = Join-Path $psScriptAnalyzerTemp "PSScriptAnalyzer.zip"
    Invoke-WebRequest -Uri "https://www.powershellgallery.com/api/v2/package/PSScriptAnalyzer/$psScriptAnalyzerVersion" `
      -OutFile $psScriptAnalyzerPackage
    $expectedHash = "14e634c828eb98efb9f40b2918ba90f139ed5eccdf663a2a747736d996995d60"
    $actualHash = (Get-FileHash $psScriptAnalyzerPackage -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
      throw "SHA-256 mismatch for PSScriptAnalyzer $psScriptAnalyzerVersion."
    }
    if (Test-Path $psScriptAnalyzerDir) {
      Remove-Item -LiteralPath $psScriptAnalyzerDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $psScriptAnalyzerDir | Out-Null
    tar.exe -xf $psScriptAnalyzerPackage -C $psScriptAnalyzerDir
    if ($LASTEXITCODE -ne 0) {
      throw "Extracting PSScriptAnalyzer $psScriptAnalyzerVersion failed (exit $LASTEXITCODE)."
    }
  } finally {
    Remove-Item -LiteralPath $psScriptAnalyzerTemp -Recurse -Force
  }
}

$repoRoot = Split-Path -Parent $PSScriptRoot

# vcpkg is a pinned git submodule (third_party/vcpkg) rather than a
# machine-wide install, so dependency resolution is reproducible per-clone
# (see CMakeLists.txt). NVIDIA Falcor (ADR-0009) is vendored the same way
# (third_party/falcor, pinned per ADR-0025 - plain upstream, no fork) -
# --recursive also pulls in its own nested submodules (glfw, imgui, ...).
# src/modules/renderer/CMakeLists.txt's own build step applies
# cmake/patches/falcor.patch (a small patch to the vendored copy,
# per ADR-0009) and pulls Falcor's remaining packman-fetched binary
# dependencies lazily, on first build.
git -C $repoRoot submodule update --init --recursive
& "$repoRoot\third_party\vcpkg\bootstrap-vcpkg.bat" -disableMetrics

git -C $repoRoot config core.hooksPath .githooks

# NVIDIA Nsight Aftermath SDK (optional, GPU crash dump capture - see
# src/modules/renderer/CMakeLists.txt, mirroring its own detection).
# Autodetected from an installed Nsight Graphics, which bundles it as a
# component - there's no separate download/setup step, so this only checks
# whether one's installed and reminds you if not. Builds work fine without
# it (FALCOR_HAS_AFTERMATH=0).
$aftermathFromNsightGraphics = Get-ChildItem -Path "$env:ProgramFiles\NVIDIA Corporation" `
  -Filter "Nsight Graphics *" -Directory -ErrorAction SilentlyContinue |
  ForEach-Object { Get-ChildItem -Path "$($_.FullName)\SDKs\NsightAftermathSDK" -Directory -ErrorAction SilentlyContinue } |
  Where-Object { Test-Path "$($_.FullName)\include\GFSDK_Aftermath.h" } |
  Select-Object -First 1
if (-not $aftermathFromNsightGraphics) {
  Write-Host ("No NVIDIA Nsight Graphics install with a bundled Aftermath SDK was found - GPU crash dumps " +
    "stay disabled. To enable: install Nsight Graphics (developer.nvidia.com/nsight-graphics), which bundles " +
    "the Aftermath SDK as a component; augusta picks it up automatically on the next cmake reconfigure.")
}

Write-Host "Windows bootstrap complete. Open a new terminal (this one's PATH predates the tools just installed) before running cmake/ninja."
