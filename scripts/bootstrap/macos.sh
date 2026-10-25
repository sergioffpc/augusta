# shellcheck shell=bash
# The macOS half of scripts/bootstrap.sh (sourced by it), on Apple silicon,
# through Homebrew: native, as scripts/bootstrap/linux.sh is on Ubuntu. The
# macos presets build the client, the server and their tests with Homebrew's
# LLVM 21, CI's (ADR-0008), and the hooks run the formatters and linters on the
# host, at CI's versions. macOS's /bin/bash is 3.2, which runs this.
#
# Without sudo, which a managed Mac may grant only for a while: Homebrew's
# installs are the user's, and the tools it doesn't keep go in the user's
# ~/.local/bin, where uv puts its own.

# The user's bin directory: the release tools, ctcache (uv's tool bin
# directory), and links to LLVM 21's clang-format and clang-tidy.
readonly LOCAL_BIN="${HOME}/.local/bin"

# The login shells' profile, zsh's (macOS's default shell), which takes the
# PATH entries the bootstrap adds.
readonly PROFILE="${HOME}/.zprofile"

# Adds a line to the profile unless it is there, records it, and runs it in
# this process too.
# Arguments:
#   The line.
add_to_profile() {
  if ! grep -qxF -- "$1" "${PROFILE}" 2>/dev/null; then
    manifest_add profile-line "${PROFILE}" "$1"
    printf '%s\n' "$1" >>"${PROFILE}"
  fi
  eval "$1"
}

# Removes a line the bootstrap added to a profile.
# Arguments:
#   The profile, the line.
remove_from_profile() {
  local kept
  [[ -f "$1" ]] || return 0
  kept="$(grep -vxF -- "$2" "$1" || true)"
  printf '%s\n' "${kept}" >"$1"
}

# Starts Podman's virtual machine, creating it first if there is none: Podman
# runs its Linux containers there, the server image's build among them. With
# every core and half the memory: that build compiles the vcpkg dependencies in
# it. The machine mounts only the home directory (podman machine init's
# default), where the checkout is.
start_podman_machine() {
  if ! podman machine inspect >/dev/null 2>&1; then
    manifest_add podman-machine
    podman machine init --cpus "$(sysctl -n hw.ncpu)" \
      --memory "$(($(sysctl -n hw.memsize) / 2 / 1024 / 1024))"
  fi
  if [[ "$(podman machine inspect --format '{{.State}}')" != running ]]; then
    podman machine start
  fi
}

# The manifest (scripts/bootstrap.sh), the user's, as Homebrew's installs are.
manifest_file() {
  echo "${XDG_STATE_HOME:-${HOME}/.local/state}/augusta-bootstrap/manifest"
}

manifest_append() {
  mkdir -p "$(dirname "$(manifest_file)")"
  cat >>"$(manifest_file)"
}

remove_manifest() {
  rm -rf "$(dirname "$(manifest_file)")"
}

require_uninstall_rights() {
  :
}

# Records each of a directory's files that does not exist yet, before the
# install that writes it.
# Arguments:
#   The directory, then the files' names.
record_new_files() {
  local dir="$1" name
  shift
  for name in "$@"; do
    [[ -e "${dir}/${name}" ]] || manifest_add file "${dir}/${name}"
  done
}

# Installs Homebrew formulae, recording those that were not installed.
# Arguments:
#   The formulae.
brew_install() {
  local formula
  for formula in "$@"; do
    brew list --formula "${formula}" >/dev/null 2>&1 \
      || manifest_add brew "${formula}"
  done
  brew install "$@"
}

# Links a command into the user's bin directory.
# Arguments:
#   The command's name there, the file it links to.
link_command() {
  record_new_files "${LOCAL_BIN}" "$1"
  ln -sf "$2" "${LOCAL_BIN}/$1"
}

# Prints the container engine scripts/dev-container.sh takes.
container_engine() {
  if command -v podman >/dev/null 2>&1; then
    echo podman
  else
    echo docker
  fi
}

# Removes every checkout's dev containers, their images and the volumes they
# share, which scripts/dev-container.sh made for an earlier bootstrap, when
# macOS built in the dev container.
remove_dev_containers() {
  local engine
  engine="$(container_engine)"
  command -v "${engine}" >/dev/null 2>&1 || return 0
  remove_listed "${engine}" "ps --all --quiet --filter name=augusta-dev-" rm
  remove_listed "${engine}" "images --quiet augusta-dev" rmi
  remove_listed "${engine}" "volume ls --quiet --filter name=augusta-" \
    "volume rm"
}

