# syntax=docker/dockerfile:1

# The Linux/server build environment of CI's server job: its runner's Ubuntu
# release, whose distro clang/clang-tidy/clang-format fix CI's LLVM major
# (bump it with the runner image, as bootstrap-wsl.sh does), and the toolchain
# .github/actions/setup-linux-build installs on top. Keep the two in step.
# amd64 like CI's runner, also on an arm64 host: install-lint-tools.sh fetches
# x86_64 binaries.
FROM --platform=linux/amd64 ubuntu:26.04

# From setup-linux-build (which says why each): clang, ninja-build, autoconf,
# autoconf-archive, automake, libtool. From the server job: clang-tidy.
# Already on CI's runner image: build-essential, cmake, git, curl, zip, unzip,
# tar, pkg-config. For the hooks: clang-format, git-lfs (pre-push). sccache:
# the root CMakeLists.txt picks it up from PATH. gdb and gh: for development.
RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf \
      autoconf-archive \
      automake \
      build-essential \
      ca-certificates \
      clang \
      clang-format \
      clang-tidy \
      cmake \
      curl \
      gdb \
      gh \
      git \
      git-lfs \
      libtool \
      ninja-build \
      pkg-config \
      sccache \
      sudo \
      tar \
      unzip \
      zip \
    && rm -rf /var/lib/apt/lists/* \
    && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu \
    && chmod 0440 /etc/sudoers.d/ubuntu

COPY scripts/install-lint-tools.sh /tmp/
RUN bash /tmp/install-lint-tools.sh /usr/local/bin && rm /tmp/install-lint-tools.sh

# The image's own non-root user (UID 1000). Its sccache directory exists here so
# the volume devcontainer.json mounts over it starts out owned by it.
USER ubuntu
RUN mkdir -p /home/ubuntu/.cache/sccache
