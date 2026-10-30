# shellcheck shell=bash
# The Linux/server half of scripts/bootstrap.sh (sourced by it), on Ubuntu
# 26.04, CI's runner release, whose distro clang/clang-tidy/clang-format fix
# CI's LLVM major. .devcontainer's image hashes this file, bootstrap.sh and the
# toolchain's (scripts/dev-container.sh), so a Windows-only change doesn't
# rebuild it.

# Runs its arguments as root: directly when already root (an image build),
# through sudo otherwise.
as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

# Where an earlier bootstrap had uv keep ctcache's tool environment and the
# Python it fetched, for every user, before the toolchain's store took it: its
# manifest's uv-tool and dir entries still name them.
readonly UV_SHARED=/usr/local/share/uv

# Runs uv as root on the shared tool directory, to undo an earlier bootstrap's
# uv-tool entry. uv by its path, which sudo's own PATH may lack.
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

# The toolchain's release downloads for this machine's architecture, which
# scripts/bootstrap/toolchain.sh installs, in globals: release_url, the asset
# at the version scripts/bootstrap/versions.env pins; release_sha, its SHA-256;
# release_member, the binary's path inside a tar.gz or zip.
# Arguments:
#   The tool.
# Returns:
#   1 when the tool has no release here: luacheck's has no arm64 build, so an
#   arm64 machine (an Apple-silicon host's dev container) builds its rock.
# shellcheck disable=SC2034 # release_*: scripts/bootstrap/toolchain.sh reads them
toolchain_release() {
  local github=https://github.com
  release_member=
  case "$1-${HOSTTYPE}" in
    yamlfmt-x86_64)
      release_url="${github}/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Linux_x86_64.tar.gz"
      release_sha=1f300d9257b232bb3b541d7fb1b0e6b3c121bcbab381c86cd38cb8722be8a566
      release_member=yamlfmt
      ;;
    yamlfmt-aarch64)
      release_url="${github}/google/yamlfmt/releases/download/v${YAMLFMT_VERSION}/yamlfmt_${YAMLFMT_VERSION}_Linux_arm64.tar.gz"
      release_sha=5b2689c963b177271330c5ce8ca7396751107e5a826be46f03d2cb9b6f0c7784
      release_member=yamlfmt
      ;;
    stylua-x86_64)
      release_url="${github}/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-linux-x86_64.zip"
      release_sha=bcb0d855e91f102f28a370e850f8566b3b44b79e6274d806ea5246837c0fd5ab
      release_member=stylua
      ;;
    stylua-aarch64)
      release_url="${github}/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-linux-aarch64.zip"
      release_sha=0ef2ebf0b7e5a652b65c4cb96c6d9ffb3981a98547de3c764465bbf54a8d761a
      release_member=stylua
      ;;
    luacheck-x86_64)
      # A standalone binary, its own Lua bundled.
      release_url="${github}/lunarmodules/luacheck/releases/download/v${LUACHECK_VERSION}/luacheck"
      release_sha=d68da17fca0697d9e2fb04201f3884abd259fa558b3a449bccaed47f1390defc
      ;;
    taplo-x86_64)
      # Taplo's tags have no "v".
      release_url="${github}/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-linux-x86_64.gz"
      release_sha=8fe196b894ccf9072f98d4e1013a180306e17d244830b03986ee5e8eabeb6156
      ;;
    taplo-aarch64)
      release_url="${github}/tamasfe/taplo/releases/download/${TAPLO_VERSION}/taplo-linux-aarch64.gz"
      release_sha=033681d01eec8376c3fd38fa3703c79316f5e14bb013d859943b60a07bccdcc3
      ;;
    buf-x86_64)
      release_url="${github}/bufbuild/buf/releases/download/v${BUF_VERSION}/buf-Linux-x86_64"
      release_sha=8f2986298ad08f0cc1bf999b9797b7c383adf32d7edf0f73d6f1e1a701baeac1
      ;;
    buf-aarch64)
      release_url="${github}/bufbuild/buf/releases/download/v${BUF_VERSION}/buf-Linux-aarch64"
      release_sha=902b75267db7f4391e99b7fa0756050e5354234cc0437ef50eee9c788950c7a3
      ;;
    # The tools CI's own jobs run and the operators' flux and cosign
    # (scripts/bootstrap/versions.env).
    trivy-x86_64)
      release_url="${github}/aquasecurity/trivy/releases/download/v${TRIVY_VERSION}/trivy_${TRIVY_VERSION}_Linux-64bit.tar.gz"
      release_sha=c6e65abddb348e25f10549df887045629cf28cc72453cd1c63acb717316b3f3f
      release_member=trivy
      ;;
    kubeconform-x86_64)
      release_url="${github}/yannh/kubeconform/releases/download/v${KUBECONFORM_VERSION}/kubeconform-linux-amd64.tar.gz"
      release_sha=9bc2bffbf71f261128533edaf912153948b7ff238f9a531ae6d34466ec287883
      release_member=kubeconform
      ;;
    kyverno-x86_64)
      release_url="${github}/kyverno/kyverno/releases/download/v${KYVERNO_VERSION}/kyverno-cli_v${KYVERNO_VERSION}_linux_x86_64.tar.gz"
      release_sha=b38228f367fc0fdc2b08f4c83ea50ac5f16c60ff8d62d76a66157c33c47b70ae
      release_member=kyverno
      ;;
    promtool-x86_64)
      release_url="${github}/prometheus/prometheus/releases/download/v${PROMETHEUS_VERSION}/prometheus-${PROMETHEUS_VERSION}.linux-amd64.tar.gz"
      release_sha=2a542df32eac02ee17b9d844fb2aa1de00dafa5476579ba8a3ba862e9d572ea0
      release_member=prometheus-${PROMETHEUS_VERSION}.linux-amd64/promtool
      ;;
    amtool-x86_64)
      release_url="${github}/prometheus/alertmanager/releases/download/v${ALERTMANAGER_VERSION}/alertmanager-${ALERTMANAGER_VERSION}.linux-amd64.tar.gz"
      release_sha=265b9d1e55ef0d5306a436018af6d2b686c2ce051f03d968f7464ecb1372a7e8
      release_member=alertmanager-${ALERTMANAGER_VERSION}.linux-amd64/amtool
      ;;
    hadolint-x86_64)
      release_url="${github}/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-linux-x86_64"
      release_sha=c7187db94eeeeca956519a6af171adc31453941a1e777961f6e680f697c8c507
      ;;
    oras-x86_64)
      release_url="${github}/oras-project/oras/releases/download/v${ORAS_VERSION}/oras_${ORAS_VERSION}_linux_amd64.tar.gz"
      release_sha=d52c4af76ce6a3ceb8579e51fb751a43ac051cca67f965f973a0b0e897a2bb86
      release_member=oras
      ;;
    syft-x86_64)
      release_url="${github}/anchore/syft/releases/download/v${SYFT_VERSION}/syft_${SYFT_VERSION}_linux_amd64.tar.gz"
      release_sha=54a87372498168b2d033e876fd41fa4e8035b872699e525a57046e1f2f09c860
      release_member=syft
      ;;
    grype-x86_64)
      release_url="${github}/anchore/grype/releases/download/v${GRYPE_VERSION}/grype_${GRYPE_VERSION}_linux_amd64.tar.gz"
      release_sha=1d444c5e7360471815f7158f71935fcecc68a3c417d85c7344f770854300bba2
      release_member=grype
      ;;
    yq-x86_64)
      release_url="${github}/mikefarah/yq/releases/download/v${YQ_VERSION}/yq_linux_amd64"
      release_sha=c5f056448f973ae7d39b5401949648a78f2dc1947d6a8eb65be60d5c504b9385
      ;;
    flux-x86_64)
      release_url="${github}/fluxcd/flux2/releases/download/v${FLUX_VERSION}/flux_${FLUX_VERSION}_linux_amd64.tar.gz"
      release_sha=b853df82adfd7736f580692f9f734473d571606307139f8fd20c2a80dd1ff473
      release_member=flux
      ;;
    cosign-x86_64)
      release_url="${github}/sigstore/cosign/releases/download/v${COSIGN_VERSION}/cosign-linux-amd64"
      release_sha=c956e5dfcac53d52bcf058360d579472f0c1d2d9b69f55209e256fe7783f4c74
      ;;
    trivy-aarch64)
      release_url="${github}/aquasecurity/trivy/releases/download/v${TRIVY_VERSION}/trivy_${TRIVY_VERSION}_Linux-ARM64.tar.gz"
      release_sha=a1ee9f6ffb7d112b64ff726a2a0717c21175c1114361391f4a132956751a13b3
      release_member=trivy
      ;;
    kubeconform-aarch64)
      release_url="${github}/yannh/kubeconform/releases/download/v${KUBECONFORM_VERSION}/kubeconform-linux-arm64.tar.gz"
      release_sha=1f53fc8e81258197a35e8603054162a5af1de8c5af13746c71ab680d9534ed87
      release_member=kubeconform
      ;;
    kyverno-aarch64)
      release_url="${github}/kyverno/kyverno/releases/download/v${KYVERNO_VERSION}/kyverno-cli_v${KYVERNO_VERSION}_linux_arm64.tar.gz"
      release_sha=d78fecd183e1e6749c653fdb744c88d065e6737ec6856c97c4efac84bec54554
      release_member=kyverno
      ;;
    promtool-aarch64)
      release_url="${github}/prometheus/prometheus/releases/download/v${PROMETHEUS_VERSION}/prometheus-${PROMETHEUS_VERSION}.linux-arm64.tar.gz"
      release_sha=f1f90ec08e849d494ca66c611470afc50192f0355f1a61c33f2cbde02d067823
      release_member=prometheus-${PROMETHEUS_VERSION}.linux-arm64/promtool
      ;;
    amtool-aarch64)
      release_url="${github}/prometheus/alertmanager/releases/download/v${ALERTMANAGER_VERSION}/alertmanager-${ALERTMANAGER_VERSION}.linux-arm64.tar.gz"
      release_sha=d98d6cbaf52151c7e76e24355fec88b11cebcb9875d4cdd8b76ddce7a7e5535c
      release_member=alertmanager-${ALERTMANAGER_VERSION}.linux-arm64/amtool
      ;;
    hadolint-aarch64)
      release_url="${github}/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-linux-arm64"
      release_sha=f6198ef8090f404dbb771abfee086eb8c48ac177f30da7fd3510aca35b344b5d
      ;;
    oras-aarch64)
      release_url="${github}/oras-project/oras/releases/download/v${ORAS_VERSION}/oras_${ORAS_VERSION}_linux_arm64.tar.gz"
      release_sha=79946ad57d732836f9242f903f476b6fa484c451d659f121bce54d931ab2a044
      release_member=oras
      ;;
    syft-aarch64)
      release_url="${github}/anchore/syft/releases/download/v${SYFT_VERSION}/syft_${SYFT_VERSION}_linux_arm64.tar.gz"
      release_sha=ee6d4566373a05b344bc6b5f1706f14419bf9338ba39ff686e247deefe9b8818
      release_member=syft
      ;;
    grype-aarch64)
      release_url="${github}/anchore/grype/releases/download/v${GRYPE_VERSION}/grype_${GRYPE_VERSION}_linux_arm64.tar.gz"
      release_sha=32aceeb8ee837244775fcb522372c8b3a47914986385f3148f4ee2c930482a84
      release_member=grype
      ;;
    yq-aarch64)
      release_url="${github}/mikefarah/yq/releases/download/v${YQ_VERSION}/yq_linux_arm64"
      release_sha=88a1016bc1d657375a35864e4f44b6f333df8ff97b559f51bba0adcb2169df09
      ;;
    flux-aarch64)
      release_url="${github}/fluxcd/flux2/releases/download/v${FLUX_VERSION}/flux_${FLUX_VERSION}_linux_arm64.tar.gz"
      release_sha=f3e159af616ec0b9bd0a405c2185cf09d06b74652c1de3c7f377e8166826651a
      release_member=flux
      ;;
    cosign-aarch64)
      release_url="${github}/sigstore/cosign/releases/download/v${COSIGN_VERSION}/cosign-linux-arm64"
      release_sha=bedac92e8c3729864e13d4a17048007cfafa79d5deca993a43a90ffe018ef2b8
      ;;
    *) return 1 ;;
  esac
}

install_platform() {
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
  # hooks: clang-format, git-lfs (pre-push), and LuaRocks, which builds the
  # toolchain's luacheck on arm64, where luacheck's own release has no build.
  # sccache: the root
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
    luarocks
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

  # uv, which installs the toolchain's PyPI tools
  # (scripts/bootstrap/toolchain.sh) and runs the rest, at its latest release:
  # what it runs is pinned.
  if ! command -v uv >/dev/null 2>&1; then
    record_new_files /usr/local/bin uv uvx
    curl -LsSf https://astral.sh/uv/install.sh \
      | as_root env UV_UNMANAGED_INSTALL=/usr/local/bin sh
  fi

  # kubectl and helm, for the server's k3s cluster and its chart
  # (charts/augustad): each one's latest stable release, which Ubuntu doesn't
  # package.
  local arch kubectl_version
  arch="$(dpkg --print-architecture)"
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
