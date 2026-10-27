# Engineering review ledger

Next ID: ER-28

## Findings

| ID    | Fingerprint                                                                       | Category    | Severity | Status           | First seen | Last checked |
| ----- | --------------------------------------------------------------------------------- | ----------- | -------- | ---------------- | ---------- | ------------ |
| ER-1  | `correctness/server-host/match_winner/stale-on-abandoned-match`                   | BUG         | Low      | fixed            | 2026-10-14 | 2026-10-26   |
| ER-2  | `security/server-host/PumpPeers/no-peer-or-rate-bound`                            | RISK        | Low      | recurring        | 2026-10-14 | 2026-10-26   |
| ER-3  | `security/scripting/MakeSandbox/warn-and-memory-unbounded`                        | RISK        | Low      | fixed            | 2026-10-14 | 2026-10-26   |
| ER-4  | `build-deploy/ci/sbom-cpes/released-sbom-names-removed-port`                      | BUG         | Low      | not-reproducible | 2026-10-14 | 2026-10-26   |
| ER-5  | `networking/protocol/ReadList/allocates-past-truncation`                          | IMPROVEMENT | Low      | fixed            | 2026-10-14 | 2026-10-26   |
| ER-6  | `security/scripting/MakeSandbox/gc-finalizer-escapes-limits`                      | BUG         | Medium   | fixed            | 2026-10-19 | 2026-10-26   |
| ER-7  | `errors/host/CaptureDirectory/narrow-path-conversion-throws`                      | BUG         | Low      | fixed            | 2026-10-19 | 2026-10-26   |
| ER-8  | `security/scripting/LimitInstructions/hook-work-escapes-instruction-limit`        | BUG         | Medium   | new              | 2026-10-26 | 2026-10-26   |
| ER-9  | `security/scripting/ToValue/shared-subtables-exponential-copy`                    | BUG         | Medium   | new              | 2026-10-26 | 2026-10-26   |
| ER-10 | `simulation/prediction/World-Impl-physics/missing-remote-colliders`               | BUG         | Medium   | new              | 2026-10-26 | 2026-10-26   |
| ER-11 | `security/ci/workflow-permissions/registry-cache-writable-by-every-job`           | RISK        | Medium   | new              | 2026-10-26 | 2026-10-26   |
| ER-12 | `errors/application/path-text/narrow-path-conversion-throws`                      | BUG         | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-13 | `security/assets/Pack-Load/mapped-pack-read-after-verify`                         | BUG         | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-14 | `simulation/presentation/Correction-Update/snap-threshold-sees-blended-increment` | BUG         | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-15 | `simulation/scripting/MakeSandbox/address-dependent-values`                       | BUG         | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-16 | `errors/host/ReplayCatalog/oversized-capture-breaks-encoding-invariant`           | BUG         | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-17 | `errors/scripting/Script-Call/hook-error-traceback-multiline-log`                 | BUG         | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-18 | `networking/protocol/DecodeEnvelope/parse-allocates-before-list-bound`            | RISK        | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-19 | `simulation/gameplay/weapon-Step/fp-contraction-differs-across-arch`              | RISK        | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-20 | `concurrency/server/HandleShutdownSignal/runtime-released-under-handler`          | RISK        | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-21 | `performance/simulation/HitboxesFor/history-compose-before-cull`                  | RISK        | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-22 | `security/deploy/augustad-config/public-key-beside-pack`                          | DESIGN      | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-23 | `simulation/simulation/ShootersDelay/claimed-delay-unbounded-by-rtt`              | DESIGN      | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-24 | `simulation/host/Replay/policy-history-differs-from-live`                         | DESIGN      | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-25 | `correctness/config/ServerConfigFrom/inert-settings-accepted`                     | IMPROVEMENT | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-26 | `build-deploy/cmake/compile-options/no-binary-hardening`                          | IMPROVEMENT | Low      | new              | 2026-10-26 | 2026-10-26   |
| ER-27 | `testing/fuzz/pack_load_fuzz/animation-blobs-unreached`                           | IMPROVEMENT | Low      | new              | 2026-10-26 | 2026-10-26   |

## History

### ER-1

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Traced (V1) from
  `Host::Impl::HandleDisconnect`'s `kEndedMatch` branch to `Capturer::EndMatch`;
  no test covers the case.
