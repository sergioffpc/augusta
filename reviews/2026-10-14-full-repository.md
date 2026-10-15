# Engineering review: full repository

## Metadata

| Field          | Value                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Date           | 2026-10-14                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Repository     | `https://github.com/sergioffpc/augusta.git` (checkout `C:/Users/sergi/src/github/sergioffpc/augusta`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| Branch, commit | `develop` `129f9a81ae1a9c62ec6c179d67b698cd9c202b0d`, working tree clean (re-checked before Phase 6)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| Scope          | Full: every area of the profile's subsystem map. Out: the k3s cluster itself, the published docs site (`tools/docs`), the Composer tool (`tools/composer`), visual and audible output (ADR-0013).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| Objectives     | Can the guarantees the profile lists be disproved at this commit: one owning thread per state, the supervisor's first-failure contract, typed tick-path errors, no I/O in ECS worlds, full validation of client input, a closed Lua sandbox, verify-before-use of packs, the protocol kept at the edge? Where is the codebase below its planned depth, and what remains unverified?                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| Depth plan     | Inbound wire, admission: Critical, L3 reached / L3 + L4 planned (L4 only as CI's fuzz, V2). Authoritative tick: Critical, L3 / L3. Threads and failure: Critical, L3 / L3 + L4 (L4 only as CI's TSan, V2). Packs and integrity: Critical, L3 / L3 + L4 (L4 only as CI's fuzz, V2). Lua sandbox and policy: Critical, L3 / L3. Prediction, replication: High, L3 / L3. Capture and replay: High, L2 plus the capture-to-replay flow at L3 / L2 + L4 (L4 only as CI's fuzz, V2). Pack writer: High, L2 / L2. Configuration: Medium, L2 / L2. Observability: Medium, L2 / L2. Supporting modules: Medium, L2 for `map`, `command`, `tick`, `primitives`, `math`, `lobby_readiness`, `frame_mapping`; L1 for `input`; not read: `animation`, `version`, `character_loader`, `scene_loader` / L2. Rendering and audio: Low, L1 / L1–L2. Agents and tools: Low, L1 (listing only) / L1. Build and deployment: Medium, L2 for presets, `Dockerfile`, CI permissions and pinning, the chart's deployment, network policy and values; not read: `chisel/`, `clusters/`, the chart's volumes, rules and dashboards / L2. |
| Tools          | `git` 2.x; `gh` (GitHub CLI) for CI and nightly run results; `python` 3 for `scripts/requirements-matrix.py --check`; the Read/Grep tools for source reading. Not run locally: `cmake`/`ctest` (builds stale, see Verification), `clang-tidy`, sanitizers, fuzzers.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| Environment    | Windows 11 Pro 10.0.26300, clang-cl toolchain and WSL Ubuntu present; no GPU used; network used only to read GitHub Actions results. Missing for this review: a Linux build current with the commit (the WSL build trees date from 2026-09-30), a Windows build current with the commit (dated 2026-10-13, before the `vcpkg.json` restructuring).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| Reviewer       | Claude Code (Claude Fable 5.1), running the `engineering-review` skill                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Prior review   | none                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |

## Executive summary

Every Critical-tier area was read at L3: the inbound wire path from
GameNetworkingSockets' callback through `protocol::Decode`, `PeerGate`, `Match`,
`CommandQueue` and `SimulationWorld`, the authoritative tick with its
lag-compensated hit detection, the supervisor and both runtimes' shutdown, pack
verification on both executables, and the Lua sandbox; the High-tier prediction,
replication and capture-to-replay flows were traced end to end as well. The
guarantees the profile lists held under reading: transport callbacks publish
events and nothing else, every tick-path error is a typed `std::expected`, the
protocol never leaves its two adapters, and no byte of a pack is trusted before
its hash and signature verify. Five findings are recorded, all Low: one
confirmed bug (a Match abandoned by its last player is captured with the
previous Match's winner), one broken nightly check (the vulnerability scan of
the released Windows client has failed since 2026-10-13 on a dependency the CPE
map dropped), two risks (the server bounds neither how many peers connect nor
how fast a connected peer may send what it routinely rejects; the Lua sandbox
leaves `warn` and unbounded allocation reachable), and one contract mismatch in
the protocol decoder's allocation bound. The main limitation is that no test,
sanitizer or fuzzer was run locally: the local build trees predate the commit,
so the review relies on CI's results for the nearest commits (full test suites
green at `1cd2d98c`, which differs from this commit in two CMake files; TSan,
fuzzing, netcode and coverage green on the nightly of `36caa979`, 71 source and
test files behind).

