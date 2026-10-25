# shellcheck shell=bash
# The Linux/server half of scripts/bootstrap.sh (sourced by it), on Ubuntu
# 26.04, CI's runner release, whose distro clang/clang-tidy/clang-format fix
# CI's LLVM major. .devcontainer's image hashes this file and bootstrap.sh
# (scripts/dev-container.sh), so a Windows-only change doesn't rebuild it.

# Runs its arguments as root: directly when already root (an image build),
# through sudo otherwise.
as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

# Where uv keeps ctcache's tool environment and the Python it fetches, for
# every user.
readonly UV_SHARED=/usr/local/share/uv

# Runs uv as root on the shared tool directory. uv by its path, which sudo's
# own PATH may lack.
uv_shared() {
  as_root env UV_TOOL_DIR="${UV_SHARED}/tools" UV_TOOL_BIN_DIR=/usr/local/bin \
    UV_PYTHON_INSTALL_DIR="${UV_SHARED}/python" "$(command -v uv)" "$@"
}

# The manifest (scripts/bootstrap.sh) is the system's, as what it records is.
manifest_file() {
  echo /var/lib/augusta-bootstrap/manifest
}

manifest_append() {
  as_root mkdir -p "$(dirname "$(manifest_file)")"
  as_root tee -a "$(manifest_file)" >/dev/null
}

remove_manifest() {
  as_root rm -rf "$(dirname "$(manifest_file)")"
}

# sudo asks as each step needs it.
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

# Undoes a manifest entry of this platform's.
# Arguments:
#   Its kind, then its fields.
undo_entry() {
  case "$1" in
    # --autoremove also takes the dependencies they pulled in, and any other
    # package nothing needs any more.
    apt)
      as_root env DEBIAN_FRONTEND=noninteractive apt-get purge -y \
        --autoremove "${@:2}"
      ;;
    file) as_root rm -f "$2" ;;
    dir) as_root rm -rf "$2" ;;
    uv-tool) uv_shared tool uninstall "$2" ;;
    *) echo "bootstrap: no way to undo $1 here; skipped." >&2 ;;
  esac
}

