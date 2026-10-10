# syntax=docker/dockerfile:1

# The unprivileged user and group the server runs as, by ID (ADR-0054). The
# chart's securityContext names the same ID.
ARG AUGUSTA_UID=65532

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

# Only what the vcpkg layers read comes in before them, so they stay cached
# across source and CMake edits: the bootstrap reads the submodule alone, and
# the install below it and the manifest (no overlays, no
# vcpkg-configuration.json).
COPY third_party/vcpkg third_party/vcpkg
RUN ./third_party/vcpkg/bootstrap-vcpkg.sh -disableMetrics
COPY vcpkg.json ./

# The dependencies too, into the install root the linux preset's configure
# uses (manifest mode, <binaryDir>/vcpkg_installed): that configure then finds
# them installed and builds none. Built after COPY src instead, a change to
# any source rebuilt all of them. The scratch trees go in the same layer, so
# the registry cache doesn't carry them.
RUN ./third_party/vcpkg/vcpkg install --x-install-root=build/x64-linux/vcpkg_installed \
    && rm -rf third_party/vcpkg/buildtrees third_party/vcpkg/packages third_party/vcpkg/downloads \
      /root/.cache/vcpkg

# The build scripts come in only now, after the dependencies: copied before
# them, a CMake-only change rebuilt every vcpkg package. vcpkg reads none of
# cmake/ (its patches are Falcor's, the Windows-only renderer's).
COPY cmake cmake
COPY CMakeLists.txt CMakePresets.json ./
COPY src src
COPY tests tests

# The install is staged under DESTDIR with the prefix the runtime stage runs
# it from, so this file never names a path inside the build tree.
# Without the C++ tools (AUGUSTA_TOOLS): the image builds augustad alone and
# does not copy tools/.
RUN cmake --preset linux -DAUGUSTA_TOOLS=OFF \
    && cmake --build --preset linux --target augustad \
    && DESTDIR=/workspace/stage cmake --install build/x64-linux --prefix /usr/local

# The debug info the build split off augustad (ADR-0047), alone: CI publishes
# it as the image's sha-<12>-debuginfo tag, what reads a core dump of the
# binary below. Not the last stage, so a plain build still makes the runtime.
FROM scratch AS debuginfo
COPY --from=build /workspace/build/x64-linux/src/server/augustad.debug /

# The runtime stage's root filesystem: only the Ubuntu 26.04 files augustad
# and tini run on, cut from the archive's packages by chisel (ADR-0054), with
# no shell, apt, dpkg or Pebble. CI rebuilds this stage every time
# (no-cache-filters), so each image takes the archive's current security
# updates rather than a cached layer's.
FROM ubuntu:26.04 AS rootfs

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# The package lists stay: apt-cache reads the control stanza of each package
# chisel cuts from them, below.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      git \
      jq \
      zstd

ARG CHISEL_VERSION=v1.5.1
ARG CHISEL_SHA256=caa1f84ef144c3311d95ee464c1b72dd237e99055a8c58b791a5591abd42d9a1
RUN curl -fsSLo /tmp/chisel.tar.gz \
      "https://github.com/canonical/chisel/releases/download/${CHISEL_VERSION}/chisel_${CHISEL_VERSION}_linux_amd64.tar.gz" \
    && echo "${CHISEL_SHA256}  /tmp/chisel.tar.gz" | sha256sum --check --status \
    && tar -xzf /tmp/chisel.tar.gz -C /usr/local/bin chisel \
    && rm /tmp/chisel.tar.gz

# The slice definitions, pinned to a commit of chisel-releases' ubuntu-26.04
# branch, with this repository's own for the packages it has none for
# (chisel/slices/). The packages themselves are the archive's current ones.
ARG CHISEL_RELEASES_COMMIT=a7e010b2ba31c1b05c4e2beca95486b73cec5822
RUN git init -q /chisel-releases \
    && git -C /chisel-releases fetch -q --depth 1 \
      https://github.com/canonical/chisel-releases.git "${CHISEL_RELEASES_COMMIT}" \
    && git -C /chisel-releases checkout -q FETCH_HEAD
COPY chisel/slices/ /chisel-releases/slices/

# augustad links libc, libm, libstdc++ and libgcc_s; base-files_chisel writes
# chisel's manifest of what was cut.
RUN mkdir /rootfs \
    && chisel cut --release /chisel-releases --root /rootfs \
      base-files_base \
      base-files_release-info \
      base-files_chisel \
      base-passwd_data \
      libc6_libs \
      libgcc-s1_libs \
      libstdc++6_libs \
      tini_bins

# trivy reads no chisel manifest, so each package it lists also gets the
# archive's control stanza in dpkg's status.d, as distroless images do, which
# trivy and the SBOM read like a dpkg database: without it the image's scan
# would find no OS packages at all. A version the package lists above lack
# (the archive moved on in between) fails the build rather than drop a
# package from the scan.
RUN mkdir -p /rootfs/var/lib/dpkg/status.d \
    && zstd -dc /rootfs/var/lib/chisel/manifest.wall \
      | jq -r 'select(.kind == "package") | "\(.name) \(.version)"' \
      | while read -r name version; do \
          apt-cache show "${name}=${version}" | awk -v RS= 'NR == 1' \
            > "/rootfs/var/lib/dpkg/status.d/${name}" || exit 1; \
        done

# The server's user, by name too, for what a debug container shows of it.
ARG AUGUSTA_UID
RUN echo "augusta:x:${AUGUSTA_UID}:${AUGUSTA_UID}::/nonexistent:/usr/sbin/nologin" >> /rootfs/etc/passwd \
    && echo "augusta:x:${AUGUSTA_UID}:" >> /rootfs/etc/group

# Runtime stage: the chiselled root and what `cmake --install` staged (vcpkg's
# own dependencies are linked statically) - no build toolchain, no vcpkg
# source tree, no shell (ADR-0054).
FROM scratch AS runtime

COPY --from=rootfs /rootfs/ /
COPY --from=build /workspace/stage/ /

# The commit the image is built from, which augustad reports as
# augustad_build_info's commit label (ADR-0049). Set here, in the last stage,
# rather than compiled in, so a new commit does not invalidate the cached
# build.
ARG AUGUSTA_COMMIT=unknown
ENV AUGUSTA_COMMIT=${AUGUSTA_COMMIT}

# By number, so the chart's runAsNonRoot can check it is not root.
ARG AUGUSTA_UID
USER ${AUGUSTA_UID}:${AUGUSTA_UID}
# tini is PID 1, not augustad: the kernel drops a signal PID 1 sends itself
# with no handler for it, so augustad re-raising a fatal signal from its crash
# handler would neither end it nor dump its core (ADR-0047). tini forwards
# SIGTERM to augustad and exits with its status.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/augustad"]