## Findings

### Critical

none

### High

none

### Medium

none

### Low

#### ER-1: A Match its last player abandons is captured with the previous Match's winner

| Field        | Value                                                                        |
| ------------ | ---------------------------------------------------------------------------- |
| Fingerprint  | `correctness/server-host/match_winner/stale-on-abandoned-match`              |
| Category     | BUG                                                                          |
| Severity     | Low (impact Medium: one capture record wrong; factors: narrow preconditions) |
| Confidence   | High                                                                         |
| Verification | V1                                                                           |
| Ledger       | new                                                                          |

**Location:** `src/server/host_impl.h:105-108` `Host::Impl::match_winner`;
`src/server/host_match.cpp:78-89` `Host::Impl::EndMatch`;
`src/server/host_network.cpp:199-231` `Host::Impl::HandleDisconnect`;
`src/server/host_match.cpp:143-155` `Host::Impl::TakeOutEndedMatch`;
`src/server/capture.cpp:613-628` `Capturer::EndMatch`.

**Path and conditions:** Network I/O thread: a peer's `kDisconnected` event →
`PumpPeers` → `HandleDisconnect` → `Match::Leave` returns
`Departure::kEndedMatch` (the last player left; `match.cpp:93-102` ends the
Match inside `Leave`) → `CountMatchEnded` and `LogMatchEnded`, and nothing
touches `match_winner`. Simulation thread, next `Tick` → `PrepareTick` →
`TakeOutEndedMatch` → `capturer->EndMatch(tick, match_winner)` → `Capturer` maps
the session through `sessions_` of the Match being closed. Needs a server with
`simulation.capture` set, in any build and platform.

**Evidence:**

- Observed: `match_winner` is written in exactly one place, `Impl::EndMatch`
  (`host_match.cpp:84`), which only the policy's Match end (`Act`) and
  `Host::EndMatch` reach (`host_match.cpp:92-96, 243-246`).
- Observed: the `kEndedMatch` branch of `HandleDisconnect`
  (`host_network.cpp:223-229`) counts and logs the end but does not reset
  `match_winner`.
- Observed: `TakeOutEndedMatch` hands `match_winner` to the capture
  (`host_match.cpp:151`), and `Capturer::EndMatch` names the winner only if its
  session is in the current Match's `sessions_` (`capture.cpp:617-622`).
- Derived: when Match N ended with winner W, and W's session plays in Match N +
  1, which ends because every player disconnected, the capture of N + 1 ends
  with `winner = W` instead of a Draw, from the three facts above.
- Observed: no test covers an abandoned Match's capture: `CaptureHostTest` has
  four cases, all of a Match played to its end (`tests/session_test.cpp`), and
  `tests/capture_runtime_test.cpp` tests only capture failure modes.

**Failure mechanism:** `Match::Leave` ends the Match on its own path, outside
`Impl::EndMatch`, so the Host's copy of the last winner is not refreshed; the
next tick closes the capture with that stale value, and the stale session maps
to a player number because the same session was in the abandoned Match too.

**Impact:** the capture's Match end record names a winner for a Match nobody
won. A Replay of that capture ends it as the capture says, with that winner sent
to the viewer (`replay.cpp` `EndOf`), and `ReplayCheck` reports a match-end
divergence against the live Match's own policy, which decided no winner:
NFR-09's comparison is then wrong for a reason that is not non-determinism.

**Preconditions and likelihood:** captures on; two consecutive Matches with a
returning winner; every player of the second disconnecting mid-Match (a crash, a
playtest cut short). Rare in play, routine in a playtest that is abandoned.

**Why it is not prevented:** `match_winner` has one writer and two Match-ending
paths; nothing ties the value to the Match it belongs to, and no test exercises
the abandoned-Match capture.

