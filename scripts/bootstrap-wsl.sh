#!/usr/bin/env bash
# Bootstraps the Linux (WSL2) dev environment for the server/shared core
# (docs/ENGINEERING.md, Developer Environment).
set -euo pipefail

# Clean up any stray apt.llvm.org source from a previous run of this script.
sudo rm -f /etc/apt/sources.list.d/*llvm*.list

sudo apt-get update
sudo apt-get install -y \
  build-essential \
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
