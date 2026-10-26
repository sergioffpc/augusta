# syntax=docker/dockerfile:1

# The Linux/server build environment of CI's server-x64-linux job: its runner's
# Ubuntu release, whose distro clang/clang-tidy/clang-format fix CI's LLVM major
# (bump it with the runner image), and the system packages
# .github/actions/setup-linux-build installs on top, as scripts/bootstrap.sh's
# platform step installs them - the one recipe for them, which a Linux host
# runs too. Keep it and setup-linux-build in step. That step also fills the
# toolchain's shared store (ADR-0077), so the container's post-create only
# links its .toolchain/ into it.
# Built for the host's architecture (amd64 or arm64), not only CI's amd64: the
# same packages and pinned tool versions, without emulating another CPU.
FROM ubuntu:26.04

# The build context is the repository (devcontainer.json), which
# Dockerfile.dockerignore narrows to the bootstrap's Linux half and the
# toolchain's files. Run as root, the step puts the store where the image's
# user, ubuntu, looks for it (${XDG_CACHE_HOME:-~/.cache}), and hands it over;
# uv's download cache goes, as the store keeps what uv installed.
RUN --mount=type=bind,source=scripts,target=/tmp/scripts \
    XDG_CACHE_HOME=/home/ubuntu/.cache bash /tmp/scripts/bootstrap.sh platform \
    && rm -rf /var/lib/apt/lists/* /home/ubuntu/.cache/uv \
    && chown -R ubuntu:ubuntu /home/ubuntu/.cache \
    && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu \
    && chmod 0440 /etc/sudoers.d/ubuntu

# The image's own non-root user (UID 1000). Its sccache directory exists here so
# the volume devcontainer.json mounts over it starts out owned by it.
USER ubuntu
RUN mkdir -p /home/ubuntu/.cache/sccache