# Prints the toolchain's release binaries for this machine's architecture,
# which scripts/bootstrap/toolchain.sh installs (ADR-0077) at the versions
# scripts/bootstrap/versions.env pins: a line each of the tool, its release
# asset, the asset's SHA-256, and the tool's path inside the tar.gz ("-" for a
# plain binary). Bump each SHA-256 with its version.
toolchain_releases() {
  local arch tools
  case "$(uname -m)" in
    x86_64) arch=amd64 ;;
    aarch64) arch=arm64 ;;
    *)
      echo "toolchain: unsupported architecture: $(uname -m)" >&2
      exit 1
      ;;
  esac
  local trivy="https://github.com/aquasecurity/trivy/releases/download/v${TRIVY_VERSION}/trivy_${TRIVY_VERSION}"
  local kubeconform="https://github.com/yannh/kubeconform/releases/download/v${KUBECONFORM_VERSION}/kubeconform"
  local kyverno="https://github.com/kyverno/kyverno/releases/download/v${KYVERNO_VERSION}/kyverno-cli_v${KYVERNO_VERSION}"
  # promtool and amtool sit in a directory named after their archive.
  local prometheus_dir="prometheus-${PROMETHEUS_VERSION}.linux-${arch}"
  local prometheus="https://github.com/prometheus/prometheus/releases/download/v${PROMETHEUS_VERSION}/${prometheus_dir}.tar.gz"
  local alertmanager_dir="alertmanager-${ALERTMANAGER_VERSION}.linux-${arch}"
  local alertmanager="https://github.com/prometheus/alertmanager/releases/download/v${ALERTMANAGER_VERSION}/${alertmanager_dir}.tar.gz"
  local hadolint="https://github.com/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint"
  local oras="https://github.com/oras-project/oras/releases/download/v${ORAS_VERSION}/oras_${ORAS_VERSION}"
  local syft="https://github.com/anchore/syft/releases/download/v${SYFT_VERSION}/syft_${SYFT_VERSION}"
  local grype="https://github.com/anchore/grype/releases/download/v${GRYPE_VERSION}/grype_${GRYPE_VERSION}"
  local yq="https://github.com/mikefarah/yq/releases/download/v${YQ_VERSION}/yq"
  local flux="https://github.com/fluxcd/flux2/releases/download/v${FLUX_VERSION}/flux_${FLUX_VERSION}"
  local cosign="https://github.com/sigstore/cosign/releases/download/v${COSIGN_VERSION}/cosign"
  case "${arch}" in
    amd64)
      tools=(
        "trivy ${trivy}_Linux-64bit.tar.gz c6e65abddb348e25f10549df887045629cf28cc72453cd1c63acb717316b3f3f trivy"
        "kubeconform ${kubeconform}-linux-amd64.tar.gz 9bc2bffbf71f261128533edaf912153948b7ff238f9a531ae6d34466ec287883 kubeconform"
        "kyverno ${kyverno}_linux_x86_64.tar.gz b38228f367fc0fdc2b08f4c83ea50ac5f16c60ff8d62d76a66157c33c47b70ae kyverno"
        "promtool ${prometheus} 2a542df32eac02ee17b9d844fb2aa1de00dafa5476579ba8a3ba862e9d572ea0 ${prometheus_dir}/promtool"
        "amtool ${alertmanager} 265b9d1e55ef0d5306a436018af6d2b686c2ce051f03d968f7464ecb1372a7e8 ${alertmanager_dir}/amtool"
        "hadolint ${hadolint}-linux-x86_64 c7187db94eeeeca956519a6af171adc31453941a1e777961f6e680f697c8c507 -"
        "oras ${oras}_linux_amd64.tar.gz d52c4af76ce6a3ceb8579e51fb751a43ac051cca67f965f973a0b0e897a2bb86 oras"
        "syft ${syft}_linux_amd64.tar.gz 54a87372498168b2d033e876fd41fa4e8035b872699e525a57046e1f2f09c860 syft"
        "grype ${grype}_linux_amd64.tar.gz 1d444c5e7360471815f7158f71935fcecc68a3c417d85c7344f770854300bba2 grype"
        "yq ${yq}_linux_amd64 c5f056448f973ae7d39b5401949648a78f2dc1947d6a8eb65be60d5c504b9385 -"
        "flux ${flux}_linux_amd64.tar.gz b853df82adfd7736f580692f9f734473d571606307139f8fd20c2a80dd1ff473 flux"
        "cosign ${cosign}-linux-amd64 c956e5dfcac53d52bcf058360d579472f0c1d2d9b69f55209e256fe7783f4c74 -"
      )
      ;;
    arm64)
      tools=(
        "trivy ${trivy}_Linux-ARM64.tar.gz a1ee9f6ffb7d112b64ff726a2a0717c21175c1114361391f4a132956751a13b3 trivy"
        "kubeconform ${kubeconform}-linux-arm64.tar.gz 1f53fc8e81258197a35e8603054162a5af1de8c5af13746c71ab680d9534ed87 kubeconform"
        "kyverno ${kyverno}_linux_arm64.tar.gz d78fecd183e1e6749c653fdb744c88d065e6737ec6856c97c4efac84bec54554 kyverno"
        "promtool ${prometheus} f1f90ec08e849d494ca66c611470afc50192f0355f1a61c33f2cbde02d067823 ${prometheus_dir}/promtool"
        "amtool ${alertmanager} d98d6cbaf52151c7e76e24355fec88b11cebcb9875d4cdd8b76ddce7a7e5535c ${alertmanager_dir}/amtool"
        "hadolint ${hadolint}-linux-arm64 f6198ef8090f404dbb771abfee086eb8c48ac177f30da7fd3510aca35b344b5d -"
        "oras ${oras}_linux_arm64.tar.gz 79946ad57d732836f9242f903f476b6fa484c451d659f121bce54d931ab2a044 oras"
        "syft ${syft}_linux_arm64.tar.gz ee6d4566373a05b344bc6b5f1706f14419bf9338ba39ff686e247deefe9b8818 syft"
        "grype ${grype}_linux_arm64.tar.gz 32aceeb8ee837244775fcb522372c8b3a47914986385f3148f4ee2c930482a84 grype"
        "yq ${yq}_linux_arm64 88a1016bc1d657375a35864e4f44b6f333df8ff97b559f51bba0adcb2169df09 -"
        "flux ${flux}_linux_arm64.tar.gz f3e159af616ec0b9bd0a405c2185cf09d06b74652c1de3c7f377e8166826651a flux"
        "cosign ${cosign}-linux-arm64 bedac92e8c3729864e13d4a17048007cfafa79d5deca993a43a90ffe018ef2b8 -"
      )
      ;;
  esac
  printf '%s\n' "${tools[@]}"
}

