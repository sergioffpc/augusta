# syntax=docker/dockerfile:1

# The Linux/server build environment of CI's server job: its runner's Ubuntu
# release, whose distro clang/clang-tidy/clang-format fix CI's LLVM major
# (bump it with the runner image), and the toolchain
# .github/actions/setup-linux-build installs on top, as
# scripts/bootstrap-linux.sh installs it - the one recipe for it, which a
# Linux host runs too. Keep it and setup-linux-build in step.
# Built for the host's architecture (amd64 or arm64), not only CI's amd64: the
# same packages and pinned tool versions, without emulating another CPU.
FROM ubuntu:26.04

# The build context is the repository (devcontainer.json), which
# Dockerfile.dockerignore narrows to this script.
RUN --mount=type=bind,source=scripts/bootstrap-linux.sh,target=/tmp/bootstrap-linux.sh \
    bash /tmp/bootstrap-linux.sh toolchain \
    && rm -rf /var/lib/apt/lists/* \
    && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu \
    && chmod 0440 /etc/sudoers.d/ubuntu

# The image's own non-root user (UID 1000). Its sccache directory exists here so
# the volume devcontainer.json mounts over it starts out owned by it.
USER ubuntu
RUN mkdir -p /home/ubuntu/.cache/sccache
