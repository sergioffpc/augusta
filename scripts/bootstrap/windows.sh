# shellcheck shell=bash
# The Windows client half of scripts/bootstrap.sh (sourced by it), run in Git
# Bash as Administrator: VS Build Tools system-wide (default install location,
# no --installPath) plus the rest of the client toolchain, winget-driven.

# Prints a Windows environment variable's directory as a Git Bash path. Through
# cmd, which reads the names case-insensitively, as Windows does: Git Bash
# can't name ProgramFiles(x86) at all.
# Arguments:
#   The variable's name.
windows_dir() {
  cygpath -u "$(cmd //c "echo %$1%" | tr -d '\r')"
}

# Extracts a zip archive into a directory, with Windows' own bsdtar: Git Bash's
# tar reads no zip, and it has no unzip.
# Arguments:
#   The archive, the directory.
extract_zip() {
  "$(windows_dir SystemRoot)/System32/tar.exe" -xf "$(cygpath -w "$1")" \
    -C "$(cygpath -w "$2")"
}

# Appends a directory to a registry PATH, Machine or User, unless it is there.
# Arguments:
#   The scope, the directory.
add_to_path() {
  # shellcheck disable=SC2016 # expanded by PowerShell
  SCOPE="$1" DIR="$(cygpath -w "$2")" powershell.exe -NoProfile \
    -NonInteractive -Command '$path = [Environment]::GetEnvironmentVariable("Path", $env:SCOPE); if (($path -split ";") -notcontains $env:DIR) { [Environment]::SetEnvironmentVariable("Path", "$path;$env:DIR", $env:SCOPE) }'
}

# winget/MSI installers update the Machine/User PATH in the registry, but this
# process's own PATH was captured at startup and won't see that change without
# this - without it, the checks below would report a false failure even on a
# fully successful install (this is also why a fresh terminal is needed
# afterward to actually use these tools). Appended, so Git Bash's own tools
# stay ahead of System32's.
refresh_path() {
  local registry
  # shellcheck disable=SC2016 # expanded by PowerShell
  registry="$(powershell.exe -NoProfile -NonInteractive -Command \
    '[Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")' \
    | tr -d '\r')"
  PATH="${PATH}:$(cygpath -p "${registry}")"
}

# Installs a winget package unless it is already installed: a bootstrap
# installs prerequisites; it should not update unrelated existing
# installations as a side effect.
# Arguments:
#   The package id, then any further winget install options.
winget_install() {
  local id="$1"
  shift
  if winget list --id "${id}" --exact --accept-source-agreements \
    >/dev/null 2>&1; then
    echo "${id} is already installed; skipping."
    return
  fi
  echo "Installing ${id}..."
  winget install --id "${id}" --exact --silent \
    --accept-package-agreements --accept-source-agreements "$@"
}

# Installs a winget package at a version, replacing a different installed one
# (--force), and pins it to a winget version pattern so a later `winget
# upgrade` stays within it.
# Arguments:
#   The package id, the version, the pin.
winget_install_pinned() {
  echo "Installing $1 $2..."
  winget install --id "$1" --exact --silent --version "$2" --force \
    --accept-package-agreements --accept-source-agreements
  winget pin add --id "$1" --exact --version "$3" --force
}

# The formatters and linters the hooks and CI's format job run at pinned
# versions, which winget doesn't keep: standalone releases in a user-local bin
# directory.
# Arguments:
#   The bin directory.
install_formatters() {
  local bin="$1" tmp
  mkdir -p "${bin}"
  tmp="$(mktemp -d)"
  download_verified \
    "https://github.com/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Windows_x86_64.tar.gz" \
    07f80ce5d741eb4b0a9380ac78a19c7cb5bd44e2a9a47a5a04839e3ba54dd463 \
    "${tmp}/yamlfmt.tar.gz"
  download_verified \
    "https://github.com/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-windows-x86_64.zip" \
    e77d0ea1226b8b389b43f702240091249a96eea25857281f90ea24d0eb9eb969 \
    "${tmp}/stylua.zip"
  download_verified \
    "https://github.com/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-windows-x86_64.gz" \
    550fdc955343f8a196447a05346ecf8827e391726e4ded577bb00a0a36cf220c \
    "${tmp}/taplo.exe.gz"
  # luacheck's standalone release bundles its own Lua.
  download_verified \
    "https://github.com/lunarmodules/luacheck/releases/download/v${LUACHECK_VERSION}/luacheck.exe" \
    0f1c69c4d09f1ebb4d8df14c215e4553e2e639bd4cb7bf3c639b0daa6198317b \
    "${tmp}/luacheck.exe"
  tar -xzf "${tmp}/yamlfmt.tar.gz" -C "${tmp}" yamlfmt.exe
  extract_zip "${tmp}/stylua.zip" "${tmp}"
  gunzip "${tmp}/taplo.exe.gz"
  cp -f "${tmp}/yamlfmt.exe" "${tmp}/stylua.exe" "${tmp}/taplo.exe" \
    "${tmp}/luacheck.exe" "${bin}"
  rm -rf "${tmp}"
  add_to_path User "${bin}"
}