**Corrective direction:** reset `match_winner` in the `kEndedMatch` branch, or
better, have both ending paths record the Match's outcome in one place that
`TakeOutEndedMatch` consumes (an `std::optional` of the ended Match's winner,
set by `Impl::EndMatch` and by `HandleDisconnect`, cleared when taken), and add
the abandoned-Match case to `CaptureHostTest`.

**Verification / reproduction:** open. A test in `tests/session_test.cpp`: play
Match 1 to a win, start Match 2 with the same players, disconnect them all, read
Match 2's capture and expect its Match end to name no winner.

**Related:** none.

#### ER-4: The nightly vulnerability scan of the released Windows client has failed since 2026-10-13

| Field        | Value                                                                   |
| ------------ | ----------------------------------------------------------------------- |
| Fingerprint  | `build-deploy/ci/sbom-cpes/released-sbom-names-removed-port`            |
| Category     | BUG                                                                     |
| Severity     | Low (impact Medium: a shipped executable goes unscanned; factors: none) |
| Confidence   | High                                                                    |
| Verification | V2                                                                      |
| Ledger       | new                                                                     |

**Location:** `scripts/sbom-cpes.py:21-60` `_CPES`, `_NO_CPE`;
`.github/workflows/nightly-jobs.yml:451-515` `sbom-scan`.

**Path and conditions:** the nightly's `sbom-scan` job downloads the latest
release's SBOM (`v3.1.0`, 2026-10-12) and runs `scripts/sbom-cpes.py` from
`NIGHTLY_REF` (`develop`) on it; the script fails on any package in neither map.

**Evidence:**

- Observed: nightly runs 38379309305 (2026-10-13) and 38533136972 (2026-10-14)
  end with `sbom-scan (augustac-windows-x64)` failed and every other job green;
  the failed step prints
  `falcor: neither a CPE nor a reason for none - add it to _CPES or _NO_CPE in sbom-cpes.py`.
- Observed: `falcor` appears nowhere in `scripts/sbom-cpes.py`, `vcpkg.json` or
  `cmake/` at this commit (ADR-0059 replaced Falcor with NVRHI).
- Observed: the job scans the latest release's SBOM, not the night's build
  (`nightly-jobs.yml:452-456, 477-494`), and the latest release still ships
  `falcor` (release asset `falcor-759aad033ff6-ea98a9c6c206`).

**Failure mechanism:** the script's maps follow the current manifest, the SBOM
it is given follows the last release; a port removed between the two fails the
script before grype runs.

**Impact:** no CVE result for the shipped Windows client until the next release;
the nightly stays red, so a new failure in another job is one more red among
reds.

**Preconditions and likelihood:** every night until a release without Falcor
ships, or the map is amended.

**Why it is not prevented:** the script's "every package must be in a map" rule
is right for the night's own build; nothing keeps the map in step with what the
last release shipped.

**Corrective direction:** list `falcor` under `_NO_CPE` with the reason that the
latest release still ships it (ADR-0059), and remove it once a release without
it is out; or make the script tolerate a port that only the released SBOM names,
with a notice.

**Verification / reproduction:** the next nightly's
`sbom-scan (augustac-windows-x64)` job green.

**Related:** none.

#### ER-3: The Lua sandbox leaves `warn` and unbounded allocation reachable

