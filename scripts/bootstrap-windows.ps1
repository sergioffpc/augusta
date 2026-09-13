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

$vcpkgRoot = "C:\vcpkg"
if (-not (Test-Path $vcpkgRoot)) {
  Write-Host "Cloning vcpkg to $vcpkgRoot..."
  git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
}
& "$vcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics

[Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, "User")
Write-Host "VCPKG_ROOT set to $vcpkgRoot (restart your shell to pick it up)."

$repoRoot = Split-Path -Parent $PSScriptRoot
git -C $repoRoot config core.hooksPath .githooks

Write-Host "Windows bootstrap complete."
