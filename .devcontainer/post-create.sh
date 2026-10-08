#!/bin/bash
# Readies the checkout once the dev container exists: what the image can't do,
# since the checkout and its volumes are only mounted when the container runs.
set -euo pipefail

# A volume mounts in owned by root when the image has no directory at its
# target, as it has none inside the workspace.
sudo chown "$(id -u):$(id -g)" build

# A bind-mounted checkout is owned by the host's user, whom git inside the
# container doesn't recognise; the submodules are repositories of their own.
git config --global --add safe.directory '*'

# The rest is a Linux host's too.
scripts/bootstrap-linux.sh checkout