| Field        | Value                                                                                                                            |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/scripting/MakeSandbox/warn-and-memory-unbounded`                                                                       |
| Category     | RISK                                                                                                                             |
| Severity     | Low (impact High: the server process can be killed; factors: content is signed and trusted, the liveness probe restarts the pod) |
| Confidence   | High                                                                                                                             |
| Verification | V1                                                                                                                               |
| Ledger       | new                                                                                                                              |

**Location:** `src/modules/scripting/sandbox/lua_sandbox.cpp:15-24`
`MakeSandbox`; `src/modules/scripting/scripting.cpp:24-35` the instruction
limits; `src/server/parameters_loader.cpp:343` the loader's own `warn`.

**Path and conditions:** a scenario's `rules.lua`, read from the verified server
pack (`policy_loader.cpp`), runs in a state `MakeSandbox` made, at load and on
every `on_tick` and `assign_spawns` call on the Simulation thread.

**Evidence:**

- Observed: `MakeSandbox` opens `base`, `math`, `string` and `table` and nils
  `dofile`, `loadfile`, `load`, `print`, `collectgarbage`, `pcall`, `xpcall`,
  `math.random` and `math.randomseed` (`lua_sandbox.cpp:17-23`); `warn` is not
  among them, and Lua is 5.5.1 (`lua.h` in the installed vcpkg tree), whose base
  library defines `warn` writing to `stderr` once `warn("@on")` has run.
- Observed: the Parameters loader overrides `warn` to route into its log sink
  (`parameters_loader.cpp:295-310, 343`); the rules' engine does not.
- Observed: the only bound on a hook is `LimitInstructions`
  (`scripting.cpp:28-29, 60, 221`); `string.rep`, `table.insert` in a loop and
  string concatenation allocate without a budget, and no `lua_setallocf` or
  `lua_gc` limit is set.
- Observed: `tests/policy_test.cpp:220-240` checks that `io`, `os`, `require`,
  `package` and `math.random` are refused; neither `warn` nor memory is checked.
- Hypothesis: `string.rep("x", 2^30)` inside `on_tick` allocates a gigabyte in a
  few instructions, under the limit; not demonstrated here.

**Failure mechanism:** a script writes to `stderr` outside ADR-0029's log
format, or allocates past the container's memory limit, which kills the server
(the chart limits memory at its request, `values.yaml`).

**Impact:** a scenario script, by mistake or on purpose, takes the server down
or pollutes its logs; ARCHITECTURE §11 names an incomplete sandbox as the risk
to "no I/O inside worlds".

**Preconditions and likelihood:** a script signed with the Environment's key, so
only the developer who cooks packs; a bug in such a script is the realistic
case.

**Why it is not prevented:** the sandbox is a curated allow-list of libraries
minus named functions; `warn` was added to Lua's base library after the list was
written for the functions ADR-0022 names, and the instruction limit bounds time,
not space.

**Corrective direction:** nil `warn` in `MakeSandbox` (the Parameters loader
reinstalls its own), and install a counting allocator through `lua_setallocf`
that refuses past a fixed budget, so an over-allocating hook fails the call as
the instruction limit does; add both to `PolicyTest`.

**Verification / reproduction:** open. Through `PolicyTest`'s fixture, load a
rules script whose `on_tick` calls `warn("@on") warn("leak")` and one that calls
`string.rep("x", 1 << 30)`; the first should fail to load or decide nothing, the
second should fail the call rather than allocate.

**Related:** none.

#### ER-5: `protocol::Decode` allocates by a list's cap, not by its input, after a truncation

| Field        | Value                                                                    |
| ------------ | ------------------------------------------------------------------------ |
| Fingerprint  | `networking/protocol/ReadList/allocates-past-truncation`                 |
| Category     | IMPROVEMENT                                                              |
| Severity     | Low (impact Low: tens of kilobytes per malformed payload; factors: none) |
| Confidence   | High                                                                     |
| Verification | V1                                                                       |
| Ledger       | new                                                                      |

**Location:** `src/modules/protocol/protocol.cpp:329-337` `ReadList`;
`src/modules/protocol/protocol.cpp:401-408` `ReadRifle`;
`src/modules/protocol/include/augusta/protocol.h:56-58` the decoder's contract.

**Path and conditions:** any payload reaching `Decode` (`Host::HandleMessage`,
`Inbox::Receive`) whose list count byte is at the cap and whose bytes end before
the elements do.

**Evidence:**

- Observed: `ReadList` reserves and pushes `count` elements whatever the
  reader's error state (`protocol.cpp:330-335`); `ReadRifle` stops its loop at
  the first error (`protocol.cpp:403`).
- Observed: the header promises Decode "never allocates more than the input
  itself holds" (`protocol.h:57-58`).
- Derived: the two-byte payload `0x0E 0xFF` (a Replay list of 255 listings)
  allocates 255 `ReplayListingWire` values, each with an empty vector and
  string, before `Finish` reports `kTruncated`: a few tens of kilobytes for two
  bytes, bounded by the cap and freed at once.

**Failure mechanism:** a short payload costs the receiver the cap's worth of
default-constructed elements; the amplification is bounded and small.

**Impact:** a contract the module states does not hold; the cost is negligible
at the current caps (255 listings, 8 players, 8 commands).

**Preconditions and likelihood:** any peer, any time; the misbehaviour threshold
disconnects a peer after 16 undecodable payloads in 5 s (`misbehaviour.h`).

**Why it is not prevented:** `ReadList` was written to read every field and ask
once at the end, which is right for fixed fields and wrong for a count that was
never backed by bytes.

**Corrective direction:** stop `ReadList`'s loop when `reader.Error()` is set,
as `ReadRifle` does, and drop the `reserve`; or amend the header to state the
real bound.

**Verification / reproduction:** a unit test in `tests/protocol_test.cpp`
decoding `{0x0E, 0xFF}` and asserting `kTruncated` is already possible; the
allocation itself would need an allocation-counting test or a corrected header.

**Related:** none.

#### ER-2: The server bounds neither how many peers connect nor how fast a connected peer sends what it routinely rejects

| Field        | Value                                                                                                                 |
| ------------ | --------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/server-host/PumpPeers/no-peer-or-rate-bound`                                                                |
| Category     | RISK                                                                                                                  |
| Severity     | Low (impact Medium: tick timing degraded, transport resources exhausted; factors: trusted LAN only, ARCHITECTURE §11) |
| Confidence   | Medium                                                                                                                |
| Verification | V1                                                                                                                    |
| Ledger       | new                                                                                                                   |

