# Engineering review profile

The Augusta-specific half of the `engineering-review` skill
(`.claude/skills/engineering-review/`), which holds the methodology. This file
says where Augusta's risk is, which guarantees to try to disprove, and which
tools can verify them. It is read in the runbook's Phase 1.

## Activation

Claude Code loads the skill from `.claude/skills/engineering-review/`. It is
user-invoked: run `/engineering-review` with the scope, for example
`/engineering-review server inbound wire path`, or with no argument for a full
review. Other agent tools need `SKILL.md` given to them explicitly.

## Reports and ledger

Reports and the ledger live in `reviews/` at the repository root:
`reviews/<YYYY-MM-DD>-<scope-slug>.md` and `reviews/ledger.md`. Not under
`docs/`, which holds decisions and requirements only and is published as the
documentation site. They are Markdown in the repository, so Prettier and
pymarkdown format and lint them like any other (ADR-0012).

## Sources of truth

Read before Phase 2, as `docs/agents/domain.md` describes: `CONTEXT.md` (use its
terms in findings), `docs/ARCHITECTURE.md` (§5 building blocks, §8 crosscutting
concepts, §11 known risks), `docs/REQUIREMENTS.md` (NFR-01 to NFR-09), and the
ADRs for each area reviewed. The project's own C++ judgment rules are
`docs/agents/coding-standards.md`; the C++ standard checks against them and
ADR-0012, ADR-0033 and ADR-0037, not against general preference. A finding that
contradicts an ADR says so explicitly.

## Subsystem map

Tiers derive from the requirement or ADR named; re-tier an area when its
requirement changes. Paths are under `src/modules/` unless they start with
`src/` or `tools/`.

