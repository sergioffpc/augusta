# Augusta

[![CI](https://github.com/sergioffpc/augusta/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/sergioffpc/augusta/actions/workflows/ci.yml)
[![Nightly](https://github.com/sergioffpc/augusta/actions/workflows/nightly.yml/badge.svg)](https://github.com/sergioffpc/augusta/actions/workflows/nightly.yml)
[![CodeQL](https://github.com/sergioffpc/augusta/actions/workflows/codeql.yml/badge.svg)](https://github.com/sergioffpc/augusta/actions/workflows/codeql.yml)
[![Docs](https://img.shields.io/badge/docs-sergioffpc.github.io-blue)](https://sergioffpc.github.io/augusta/)
[![Release](https://img.shields.io/github/v/release/sergioffpc/augusta?sort=semver&filter=v*)](https://github.com/sergioffpc/augusta/releases/latest)
[![License: MIT](https://img.shields.io/github/license/sergioffpc/augusta)](LICENSE)
![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus)
![Platforms](https://img.shields.io/badge/platform-Windows%20client%20%7C%20Linux%20server-lightgrey)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-fe5196?logo=conventionalcommits)](https://www.conventionalcommits.org)

A realistic, physics-driven multiplayer FPS simulator engine —
server-authoritative, with a Windows and Linux client and a headless Linux
dedicated server.

## Contents

- [Highlights](#highlights)
- [Status](#status)
- [Development Setup](#development-setup)
    - [Bootstrap](#bootstrap)
        - [Windows (client)](#windows-client)
        - [Linux (server and shared core, Ubuntu 26.04)](#linux-server-and-shared-core-ubuntu-2604)
        - [macOS (server and shared core, through the dev container)](#macos-server-and-shared-core-through-the-dev-container)
        - [Dev container or Codespaces (server and shared core)](#dev-container-or-codespaces-server-and-shared-core)
        - [Uninstall](#uninstall)
        - [k3s node (the cluster's server, Debian 13)](#k3s-node-the-clusters-server-debian-13)
    - [Build and Run](#build-and-run)
    - [Tests](#tests)
    - [Agent Skills](#agent-skills)
- [Documentation](#documentation)
- [License](#license)

## Highlights

- **Server-authoritative** — the Linux dedicated server is the single source of
  truth for all gameplay state; the client predicts locally and reconciles
  against authoritative snapshots (no exact replay).
- **Physics-based ballistics** — real bullet drop and travel time, not hitscan;
  hit location and body part determine damage (no regenerating health).
- **Match-based, tactical** — no respawn until the match ends; movement includes
  walk/run/crouch/prone, stamina, and recoil.
- **ECS core** (Flecs) shared between client and server, with a physics layer
  for collision/movement and a custom ballistics module.
- **Mechanism / policy / data separation** — engine mechanism in C++, game
  policy (round lifecycle, win conditions, spawn rules) in sandboxed Lua,
  tunable balance values as data.

## Status

See [docs/ROADMAP.md](docs/ROADMAP.md) for the milestone plan.

Each [release](https://github.com/sergioffpc/augusta/releases/latest) publishes
the client for Windows x64 (`augustac-windows-x64.zip`) and Linux x64
(`augustac-linux-x64.tar.gz`): extract either anywhere and run `augustac` from
its folder. The Linux client needs the glibc of Ubuntu 26.04 or newer, a Vulkan
1.3 driver, and an X11 or Wayland session.

The [documentation site](https://sergioffpc.github.io/augusta/) publishes these
docs, the decisions behind the engine (ADRs) and the C++ API reference.

## Development Setup

### Bootstrap

One command per platform sets up a machine and a checkout ready to build, test
and commit: it installs the toolchain, clones the repository into `./augusta`
(or `$AUGUSTA_DIR`) and readies the checkout (submodules, vcpkg, the git hooks).
In an existing checkout, run `scripts/bootstrap.sh` instead. The reference
editor is [VS Code](https://code.visualstudio.com): open the checkout's folder
in it and install the extensions it recommends.

#### Windows (client)

In PowerShell as Administrator (Win+X → "Terminal (Admin)"):

```powershell
winget install --id Git.Git --exact --source winget --accept-package-agreements --accept-source-agreements; & "$env:ProgramFiles\Git\bin\bash.exe" -c "curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap.sh | bash"
```

It installs Git for Windows, then runs the bootstrap in its Git Bash, which
installs Visual Studio Build Tools, LLVM, GNU make and the rest, and Podman with
its WSL 2 machine. Open a new terminal afterwards: the Makefile loads the Build
Tools' environment for each command.

#### Linux (server and shared core, Ubuntu 26.04)

```bash
curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap.sh | bash
```

It installs the dev container's toolchain on the host, through sudo.

#### macOS (server and shared core, through the dev container)

With [Homebrew](https://brew.sh) installed:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap.sh | bash
```

Nothing builds natively: it installs git, Git LFS and Podman (unless Docker is
already there), starts Podman's machine, and builds the dev container's image
and the checkout's container. `scripts/dev-container.sh` opens a shell in it (or
runs the command it's given), and the git hooks run their checks there. Run
`scripts/dev-container.sh make configure PRESET=linux-debug` once so the
`pre-push` hook can run clang-tidy.

#### Dev container or Codespaces (server and shared core)

Open the repository in VS Code's Dev Containers or in GitHub Codespaces: there
is nothing to run. The container reproduces CI's Linux build environment, so
`make test` builds and tests the `linux` preset. The first build compiles the
vcpkg dependencies; later ones reuse them from `.vcpkg-bincache`, and sccache's
objects from a volume.

#### Uninstall

```bash
scripts/bootstrap.sh uninstall
```

In Git Bash as Administrator on Windows. It removes what the bootstrap installed
that was not there before, newest first (`uninstall toolchain` alone), and the
checkout's readying (`uninstall checkout` alone): vcpkg's tool and binary cache,
the git hooks and the submodules, and on macOS the dev containers. The
repository's files and your work stay. A machine bootstrapped before the
bootstrap kept a record of what it installed keeps what it installed then.

#### k3s node (the cluster's server, Debian 13)

Over SSH, by key, as your own user:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap-node.sh | bash
```

[scripts/bootstrap-node.sh](scripts/bootstrap-node.sh) sets up the node that
runs `develop` and `staging`, through sudo, without a checkout: the crash-core
handler, Podman and lldb to read a core, the server's `augusta` user, the
asset-pack volume, the capture filesystem, the OS's hardening (a firewall, SSH
by key only, unattended security updates, auditd), k3s at the cluster's release,
and Flux, which then installs the monitoring stack and both environments from
Git. It skips what the node already has. The firewall then lets only the SSH
session's address, or those given with `--admin`, reach SSH and the API server.
The asset packs are the one state not in Git: `augusta-publish` puts them on the
node, or `--packs` unpacks a copy
([k3s-node-recovery.md](docs/runbooks/k3s-node-recovery.md)).

### Build and Run

The [Makefile](Makefile) wraps the build presets. On Windows it loads the Visual
Studio Build Tools environment through [scripts/vcenv.cmd](scripts/vcenv.cmd).

Build and start the Linux server in the dev container first. Leave it running,
listening on the configured address (the default client connects to
`127.0.0.1:27015`):

```bash
make
./build/x64-linux/src/server/augustad --config config/augustad.yaml
```

Then, from a separate Windows PowerShell terminal, build and start the client
with its local config and cooked client pack:

```powershell
make
& "build/x64-windows/src/client/augustac.exe" --config config/augustac.yaml
```

To bring back a playtest from its Match capture (ADR-0050), start a Development
server (ADR-0074) with `debug.reenactments: true` on the capture's packs, then
one client per captured player, all at once, each the player of the capture
`--player` names:

```powershell
& "build/x64-windows/src/client/augustac.exe" --config config/augustac.yaml --reenact captures/20261009T101500123Z-0001.acap --player 1
```

`augusta-inspect` lists a capture's players by those numbers.

Create each local YAML from its `*.example.yaml` file and edit its pack and
public-key paths to point to cooked content before running. `make` builds the
default release preset (`windows` on Windows, `linux` on Linux). `make install`
installs only the server; the client has no install step and runs from its build
directory. Use `prefix=C:/augusta` on Windows (a development build; Linux x86-64
and Linux arm64 are the production server platforms) or a Unix-style prefix on
Linux. Configuring on any other platform stops with the supported list (NFR-04).

### Tests

Run the tests with the host's default preset, or select another preset:

```bash
make test
make test PRESET=windows-debug
make test PRESET=linux-san
```

Every preset makes a Development build, which carries the debug tools, except
`windows-shipping` and `linux-shipping`: they inherit `windows` and `linux` and
make a Shipping build, what players get, without them (ADR-0074). A Development
build's engine version ends in `+dev`, so the two kinds refuse each other at
Join:

```bash
make test PRESET=windows-shipping
make test PRESET=linux-shipping
```

The `linux-fuzz` preset builds the fuzz targets:

```bash
make PRESET=linux-fuzz
```

[tests/fuzz/README.md](tests/fuzz/README.md) explains how to run them.

The `linux-coverage` preset measures the tests' coverage (ADR-0013). In VS Code,
select it and run "Test: Run All Tests with Coverage": CMake Tools shows the
result in the editor and the Test Coverage view. From a shell:

```bash
make coverage   # HTML and LCOV in build/x64-linux-coverage/report
```

The nightly uploads the same report as its `coverage-report` artifact.

Other useful checks:

```bash
make format-check
make lint
```

### Agent Skills

The development flows in [CLAUDE.md](CLAUDE.md) (main, engineering) run on
[Matt Pocock's skills](https://github.com/mattpocock/skills). Install them
either as editable files, choosing the skills and agents you use:

```bash
npx skills@latest add mattpocock/skills
```

and update them with `npx skills update`; or, in Claude Code, as the complete
set in a managed, read-only plugin from the official marketplace:

```bash
claude plugins install mattpocock-skills
```

## Documentation

- [VISION.md](docs/VISION.md) — product vision
- [REQUIREMENTS.md](docs/REQUIREMENTS.md) — functional and non-functional
  requirements
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — arc42 architecture document
- [ENGINEERING.md](docs/ENGINEERING.md) — engineering practices, CI/CD, workflow
- [ROADMAP.md](docs/ROADMAP.md) — milestone-driven roadmap
- [GLOSSARY.md](GLOSSARY.md) — domain glossary
- [docs/runbooks/](docs/runbooks/) — procedures for rollbacks, key rotation,
  node recovery and releases
- [docs/adr/](docs/adr/) — architecture decision records

## License

[MIT](LICENSE)