- 2026-10-15 129f9a81 triage: filed as #659 (`ready-for-agent`).
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: fixed. `96e1b2bc` clears
  `match_winner` in `HandleDisconnect`'s `kEndedMatch` branch
  (`host_network.cpp:225`), with
  `FirstMatchWonCaptureHostTest.AMatchEveryPlayerLeftEndsAsADrawWhoeverWonTheMatchBefore`;
  CI green at identical code (`ee6abcbe`).
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: fixed. Re-read (V1):
  `host_network.cpp:259` still clears `match_winner`.

### ER-2

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `PumpPeers`, `AdmissionDeadlines` and `IsMisbehaviour`; magnitude not
  measured.
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: recurring. Re-read (V1) in
  `src/modules/host/peer_gate.cpp` and `misbehaviour.h`: accept and routine
  rejections unchanged; decoding now outside the Host's mutex, handling still
  under it.
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: recurring. Re-read (V1):
  accept at `peer_gate.cpp:50-58`, handling under the Host mutex at `:75-83`,
  unbounded drain now in `networking/server.cpp:249-281`. New symptoms: an
  unthrottled INFO `join_refused` line per refused request
  (`host_network.cpp:56,193`; `replay_server.cpp:247`), and a full replay server
  reading a whole capture before refusing (`replay_server.cpp:288` before
  `:294`). ER-18 amplifies it.
- 2026-10-26 550cfe88 triage: filed as #1333 (`ready-for-human`, milestone
  Codebase Review 2).

### ER-3

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `MakeSandbox` against Lua 5.5.1's base library; not demonstrated.
- 2026-10-15 129f9a81 triage: filed as #661 (`ready-for-agent`).
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: fixed. `warn` taken out and
  a counting allocator at `kMemoryLimit` (`lua_sandbox.cpp:36-67`), both tested
  in `PolicyTest`; #661 closed. A residual by another mechanism is ER-6.
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: fixed. Re-read (V1): `warn`
  still removed, allocator limit at `lua_sandbox.cpp:61-83`.

### ER-4

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Observed (V2) in
  nightly runs 38379309305 and 38533136972; `falcor` absent from
  `scripts/sbom-cpes.py`, present in release `v3.1.0`.
- 2026-10-15 129f9a81 triage: filed as #660 (`ready-for-agent`).
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: not-reproducible.
  `sbom-scan (augustac-windows-x64)` green on the nightlies of 2026-10-18 and
  2026-10-19, the latest release (`v3.3.0`) shipping no Falcor; #660 closed as
  not planned. The script's rule is unchanged, so a port removed before the next
  release brings it back.
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: not-reproducible. The rule
  is unchanged (only `ozz-animation` added to `_NO_CPE`); all three `sbom-scan`
  jobs green in nightly 40290941153; no port removed since `909d1aa1`.

### ER-5

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `protocol.cpp`'s `ReadList` against the header's contract.
- 2026-10-15 129f9a81 triage: filed as #662 (`ready-for-agent`).
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: fixed. `ReadList` stops at
  the first read error and reserves nothing (`protocol.cpp:343-355`); #662
  closed.
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: fixed. Re-read (V1) after
  the protocol split: `ReadList` reserves nothing past the bound
  (`decoder.cpp:174-185`). Protobuf's own parse before it is ER-18.

### ER-6

Former fingerprints: none

- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: new. Lua 5.5.1's `GCTM`
  disables debug hooks during a finalizer (`lgc.c:980`); a looping `__gc`
  finalizer ran past 5 s under the sandbox's count hook in a C-API reproduction
  (V3); the engine path from `on_tick` traced (V1).
