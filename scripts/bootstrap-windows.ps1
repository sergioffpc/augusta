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
# clang-format only - clang-tidy stays CI-only (Linux/WSL), see
# docs/ENGINEERING.md, Code Quality. Backs the .githooks/pre-commit hook
# below.
Install-WingetPackage -Id "LLVM.LLVM"

$repoRoot = Split-Path -Parent $PSScriptRoot

# vcpkg is a pinned git submodule (third_party/vcpkg) rather than a
# machine-wide install, so dependency resolution is reproducible per-clone
# (see CMakeLists.txt). NVIDIA Falcor (ADR-0009) is vendored the same way
# (third_party/falcor, pinned per ADR-0025 - plain upstream, no fork) -
# --recursive also pulls in its own nested submodules (glfw, imgui, ...).
# src/modules/renderer/CMakeLists.txt's own build step applies
# cmake/patches/falcor-augusta.patch (a small patch to the vendored copy,
# per ADR-0009) and pulls Falcor's remaining packman-fetched binary
# dependencies lazily, on first build.
git -C $repoRoot submodule update --init --recursive
& "$repoRoot\third_party\vcpkg\bootstrap-vcpkg.bat" -disableMetrics

git -C $repoRoot config core.hooksPath .githooks

Write-Host "Windows bootstrap complete. Open a new terminal (this one's PATH predates the tools just installed) before running cmake/ninja."
