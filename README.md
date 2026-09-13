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
- **Round-based, tactical** — no respawn until round end; movement includes
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

## Building

One-time setup, then the same CMake presets on either side.

**Windows (client):**

`bootstrap-windows.ps1` installs Visual Studio Build Tools, so it needs an
**elevated** PowerShell (Win+X → "Terminal (Admin)"), and PowerShell's default
execution policy blocks running local scripts at all:
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force   # this session only
.\scripts\bootstrap-windows.ps1
```

Then build **from inside VS Code** (open the repo, install the recommended
extensions), using the CMake Tools extension's Configure/Build/Test commands
(command palette, or the status bar buttons) with the `windows` preset. A
plain terminal doesn't have `cl.exe`'s `INCLUDE`/`LIB`/`PATH` set up, which
the Ninja generator needs and a Build Tools-only install has no Start Menu
shortcut to get for you — CMake Tools finds and loads it automatically,
which a raw `cmake --preset windows` in a terminal won't.

**WSL2 (server / shared core):**
```bash
./scripts/bootstrap-wsl.sh        # build-essential, CMake, Ninja, clang-format/clang-tidy, vcpkg, sccache
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

Both bootstrap scripts also `git submodule update --init` the vendored vcpkg
(`third_party/vcpkg`) and wire up the Conventional Commits `commit-msg` hook.

**Sanitizer build (Linux, ASan+UBSan):**
```bash
cmake --preset linux-sanitizers
cmake --build --preset linux-sanitizers
ctest --preset linux-sanitizers
```

The three presets (`windows`, `linux`, `linux-sanitizers`) are defined in
[CMakePresets.json](CMakePresets.json) and are what CI builds with too.

## Documentation

- [VISION.md](docs/VISION.md) — product vision and v1 definition of done
- [REQUIREMENTS.md](docs/REQUIREMENTS.md) — functional and non-functional requirements
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — arc42 architecture document
- [ENGINEERING.md](docs/ENGINEERING.md) — engineering practices, CI/CD, workflow
- [ROADMAP.md](docs/ROADMAP.md) — milestone-driven roadmap
- [CONTEXT.md](CONTEXT.md) — domain glossary
- [docs/adr/](docs/adr/) — architecture decision records

## License

[MIT](LICENSE)
