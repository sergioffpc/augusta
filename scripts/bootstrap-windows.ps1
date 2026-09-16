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

# Unlike the other packages here, LLVM's installer doesn't add itself to
# PATH under winget's --silent flag (that's an interactive-installer
# checkbox, unchecked by default in silent mode) - add its default
# install location explicitly rather than relying on that checkbox.
$llvmBin = "$env:ProgramFiles\LLVM\bin"
if ((Test-Path $llvmBin) -and
    ([System.Environment]::GetEnvironmentVariable("Path", "Machine") -notlike "*$llvmBin*")) {
  $machinePath = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
  [System.Environment]::SetEnvironmentVariable("Path", "$machinePath;$llvmBin", "Machine")
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
$requiredCommands = @("cmake", "ninja", "git", "sccache", "clang-format")
$missing = $requiredCommands | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($missing) {
  throw "Bootstrap installed packages but these commands still aren't on PATH: $($missing -join ', '). " +
    "Try opening a new terminal and re-running this script; if a specific package failed to install, " +
    "check 'winget list' and re-run 'winget install' for it directly."
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
