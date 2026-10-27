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
- [Installation](#installation)
    - [Client](#client)
    - [Server](#server)
        - [k3s node (Debian 13)](#k3s-node-debian-13)
        - [Standalone](#standalone)
- [Development Setup](#development-setup)
    - [Development Environment](#development-environment)
        - [Windows (client)](#windows-client)
        - [Linux (server and shared core, Ubuntu 26.04)](#linux-server-and-shared-core-ubuntu-2604)
        - [macOS (client, server and shared core, Apple silicon)](#macos-client-server-and-shared-core-apple-silicon)
        - [Dev container or Codespaces (server and shared core)](#dev-container-or-codespaces-server-and-shared-core)
        - [Uninstall](#uninstall)
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

The [documentation site](https://sergioffpc.github.io/augusta/) publishes these
docs, the decisions behind the engine (ADRs) and the C++ API reference.

## Installation

Running the engine needs no checkout and no toolchain: a player installs the
client, an operator the server. To build them, see
[Development Setup](#development-setup).

### Client

Each [release](https://github.com/sergioffpc/augusta/releases/latest) publishes
the client for Windows x64 (`augustac-windows-x64.zip`) and Linux x64
(`augustac-linux-x64.tar.gz`): extract either anywhere and run `augustac` from
its folder. It needs a Vulkan 1.3 driver; the Linux client also needs the glibc
of Ubuntu 26.04 or newer and an X11 or Wayland session.

Before the first run, create `augustac.yaml` beside `augustac` from
[config/augustac.example.yaml](config/augustac.example.yaml), and point it at:

- the client pack and the public key it is signed with, from the same cook as
  the server's pack: Join refuses any other. No channel distributes them; they
  are copied by hand
  ([pack-key-rotation.md](docs/runbooks/pack-key-rotation.md));
- the server, `network.server_address`: the cluster node's address with the
  environment's node port, `30700` for `develop` and `30800` for `staging`, or
  `127.0.0.1:27015` for a local server.

Then, on Windows and on Linux:

```powershell
.\augustac.exe
```

```bash
./augustac
```

`--config <file>` reads a config kept elsewhere.

### Server

The production server is a Linux x86-64 or arm64 one. Its environments,
`develop` and `staging`, run on a k3s node that Flux deploys from Git; a server
also runs on its own, from an install.

#### k3s node (Debian 13)

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

#### Standalone

From a checkout with the [development environment](#development-environment),
`make install` builds the server and installs it with its libraries (the client
is never installed: it runs from its build directory or a release's folder):

```bash
make install prefix="$HOME/augusta"
```

On Windows, `prefix=C:/augusta` installs a development build. Create
`augustad.yaml` from
[config/augustad.example.yaml](config/augustad.example.yaml), pointing it at the
server pack and its public key, then start it:

```bash
~/augusta/bin/augustad --config augustad.yaml
```

It listens on UDP `0.0.0.0:27015` and serves its metrics on TCP `9464`.

## Development Setup

### Development Environment

One command per platform sets up a machine and a checkout ready to build, test
and commit: it installs the system's packages and the pinned tools (the
`platform` step, once per machine), clones the repository into `./augusta` (or
`$AUGUSTA_DIR`) and readies the checkout (the `checkout` step: submodules,
vcpkg, the git hooks, and `make toolchain`, which links the checkout's
`.toolchain/` to the pinned tools). In an existing checkout, run
`scripts/bootstrap.sh` instead, or `scripts/bootstrap.sh checkout` on a machine
already bootstrapped. Run `make toolchain` after a pull that bumps a tool's
version; `make toolchain-check` says which tools are stale, and
`source .toolchain/activate` puts them on a shell's PATH. The reference editor
is [VS Code](https://code.visualstudio.com): open the checkout's folder in it,
trust it, and install the extensions it recommends.

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

It installs the dev container's packages and pinned tools on the host, the
packages through sudo.

#### macOS (client, server and shared core, Apple silicon)

With the Command Line Tools (`xcode-select --install`) and
[Homebrew](https://brew.sh) installed:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/sergioffpc/augusta/develop/scripts/bootstrap.sh | bash
```

Everything builds natively, as on Linux: it installs LLVM 21 (CI's), CMake,
Ninja and Mesa's Vulkan through Homebrew, and the pinned tools from their
releases, without sudo; it puts GNU make on PATH in `~/.zprofile` (open a new
terminal after it). `make` builds the `macos` preset,
`make test PRESET=macos-debug` a Debug build; configure one once so the
`pre-push` hook can run clang-tidy. Xcode is not needed.

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
that was not there before, the pinned tools' store among it, newest first
(`uninstall platform` alone), and the checkout's readying (`uninstall checkout`
alone): `.toolchain/`, vcpkg's tool and binary cache, the git hooks and the
submodules, and on macOS an earlier bootstrap's dev containers. The repository's
files and your work stay. A machine bootstrapped before the bootstrap kept a
record of what it installed keeps what it installed then.

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
default release preset (`windows` on Windows, `linux` on Linux); `make install`
installs the server ([Standalone](#standalone)). Configuring on any other
platform stops with the supported list (NFR-04).

### Tests

Run the tests with the host's default preset, or select another preset:

```bash
make test
make test PRESET=windows-debug
make test PRESET=linux-san
```

`TAGS` runs only the tests with any of the given ctest labels, and `make labels`
lists the labels the build knows (the GoogleTest ones once `make test` has built
them):

```bash
make labels
make test TAGS="property"
make test TAGS="property check"
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
