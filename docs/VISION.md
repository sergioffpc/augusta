# Product Vision Board — FPS Simulator Engine

## Vision

A realistic, physics-driven multiplayer FPS simulator engine —
server-authoritative, built in C++ with a Windows client and a headless Linux
server.

## Target Group

Solo developer (small informal team possible), not open source for now.

## Needs

Full control over architecture and deep, hands-on learning of low-level graphics
and systems programming.

## Product

- Client: Windows-only, rendering via NVIDIA Falcor (D3D12)
- Server: Linux in production (Windows for development only), headless (no
  rendering dependency)
- Multiplayer only, dedicated authoritative server — no singleplayer, no enemy
  AI
- Physics-based ballistics (real bullet drop and travel time, not hitscan)
- Match-based gameplay, no respawn until the match ends (tactical/milsim style)
- Movement: walk/run/crouch/prone + basic stamina + simple recoil (weapon sway
  and breath control deferred)
- Realistic, hit-location-based damage — no regenerating health

## Business Goals

2-8 players connected to a dedicated authoritative server, completed at least
one full match (no respawn) on the test map, fired a rifle whose bullet followed
real ballistic physics simulated by the server, and took damage determined by
hit location (no regenerating health).

---

_Inspiration: id Tech / Quake-era engines (realistic scope for a solo
developer)._ _Timeline: no fixed deadline, progress by milestones._
