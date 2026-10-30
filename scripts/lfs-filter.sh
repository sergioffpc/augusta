#!/bin/bash
# A git filter for Git LFS on a host without git-lfs: the dev container's runs
# it (ADR-0079). scripts/bootstrap.sh names this script in the checkout's own
# config, which the container reads too, where git-lfs is on PATH.
#
# Usage: scripts/lfs-filter.sh clean|smudge -- <path>
set -euo pipefail

if command -v git-lfs >/dev/null 2>&1; then
  exec git-lfs "$@"
fi
exec "$(dirname "$0")/dev-container.sh" git-lfs "$@"