# Removes, with --force, what an engine's listing prints, one id per line.
# Arguments:
#   The engine, the listing's arguments, the removal's arguments.
remove_listed() {
  local ids=() id listing removal
  read -r -a listing <<<"$2"
  read -r -a removal <<<"$3"
  while IFS= read -r id; do
    [[ -n "${id}" ]] && ids+=("${id}")
  done < <("$1" "${listing[@]}")
  ((${#ids[@]} == 0)) || "$1" "${removal[@]}" --force "${ids[@]}" >/dev/null
}

# Undoes a manifest entry of this platform's.
# Arguments:
#   Its kind, then its fields.
undo_entry() {
  case "$1" in
    brew) brew uninstall "$2" ;;
    file) rm -f "$2" ;;
    dir) rm -rf "$2" ;;
    profile-line) remove_from_profile "$2" "$3" ;;
    uv-tool) uv tool uninstall "$2" ;;
    podman-machine) podman machine rm --force ;;
    dev-containers) remove_dev_containers ;;
    *) echo "bootstrap: no way to undo $1 here; skipped." >&2 ;;
  esac
}

# Installs a release's command into a bin directory: a plain binary, or one
# member of a tar.gz.
# Arguments:
#   The command's name, the asset's URL, its SHA-256, the member ("-" for a
#   plain binary), the bin directory.
install_release() {
  local name="$1" url="$2" sha="$3" member="$4" bin="$5" tmp
  tmp="$(mktemp -d)"
  download_verified "${url}" "${sha}" "${tmp}/${name}.download"
  if [[ "${member}" = - ]]; then
    mv "${tmp}/${name}.download" "${tmp}/${name}"
  else
    tar -xzf "${tmp}/${name}.download" -C "${tmp}" "${member}"
    mv "${tmp}/${member}" "${tmp}/${name}"
  fi
  record_new_files "${bin}" "${name}"
  install -m 0755 "${tmp}/${name}" "${bin}"
  rm -rf "${tmp}"
}

# The toolchain's darwin-arm64 release downloads, which
# scripts/bootstrap/toolchain.sh installs, in globals: release_url, the asset
# at the version scripts/bootstrap/versions.env pins; release_sha, its SHA-256;
# release_member, the binary's path inside a tar.gz or zip.
# Arguments:
#   The tool.
# Returns:
#   1 when the tool has no release here: luacheck's has no macOS build, so
#   Homebrew's is linked.
# shellcheck disable=SC2034 # release_*: scripts/bootstrap/toolchain.sh reads them
toolchain_release() {
  local github=https://github.com
  release_member=
  case "$1" in
    yamlfmt)
      release_url="${github}/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Darwin_arm64.tar.gz"
      release_sha=4b417ecb94339d57e4c122ecc948c1a00fe328b5853266de9806e652a92858fa
      release_member=yamlfmt
      ;;
    stylua)
      release_url="${github}/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-macos-aarch64.zip"
      release_sha=92ff0889e16324801bc072692974bb67f8161e62010fc90f96c62a17f81f32c7
      release_member=stylua
      ;;
    taplo)
      # Taplo's tags have no "v".
      release_url="${github}/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-darwin-aarch64.gz"
      release_sha=713734314c3e71894b9e77513c5349835eefbd52908445a0d73b0c7dc469347d
      ;;
    buf)
      release_url="${github}/bufbuild/buf/releases/download/v${BUF_VERSION}/buf-Darwin-arm64"
      release_sha=6e6df0fef4522e4e43dfe7c341873c3f2c29ceb45a9dfa5e0bad5580b8b2022f
      ;;
    *) return 1 ;;
  esac
}

# The flux CLI at the cluster's Flux and cosign at CI's, which Homebrew doesn't
# keep at those versions: their darwin-arm64 releases, as
# scripts/bootstrap/linux.sh installs Linux's.
# Arguments:
#   The bin directory.
install_formatters() {
  local bin="$1"
  install_release flux \
    "https://github.com/fluxcd/flux2/releases/download/v${FLUX_VERSION}/flux_${FLUX_VERSION}_darwin_arm64.tar.gz" \
    2869ef7151a6f1b27e6b5d2a6804f3ef23c7bdaa06a74e00d3fe5bfc646547fd flux \
    "${bin}"
  install_release cosign \
    "https://github.com/sigstore/cosign/releases/download/v${COSIGN_VERSION}/cosign-darwin-arm64" \
    5fadd012ae6381a6a29ff86a7d39aa873878852f1073fc90b15995961ecfb084 - \
    "${bin}"
}