**Location:** `src/server/peer_gate.cpp:52-57` `PumpPeers` (accept);
`src/server/peer_gate.cpp:76-84` `PumpPeers` (messages under the lock);
`src/server/admission.h:23` `kAdmissionDeadline`;
`src/server/misbehaviour.h:48-62` `IsMisbehaviour`;
`src/server/host_match.cpp:160-161` `PrepareTick` (the same lock).

**Path and conditions:** any UDP peer on the LAN, against a live or replay
server, in any build.

**Evidence:**

- Observed: every `kConnectRequested` is accepted before anything is known of
  the peer (`peer_gate.cpp:52-56`); the only bound on an unadmitted peer is the
  30 s deadline (`admission.h:23`), and no count of connections is kept anywhere
  in `Host`, `PeerGate` or `networking::Server`.
- Observed: `kStaleCommand`, `kCommandsOutsideMatch`, `kStaleReady` and
  `kJoinRefused` never count toward a disconnect (`misbehaviour.h:48-62`), so a
  connected peer may send Commands messages with old sequences, or Join
  requests, at any rate forever.
- Observed: each received message is decoded and handled with the Host's `mutex`
  held (`peer_gate.cpp:78-84`, `Host::PumpNetwork`), the mutex `PrepareTick`
  takes every tick (`host_match.cpp:161`).
- Hypothesis: at tens of thousands of small messages a second from one peer the
  Network I/O thread holds the mutex most of the time and `PrepareTick` waits,
  so ticks start late and overrun (NFR-01); not measured.

**Failure mechanism:** exhaustion rather than malformed input: the misbehaviour
threshold stops what no honest client sends, but not many connections, nor many
messages an honest client could send.

**Impact:** a LAN peer can degrade the tick loop for everyone in the Match, or
hold thousands of GameNetworkingSockets connections open for 30 s each.

**Preconditions and likelihood:** a hostile or runaway process on the trusted
LAN; the accepted deployment assumption makes it unlikely today, and it stays
one bug in a client away.

**Why it is not prevented:** admission is by what the peer asks first, which
needs a connection; rate is bounded only for rejections that count as
misbehaviour; the Host's one mutex serialises the Network I/O thread's
per-message work with the tick.