install_toolchain() {
  # shellcheck source=/dev/null
  . /etc/os-release
  if [[ "${ID}" != ubuntu || "${VERSION_ID}" != 26.04 ]]; then
    echo "bootstrap: ${PRETTY_NAME} is not Ubuntu 26.04, CI's runner" \
      "release - its clang may format and lint differently from CI's." >&2
  fi

  # From .github/actions/setup-linux-build (which says why each): clang, lld,
  # llvm, libc++-dev, libc++abi-dev, libunwind-21-dev, libclang-rt-dev,
  # ninja-build, autoconf, autoconf-archive, automake, libtool. From the
  # server-x64-linux job: clang-tidy.
  # Already on CI's runner image: make, cmake, git, curl, zip, unzip, tar,
  # pkg-config. clangd: the editor's, which .toolchain/bin links with
  # clang-format and clang-tidy (scripts/bootstrap/toolchain.sh). For the
  # hooks: clang-format, git-lfs (pre-push), and lua-check
  # (luacheck), which this release packages at CI's pinned LUACHECK_VERSION for
  # every architecture, unlike luacheck's own release. sccache: the root
  # CMakeLists.txt picks it up from PATH. doxygen: `make docs`. lldb and gh:
  # for development. jq: with the toolchain's cosign, the release runbook's
  # wait on a signed image (docs/runbooks/cut-release.md). podman: the
  # container engine, for scripts/dev-container.sh and the server image.
  # tshark: the Wireshark dissector's test (tools/wireshark), as CI's job
  # installs it, so preseeded to install dumpcap without setuid and ask
  # nothing. auditd: scripts/augustad-seccomp.sh record.
  # The client's (AUGUSTA_CLIENT), as the nightly's client-x64-linux job
  # installs them: vcpkg's GLFW builds against the system's X11 (libx11-dev,
  # libxcursor-dev, libxi-dev, libxinerama-dev, libxrandr-dev), Wayland
  # (libwayland-dev, with wayland-scanner) and xkbcommon (libxkbcommon-dev)
  # development files, and its header includes OpenGL's (libgl-dev), as ImGui's
  # GLFW binding builds it. GLFW loads those libraries at runtime, as miniaudio
  # does PipeWire's, PulseAudio's or ALSA's, which need no headers. The
  # renderer smoke test draws on Mesa's lavapipe (mesa-vulkan-drivers) through
  # the system's Vulkan loader (libvulkan1), in a virtual X server where there
  # is no display (xvfb, and xauth for xvfb-run).
  echo "wireshark-common wireshark-common/install-setuid boolean false" \
    | as_root debconf-set-selections
  as_root apt-get update
  local packages=(
    auditd
    autoconf
    autoconf-archive
    automake
    ca-certificates
    clang
    clang-format
    clang-tidy
    clangd
    cmake
    curl
    doxygen
    gh
    git
    git-lfs
    jq
    libc++-dev
    libc++abi-dev
    libclang-rt-dev
    libgl-dev
    libtool
    libunwind-21-dev
    libvulkan1
    libwayland-dev
    libx11-dev
    libxcursor-dev
    libxi-dev
    libxinerama-dev
    libxkbcommon-dev
    libxrandr-dev
    lld
    lldb
    llvm
    lua-check
    make
    mesa-vulkan-drivers
    ninja-build
    pkg-config
    podman
    sccache
    sudo
    tar
    tshark
    unzip
    xauth
    xvfb
    zip
  )
  # The manifest takes the packages this run installs, not those already
  # there.
  local package new_packages=()
  for package in "${packages[@]}"; do
    dpkg-query -W -f '${Status}' "${package}" 2>/dev/null \
      | grep -q 'install ok installed' || new_packages+=("${package}")
  done
  as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    --no-install-recommends "${packages[@]}"
  if ((${#new_packages[@]} > 0)); then
    manifest_add apt "${new_packages[@]}"
  fi

  # The hooks' and CI's format job's formatters and linters that Ubuntu doesn't
  # package at CI's versions: uv (for yamllint and the rest uv runs), yamlfmt,
  # StyLua and taplo. uv is its latest release: what it runs is pinned.
  local arch yamlfmt_arch yamlfmt_sha stylua_arch stylua_sha taplo_arch \
    taplo_sha
  arch="$(dpkg --print-architecture)"
  case "${arch}" in
    amd64)
      yamlfmt_arch=x86_64
      yamlfmt_sha=1f300d9257b232bb3b541d7fb1b0e6b3c121bcbab381c86cd38cb8722be8a566
      stylua_arch=x86_64
      stylua_sha=bcb0d855e91f102f28a370e850f8566b3b44b79e6274d806ea5246837c0fd5ab
      taplo_arch=x86_64
      taplo_sha=8fe196b894ccf9072f98d4e1013a180306e17d244830b03986ee5e8eabeb6156
      ;;
    arm64)
      yamlfmt_arch=arm64
      yamlfmt_sha=5b2689c963b177271330c5ce8ca7396751107e5a826be46f03d2cb9b6f0c7784
      stylua_arch=aarch64
      stylua_sha=0ef2ebf0b7e5a652b65c4cb96c6d9ffb3981a98547de3c764465bbf54a8d761a
      taplo_arch=aarch64
      taplo_sha=033681d01eec8376c3fd38fa3703c79316f5e14bb013d859943b60a07bccdcc3
      ;;
    *)
      echo "bootstrap: unsupported architecture: ${arch}" >&2
      exit 1
      ;;
  esac
  if ! command -v uv >/dev/null 2>&1; then
    record_new_files /usr/local/bin uv uvx
    curl -LsSf https://astral.sh/uv/install.sh \
      | as_root env UV_UNMANAGED_INSTALL=/usr/local/bin sh
  fi
  # ctcache (scripts/bootstrap.sh) for every user, the dev container's too: its
  # tool environment and the Python uv may fetch for it under /usr/local, not
  # root's home. uv by its path, which sudo's own PATH may lack.
  [[ -e "${UV_SHARED}" ]] || manifest_add dir "${UV_SHARED}"
  if ! uv_shared tool list 2>/dev/null | grep -q '^ctcache '; then
    manifest_add uv-tool ctcache
  fi
  uv_shared tool install "${CTCACHE_PACKAGE}"
  local tmp
  tmp="$(mktemp -d)"
  download_verified \
    "https://github.com/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Linux_${yamlfmt_arch}.tar.gz" \
    "${yamlfmt_sha}" "${tmp}/yamlfmt.tar.gz"
  download_verified \
    "https://github.com/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-linux-${stylua_arch}.zip" \
    "${stylua_sha}" "${tmp}/stylua.zip"
  download_verified \
    "https://github.com/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-linux-${taplo_arch}.gz" \
    "${taplo_sha}" "${tmp}/taplo.gz"
  tar -xzf "${tmp}/yamlfmt.tar.gz" -C "${tmp}" yamlfmt
  unzip -q "${tmp}/stylua.zip" -d "${tmp}"
  gunzip "${tmp}/taplo.gz"
  record_new_files /usr/local/bin yamlfmt stylua taplo
  as_root install -m 0755 "${tmp}/yamlfmt" "${tmp}/stylua" "${tmp}/taplo" \
    /usr/local/bin
  rm -rf "${tmp}"

  # kubectl and helm, for the server's k3s cluster and its chart
  # (charts/augustad): each one's latest stable release, which Ubuntu doesn't
  # package.
  local kubectl_version
  record_new_files /usr/local/bin kubectl helm
  kubectl_version="$(curl -fsSL https://dl.k8s.io/release/stable.txt)"
  as_root curl -fsSLo /usr/local/bin/kubectl \
    "https://dl.k8s.io/release/${kubectl_version}/bin/linux/${arch}/kubectl"
  as_root chmod 0755 /usr/local/bin/kubectl
  curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 \
    | as_root bash
}

# The repository's submodules.
update_submodules() {
  git submodule update --init third_party/vcpkg third_party/nvtx
}

bootstrap_vcpkg() {
  ./third_party/vcpkg/bootstrap-vcpkg.sh -disableMetrics
  mkdir -p .vcpkg-bincache
}

# Removes what bootstrap_vcpkg wrote: vcpkg's tool and the binary cache.
unbootstrap_vcpkg() {
  rm -rf third_party/vcpkg/vcpkg .vcpkg-bincache
}

bootstrap_done() {
  # The dev container sets this itself (devcontainer.json).
  echo "bootstrap: done. Builds use CI's Linux binary cache with:" \
    "export VCPKG_BINARY_SOURCES=\"clear;files,${PWD}/.vcpkg-bincache,readwrite\""
}