# Installs the tools CI's own jobs run (scripts/bootstrap.sh), from their
# darwin-arm64 releases, at CI's versions.
# Arguments:
#   The bin directory.
install_ci_tools() {
  local bin="$1" tools tool name url sha member
  # Each: the command, its release asset, the asset's SHA-256, and the
  # command's path inside the archive ("-" for a plain binary).
  # promtool and amtool sit in a directory named after their archive.
  local prometheus_dir="prometheus-${PROMETHEUS_VERSION}.darwin-arm64"
  local alertmanager_dir="alertmanager-${ALERTMANAGER_VERSION}.darwin-arm64"
  tools=(
    "trivy https://github.com/aquasecurity/trivy/releases/download/v${TRIVY_VERSION}/trivy_${TRIVY_VERSION}_macOS-ARM64.tar.gz 4a77108cccf8e55c8d6823e1e759939a622277e66cd0daa3c1fc621ed69e4568 trivy"
    "kubeconform https://github.com/yannh/kubeconform/releases/download/v${KUBECONFORM_VERSION}/kubeconform-darwin-arm64.tar.gz f84f4dfbebf4a6b0b230385fa065a39ea35e02608c2b50d025dcf64775a69d67 kubeconform"
    "kyverno https://github.com/kyverno/kyverno/releases/download/v${KYVERNO_VERSION}/kyverno-cli_v${KYVERNO_VERSION}_darwin_arm64.tar.gz 2cf5febbedaafca5d3c7819925a49f9723bf4c3337a1327b6f72970480af385a kyverno"
    "promtool https://github.com/prometheus/prometheus/releases/download/v${PROMETHEUS_VERSION}/${prometheus_dir}.tar.gz 920df4d17e78b3b0175af144eb318b0c74d1cf7b1d1251b326966f0e81977260 ${prometheus_dir}/promtool"
    "amtool https://github.com/prometheus/alertmanager/releases/download/v${ALERTMANAGER_VERSION}/${alertmanager_dir}.tar.gz a3941879f340ef12a4cc1a479c82bd8520611265ea8d1ee25c88bd5f944d8293 ${alertmanager_dir}/amtool"
    "hadolint https://github.com/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-macos-arm64 5c09f3213f8e40406abe048233d985eebef336d4a6a20021be47fadb6cf480a2 -"
    "oras https://github.com/oras-project/oras/releases/download/v${ORAS_VERSION}/oras_${ORAS_VERSION}_darwin_arm64.tar.gz d545a238f4c8be6dfae6c3d0ff5b09720c5588641d4a5180fb87c38a64500cc4 oras"
    "syft https://github.com/anchore/syft/releases/download/v${SYFT_VERSION}/syft_${SYFT_VERSION}_darwin_arm64.tar.gz 7e0bdad94c569fc6d5785c9a657bbae3d4c4e140ccb5eace3d0b5b6bc2b6dbcf syft"
    "grype https://github.com/anchore/grype/releases/download/v${GRYPE_VERSION}/grype_${GRYPE_VERSION}_darwin_arm64.tar.gz 938f050bb5076c8aa761867b39843abad2414dfe4cc82b7d36886e634f49c640 grype"
    "yq https://github.com/mikefarah/yq/releases/download/v${YQ_VERSION}/yq_darwin_arm64 cceb0b8d71ea5294334121f8429f33f92b920e7217d904a2f9f35443968ac424 -"
  )
  for tool in "${tools[@]}"; do
    read -r name url sha member <<<"${tool}"
    install_release "${name}" "${url}" "${sha}" "${member}" "${bin}"
  done
}