**Corrective direction:** refuse `kConnectRequested` past a cap of open
connections (the Lobby's `player_count` plus a small slack), and count routine
rejections above a generous per-window rate as misbehaviour; longer term, hand
the Simulation thread its tick input through a queue so no received message is
handled under the tick's lock.

**Verification / reproduction:** open. A test under the `linux-netcode` preset
that floods a `Host` with stale Commands from one peer while another plays,
measuring late and overrun ticks (`HostMetrics`), would confirm or dismiss the
hypothesis.

**Related:** none.

## Architectural observations

- **Boundary enforcement is real, not conventional.**
  `tests/core_boundary.cmake`, `tests/protocol_boundary.cmake` and
  `tests/server_boundary.cmake` run under ctest and check what their comments
  say: no protocol type past `server/wire.h` and `harness_wire.h`, no harness
  type in presentation, the protocol linking only `math` and `primitives`, the
  server never naming client Input, and a configure-time fixture proving the
  client/server scope check fires both ways. Every adapter read
  (`server/wire.cpp`, `harness_wire.cpp`, `shared_wire.cpp`) is a field-by-field
  copy; `static_assert`s pin the stance enumerators and the hash size across the
  edge.
- **ADR-0005 holds as read.** GameNetworkingSockets' status callback publishes
  one `TransportEvent` and nothing else (`networking.cpp:381-382, 567-568`);
  `PumpEvents` applies it under the owner's lock and makes the transport call
  outside it. The client reads the server through an immutable `ServerView`
  published whole (`inbox.h:72-78`); `SimulationWorld` returns a `TickResult` by
  value; the server's shared state sits under one mutex with its threads named
  at each member (`host_impl.h`).
- **ADR-0033 holds as read.** Every tick-path error is a typed `std::expected`;
  the supervisor records the first cause, requests the stop before publishing
  it, and joins every worker before the runtime's resources go
  (`supervisor.cpp:97-116`, `runtime.cpp:184-201`); `FirstFailure` makes each
  failure reported once. Only two runtime `assert`s exist in `src/`, each behind
  a check made in every build.
- **Pack verification order differs from ADR-0031's wording.** `Pack::Load`
  parses the header before the hash and signature are checked
  (`assets.cpp:601-619`); every read is bounds-checked through `ByteReader` and
  only error codes depend on the unverified bytes, and the index is parsed after
  verification. Safe, but the ADR says the header is read only after verifying.
- **Decision and mechanism are separated as the coding standards ask.**
  `PeerTable`, `ApplyToClient`, `ClassifySend`, `Validate`, `ReplayCheck`,
  `PlanRetention`, `ReadTickAction`, `SeenTimeAt`, `AcceptRetryDelay` and
  `ResolveStamina` are pure and tested without their mechanism.
- **Observability writes never wait on reads.** Counters, gauges and histograms
  are lock-free atomics; the per-Session gauges go through a sequence lock
  (`connection_health.cpp:95-165`); the metrics endpoint is unsupervised so an
  HTTP failure cannot stop the tick.
- **Performance, labelled per the standard.** Opportunity: `Inbox::Publish`
  copies the whole `ServerView` per message (`inbox.h:72-78`), at most eight
  bodies, unmeasured. Opportunity: `World::Tick` returns the committed state by
  copy each tick (`simulation.cpp:834`), unmeasured. Risk: the Host's single
  mutex over network handling and tick input (ER-2) will breach NFR-01 at a
  message rate no honest client reaches.
- **Deployment hardening is layered.** Non-root chiselled image, user
  namespaces, read-only root, all capabilities dropped, a Localhost seccomp
  profile, a default-deny NetworkPolicy with egress empty, CI permissions scoped
  per job and every action pinned by commit SHA.

## Verification performed

| Check                                        | Command or method                                                                                                                       | Scope it reached                                                                                                                 | Result                                                                                                                     |
| -------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Identity                                     | `git rev-parse HEAD`, `git status --short`, before Phase 2 and again before Phase 6                                                     | the checkout                                                                                                                     | `129f9a81…`, clean, both times                                                                                             |
| Requirements traceability                    | `python scripts/requirements-matrix.py --check`                                                                                         | every `// Requirements:` marker in `tests/`                                                                                      | exit status 0                                                                                                              |
| Full test suites at the nearest built commit | `gh run view 38587660516` (CI on `develop@1cd2d98c`), then `git diff --stat 1cd2d98c HEAD -- src tests`                                 | `server-x64-linux` (ctest, Linux), `client-x64-windows` (ctest, Windows), `confinement`, `container-verify`                      | all green; this commit differs from it in `src/client/CMakeLists.txt` and `src/modules/renderer/CMakeLists.txt` only       |
| Sanitizers, fuzzing, netcode, coverage       | `gh run view 38533136972` (nightly on `develop@36caa979`, 2026-10-14 08:09), then `git diff --stat 36caa979 HEAD -- src tests`          | `tsan`, `fuzz (protocol_decode, capture_read, pack_load)`, `netcode`, `coverage`, `property-tests`, `benchmarks`, `sbom-scan` ×3 | all green but `sbom-scan (augustac-windows-x64)` (ER-4); 71 source and test files changed between that commit and this one |
| CI at this exact tree                        | `gh run view 38600100052` (PR of `a309b8b0`, same tree as `129f9a81`) and `38600615240` (`develop@129f9a81`, in progress)               | the change filter                                                                                                                | only `format` ran: the merged change was documentation, so the build jobs were skipped                                     |
| Static analysis                              | read `.github/workflows/ci.yml:309-314, 383-401` and `Makefile` (`tidy`)                                                                | `clang-tidy` with `.clang-tidy`'s full profile on `src/` in `server-x64-linux`; `misc-include-cleaner` on the client and tests   | runs in CI at every push; green at `1cd2d98c`; not run locally                                                             |
| Local build and tests                        | checked `build/x64-windows-debug` (built 2026-10-13 at `3091c159`, before `vcpkg.json` was restructured) and the WSL trees (2026-09-30) | none                                                                                                                             | not run: rebuilding would reinstall vcpkg ports, beyond a read-only review's budget                                        |
| Runtime asserts                              | `grep -rn 'assert(' src`                                                                                                                | `src/`                                                                                                                           | two: `capture_file.cpp:131` (guarded by `Capturer::Admit`), `replication.cpp:27` (state sorted by `World::Tick`)           |
| Lua version and sandbox surface              | `lua.h` in `build/x64-windows-debug/vcpkg_installed`; `lua_sandbox.cpp`; `tests/policy_test.cpp`                                        | the scripting sandbox                                                                                                            | Lua 5.5.1; `warn` not removed; memory unbounded (ER-3)                                                                     |
| Nightly SBOM failure cause                   | `gh run view --job 115749523588 --log-failed`; `grep -i falcor vcpkg.json scripts/sbom-cpes.py`; `gh release list`                      | the `sbom-scan` job                                                                                                              | `falcor` unmapped; latest release `v3.1.0` still ships it (ER-4)                                                           |
| Abandoned-Match capture test coverage        | `grep` of `tests/session_test.cpp` and `tests/capture_runtime_test.cpp`                                                                 | capture tests                                                                                                                    | no case covers a Match ended by its last player leaving (ER-1)                                                             |

## Not reviewed or not verified

- **L4 nowhere run by this review.** TSan, ASan/UBSan, the three fuzzers and the
  impaired-link netcode preset need Linux; no local Linux build is current with
  the commit. CI's results for `36caa979` and `1cd2d98c` stand in (V2,
  observed), with the drift stated above. A Linux build of this commit in the
  dev container, then `ctest --preset linux-tsan` and the three `linux-fuzz`
  targets, would close it.
