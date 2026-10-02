# syntax=docker/dockerfile:1

# Build stage: same base as the CI Linux runner (see .github/workflows/ci.yml)
# so the image is built with the exact toolchain/glibc combination CI already
# validates the server against.
FROM ubuntu:26.04 AS build

# clang compiles (the linux preset selects it, ADR-0008); build-essential
# still provides libstdc++ and binutils.
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      clang \
      cmake \
      ninja-build \
      git \
      curl \
      zip \
      unzip \
      tar \
      pkg-config \
      ca-certificates \
      autoconf \
      autoconf-archive \
      automake \
      libtool \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# vcpkg bootstrap depends only on the submodule + manifest, not on source
# changes, so it's copied and run first to keep that layer cached across
# src/ edits.
COPY third_party/vcpkg third_party/vcpkg
COPY cmake cmake
COPY vcpkg.json CMakeLists.txt CMakePresets.json ./
RUN ./third_party/vcpkg/bootstrap-vcpkg.sh -disableMetrics

# The dependencies too, into the install root the linux preset's configure
# uses (manifest mode, <binaryDir>/vcpkg_installed): that configure then finds
# them installed and builds none. Built after COPY src instead, a change to
# any source rebuilt all of them. The scratch trees go in the same layer, so
# the registry cache doesn't carry them. vcpkg retries one source download,
# but a short GitHub outage can still exhaust that retry budget (for example,
# PhysX returned HTTP 504 in CI). Retry the whole install: work and downloads
# already completed by earlier attempts remain available. The cache mount also
# preserves downloaded source archives for later steps using this builder,
# without putting them in the final image.
RUN --mount=type=cache,target=/workspace/third_party/vcpkg/downloads \
    set -e; \
    for attempt in 1 2 3; do \
      if ./third_party/vcpkg/vcpkg install --x-install-root=build/x64-linux/vcpkg_installed; then \
        break; \
      fi; \
      if [ "$attempt" -eq 3 ]; then \
        exit 1; \
      fi; \
      sleep "$((attempt * 15))"; \
    done; \
    rm -rf third_party/vcpkg/buildtrees third_party/vcpkg/packages /root/.cache/vcpkg

COPY src src
COPY tests tests

RUN cmake --preset linux
RUN cmake --build --preset linux --target augustad
# Staged under DESTDIR with the prefix the runtime stage runs it from, so this
# file never names a path inside the build tree.
RUN DESTDIR=/workspace/stage cmake --install build/x64-linux --prefix /usr/local

# Runtime stage: just what `cmake --install` staged and the shared libraries
# it links against (vcpkg's own dependencies are linked statically) - no build
# toolchain, no vcpkg source tree.
FROM ubuntu:26.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
      libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --shell /usr/sbin/nologin augusta

COPY --from=build /workspace/stage/ /

USER augusta
ENTRYPOINT ["/usr/local/bin/augustad"]
