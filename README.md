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
        - [Dev container or Codespaces](#dev-container-or-codespaces)
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

A machine needs [git](https://git-scm.com) and [Podman](https://podman.io) and
nothing else: everything the project builds, tests and lints with lives in a
Linux dev container, the image of CI's Linux build. On Windows, install both in
WSL and work there; on macOS, start a Podman machine first
(`podman machine init && podman machine start`) and keep the checkout under the
home directory, which is what the machine mounts. On Linux, rootless Podman
needs `newuidmap` (Ubuntu's `uidmap` package).

```bash
git clone --branch develop https://github.com/sergioffpc/augusta.git
cd augusta
scripts/bootstrap.sh
```

The first run builds the image, creates this checkout's container and readies
the checkout in it: submodules, vcpkg, the git hooks and `.toolchain/`. Then run
everything through the container, or from a shell in it:

```bash
scripts/dev-container.sh make test
scripts/dev-container.sh
```

The git hooks run in the container by themselves. git's identity, signing and
credentials stay on the host, so commit and push there. Git LFS's content comes
through the container too; pushing a new LFS file needs `git-lfs` on the host,
as the container holds no credentials.

The container compiles and tests the Linux client on Mesa's software Vulkan; it
does not open a window on the host's display or use its GPU. The native Windows
and macOS builds live in CI, whose `client-x64-windows` job alone reproduces
Windows-only failures: `scripts/bootstrap.sh platform` and
`scripts/bootstrap.sh checkout` remain the recipes for them.

#### Dev container or Codespaces

Open the repository in VS Code's Dev Containers or in GitHub Codespaces: there
is nothing to run. It is the same image, with `.vcpkg-bincache` for vcpkg's
binary cache and sccache's objects in a volume.

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
