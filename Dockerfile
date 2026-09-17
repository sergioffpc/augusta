# syntax=docker/dockerfile:1

# Build stage: same base as the CI Linux runner (see .github/workflows/ci.yml)
# so the image is built with the exact toolchain/glibc combination CI already
# validates the server against.
FROM ubuntu:26.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      git \
      curl \
      zip \
      unzip \
      tar \
      pkg-config \
      ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# vcpkg bootstrap depends only on the submodule + manifest, not on source
# changes, so it's copied and run first to keep that layer cached across
# src/ edits.
COPY third_party/vcpkg third_party/vcpkg
COPY cmake cmake
COPY vcpkg.json CMakeLists.txt CMakePresets.json ./
RUN ./third_party/vcpkg/bootstrap-vcpkg.sh -disableMetrics

COPY src src
COPY tests tests

# AUGUSTA_BUILD_RUNTIME defaults to ON (the linux preset's own default),
# which builds augustad's runtime and is mutually exclusive with the
# offline cooker (ARCHITECTURE.md "Tooling" section - see the root
# CMakeLists.txt's AUGUSTA_BUILD_RUNTIME else() branch), which pulls in
# vcpkg deps (USD, DirectXTex) augustad never needs - tools/ isn't even
# COPYed into this build context.
RUN cmake --preset linux
RUN cmake --build --preset linux --target augustad

# Runtime stage: just the binary and the shared libraries it links against
# (vcpkg's own dependencies are linked statically) - no build toolchain, no
# vcpkg source tree.
FROM ubuntu:26.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
      libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --shell /usr/sbin/nologin augusta

COPY --from=build /workspace/build/x64-linux/src/server/augustad /usr/local/bin/augustad

USER augusta
ENTRYPOINT ["/usr/local/bin/augustad"]
