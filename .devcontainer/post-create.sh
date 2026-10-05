#!/usr/bin/env bash
# Readies the checkout once the dev container exists: what the image can't do,
# since the checkout and its volumes are only mounted when the container runs.
set -euo pipefail

# A volume mounts in owned by root when the image has no directory at its
# target, as it has none inside the workspace.
sudo chown "$(id -u):$(id -g)" build

# A bind-mounted checkout is owned by the host's user, whom git inside the
# container doesn't recognise; the submodules are repositories of their own.
git config --global --add safe.directory '*'
# LFS's filters only: .githooks' pre-push already runs git lfs pre-push.
git lfs install --skip-repo

# The submodules the Linux build uses; Falcor is the Windows client's alone.
git submodule update --init third_party/vcpkg third_party/nvtx
./third_party/vcpkg/bootstrap-vcpkg.sh -disableMetrics
mkdir -p .vcpkg-bincache

git config core.hooksPath .githooks