| Area                    | Code                                                                                                                                                              | Tier     | Default depth | Standards                                 | Anchors                                                    |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | ------------- | ----------------------------------------- | ---------------------------------------------------------- |
| Inbound wire, admission | `protocol`, `shared_wire`, `networking`, `src/server/{wire,tick_messages,admission,peer_gate,misbehaviour,connection_health,host_network}`                        | Critical | L3, L4 (fuzz) | networking, security, cpp                 | NFR-05, ADR-0003, ADR-0038, ADR-0044                       |
| Authoritative tick      | `simulation`, `tick`, `physics`, `ballistics`, `weapon`, `command`, `primitives`, `src/server/{host,host_impl,host_match,match,simulation_mapping,command_queue}` | Critical | L3            | simulation, correctness, performance, cpp | NFR-01, NFR-03, NFR-05, NFR-06, ADR-0023                   |
| Threads and failure     | `supervisor`, `failure`, `application`, `harness`, `src/server/runtime`, `src/client/runtime`                                                                     | Critical | L3, L4 (TSan) | concurrency, errors                       | ADR-0005, ADR-0033                                         |
| Packs and integrity     | `assets`, `src/server/content`, `src/client/content`                                                                                                              | Critical | L3, L4 (fuzz) | assets, security                          | NFR-08, ADR-0030, ADR-0031                                 |
| Lua sandbox and policy  | `scripting`, `parameters`, `src/server/{policy_loader,parameters_loader}`                                                                                         | Critical | L3            | security, simulation                      | ADR-0022, ADR-0039                                         |
| Prediction, replication | `prediction`, `simulation/replication`, `presentation`, `src/client/{frame_mapping,net_stats}`                                                                    | High     | L3            | simulation, networking                    | NFR-02, ADR-0004, ADR-0024                                 |
| Capture and replay      | `capture`, `src/server/{capture,capture_retention,replay,replay_catalog,replay_server}`                                                                           | High     | L2, L4 (fuzz) | simulation, assets, errors                | NFR-09, ADR-0048, ADR-0050, ADR-0051                       |
| Pack writer             | `tools/pack`                                                                                                                                                      | High     | L2            | assets, testing                           | ADR-0030, ADR-0031, ADR-0013 (golden packs)                |
| Configuration           | `config`                                                                                                                                                          | Medium   | L2            | correctness, build-deploy                 | ADR-0034                                                   |
| Observability           | `logging`, `src/server/{metrics,host_metrics,lock_free_metrics,liveness,heartbeat,crash,host_log}`                                                                | Medium   | L2            | errors, concurrency                       | NFR-07, ADR-0029, ADR-0047, ADR-0049                       |
| Supporting modules      | `map`, `animation`, `math`, `input`, `version`, `src/client/{lobby_readiness,character_loader,scene_loader}`                                                      | Medium   | L2            | correctness                               | per module                                                 |
| Rendering and audio     | `renderer`, `audio`                                                                                                                                               | Low      | L1–L2         | cpp, concurrency, performance             | ADR-0009, ADR-0059, ADR-0013 (judged by eye)               |
| Agents and tools        | `tools/agent`, `tools/composer`, `tools/docs`                                                                                                                     | Low      | L1            | testing                                   | ADR-0052                                                   |
| Build and deployment    | `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `vcpkg.json`, `.github/workflows/`, `Dockerfile`, `chisel/`, `charts/`, `clusters/`                              | Medium   | L2            | build-deploy, security                    | ADR-0008, ADR-0025, ADR-0026, ADR-0054, ADR-0056, ADR-0057 |

Rendering and audio output are validated by eye and ear (ADR-0013): review their
resource lifetimes, threading and decision code, not visual results.

## Critical flows

Trace these end to end at L3 when their area is in scope:

1. **Command to authoritative state:** client Input → Command → client
   Networking → server decode and screening → command queue → SimulationWorld
   tick → TickResult → replication → client decode → PredictionWorld
   reconciliation.
2. **Shot:** fire → WeaponHandling → Ballistics → Lag compensation against the
   Hitbox history → Hit confirmation (ADR-0044).
3. **Connection lifecycle:** connect → Lobby admission within its deadline →
   Session → misbehaviour threshold or disconnect → teardown (ARCHITECTURE §8,
   Security).
4. **Pack load:** config path and public key → BLAKE3 and Ed25519 verification →
   parse → content, on client and server.
5. **Policy hook:** SimulationWorld Scripts/Behaviours phase → Lua hook → typed
   Policy action → `server::Host` (ADR-0022, ADR-0023).
6. **Failure and shutdown:** a worker failure, an exception, or SIGINT/SIGTERM →
   supervisor → stop → join → exit status (ADR-0005, ADR-0033).
7. **Capture to replay:** Match capture → capture file → Replay or Reenactment
   (NFR-09).

## Guarantees to try to disprove

The project states these; a review checks them rather than assuming them.

- Each piece of mutable state has one owning thread and crosses threads only as
  an immutable value; transport callbacks publish events and nothing else
  (ADR-0005).
- The supervisor records the first failure, stops once, and joins every worker
  before releasing what they use (ADR-0005).
- Tick-path failures return `std::expected`; errors are types, never strings;
  integrity invariants are checked in every build (ADR-0033).
- No I/O inside ECS worlds (ARCHITECTURE §8).
- The server validates all client input and disconnects a peer past the
  misbehaviour threshold (US-15, NFR-05).
- The Lua sandbox exposes no I/O and bounds each hook's instructions (ADR-0022).
- A pack is verified before any of it is used, and a failed verification stops
  the load (NFR-08).
- The shared core does not depend on the protocol or on client input; the
  boundary tests `tests/*_boundary.cmake` enforce what their comments state.

Known, accepted risks are listed in ARCHITECTURE §11 (no peer authentication on
a trusted LAN, cross-platform PhysX divergence). A finding on one of them must
show more than the documented risk.

## Verification tools

Which test kind runs at which CI stage is ADR-0013; the CI jobs are in
`docs/ENGINEERING.md`, CI/CD. Build presets are in `CMakePresets.json`.

- **Sanitizers and fuzzing (Linux only):** the `linux-san` (ASan and UBSan),
  `linux-tsan` (with `tests/tsan.supp`) and `linux-fuzz` presets. Fuzz targets
  and corpora are in `tests/fuzz/`: protocol decoding, pack loading and capture
  reading.
- **Impaired-link netcode:** the `linux-netcode` preset.
- **Coverage:** the `linux-coverage` preset, a report rather than a gate.
- **Static analysis:** `clang-tidy` through `make tidy`; on Windows through
  `scripts/vcenv.cmd`.
- **Benchmarks:** `tests/*_benchmark.cpp` (Google Benchmark); the nightly keeps
  their history.
- **Requirements traceability:** `scripts/requirements-matrix.py` maps tests to
  the requirement IDs their `// Requirements:` lines name.
- **Profiling:** Nsight Systems and Nsight Graphics, `docs/agents/tooling.md`.

Platform limits to record in the report when they apply: sanitizers and fuzzers
need Linux (CI, WSL or the dev container); Windows-only failures reproduce only
in CI's `client-x64-windows` job; the renderer needs a GPU; the k3s cluster is
not touched by a review.