- **No local ctest on Windows.** The Windows debug tree predates the
  `vcpkg.json` restructuring; CI's `client-x64-windows` at `1cd2d98c` stands in.
- **Rendering and audio** (Low tier): L1 only. Their threading primitives were
  grepped (`vulkan_device.cpp`'s validation mutex, `output_miniaudio.cpp`'s
  mutex, `gpu_crash_dumps.cpp`'s atomics); resource lifetimes and decision code
  were not read. A scoped L2 review of `renderer` and `audio` would close it.
- **Supporting modules** below L2: `input` (header and lock use only),
  `animation`, `version`, `character_loader`, `scene_loader` not read.
- **Agents and tools** (Low tier): `tools/agent`, `tools/composer` and
  `tools/docs` listed, not read. `tools/pack`'s `cook.py` (1103 lines),
  `scenario.py`, `reader.py`, `capture.py` and most of `publish.py` not read;
  the writer `pack.py`, `wire.py` and `keys.py` were read against the C++ format
  and match it constant for constant.
- **Build and deployment** below L2: `chisel/`, `clusters/` (Flux sources,
  Kyverno, node hardening), the chart's `volumes.yaml`, `prometheusrule.yaml`,
  the dashboards and `.grype.yaml` not read.
- **Documentation and ADR text** was read only where a guarantee or a flow
  needed it: CONTEXT.md, ARCHITECTURE §5, §8, §11, the NFRs, ADR-0031, the
  coding standards. Other ADRs were taken as the code cites them.
- **Out of scope:** the k3s cluster, the published docs site, visual and audible
  output.

## Open questions

- **ER-2's magnitude.** How many stale Commands messages a second from one peer
  make the Simulation thread's `PrepareTick` wait long enough for late or
  overrun ticks? The flood test described under ER-2 would answer it.
- **Messages queued from a connection this side closes.** `Server::Disconnect`
  and `PeerTable::Lose` close the connection, and `PumpPeers` drops a message
  from an expelled peer only for the rest of the round (`peer_gate.cpp:80-82`,
  `EndRound`). If GameNetworkingSockets delivers a message already queued on the
  poll group for a closed connection in a later round, `HandleMessage` judges it
  and `PeerGate::Judge` creates a `MisbehaviourTracker` for a peer `Left` has
  already forgotten, which nothing erases. Reading the library's
  `CloseConnection` path, or a test that sends after expulsion and counts
  `misbehaviour_`'s size, would settle whether the entry leaks.
- **`warn` in the container.** Whether a `warn` line from a rules script reaches
  the pod's logs as an unformatted line (stderr is captured), which would also
  break ADR-0029's one-line format for the log pipeline.

## Residual risks and assumptions

- The review assumes GameNetworkingSockets behaves as its headers document:
  callbacks only from `RunCallbacks`, `CloseConnection` ending delivery, and the
  `SendMessageToConnection` result codes `ClassifySend` maps.
- The review assumes flecs flushes deferred structural changes at each system's
  end, which `Kill` and `OnBallistics` rely on (`.write<>()` is declared for
  them).
- Conclusions about threads rest on the code's own thread annotations and on
  CI's TSan at `36caa979`; nothing here ran the code.
- Documented and accepted risks stand: no peer authentication on a trusted LAN
  (ARCHITECTURE §11), PhysX divergence corrected by reconciliation, a Reenact
  request's spawn taken on trust behind `simulation.reenactments`.
- The metrics endpoint listens on every interface without authentication
  (`metrics.cpp:183-190`); in the cluster the NetworkPolicy limits it to
  Prometheus, on a developer's Windows machine it is open to the LAN.
- Even with every finding fixed, a single Match server on one replica cuts a
  Match on every rollout (ADR-0026), and a crash in a thread other than the
  crashing one leaves no logged stack (NFR-10).

## Comparison with prior reviews

None: no ledger existed; it was created from this review.

## Recommended next actions

1. Fix ER-1 with its regression test in `CaptureHostTest`; hand off through
   `/triage`.
2. Amend `scripts/sbom-cpes.py` for ER-4 so the released SBOM scans again;
   `/triage`, one-line change.
3. Close the sandbox gaps of ER-3 (`warn`, an allocation budget) with tests in
   `PolicyTest`; `/triage`.
4. Measure ER-2 with a flood test under the `linux-netcode` preset; if the tick
   loop degrades, `/improve-codebase-architecture` for moving tick input off the
   Host mutex, and a connection cap through `/triage` either way.
5. Fix ER-5's loop guard or its header; `/triage`.
6. Run the L4 checks on a Linux build of this commit (`linux-tsan`,
   `linux-fuzz`, `linux-netcode`) to replace the CI stand-ins in this report.
7. A scoped review of `renderer` and `audio` at L2, and of `clusters/` and
   `chisel/` for the build-deploy dimension, to reach their planned depth.
