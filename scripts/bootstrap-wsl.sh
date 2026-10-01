#!/usr/bin/env bash
# Bootstraps the Linux (WSL2) dev environment for the server/shared core
# (docs/ENGINEERING.md, Developer Environment).
set -euo pipefail

# The clang/clang-format/clang-tidy below are the distro's packages, whose
# LLVM major is fixed per Ubuntu release: only the release CI's runner and the
# Dockerfile use (26.04) gives the same one, so the hooks agree with CI's gates.
# Bump it together with the runner image.
required_ubuntu="26.04"
. /etc/os-release
if [[ "${ID:-}" != "ubuntu" || "${VERSION_ID:-}" != "$required_ubuntu" ]]; then
  echo "error: requires Ubuntu $required_ubuntu, found ${PRETTY_NAME:-unknown}" >&2
  exit 1
fi

# Clean up any stray apt.llvm.org source from a previous run of this script.
sudo rm -f /etc/apt/sources.list.d/*llvm*.list

# autoconf/autoconf-archive/automake/libtool: vcpkg's libsodium port builds
# via autotools on Linux (vcpkg_run_autoreconf), as in CI and the Dockerfile.
sudo apt-get update
sudo apt-get install -y \
  autoconf \
  autoconf-archive \
  automake \
  libtool \
  build-essential \
  clang \
  make \
  cmake \
  ninja-build \
  clang-format \
  clang-tidy \
  gdb \
  curl \
  git \
  gnupg \
  sccache \
  zip \
  unzip \
  tar \
  pkg-config \
  gh

# Install uv for the pinned yamllint invocation used by the YAML hook/CI.
if ! command -v uv >/dev/null 2>&1; then
  curl -LsSf https://astral.sh/uv/install.sh | sh
fi
export PATH="$HOME/.local/bin:$PATH"
yamlfmt_version="0.21.0"
yamlfmt_asset="yamlfmt_${yamlfmt_version}_Linux_x86_64.tar.gz"
yamlfmt_tmp="$(mktemp -d)"
trap 'rm -rf "$yamlfmt_tmp"' EXIT
yamlfmt_url="https://github.com/google/yamlfmt/releases/download/v${yamlfmt_version}"
curl -fsSLo "$yamlfmt_tmp/$yamlfmt_asset" "$yamlfmt_url/$yamlfmt_asset"
printf '%s  %s\n' \
  '1f300d9257b232bb3b541d7fb1b0e6b3c121bcbab381c86cd38cb8722be8a566' \
  "$yamlfmt_tmp/$yamlfmt_asset" | sha256sum --check --status
tar -xzf "$yamlfmt_tmp/$yamlfmt_asset" -C "$yamlfmt_tmp"
install -m 0755 "$yamlfmt_tmp/yamlfmt" "$HOME/.local/bin/yamlfmt"
if ! grep -Fq 'export PATH="$HOME/.local/bin:$PATH"' ~/.bashrc 2>/dev/null; then
  printf '\nexport PATH="$HOME/.local/bin:$PATH"\n' >> ~/.bashrc
fi
trap - EXIT
rm -rf "$yamlfmt_tmp"

if ! command -v kubectl >/dev/null 2>&1; then
  curl -fsSL -o /tmp/kubectl "https://dl.k8s.io/release/$(curl -fsSL https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
  sudo install -o root -g root -m 0755 /tmp/kubectl /usr/local/bin/kubectl
  rm -f /tmp/kubectl
fi

if ! command -v helm >/dev/null 2>&1; then
  curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash
fi

# WSL2 mirrored networking (set up by bootstrap-windows.ps1) puts WSL and
# Windows on the same 127.0.0.1 port space, so sccache's default port
# (4226) collides between the two sides' independent sccache servers -
# give WSL's its own port.
if ! grep -q "^export SCCACHE_SERVER_PORT=" ~/.bashrc 2>/dev/null; then
  echo "export SCCACHE_SERVER_PORT=4227" >> ~/.bashrc
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# vcpkg is a pinned git submodule (third_party/vcpkg) rather than a
# machine-wide install, so dependency resolution is reproducible per-clone
# (see CMakeLists.txt).
git -C "$repo_root" submodule update --init --recursive
"$repo_root/third_party/vcpkg/bootstrap-vcpkg.sh" -disableMetrics

git -C "$repo_root" config core.hooksPath .githooks

echo "WSL bootstrap complete."
