# shellcheck shell=bash
# The Windows client half of scripts/bootstrap.sh (sourced by it), run in Git
# Bash as Administrator: the client's system packages, winget-driven. No
# Visual Studio: the build takes the MSVC STL, the CRT and the Windows SDK
# from the sysroot `make toolchain` installs (ADR-0079).

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
#   The archive, the directory, then the members to extract, or none for all.
extract_zip() {
  "$(windows_dir SystemRoot)/System32/tar.exe" -xf "$(cygpath -w "$1")" \
    -C "$(cygpath -w "$2")" "${@:3}"
}

# Appends a directory to a registry PATH, Machine or User, unless it is there,
# and records it when it was not.
# Arguments:
#   The scope, the directory.
add_to_path() {
  local dir added
  dir="$(cygpath -w "$2")"
  # shellcheck disable=SC2016 # expanded by PowerShell
  added="$(SCOPE="$1" DIR="${dir}" powershell.exe -NoProfile \
    -NonInteractive -Command '$path = [Environment]::GetEnvironmentVariable("Path", $env:SCOPE); if (($path -split ";") -notcontains $env:DIR) { [Environment]::SetEnvironmentVariable("Path", "$path;$env:DIR", $env:SCOPE); "added" }' \
    | tr -d '\r')"
  [[ "${added}" = added ]] && manifest_add path "$1" "${dir}"
  return 0
}

# Removes a directory from a registry PATH, Machine or User.
# Arguments:
#   The scope, the directory (a Windows path).
remove_from_path() {
  # shellcheck disable=SC2016 # expanded by PowerShell
  SCOPE="$1" DIR="$2" powershell.exe -NoProfile -NonInteractive -Command \
    '$path = [Environment]::GetEnvironmentVariable("Path", $env:SCOPE); [Environment]::SetEnvironmentVariable("Path", (($path -split ";") | Where-Object { $_ -ne $env:DIR }) -join ";", $env:SCOPE)'
}

# Fails unless this Git Bash runs as Administrator.
require_administrator() {
  if ! net session >/dev/null 2>&1; then
    echo "bootstrap: run Git Bash as Administrator (the system-wide" \
      "packages and the Machine PATH need it)." >&2
    exit 1
  fi
}

# The manifest (scripts/bootstrap.sh), the user's: the bootstrap runs as them,
# elevated.
manifest_file() {
  echo "$(windows_dir LOCALAPPDATA)/augusta-bootstrap/manifest"
}

manifest_append() {
  mkdir -p "$(dirname "$(manifest_file)")"
  cat >>"$(manifest_file)"
}

remove_manifest() {
  rm -rf "$(dirname "$(manifest_file)")"
}

require_uninstall_rights() {
  require_administrator
}

# Records each of a directory's files that does not exist yet, before the
# install that writes it.
# Arguments:
#   The directory, then the files' names.
record_new_files() {
  local dir="$1" name
  shift
  for name in "$@"; do
    [[ -e "${dir}/${name}" ]] \
      || manifest_add file "$(cygpath -w "${dir}/${name}")"
  done
}

# Undoes a manifest entry of this platform's.
# Arguments:
#   Its kind, then its fields.
undo_entry() {
  case "$1" in
    winget)
      winget uninstall --id "$2" --exact --silent --accept-source-agreements
      ;;
    winget-pin) winget pin remove --id "$2" --exact ;;
    # Recorded only by a bootstrap that still installed the Build Tools.
    vs-components)
      local args=(modify --installPath "$2" --quiet --norestart) component
      for component in "${@:3}"; do
        args+=(--remove "${component}")
      done
      "$(windows_dir 'ProgramFiles(x86)')/Microsoft Visual Studio/Installer/setup.exe" \
        "${args[@]}" >/dev/null
      ;;
    path) remove_from_path "$2" "$3" ;;
    file) rm -f "$(cygpath -u "$2")" ;;
    dir) rm -rf "$(cygpath -u "$2")" ;;
    uv-tool) uv tool uninstall "$2" ;;
    podman-machine) podman machine rm --force ;;
    *) echo "bootstrap: no way to undo $1 here; skipped." >&2 ;;
  esac
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
  manifest_add winget "${id}"
}

