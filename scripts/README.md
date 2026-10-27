# Scripts

The repository's own scripts: developer setup, CI and release steps, and the k3s
node's operation. Each script's header says what it does, why, and how to run
it; this index says which one to look at.

## Developer environment

- [`bootstrap.sh`](bootstrap.sh) sets up the development environment
  (`docs/ENGINEERING.md`, Developer Environment): the platform, then the
  checkout. What differs per platform lives in [`bootstrap/`](bootstrap/):
  `linux.sh` (Ubuntu 26.04, the server and the dev container), `windows.sh` (the
  client, in Git Bash as Administrator) and `macos.sh` (Apple silicon, native).
  Beside them, `versions.env` pins the toolchain's versions and `toolchain.sh`
  installs them into the shared store and links a checkout's `.toolchain/`
  (`make toolchain`, `make toolchain-check`).
- [`dev-container.sh`](dev-container.sh) runs a command, or a shell, in this
  checkout's Linux dev container, for a Linux build on a host that has its own.
- [`vcenv.cmd`](vcenv.cmd) runs a command inside the Visual Studio Build Tools
  x64 developer environment, so Ninja, clang-cl and vcpkg work from a plain
  terminal on Windows.
- [`worktree.sh`](worktree.sh) adds or removes a worktree under
  `augusta-wt/<name>`, with the submodules a build needs linked in and its
  `.toolchain/` made.

## Tests and reports

- [`coverage-report.sh`](coverage-report.sh) turns a `linux-coverage` ctest run
  into an llvm-cov HTML report and an LCOV file for the editor.
- [`requirements-matrix.py`](requirements-matrix.py) checks the tests'
  `// Requirements:` lines and the CI jobs' `# Requirements:` lines in
  `.github/workflows/` against `docs/REQUIREMENTS.md` (`--check`), or writes the
  matrix of each requirement's tests and results, and the CI jobs that check it.
- [`test-report.sh`](test-report.sh) joins the nightly's JUnit results,
  requirements matrix and coverage into the test report the documentation site
  publishes.

## Releases

- [`changelog.sh`](changelog.sh) cuts a release's `CHANGELOG.md` section with
  git-cliff, or prints it (`--notes`) as the GitHub Release's notes.
- [`stage-client.sh`](stage-client.sh) stages the client's runtime folder from a
  build, checks `augustac` runs from it alone, and archives it as a release
  attaches it.
- [`vcpkg-sbom.py`](vcpkg-sbom.py) writes an executable's SPDX SBOM from the
  vcpkg ports it was built with.
- [`sbom-cpes.py`](sbom-cpes.py) adds each dependency's NVD CPE to that SBOM, so
  grype can match it against vulnerabilities.
- [`prune-packages.py`](prune-packages.py) deletes the GitHub Packages versions
  nothing needs any more (server images, develop's charts, the vcpkg NuGet
  cache); it prints the plan, and deletes only with `--apply`.

## Cluster and server

- [`bootstrap-node.sh`](bootstrap-node.sh) bootstraps the k3s node that runs
  develop and staging, on Debian 13: OS and kernel hardening, k3s, the server's
  seccomp profile and Flux. A re-run finishes an interrupted one.
- [`operator-kubeconfig.sh`](operator-kubeconfig.sh) issues or renews the
  maintainer's day-long kubeconfig for the node.
- [`alert-notifications-wizard.sh`](alert-notifications-wizard.sh) walks the
  operator through Alertmanager's Telegram notifications, stage by stage.
- [`augustad-seccomp.sh`](augustad-seccomp.sh) checks the server image against
  its seccomp profile (`check`), or records the syscalls it is missing
  (`record`); x86-64 Linux only.
- [`augustad-seccomp-matches.py`](augustad-seccomp-matches.py) plays the Matches
  `augustad-seccomp.sh` runs the server through, one Agent each.
- [`check-pack-formats.py`](check-pack-formats.py) checks every environment's
  packs in `clusters/` are of the pack format the engine reads.
