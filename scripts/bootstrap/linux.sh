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
  # pkg-config. For the hooks: clang-format, git-lfs (pre-push), and lua-check
  # (luacheck), which this release packages at CI's pinned LUACHECK_VERSION for
  # every architecture, unlike luacheck's own release. sccache: the root
  # CMakeLists.txt picks it up from PATH. doxygen: `make docs`. lldb and gh:
  # for development.
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
  as_root apt-get update
  as_root apt-get install -y --no-install-recommends \
    autoconf \
    autoconf-archive \
    automake \
    ca-certificates \
    clang \
    clang-format \
    clang-tidy \
    cmake \
    curl \
    doxygen \
    gh \
    git \
    git-lfs \
    libc++-dev \
    libc++abi-dev \
    libclang-rt-dev \
    libgl-dev \
    libtool \
    libunwind-21-dev \
    libvulkan1 \
    libwayland-dev \
    libx11-dev \
    libxcursor-dev \
    libxi-dev \
    libxinerama-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    lld \
    lldb \
    llvm \
    lua-check \
    make \
    mesa-vulkan-drivers \
    ninja-build \
    pkg-config \
    sccache \
    sudo \
    tar \
    unzip \
    xauth \
    xvfb \
    zip

  # The hooks' and CI's format job's formatters and linters that Ubuntu doesn't
  # package at CI's versions: uv (for yamllint and the rest uv runs), yamlfmt,
  # StyLua and taplo. uv is its latest release: what it runs is pinned. With
  # them, the flux CLI at the cluster's Flux.
  local arch yamlfmt_arch yamlfmt_sha stylua_arch stylua_sha taplo_arch \
    taplo_sha flux_sha
  arch="$(dpkg --print-architecture)"
  case "${arch}" in
    amd64)
      yamlfmt_arch=x86_64
      yamlfmt_sha=1f300d9257b232bb3b541d7fb1b0e6b3c121bcbab381c86cd38cb8722be8a566
      stylua_arch=x86_64
      stylua_sha=bcb0d855e91f102f28a370e850f8566b3b44b79e6274d806ea5246837c0fd5ab
      taplo_arch=x86_64
      taplo_sha=8fe196b894ccf9072f98d4e1013a180306e17d244830b03986ee5e8eabeb6156
      flux_sha=b853df82adfd7736f580692f9f734473d571606307139f8fd20c2a80dd1ff473
      ;;
    arm64)
      yamlfmt_arch=arm64
      yamlfmt_sha=5b2689c963b177271330c5ce8ca7396751107e5a826be46f03d2cb9b6f0c7784
      stylua_arch=aarch64
      stylua_sha=0ef2ebf0b7e5a652b65c4cb96c6d9ffb3981a98547de3c764465bbf54a8d761a
      taplo_arch=aarch64
      taplo_sha=033681d01eec8376c3fd38fa3703c79316f5e14bb013d859943b60a07bccdcc3
      flux_sha=f3e159af616ec0b9bd0a405c2185cf09d06b74652c1de3c7f377e8166826651a
      ;;
    *)
      echo "bootstrap: unsupported architecture: ${arch}" >&2
      exit 1
      ;;
  esac
  if ! command -v uv >/dev/null 2>&1; then
    curl -LsSf https://astral.sh/uv/install.sh \
      | as_root env UV_UNMANAGED_INSTALL=/usr/local/bin sh
  fi
  # ctcache (scripts/bootstrap.sh) for every user, the dev container's too: its
  # tool environment and the Python uv may fetch for it under /usr/local, not
  # root's home. uv by its path, which sudo's own PATH may lack.
  as_root env UV_TOOL_DIR=/usr/local/share/uv/tools \
    UV_TOOL_BIN_DIR=/usr/local/bin \
    UV_PYTHON_INSTALL_DIR=/usr/local/share/uv/python \
    "$(command -v uv)" tool install "${CTCACHE_PACKAGE}"
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
  download_verified \
    "https://github.com/fluxcd/flux2/releases/download/v${FLUX_VERSION}/flux_${FLUX_VERSION}_linux_${arch}.tar.gz" \
    "${flux_sha}" "${tmp}/flux.tar.gz"
  tar -xzf "${tmp}/yamlfmt.tar.gz" -C "${tmp}" yamlfmt
  unzip -q "${tmp}/stylua.zip" -d "${tmp}"
  gunzip "${tmp}/taplo.gz"
  tar -xzf "${tmp}/flux.tar.gz" -C "${tmp}" flux
  as_root install -m 0755 "${tmp}/yamlfmt" "${tmp}/stylua" "${tmp}/taplo" \
    "${tmp}/flux" /usr/local/bin
  rm -rf "${tmp}"

  # kubectl and helm, for the server's k3s cluster and its chart
  # (charts/augustad): each one's latest stable release, which Ubuntu doesn't
  # package.
  local kubectl_version
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

bootstrap_done() {
  # The dev container sets this itself (devcontainer.json).
  echo "bootstrap: done. Builds use CI's Linux binary cache with:" \
    "export VCPKG_BINARY_SOURCES=\"clear;files,${PWD}/.vcpkg-bincache,readwrite\""
}