# Installs a winget package at a version, replacing a different installed one
# (--force), and pins it to a winget version pattern so a later `winget
# upgrade` stays within it.
# Arguments:
#   The package id, the version, the pin.
winget_install_pinned() {
  winget list --id "$1" --exact --accept-source-agreements >/dev/null 2>&1 \
    || manifest_add winget "$1"
  winget pin list --id "$1" --exact 2>/dev/null | grep -qF "$1" \
    || manifest_add winget-pin "$1"
  echo "Installing $1 $2..."
  winget install --id "$1" --exact --silent --version "$2" --force \
    --accept-package-agreements --accept-source-agreements
  winget pin add --id "$1" --exact --version "$3" --force
}

# The toolchain's release downloads, which scripts/bootstrap/toolchain.sh
# installs, in globals: release_url, the asset at the version
# scripts/bootstrap/versions.env pins; release_sha, its SHA-256;
# release_member, the binary's path inside a tar.gz or zip.
# Arguments:
#   The tool.
# Returns:
#   1 when the tool has no release here.
# shellcheck disable=SC2034 # release_*: scripts/bootstrap/toolchain.sh reads them
toolchain_release() {
  local github=https://github.com
  release_member=
  case "$1" in
    yamlfmt)
      release_url="${github}/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Windows_x86_64.tar.gz"
      release_sha=07f80ce5d741eb4b0a9380ac78a19c7cb5bd44e2a9a47a5a04839e3ba54dd463
      release_member=yamlfmt.exe
      ;;
    stylua)
      release_url="${github}/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-windows-x86_64.zip"
      release_sha=e77d0ea1226b8b389b43f702240091249a96eea25857281f90ea24d0eb9eb969
      release_member=stylua.exe
      ;;
    luacheck)
      # A standalone binary, its own Lua bundled.
      release_url="${github}/lunarmodules/luacheck/releases/download/v${LUACHECK_VERSION}/luacheck.exe"
      release_sha=0f1c69c4d09f1ebb4d8df14c215e4553e2e639bd4cb7bf3c639b0daa6198317b
      ;;
    taplo)
      # Taplo's tags have no "v".
      release_url="${github}/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-windows-x86_64.gz"
      release_sha=550fdc955343f8a196447a05346ecf8827e391726e4ded577bb00a0a36cf220c
      ;;
    xwin)
      # Which makes the MSVC sysroot (scripts/bootstrap/toolchain/sysroot.sh).
      # xwin's tags have no "v".
      release_url="${github}/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-x86_64-pc-windows-msvc.tar.gz"
      release_sha=96e83665ef5c1406aabd0be603a8d08e67e604f36fda630b6807ee948a614e26
      release_member=xwin-${XWIN_VERSION}-x86_64-pc-windows-msvc/xwin.exe
      ;;
    buf)
      release_url="${github}/bufbuild/buf/releases/download/v${BUF_VERSION}/buf-Windows-x86_64.exe"
      release_sha=13542f2892c4f774150ddb525266d6421d457b3e741297056b64427853526e36
      ;;
    # The tools CI's own jobs run and the operators' flux and cosign
    # (scripts/bootstrap/versions.env).
    trivy)
      release_url="${github}/aquasecurity/trivy/releases/download/v${TRIVY_VERSION}/trivy_${TRIVY_VERSION}_windows-64bit.zip"
      release_sha=4e43bd71a30f51aee39525f60f2b47043af77eb8df8fe082aae4372b69c6660f
      release_member=trivy.exe
      ;;
    kubeconform)
      release_url="${github}/yannh/kubeconform/releases/download/v${KUBECONFORM_VERSION}/kubeconform-windows-amd64.zip"
      release_sha=e3f56102bcf4f50b034a567e2482a1c5330799983ddd655952310211aef73d93
      release_member=kubeconform.exe
      ;;
    kyverno)
      release_url="${github}/kyverno/kyverno/releases/download/v${KYVERNO_VERSION}/kyverno-cli_v${KYVERNO_VERSION}_windows_x86_64.zip"
      release_sha=fe329f8ec5cfb19d3776d6a9e568d9857e3bf2be6ed82c6feb12d4fa73b626b8
      release_member=kyverno.exe
      ;;
    promtool)
      release_url="${github}/prometheus/prometheus/releases/download/v${PROMETHEUS_VERSION}/prometheus-${PROMETHEUS_VERSION}.windows-amd64.zip"
      release_sha=5d333b385557d9adc2ff015d13da9809baccc52fb800a82d1e5c94b79258f87e
      release_member=prometheus-${PROMETHEUS_VERSION}.windows-amd64/promtool.exe
      ;;
    amtool)
      release_url="${github}/prometheus/alertmanager/releases/download/v${ALERTMANAGER_VERSION}/alertmanager-${ALERTMANAGER_VERSION}.windows-amd64.zip"
      release_sha=69624d6ce3674dbcf8cefc5a93d7028d00bbd5e0196a8384907f51593a495fef
      release_member=alertmanager-${ALERTMANAGER_VERSION}.windows-amd64/amtool.exe
      ;;
    hadolint)
      release_url="${github}/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-windows-x86_64.exe"
      release_sha=01d927294962b5387f9ead4f18679158452be4f17c765ad0bdffe5264b9c7b0a
      ;;
    oras)
      release_url="${github}/oras-project/oras/releases/download/v${ORAS_VERSION}/oras_${ORAS_VERSION}_windows_amd64.zip"
      release_sha=8cf69e46f966e592ad36261c05b8f59c02125743b7261c03f6badc3919b40cb0
      release_member=oras.exe
      ;;
    syft)
      release_url="${github}/anchore/syft/releases/download/v${SYFT_VERSION}/syft_${SYFT_VERSION}_windows_amd64.zip"
      release_sha=77f4b472779058e819eec9a054753a5071a996aaa40db31a290f8b256748593f
      release_member=syft.exe
      ;;
    grype)
      release_url="${github}/anchore/grype/releases/download/v${GRYPE_VERSION}/grype_${GRYPE_VERSION}_windows_amd64.zip"
      release_sha=82fb07f246e61526e8f2bf187fc6bb29ed23c7450cda6e9d1868a60fb942fc98
      release_member=grype.exe
      ;;
    yq)
      release_url="${github}/mikefarah/yq/releases/download/v${YQ_VERSION}/yq_windows_amd64.exe"
      release_sha=ece3dd8bb50d39f93610506273ea262feb91e5c486bbddbb10abf91b2a6c0f14
      ;;
    flux)
      release_url="${github}/fluxcd/flux2/releases/download/v${FLUX_VERSION}/flux_${FLUX_VERSION}_windows_amd64.zip"
      release_sha=1a23053bf9abbedbdac07237f5c6ae9b0da0bcb77ddbd103faa99839c6a7f562
      release_member=flux.exe
      ;;
    cosign)
      release_url="${github}/sigstore/cosign/releases/download/v${COSIGN_VERSION}/cosign-windows-amd64.exe"
      release_sha=9b85a88ebff2d9dd30ff4984a6f61f2cedc232dd87d81fa7f2ff3c0ed96c241c
      ;;
    *) return 1 ;;
  esac
}

