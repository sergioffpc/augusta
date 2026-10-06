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
# the registry cache doesn't carry them.
RUN ./third_party/vcpkg/vcpkg install --x-install-root=build/x64-linux/vcpkg_installed \
    && rm -rf third_party/vcpkg/buildtrees third_party/vcpkg/packages third_party/vcpkg/downloads \
      /root/.cache/vcpkg

COPY src src
COPY tests tests
# augusta-replay and its tests, which the configure always reads (CMakeLists.txt).
COPY tools/replay tools/replay

# The install is staged under DESTDIR with the prefix the runtime stage runs
# it from, so this file never names a path inside the build tree.
RUN cmake --preset linux \
    && cmake --build --preset linux --target augustad \
    && DESTDIR=/workspace/stage cmake --install build/x64-linux --prefix /usr/local

# The debug info the build split off augustad (ADR-0047), alone: CI publishes
# it as the image's sha-<12>-debuginfo tag, what reads a core dump of the
# binary below. Not the last stage, so a plain build still makes the runtime.
FROM scratch AS debuginfo
COPY --from=build /workspace/build/x64-linux/src/server/augustad.debug /

# Runtime stage: just what `cmake --install` staged and the shared libraries
# it links against (vcpkg's own dependencies are linked statically) - no build
# toolchain, no vcpkg source tree.
FROM ubuntu:26.04 AS runtime

# Upgraded, not just installed onto: the base image trails Ubuntu's security
# updates, and CI fails an image with a fixable high or critical CVE. CI
# rebuilds this stage every time (no-cache-filters), so a cached layer never
# holds an update back.
RUN apt-get update && apt-get upgrade -y --no-install-recommends \
    && apt-get install -y --no-install-recommends \
      libstdc++6 \
      tini \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --shell /usr/sbin/nologin augusta

COPY --from=build /workspace/stage/ /

USER augusta
# tini is PID 1, not augustad: the kernel drops a signal PID 1 sends itself
# with no handler for it, so augustad re-raising a fatal signal from its crash
# handler would neither end it nor dump its core (ADR-0047). tini forwards
# SIGTERM to augustad and exits with its status.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/augustad"]
