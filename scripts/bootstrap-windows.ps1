#Requires -RunAsAdministrator
<#
Bootstraps the Windows client development environment (docs/ENGINEERING.md,
Developer Environment). Installs VS Build Tools system-wide (default
install location, no --installPath) plus the rest of the client toolchain.
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

Install-WingetPackage -Id "Microsoft.VisualStudio.2022.BuildTools" `
  -Override @("--wait", "--quiet", "--add", "Microsoft.VisualStudio.Workload.VCTools", "--includeRecommended")
Install-WingetPackage -Id "Microsoft.WindowsSDK.10"
Install-WingetPackage -Id "Kitware.CMake"
Install-WingetPackage -Id "Ninja-build.Ninja"
Install-WingetPackage -Id "Git.Git"
Install-WingetPackage -Id "Mozilla.sccache"

$repoRoot = Split-Path -Parent $PSScriptRoot

# vcpkg is a pinned git submodule (third_party/vcpkg) rather than a
# machine-wide install, so dependency resolution is reproducible per-clone
# (see CMakeLists.txt).
git -C $repoRoot submodule update --init --recursive
& "$repoRoot\third_party\vcpkg\bootstrap-vcpkg.bat" -disableMetrics

git -C $repoRoot config core.hooksPath .githooks

Write-Host "Windows bootstrap complete."