install_toolchain() {
  if ! net session >/dev/null 2>&1; then
    echo "bootstrap: run Git Bash as Administrator (VS Build Tools and the" \
      "Machine PATH need it)." >&2
    exit 1
  fi

  winget_install Microsoft.VisualStudio.BuildTools --override \
    "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  winget_install Microsoft.WindowsSDK.10.0.26100
  winget_install Kitware.CMake
  winget_install Ninja-build.Ninja
  # GNU make, for the top-level Makefile (a wrapper over the CMake presets).
  winget_install ezwinports.make
  winget_install Git.Git
  winget_install Mozilla.sccache
  winget_install astral-sh.uv
  # The cooker's (tools/pack/scripts/bootstrap.sh): usd-optimize's USD runtime
  # DLLs import MSVCP140.dll/VCRUNTIME140*.dll, not bundled in its wheel (see
  # usd-optimize's own PyPI README). Here, so that bootstrap needs no
  # Administrator.
  winget_install Microsoft.VCRedist.2015+.x64
  # Doxygen builds the documentation site's C++ API reference (`make docs`,
  # ADR-0046), as the docs workflow does with the runner's own.
  winget_install DimitriVanHeesch.Doxygen
  # clang-format and clang-tidy, see docs/ENGINEERING.md, Code Quality. Back
  # the .githooks/pre-commit and .githooks/pre-push hooks. Pinned to the clang
  # CI runs (the ubuntu-26.04 runner's distro package): another major formats
  # and lints differently, so the hooks would disagree with CI's gates. Bump it
  # together with the runner image. The pin keeps `winget upgrade` on that
  # major.
  winget_install_pinned LLVM.LLVM 21.1.8 '21.*'

  # Unlike the other packages here, LLVM's and Doxygen's installers don't add
  # themselves to PATH under winget's --silent flag (that's an
  # interactive-installer checkbox, unchecked by default in silent mode) - add
  # their default install locations explicitly rather than relying on it.
  local program_files bin
  program_files="$(windows_dir ProgramFiles)"
  for bin in "${program_files}/LLVM/bin" "${program_files}/doxygen/bin"; do
    if [[ -d "${bin}" ]]; then
      add_to_path Machine "${bin}"
    fi
  done

  install_formatters "$(windows_dir LOCALAPPDATA)/Programs/augusta-tools"
  refresh_path

  # A silently-missing tool here doesn't fail loud until much later - e.g.
  # clang-format's absence only shows up as ".githooks/pre-commit: not found
  # on PATH - skipping" at commit time, which is easy to miss and leaves every
  # local commit unformatted. Check now, once, instead.
  require_commands cmake ninja make git sccache uv clang-format clang-tidy \
    doxygen yamlfmt stylua taplo luacheck

  local program_files_x86 vs_install
  program_files_x86="$(windows_dir 'ProgramFiles(x86)')"
  vs_install="$("${program_files_x86}/Microsoft Visual Studio/Installer/vswhere.exe" \
    -latest -products '*' \
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
    -property installationPath | tr -d '\r')"
  if [[ -z "${vs_install}" ]]; then
    echo "bootstrap: Visual Studio Build Tools is installed, but its VC C++" \
      "toolset is missing. Add the Microsoft.VisualStudio.Workload.VCTools" \
      "workload and re-run this script." >&2
    exit 1
  fi
  local sdk_include="${program_files_x86}/Windows Kits/10/Include/10.0.26100.0"
  if [[ ! -d "${sdk_include}" ]]; then
    echo "bootstrap: Windows SDK 10.0.26100 headers are missing at" \
      "${sdk_include}." >&2
    exit 1
  fi
}

# vcpkg and NVIDIA Falcor (ADR-0009, pinned per ADR-0025 - plain upstream, no
# fork) are pinned git submodules; --recursive also pulls in Falcor's own
# nested submodules (glfw, imgui, ...). src/modules/renderer/CMakeLists.txt's
# own build step applies cmake/patches/falcor.patch and pulls Falcor's
# remaining packman-fetched binary dependencies lazily, on first build.
update_submodules() {
  git submodule update --init --recursive
}

bootstrap_vcpkg() {
  cmd //c 'third_party\vcpkg\bootstrap-vcpkg.bat -disableMetrics'
}

bootstrap_done() {
  # NVIDIA Nsight Aftermath SDK (optional, GPU crash dump capture - see
  # src/modules/renderer/CMakeLists.txt, mirroring its own detection).
  # Autodetected from an installed Nsight Graphics, which bundles it as a
  # component, so this only reminds you if there is none. Builds work fine
  # without it (FALCOR_HAS_AFTERMATH=0).
  if ! compgen -G "$(windows_dir ProgramFiles)/NVIDIA Corporation/Nsight Graphics */SDKs/NsightAftermathSDK/*/include/GFSDK_Aftermath.h" \
    >/dev/null; then
    echo "No NVIDIA Nsight Graphics install with a bundled Aftermath SDK was" \
      "found - GPU crash dumps stay disabled. To enable: install Nsight" \
      "Graphics (developer.nvidia.com/nsight-graphics), which bundles the" \
      "Aftermath SDK as a component; augusta picks it up automatically on" \
      "the next cmake reconfigure."
  fi
  echo "bootstrap: done. Open a new terminal (this one's PATH predates the" \
    "tools just installed) before running make."
}