# Starts Podman's virtual machine, creating it first if there is none: Podman
# runs its Linux containers there, on WSL 2. As scripts/bootstrap/macos.sh's.
start_podman_machine() {
  local state
  if ! podman machine inspect >/dev/null 2>&1; then
    manifest_add podman-machine
    podman machine init
  fi
  state="$(podman machine inspect --format '{{.State}}' | tr -d '\r')"
  if [[ "${state}" != running ]]; then
    podman machine start
  fi
}

install_platform() {
  require_administrator

  winget_install Kitware.CMake
  winget_install Ninja-build.Ninja
  # GNU make, for the top-level Makefile (a wrapper over the CMake presets).
  winget_install ezwinports.make
  winget_install Git.Git
  winget_install Mozilla.sccache
  winget_install astral-sh.uv
  # Doxygen builds the documentation site's C++ API reference (`make docs`,
  # ADR-0046), as the docs workflow does with the runner's own.
  winget_install DimitriVanHeesch.Doxygen
  # LLVM: clang-cl and lld-link, which the windows presets compile and link
  # with (ADR-0008), against the MSVC sysroot of `make toolchain` (ADR-0079),
  # and clang-format and clang-tidy, see docs/ENGINEERING.md,
  # Code Quality, which back the .githooks/pre-commit and .githooks/pre-push
  # hooks. Pinned to the clang CI runs, LLVM_VERSION
  # (scripts/bootstrap/versions.env): another version compiles, formats and
  # lints differently, so a local build and the hooks would disagree with CI's
  # gates. The pin keeps `winget upgrade` on that major.
  winget_install_pinned LLVM.LLVM "${LLVM_VERSION}" "${LLVM_VERSION%%.*}.*"
  # kubectl and helm, for the server's k3s cluster and its chart
  # (charts/augustad), and with the toolchain's flux the cluster
  # runbooks' day-to-day access (scripts/operator-kubeconfig.sh): each one's
  # latest stable release, as scripts/bootstrap/linux.sh's.
  winget_install Kubernetes.kubectl
  winget_install Helm.Helm
  # jq: with the toolchain's cosign, the release runbook's wait on a signed
  # image (docs/runbooks/cut-release.md).
  winget_install jqlang.jq
  # Podman: the container engine, for scripts/dev-container.sh and the server
  # image. Wireshark for its tshark: the Wireshark dissector's test
  # (tools/wireshark). Each one's latest release.
  winget_install RedHat.Podman
  winget_install WiresharkFoundation.Wireshark

  # Unlike the other packages here, LLVM's, Doxygen's and Wireshark's installers
  # don't add themselves to PATH under winget's --silent flag (that's an
  # interactive-installer checkbox, unchecked by default in silent mode) - add
  # their default install locations explicitly rather than relying on it.
  # Git's bin directory too (bash.exe, sh.exe), which Git's installer leaves off
  # PATH (only its cmd directory is on it): scripts/*.sh then run from
  # PowerShell, cmd and VS Code as `bash scripts/<script>.sh`. On the Machine
  # PATH, which Windows puts ahead of the User one, so it wins over WSL's
  # bash.exe alias in the User PATH's WindowsApps.
  local program_files bin
  program_files="$(windows_dir ProgramFiles)"
  for bin in "${program_files}/LLVM/bin" "${program_files}/doxygen/bin" \
    "${program_files}/Wireshark" "${program_files}/Git/bin"; do
    if [[ -d "${bin}" ]]; then
      add_to_path Machine "${bin}"
    fi
  done

  refresh_path
  start_podman_machine

  # A silently-missing tool here doesn't fail loud until much later - e.g.
  # clang-format's absence only shows up as ".githooks/pre-commit: not found
  # on PATH - skipping" at commit time, which is easy to miss and leaves every
  # local commit unformatted. Check now, once, instead.
  require_commands cmake ninja make git sccache uv clang-cl lld-link \
    clang-format clang-tidy doxygen kubectl helm jq podman tshark
}

