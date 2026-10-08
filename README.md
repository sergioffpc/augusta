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
server-authoritative, with a Windows client and a headless Linux dedicated
server.

## Highlights

- **Server-authoritative** — the Linux dedicated server is the single source of
  truth for all gameplay state; the Windows client predicts locally and
  reconciles against authoritative snapshots (no exact replay).
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

## Development Setup

### Bootstrap

**Windows (client):** run PowerShell as Administrator (Win+X → "Terminal
(Admin)"). The bootstrap installs Visual Studio Build Tools and GNU make:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
./scripts/bootstrap-windows.ps1
```

Open a new terminal after setup. The Makefile loads the Visual Studio Build
Tools environment automatically for each command on Windows.

**Dev container (server / shared core):** open the repository in VS Code's Dev
Containers or in GitHub Codespaces. The container reproduces CI's Linux build
environment, so `make test` builds and tests the `linux` preset with nothing
else to install. The first build compiles the vcpkg dependencies; later ones
reuse them from `.vcpkg-bincache`, and sccache's objects from a volume.

**macOS:** nothing builds natively. With Docker or Podman running,
`scripts/dev-container.sh` opens a shell in the same container from a terminal
(or runs the command it's given), and the git hooks run their checks there. Run
`scripts/dev-container.sh make configure PRESET=linux-debug` once so the
`pre-push` hook can run clang-tidy.

Both the Windows bootstrap and the container initialize the vendored submodules
and configure the Conventional Commits `commit-msg` hook.

### Build and Run

The [Makefile](Makefile) wraps the build presets. On Windows it loads the Visual
Studio Build Tools environment through [scripts/vcenv.ps1](scripts/vcenv.ps1).

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

Create each local YAML from its `*.example.yaml` file and edit its pack and
public-key paths to point to cooked content before running. `make` builds the
default release preset (`windows` on Windows, `linux` on Linux). To install the
server, use `make install prefix=C:/augusta` on Windows (a development build;
Linux x86-64 is the only production server platform) or choose a Unix-style
prefix on Linux. Configuring on any other platform stops with the supported list
(NFR-04).

### Tests

Run the tests with the host's default preset, or select another preset:

```bash
make test
make test PRESET=windows-debug
make test PRESET=linux-san
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

## Documentation

- [VISION.md](docs/VISION.md) — product vision and v1 definition of done
- [REQUIREMENTS.md](docs/REQUIREMENTS.md) — functional and non-functional
  requirements
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — arc42 architecture document
- [ENGINEERING.md](docs/ENGINEERING.md) — engineering practices, CI/CD, workflow
- [ROADMAP.md](docs/ROADMAP.md) — milestone-driven roadmap
- [CONTEXT.md](CONTEXT.md) — domain glossary
- [docs/runbooks/](docs/runbooks/) — procedures for rollbacks, key rotation,
  node recovery and releases
- [docs/adr/](docs/adr/) — architecture decision records

## License

[MIT](LICENSE)
