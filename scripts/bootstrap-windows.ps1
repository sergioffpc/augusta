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

Install-WingetPackage -Id "Microsoft.VisualStudio.BuildTools" `
  -Override @("--wait", "--quiet", "--add", "Microsoft.VisualStudio.Workload.VCTools", "--includeRecommended")
Install-WingetPackage -Id "Microsoft.WindowsSDK.10"
Install-WingetPackage -Id "Kitware.CMake"
Install-WingetPackage -Id "Ninja-build.Ninja"
Install-WingetPackage -Id "Git.Git"
Install-WingetPackage -Id "Mozilla.sccache"

$repoRoot = Split-Path -Parent $PSScriptRoot

# vcpkg is a pinned git submodule (third_party/vcpkg) rather than a
# machine-wide install, so dependency resolution is reproducible per-clone
# (see CMakeLists.txt). NVIDIA Falcor (ADR-0009) is vendored the same way
# (third_party/falcor, a pinned fork per ADR-0025) - --recursive also pulls
# in its own nested submodules (glfw, imgui, ...). Falcor's remaining
# packman-fetched binary dependencies are pulled lazily on first build, by
# src/modules/renderer/CMakeLists.txt's own build step.
git -C $repoRoot submodule update --init --recursive
& "$repoRoot\third_party\vcpkg\bootstrap-vcpkg.bat" -disableMetrics

git -C $repoRoot config core.hooksPath .githooks

Write-Host "Windows bootstrap complete. Open a new terminal (this one's PATH predates the tools just installed) before running cmake/ninja."
