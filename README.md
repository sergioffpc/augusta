# Augusta

A realistic, physics-driven multiplayer FPS simulator engine — server-authoritative,
built in C++23 with a Windows client (rendering via NVIDIA Falcor/D3D12) and a
headless Linux dedicated server. A hobby project to master low-level systems and
networking programming, deliberately built from scratch instead of on top of
Unreal/Unity/Godot.

## Highlights

- **Server-authoritative** — the Linux dedicated server is the single source of
  truth for all gameplay state; the Windows client predicts locally and
  reconciles against authoritative snapshots (no exact replay).
- **Physics-based ballistics** — real bullet drop and travel time, not
  hitscan; hit location and body part determine damage (no regenerating health).
- **Match-based, tactical** — no respawn until the match ends; movement includes
  walk/run/crouch/prone, stamina, and recoil.
- **ECS core** (Flecs) shared between client and server, with PhysX for
  collision/movement and a custom ballistics module.
- **Mechanism / policy / data separation** — engine mechanism in C++, game
  policy (round lifecycle, win conditions, spawn rules) in sandboxed Lua,
  tunable balance values as data.
- **v1 scope** — 1 weapon (rifle), 1 test map, 2–8 concurrent players.

## Status

Early stage — see [docs/ROADMAP.md](docs/ROADMAP.md) for the milestone plan.
This is a solo-developer hobby project with no fixed deadline.

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

**WSL2 (server / shared core):** use Ubuntu 26.04 under WSL2:
```bash
./scripts/bootstrap-wsl.sh
```

Both scripts initialize the vendored submodules and configure the Conventional
Commits `commit-msg` hook.

**Dev container (server / shared core, alternative to WSL2):** open the
repository in VS Code's Dev Containers or in GitHub Codespaces. The container
reproduces CI's Linux build environment, so `make test` builds and tests the
`linux` preset with nothing else to install. The first build compiles the vcpkg
dependencies; later ones reuse them from `.vcpkg-bincache`, and sccache's
objects from a volume.

### Build and Run

The [Makefile](Makefile) wraps the build presets. On Windows it loads the
Visual Studio Build Tools environment through [scripts/vcenv.ps1](scripts/vcenv.ps1).

Build and start the Linux server in WSL first. Leave it running, listening on
the configured address (the default client connects to `127.0.0.1:27015`):
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
default release preset (`windows` on Windows, `linux` on WSL). To install the
server, use `make install prefix=C:/augusta` on Windows or choose a Unix-style
prefix on Linux.

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

Other useful checks:
```bash
make format-check
make lint
```

## Documentation

- [VISION.md](docs/VISION.md) — product vision and v1 definition of done
- [REQUIREMENTS.md](docs/REQUIREMENTS.md) — functional and non-functional requirements
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — arc42 architecture document
- [ENGINEERING.md](docs/ENGINEERING.md) — engineering practices, CI/CD, workflow
- [ROADMAP.md](docs/ROADMAP.md) — milestone-driven roadmap
- [CONTEXT.md](CONTEXT.md) — domain glossary
- [docs/runbooks/](docs/runbooks/) — procedures for rollbacks, key rotation, node recovery and releases
- [docs/adr/](docs/adr/) — architecture decision records

## License

[MIT](LICENSE)
