# syntax=docker/dockerfile:1

# The Linux/server build environment of CI's server job: its runner's Ubuntu
# release, whose distro clang/clang-tidy/clang-format fix CI's LLVM major
# (bump it with the runner image), and the toolchain
# .github/actions/setup-linux-build installs on top. Keep the two in step.
# Built for the host's architecture (amd64 or arm64), not only CI's amd64: the
# same packages and pinned tool versions, without emulating another CPU.
FROM ubuntu:26.04

# From setup-linux-build (which says why each): clang, ninja-build, autoconf,
# autoconf-archive, automake, libtool. From the server job: clang-tidy.
# Already on CI's runner image: build-essential, cmake, git, curl, zip, unzip,
# tar, pkg-config. For the hooks: clang-format, git-lfs (pre-push), and
# lua-check (luacheck), which this release packages at CI's pinned 1.2.0 for
# every architecture, unlike luacheck's own release. sccache: the root
# CMakeLists.txt picks it up from PATH. doxygen: `make docs`. gdb and gh: for
# development.
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
      doxygen \
      gdb \
      gh \
      git \
      git-lfs \
      libtool \
      lua-check \
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

# The hooks' and CI's format job's formatters and linters that Ubuntu doesn't
# package at CI's versions: uv (for yamllint), yamlfmt, StyLua and taplo. CI's
# format job pins the same versions and the amd64 checksums of the last three;
# bump them together. uv is its latest release: yamllint, which it runs, is the
# pinned one. TARGETARCH is the platform being built for, which BuildKit sets.
ARG TARGETARCH
RUN set -eu; \
    case "$TARGETARCH" in \
      amd64) \
        yamlfmt_arch=x86_64; yamlfmt_sha=1f300d9257b232bb3b541d7fb1b0e6b3c121bcbab381c86cd38cb8722be8a566; \
        stylua_arch=x86_64; stylua_sha=bcb0d855e91f102f28a370e850f8566b3b44b79e6274d806ea5246837c0fd5ab; \
        taplo_arch=x86_64; taplo_sha=8fe196b894ccf9072f98d4e1013a180306e17d244830b03986ee5e8eabeb6156 ;; \
      arm64) \
        yamlfmt_arch=arm64; yamlfmt_sha=5b2689c963b177271330c5ce8ca7396751107e5a826be46f03d2cb9b6f0c7784; \
        stylua_arch=aarch64; stylua_sha=0ef2ebf0b7e5a652b65c4cb96c6d9ffb3981a98547de3c764465bbf54a8d761a; \
        taplo_arch=aarch64; taplo_sha=033681d01eec8376c3fd38fa3703c79316f5e14bb013d859943b60a07bccdcc3 ;; \
      *) echo "unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac; \
    curl -LsSf https://astral.sh/uv/install.sh | env UV_UNMANAGED_INSTALL=/usr/local/bin sh; \
    tmp="$(mktemp -d)"; \
    curl -fsSLo "$tmp/yamlfmt.tar.gz" \
      "https://github.com/google/yamlfmt/releases/download/v0.21.0/yamlfmt_0.21.0_Linux_${yamlfmt_arch}.tar.gz"; \
    curl -fsSLo "$tmp/stylua.zip" \
      "https://github.com/JohnnyMorganz/StyLua/releases/download/v2.5.2/stylua-linux-${stylua_arch}.zip"; \
    curl -fsSLo "$tmp/taplo.gz" \
      "https://github.com/tamasfe/taplo/releases/download/0.10.0/taplo-linux-${taplo_arch}.gz"; \
    printf '%s  %s\n' \
      "$yamlfmt_sha" "$tmp/yamlfmt.tar.gz" \
      "$stylua_sha" "$tmp/stylua.zip" \
      "$taplo_sha" "$tmp/taplo.gz" \
      | sha256sum --check --status; \
    tar -xzf "$tmp/yamlfmt.tar.gz" -C "$tmp"; \
    unzip -q "$tmp/stylua.zip" -d "$tmp"; \
    gunzip "$tmp/taplo.gz"; \
    install -m 0755 "$tmp/yamlfmt" "$tmp/stylua" "$tmp/taplo" /usr/local/bin; \
    rm -rf "$tmp"

# kubectl and helm, for the server's k3s cluster and its chart (charts/augustad):
# each one's latest stable release, which Ubuntu doesn't package.
RUN curl -fsSLo /usr/local/bin/kubectl \
      "https://dl.k8s.io/release/$(curl -fsSL https://dl.k8s.io/release/stable.txt)/bin/linux/${TARGETARCH}/kubectl" \
    && chmod 0755 /usr/local/bin/kubectl \
    && curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash

# The image's own non-root user (UID 1000). Its sccache directory exists here so
# the volume devcontainer.json mounts over it starts out owned by it.
USER ubuntu
RUN mkdir -p /home/ubuntu/.cache/sccache
