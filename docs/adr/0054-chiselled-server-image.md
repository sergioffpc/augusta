# Server Image: a Chiselled Ubuntu 26.04 Root on `scratch`

The server image runs `augustad` under `tini` (ADR-0047), and that is all it
runs. `ldd` names four libraries it needs: `libc`, `libm`, `libstdc++` and
`libgcc_s`; vcpkg's dependencies are linked statically. Its runtime stage was a
full `ubuntu:26.04`, 146 MB of which `augustad` was 24 MB and 88 packages (107
MB) were apt, dpkg, perl, coreutils, PAM, OpenSSL, the systemd libraries and the
rest, none of them used, all of them CVEs the CI scan had to keep clean. That
base is also a rock (an image Canonical builds with rockcraft): it ships Pebble,
a Go binary no package owns, which `apt-get upgrade` cannot patch and which
failed the scan until the Dockerfile deleted it (#455). `apt-get purge` cannot
take much more out: most of the rest is essential, required, or what apt and
dpkg need.

The runtime stage is `FROM scratch`, its root filesystem cut by
[chisel](https://github.com/canonical/chisel) from Ubuntu 26.04's packages in a
`rootfs` stage of the Dockerfile:

- **Slices.** `libc6_libs`, `libgcc-s1_libs`, `libstdc++6_libs`,
  `base-files_base` (the directory tree), `base-files_release-info`
  (`/etc/os-release`, what a scanner tells the distribution by),
  `base-passwd_data`, and `base-files_chisel` (chisel's manifest of what it cut,
  `/var/lib/chisel/manifest.wall`). The slice definitions come from a pinned
  commit of chisel-releases' `ubuntu-26.04` branch; the packages are the
  archive's current ones (`resolute`, `-updates` and `-security`). The `chisel`
  binary is a pinned, checksummed release.
- **tini.** chisel-releases has no slice for it, and Ubuntu's package has no
  `tini-static`, so the repository defines `tini_bins` itself
  (`chisel/slices/tini.yaml`): `/usr/bin/tini`, needing only `libc6_libs`. It is
  cut like the rest, so the scan sees it too.
- **The scan.** trivy (0.75) reads no chisel manifest: an image with only that
  has no OS packages to scan. For each package in the manifest, the `rootfs`
  stage writes the archive's control stanza for that exact version
  (`apt-cache show <name>=<version>`) to `/var/lib/dpkg/status.d/<name>`, the
  layout distroless images use and trivy and the SBOM read as a dpkg database.
  The `Source` field is what maps `libc6` to `glibc`'s Ubuntu advisories. A
  version the stage's package lists do not have fails the build.
- **User.** `augusta`, UID and GID 65532, appended to `/etc/passwd` and
  `/etc/group`, and named by number in `USER` so the chart's `runAsNonRoot` can
  check it. The chart's pod `securityContext` sets the same IDs.
- **Updates.** No `apt-get upgrade` runs in the image: a security update reaches
  it as the archive's new version of a package, cut when the image is built. CI
  rebuilds the `rootfs` stage on every run (`no-cache-filters`), as it did the
  runtime stage, so an image is never older than its commit's build.

The result is the server and its 8 MB root: about 32 MB, from 146 MB, with seven
packages (`base-files`, `base-passwd`, `gcc-16-base`, `libc6`, `libgcc-s1`,
`libstdc++6`, `tini`) and no shell, apt, dpkg or Pebble.

**Crash reports.** They are unchanged (ADR-0047). The kernel pipes a core to the
node's `systemd-coredump`, outside the container, so nothing in the image takes
part; `tini` is still PID 1, and `augustad` still raises its own `RLIMIT_CORE`.

**Debugging a running server.** With no shell, `kubectl exec` has nothing to
run. An ephemeral container does, sharing the server's process namespace:

```sh
pod=$(kubectl -n develop get pod -l app.kubernetes.io/name=augustad,scenario=<scenario> \
  -o jsonpath='{.items[0].metadata.name}')
kubectl -n develop debug -it "$pod" --image=ubuntu:26.04 --target=augustad -- bash
```

It runs as the pod's user, 65532, as `augustad` does: `/proc/1` is `tini`, its
child is `augustad`, and the server's root filesystem is under
`/proc/<pid>/root/`. A core is still read with gdb off the node, as ADR-0047
says.

## Considered Options

- **`ubuntu:26.04` with packages purged**: rejected. Most of what is unused is
  essential or required, or what apt and dpkg depend on; little comes out, and
  the rock's files no package owns stay a manual chase.
- **`debian:13-slim`, or Google's distroless `cc` (Debian 13)**: rejected.
  Debian 13's glibc 2.41 and libstdc++ 14 cannot run a binary built on Ubuntu
  26.04 (glibc 2.43, libstdc++ 16). The build would have to move to an older
  toolchain than the one CI tests.
- **Copying the four `.so` files into `scratch` by hand**: rejected. Nothing in
  the image would say which packages and versions they came from, so the scan
  would see no packages and pass whatever their CVEs.
- **`tini` copied from the build stage**: rejected. It works, but it is a binary
  no package metadata names, which is what #455 was; a local slice costs one
  file and keeps it scanned.
- **chisel-releases at the branch head**: rejected. The rest of CI pins what it
  downloads; a slice definition changing under an unchanged commit would change
  the image without a diff. Bumping the commit is a one-line change.
- **Static linking `augustad`**: rejected. glibc discourages it (NSS and
  `dlopen` still load shared libraries at run time), and glibc and libstdc++
  would still be in the image, inside the binary, where no scanner sees their
  versions.

## Consequences

- No shell or package manager in the container: `kubectl exec` fails, and
  debugging goes through `kubectl debug` as above.
- The `rootfs` stage downloads `chisel` from GitHub Releases and chisel-releases
  from GitHub on every image build; either being unavailable fails the build.
- A package `augustad` starts to need, such as `ca-certificates` for an outbound
  TLS connection, is a slice added to the Dockerfile's `chisel cut`. A
  `status.d` entry follows from the manifest with no further change.
- `CHISEL_VERSION` and its checksum, and `CHISEL_RELEASES_COMMIT`, are bumped by
  hand. The packages are not: they follow the archive.
- The image is amd64 only, as the build stage already was (`x64-linux`).