- 2026-10-19 909d1aa1 triage: filed as #898 (`ready-for-agent`).
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: fixed. `f5e8651b` (#900)
  wraps `setmetatable` and refuses a raw `__gc` field (`lua_sandbox.cpp:35-50`),
  matching `luaC_checkfinalizer` (V1); tested in `policy_test.cpp:337-433`; #898
  closed. Other routes out of the limits are ER-8.

### ER-7

Former fingerprints: none

- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: new.
  `std::filesystem::path::string()` threw `std::system_error` 1113 for a
  non-ANSI name on ACP 1252 (V3); traced (V1) from `Capturer::StartMatch` to
  `CaptureDirectory::Scan` on the unguarded writer thread.
- 2026-10-19 909d1aa1 triage: filed as #899 (`ready-for-agent`).
- 2026-10-26 550cfe88 2026-10-26-full-repository.md: fixed. `c68fc627` (#902)
  names capture and replay paths in UTF-8 and runs the writer under
  `failure::Guard` (`capture_writer.cpp:195-201`), tested with `u8"中文"`
  (`capture_retention_test.cpp:78`); #899 closed. The same mechanism at startup
  sites is ER-12.

### ER-8

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Three routes
  reproduced (V3) with the engine's `scripting.cpp` and `lua_sandbox.cpp`
  against Lua 5.5.1: a replaced sol2 default handler runs with hooks off, C
  library work (`table.move`, patterns) runs no VM instructions, `__close`
  handlers each get a fresh count.
- 2026-10-26 550cfe88 triage: filed as #1329 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-9

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. A 15-level shared
  table converted to 32.8 M values in 7.9 s and 1.3 GiB (V3, engine code).
- 2026-10-26 550cfe88 triage: filed as #1330 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-10

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Prediction's world
  holds only the local body (`prediction.cpp:116`); the server's controllers
  collide (`physics.cpp:385`); PhysX reproduction with the project's capsule
  (V3); reconciliation loop traced (V1).
- 2026-10-26 550cfe88 triage: filed as #1331 (`ready-for-human`, milestone
  Codebase Review 2).

### ER-11

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Workflow-level
  `packages: write` (`ci.yml:13-17`) seen in the `format` job's token log of run
  40357984952 (V2); cache overwrite not tried.
- 2026-10-26 550cfe88 triage: filed as #1332 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-12

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. `path::string()` threw
  on ACP 1252 and the unguarded config path ended in `std::terminate` (V3);
  startup sites traced (V1). Related to ER-7.
- 2026-10-26 550cfe88 triage: filed as #1334 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-13

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. In-place edit of a
  loaded, verified pack visible through the mapping (V3, older `_native` build);
  client lazy resolves traced (V1).
- 2026-10-26 550cfe88 triage: filed as #1335 (`ready-for-human`, milestone
  Codebase Review 2).

### ER-14

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. 2.5 m at 60 fps and
  4.0 m at 144 fps never snapped (V3, real `correction.cpp`).
- 2026-10-26 550cfe88 triage: filed as #1336 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-15

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Three runs gave three
  `pairs` orders over table keys (V3).
- 2026-10-26 550cfe88 triage: filed as #1337 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-16

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Reader admits 255
  players, encoder refuses more than 8 as an invariant failure (V1).
- 2026-10-26 550cfe88 triage: filed as #1338 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-17

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Default handler's
  traceback seen in the engine reproduction's output (V2).
- 2026-10-26 550cfe88 triage: covered by #1329's brief (an explicit hook handler
  returning the message alone); no issue of its own.

### ER-18

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. 512 KiB payload parsed
  into a 21 MB arena in 13.1 ms (V3, protoc 33.4 on the worktree's schema);
  `kMaxPayloadSize` enforced nowhere.
- 2026-10-26 550cfe88 triage: filed as #1339 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-19

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. clang 21.1.8 emits
  `fmadd` for aarch64 (V3); no `-ffp-contract` in the build.
- 2026-10-26 550cfe88 triage: filed as #1340 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-20

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Load-then-`Stop()` in
  `HandleShutdownSignal` against `runtime->reset()` (V1).
- 2026-10-26 550cfe88 triage: filed as #1341 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-21

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. About 71 ns per Joint
  per `At()` call (V3); benchmark keeps one Joint.
- 2026-10-26 550cfe88 triage: filed as #1342 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-22

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. `signing.pub`
  published beside `server.apak` by the same credential (V1).
- 2026-10-26 550cfe88 triage: filed as #1343 (`ready-for-human`, milestone
  Codebase Review 2).

### ER-23

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. `seen_tick` checked
  only against now − 1 and the cap (`combat.cpp:20-26`, V1).
- 2026-10-26 550cfe88 triage: filed as #1344 (`ready-for-human`, milestone
  Codebase Review 2).

### ER-24

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Replay skips
  `assign_spawns`, uses a fresh Lua state and renumbers Entity IDs (V1).
- 2026-10-26 550cfe88 triage: filed as #1345 (`ready-for-human`, milestone
  Codebase Review 2).

### ER-25

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. Capture mode and
  retention accepted without a capture directory (V1).
- 2026-10-26 550cfe88 triage: filed as #1346 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-26

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. No hardening flag in
  the build; test link shows no BIND_NOW or stack protector (V3).
- 2026-10-26 550cfe88 triage: filed as #1347 (`ready-for-agent`, milestone
  Codebase Review 2).

### ER-27

Former fingerprints: none

- 2026-10-26 550cfe88 2026-10-26-full-repository.md: new. `ResolveEverything`
  calls no animation resolver (V1).
- 2026-10-26 550cfe88 triage: filed as #1348 (`ready-for-agent`, milestone
  Codebase Review 2).