# vcpkg and NVTX are pinned git submodules (ADR-0025 - plain upstream, no
# fork).
update_submodules() {
  git submodule update --init third_party/vcpkg third_party/nvtx
}

bootstrap_vcpkg() {
  cmd //c 'third_party\vcpkg\bootstrap-vcpkg.bat -disableMetrics'
}

# Removes what bootstrap_vcpkg wrote: vcpkg's tool.
unbootstrap_vcpkg() {
  rm -f third_party/vcpkg/vcpkg.exe
}

bootstrap_done() {
  # NVIDIA Nsight Aftermath SDK (optional, GPU crash dump capture - see
  # cmake/Aftermath.cmake, mirroring its own detection).
  # Autodetected from an installed Nsight Graphics, which bundles it as a
  # component, so this only reminds you if there is none. Builds work fine
  # without it.
  if ! compgen -G "$(windows_dir ProgramFiles)/NVIDIA Corporation/Nsight Graphics */SDKs/NsightAftermathSDK/*/include/GFSDK_Aftermath.h" \
    >/dev/null; then
    echo "No NVIDIA Nsight Graphics install with a bundled Aftermath SDK was" \
      "found - GPU crash dumps (ADR-0059) need it. To have it: install" \
      "Nsight Graphics (developer.nvidia.com/nsight-graphics), which bundles" \
      "the Aftermath SDK as a component."
  fi
  echo "bootstrap: done. Open a new terminal (this one's PATH predates the" \
    "tools just installed) before running make."
}
