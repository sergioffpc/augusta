#!/usr/bin/env bash
# Bootstraps the Linux (WSL2) dev environment for the server/shared core
# (docs/ENGINEERING.md, Developer Environment).
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  clang-tidy \
  clang-format \
  gdb \
  curl \
  git \
  gnupg

if ! command -v gh >/dev/null 2>&1; then
  curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo tee /usr/share/keyrings/githubcli-archive-keyring.gpg >/dev/null
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
    | sudo tee /etc/apt/sources.list.d/github-cli.list >/dev/null
  sudo apt-get update
  sudo apt-get install -y gh
fi

if ! command -v kubectl >/dev/null 2>&1; then
  curl -fsSL -o /tmp/kubectl "https://dl.k8s.io/release/$(curl -fsSL https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
  sudo install -o root -g root -m 0755 /tmp/kubectl /usr/local/bin/kubectl
  rm -f /tmp/kubectl
fi

if ! command -v helm >/dev/null 2>&1; then
  curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# vcpkg is a pinned git submodule (vcpkg/) rather than a machine-wide install,
# so dependency resolution is reproducible per-clone (see CMakeLists.txt).
git -C "$repo_root" submodule update --init --recursive
"$repo_root/vcpkg/bootstrap-vcpkg.sh" -disableMetrics

git -C "$repo_root" config core.hooksPath .githooks

echo "WSL bootstrap complete."