install_toolchain() {
  if [[ "$(uname -m)" != arm64 ]]; then
    echo "bootstrap: macOS builds on Apple silicon alone (the macos presets)." >&2
    exit 1
  fi
  # The SDK every native build compiles against, and Homebrew's own
  # prerequisite.
  if ! xcode-select -p >/dev/null 2>&1; then
    echo "bootstrap: the Command Line Tools are not installed - run" \
      "'xcode-select --install' and re-run this script." >&2
    exit 1
  fi
  if ! command -v brew >/dev/null 2>&1; then
    echo "bootstrap: Homebrew not found - install it (https://brew.sh) and" \
      "re-run this script." >&2
    exit 1
  fi

  # As scripts/bootstrap/linux.sh's apt packages, from Homebrew. LLVM 21 and
  # its lld, CI's (ADR-0008): the compiler and linker (the macos presets,
  # cmake/LlvmMacosToolchain.cmake), and clang-format and clang-tidy, which
  # the hooks run, so they format and lint as CI's do. LLVM's lldb, which
  # llvm@21 leaves out, for the (lldb) launch configs (.vscode). CMake and
  # Ninja, as the presets build with; GNU make 4, the Makefile's (macOS's own
  # is 3.81); sccache, which the root CMakeLists.txt picks up from PATH;
  # pkg-config and autotools for the vcpkg ports that build with them. Git LFS
  # (pre-push), which Apple's git has none of, and luacheck, which the
  # toolchain links as luacheck has no macOS release, and holds to the pinned
  # LUACHECK_VERSION. uv, for yamllint and the rest uv runs. doxygen: `make
  # docs`. gh: for development. jq: with cosign, the release runbook's wait on
  # a signed image (docs/runbooks/cut-release.md). kubectl and helm, for the
  # server's k3s cluster and its chart (charts/augustad). Podman: the container
  # engine, for the server image. Wireshark's tshark: the Wireshark dissector's
  # test (tools/wireshark). The client's: the Vulkan loader, which it finds
  # through Homebrew's prefix (src/modules/window), and Mesa, whose macOS 26
  # build has KosmicKrisp, a conformant Vulkan on Metal (Apple silicon), and
  # lavapipe, the software Vulkan the renderer smoke test draws on.
  brew_install autoconf autoconf-archive automake cmake doxygen gh git git-lfs \
    helm jq kubernetes-cli libtool lld@21 lldb llvm@21 luacheck make mesa \
    ninja pkg-config podman sccache uv vulkan-loader wireshark

  # The user's bin directory on PATH, after the rest; and GNU make ahead of
  # macOS's 3.81 in /usr/bin, by the directory Homebrew keeps it in under its
  # usual name, as its formula says. Single quotes: the profile expands them.
  local brew_prefix
  brew_prefix="$(brew --prefix)"
  [[ -e "${LOCAL_BIN}" ]] || manifest_add dir "${LOCAL_BIN}"
  mkdir -p "${LOCAL_BIN}"
  # shellcheck disable=SC2016 # expanded by the profile
  add_to_profile 'export PATH="${PATH}:${HOME}/.local/bin"'
  add_to_profile "export PATH=\"${brew_prefix}/opt/make/libexec/gnubin:\${PATH}\""

  # LLVM 21 is keg-only, off PATH: its clang-format and clang-tidy by the names
  # the hooks and the Makefile call.
  link_command clang-format "${brew_prefix}/opt/llvm@21/bin/clang-format"
  link_command clang-tidy "${brew_prefix}/opt/llvm@21/bin/clang-tidy"

  # ctcache (scripts/bootstrap.sh), in uv's tool bin directory, the same.
  uv tool list 2>/dev/null | grep -q '^ctcache ' || manifest_add uv-tool ctcache
  uv tool install "${CTCACHE_PACKAGE}"

  install_formatters "${LOCAL_BIN}"
  install_ci_tools "${LOCAL_BIN}"

  start_podman_machine

  # A tool missing here fails loud now, not at the first commit or push.
  hash -r
  require_commands cmake ninja make git git-lfs sccache uv clang-format \
    clang-tidy clang-tidy-cache doxygen luacheck kubectl helm flux jq cosign \
    podman tshark trivy kubeconform kyverno promtool amtool hadolint oras syft \
    grype yq
}

# The submodules every build uses, as scripts/bootstrap/linux.sh's.
update_submodules() {
  git submodule update --init third_party/vcpkg third_party/nvtx
}

bootstrap_vcpkg() {
  ./third_party/vcpkg/bootstrap-vcpkg.sh -disableMetrics
}

# Removes what bootstrap_vcpkg wrote: vcpkg's tool.
unbootstrap_vcpkg() {
  rm -f third_party/vcpkg/vcpkg
}

bootstrap_done() {
  echo "bootstrap: done. 'make' builds with the macos preset, 'make test" \
    "PRESET=macos-debug' a Debug build; configure one once (make configure)" \
    "so the pre-push hook can run clang-tidy."
}
