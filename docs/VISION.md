# Product Vision Board — FPS Simulator Engine

## Vision
A realistic, physics-driven multiplayer FPS simulator engine — server-authoritative,
built in C++ with a Windows client (rendering via NVIDIA Falcor/D3D12) and a
headless Linux server, to master low-level systems and networking programming.

## Target Group
Solo developer (small informal team possible), not open source for now.

## Needs
Full control over architecture and deep, hands-on learning of low-level graphics
and systems programming — deliberately not using Unreal/Unity/Godot.

## Product
- Client: Windows-only, rendering via NVIDIA Falcor (D3D12)
- Server: Linux-only, headless (no rendering dependency)
- Multiplayer only, dedicated authoritative server — no singleplayer, no enemy AI
- Physics-based ballistics (real bullet drop and travel time, not hitscan)
- Round-based gameplay, no respawn until round end (tactical/milsim style)
- Movement: walk/run/crouch/prone + basic stamina + simple recoil
  (weapon sway and breath control deferred post-v1)
- Realistic, hit-location-based damage — no regenerating health
- v1 scope: 1 weapon (rifle), 1 test map, 2-8 concurrent players

## Business Goals
v1 is done when: 2-8 players connect to a dedicated authoritative server, complete
at least one full round (no respawn) on the test map, fire a rifle whose bullet
follows real client-server-synced ballistic physics, and take damage determined
by hit location (no regenerating health).

---
*Inspiration: id Tech / Quake-era engines (realistic scope for a solo developer).*
*Timeline: hobby project, no fixed deadline, progress by milestones.*
