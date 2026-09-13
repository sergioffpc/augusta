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
