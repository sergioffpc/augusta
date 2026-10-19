# Engineering review: full repository

## Metadata

| Field          | Value                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Date           | 2026-10-19                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| Repository     | `https://github.com/sergioffpc/augusta.git`, reviewed in a dedicated worktree (`C:/Users/sergi/src/github/sergioffpc/augusta-wt/review`) detached at `origin/develop`, so parallel sessions switching the main checkout's branch could not move it                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| Branch, commit | `develop` `909d1aa1deec3ea1f6d7582d0989aff449acfb77`, working tree clean (re-checked before Phase 6)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Scope          | Full: every area of the profile's subsystem map, weighted towards the module boundaries the v4.1 milestone (Codebase Review) means to rework and towards the code new since the last review (549 files changed since `129f9a81`: the Link replacing the wire module, the capture, policy and window modules, the Host moved under `src/modules`, the pack tools' native module). Out: the k3s cluster itself, the published docs site, the Composer tool, visual and audible output (ADR-0013).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| Objectives     | Can the profile's guarantees be disproved at this commit (one owning thread per state, the supervisor's never-terminate contract, typed tick-path errors, full validation of client input, a closed and bounded Lua sandbox, verify-before-use of packs, the protocol kept at the edge)? Do the findings of 2026-10-14 still hold? Where do the module boundaries stand, as input to the v4.1 refactoring?                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| Depth plan     | Inbound wire, admission: Critical, L3 reached / L3 + L4 planned (L4 only as the nightly's fuzz, V2). Authoritative tick: Critical, L3 for `simulation`, `hitbox_history`, `host_match`, `policy/answers`; `gameplay` and `physics` not read / L3. Threads and failure: Critical, L3 for `server`, `supervisor`, `harness/runner`, `client`, the capture writer; `application` and `failure` not read / L3 + L4 (L4 only as the nightly's TSan, V2). Packs and integrity: Critical, L2 (`assets.cpp` load, verify, index) / L3 + L4 (fuzz, V2). Lua sandbox and policy: Critical, L3 + L4 (a reproduction of this review's, V3) / L3. Prediction, replication: High, L1 (`reconciliation.cpp` only) / L3. Capture and replay: High, L2 for the capture writer, retention and the replay catalog; the capture reader and `replay.cpp` not read / L2 + L4. Pack writer: High, L1 (the native module's byte handling) / L2. Configuration: Medium, L1 / L2. Observability: Medium, L1 / L2. Supporting modules: Medium, L1 (`math/grid.h`) / L2. Rendering and audio: Low, L1 (build files) / L1–L2. Agents and tools: Low, L1 (listing) / L1. Build and deployment: Medium, L1 (warning flags, the chart's probe and limits, the nightly's trigger) / L2. Architecture (cross-cutting): L2, every module's build file, README and boundary check read, include graph analysed by script. |
| Tools          | `git` 2.x; `gh` for CI and nightly results; the Read/Grep tools for source reading; `clang-cl` (LLVM, MSVC STL, through `scripts/vcenv.cmd`) for two throwaway programs in the session scratchpad, linked against the Lua 5.5.1 of `build/x64-windows-debug/vcpkg_installed`; `python -I` for a throwaway include-graph script. Not run locally: `cmake`/`ctest`, `clang-tidy`, sanitizers, fuzzers.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Environment    | Windows 11 Pro 10.0.26300 (ANSI code page 1252), clang-cl toolchain; no GPU used; network used only to read GitHub Actions results and issues. Missing: a local build of this commit on either platform.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| Reviewer       | Claude Code (Claude Opus 5.5), running the `engineering-review` skill                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Prior review   | `reviews/2026-10-14-full-repository.md` (`129f9a81`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |

## Executive summary

The inbound wire path (`protocol::Decode`, the Link's `Server::Receive`,
`PeerGate`, the Host's handlers, `CommandQueue`), SimulationWorld's tick with
its Lag compensation, both runtimes' threads under the supervisor, pack
verification and the Lua sandbox were read at L3. The new Link, capture and
policy modules were read along those paths, and every module's build file,
README and boundary check was read for the architecture. Of the five findings of
2026-10-14, three are fixed at this commit (ER-1, ER-3, ER-5), the nightly SBOM
failure no longer reproduces (ER-4), and the open-connection and message rate
risk stands (ER-2). Two new findings are recorded. One is a confirmed Medium
bug: a rules script escapes the instruction limit, and the memory limit, through
a `__gc` finalizer, which Lua 5.5.1 runs with its debug hooks off, so one line
of a signed scenario hangs the Simulation thread for good. This was reproduced
against the same Lua and the same sandbox recipe. The other is a Low bug on the
Windows server: a file name outside the ANSI code page in the capture directory
makes `std::filesystem::path::string()` throw on the unsupervised capture writer
thread, which terminates augustad. The main limitation is that nothing was built
or run locally against the engine: tests, TSan and fuzzing stand on CI's results
for `ee6abcbe` (identical code) and the nightly for `9d781716`, and several High
and Medium areas stay below their planned depth, listed under "Not reviewed".

## Findings

### Critical

none

### High

none

### Medium

#### ER-6: A rules script's `__gc` finalizer runs outside the instruction and memory limits, so it can hang the Simulation thread

| Field        | Value                                                                                                                                                               |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/scripting/MakeSandbox/gc-finalizer-escapes-limits`                                                                                                        |
| Category     | BUG                                                                                                                                                                 |
| Severity     | Medium (impact High: the tick loop stops for good; factors: only signed, trusted content reaches it. The liveness restart is not counted: it reloads the same pack) |
| Confidence   | High                                                                                                                                                                |
| Verification | V3 at the Lua level (the same Lua 5.5.1, the same sandbox recipe); the engine path from `on_tick` to `lua_pcall` traced (V1)                                        |
| Ledger       | new                                                                                                                                                                 |

**Location:** `src/modules/scripting/sandbox/lua_sandbox.cpp:62-72`
`MakeSandbox`; `src/modules/scripting/scripting.cpp:229-249` `Script::Call`;
`src/modules/simulation/simulation.cpp:580-584` `OnScriptsBehaviours`;
`src/modules/scenario/parameters_loader.cpp:340, 353` (the Parameters script's
sandbox); Lua 5.5.1 `lgc.c:968-993` `GCTM`, `lgc.c:1669-1674` (`GCScallfin`),
`lgc.c:1525-1535` `luaC_freeallobjects`, `lbaselib.c:139-148`
`luaB_setmetatable`.

**Path and conditions:** the Simulation thread, each tick:
`SimulationWorld::Tick` → `OnScriptsBehaviours` → `policy::Policy::OnTick` →
`scripting::Script::Call` → the hook, run by sol2's `protected_function` under
`LimitInstructions(kHookInstructionLimit)`. A table the hook (or the script's
top level) made with a `__gc` metamethod is finalized by whichever allocation
next runs a GC step in that state: inside the hook, or in the engine's own work
around it (building the view, reading the answer), or at `lua_close` when the
Policy is destroyed. This needs a server pack whose `rules.lua`, or Parameters
script, does it, in any build, on any platform.

**Evidence:**

- Observed: `MakeSandbox` opens `base` and takes out `dofile`, `loadfile`,
  `load`, `print`, `warn`, `collectgarbage`, `pcall` and `xpcall`
  (`lua_sandbox.cpp:64-67`). `setmetatable` stays, and Lua's `luaB_setmetatable`
  accepts any table as a metatable (`lbaselib.c:139-148`).
- Observed: the only bound on a hook's time is the count hook
  `LimitInstructions` arms (`lua_sandbox.cpp:74-76`, `scripting.cpp:243`).
- Observed: Lua 5.5.1's `GCTM` sets `L->allowhook = 0` ("stop debug hooks during
  GC metamethod") before it calls a finalizer, and restores it only once the
  finalizer returns (`lgc.c:977-986`). Finalizers run from a GC step
  (`lgc.c:1669-1674`) and, for all that are pending, from `lua_close`
  (`lgc.c:1525-1535`).
- Observed: `Script::Call` lifts the memory limit for everything but the hook's
  own run (`scripting.cpp:233, 246-249`). A finalizer a GC step runs during the
  engine's view-building or answer-reading therefore runs unlimited too.
- Observed (V3): `gc_repro.cpp`, built with clang-cl against
  `build/x64-windows-debug/vcpkg_installed`'s `lua.lib` (5.5.1), mirrors
  `MakeSandbox` and `Script::Call` with the C API and calls `on_tick` under a
  100 000-instruction count hook. `function on_tick() while true do end end`
  returns `status=2 (instruction limit exceeded)`. The `on_tick` below is still
  running after 5 s, and the program exits on its watchdog (exit code 2):

    ```lua
    function on_tick()
      setmetatable({}, {__gc = function() while true do end end})
      for i = 1, 20000 do local t = {i} end
    end
    ```

- Observed: `/livez` fails once no tick has ended for 5 s
  (`src/server/liveness.h:17`), the chart probes it (`deployment.yaml:84-88`),
  and the restarted pod loads the same pack.
- Derived: from the facts above, a rules script that makes such a table in
  `on_tick` or `assign_spawns` stops the Simulation thread inside
  `SimulationWorld::Tick` for good. No Tick returns, so the Match freezes for
  every player, `Stop()` never takes effect (the loop never reaches its check),
  and after the liveness restart the first Match hangs again. The same script at
  its top level hangs the load, and one whose finalizer only runs at `lua_close`
  hangs shutdown.
- Hypothesis: a finalizer that allocates (`string.rep('x', 1 << 30)`), run in
  `Script::Call`'s unlimited window, takes the state past `kMemoryLimit` up to
  the container's 512 MiB (`values.yaml:126-131`). Not demonstrated.
- Observed: no test of `tests/policy_test.cpp` makes a table with a `__gc`
  metamethod; `grep` finds `setmetatable` only in `scripting.cpp`'s own freeze
  function.

**Failure mechanism:** the sandbox bounds a script by a debug hook and by an
allocator flag that only the hook's own run holds. Lua runs finalizers with
debug hooks disabled, at GC steps the script cannot see and the engine cannot
schedule. So code the script places in `__gc` runs on the Simulation thread with
no instruction bound, and in part of the call with no memory bound.

**Impact:** a scenario script, by mistake (a finalizer that loops on a bad
condition) or on purpose, freezes every Match the server runs and keeps it
restarting. ADR-0022's and ARCHITECTURE §8's "an instruction limit per hook call
and a memory limit per Lua state" does not hold, nor does §11's "Audit the
exposed API surface". The risk §11 names is an incomplete sandbox.

**Preconditions and likelihood:** a script signed with the Environment's key, so
only the developer who cooks packs. A deliberate `__gc` loop is not a realistic
accident, but a finalizer is an ordinary Lua tool that a rules author can reach
for without knowing what it bypasses.

**Why it is not prevented:** ER-3's fix took `warn` out and added the memory
budget, and both bounds are tested (`PolicyTest`'s hoarding and top-level
allocation cases). Neither bound applies to finalizers: the hook is suspended by
Lua itself, and the budget is lifted for the engine's work, the very window a
finalizer can run in. `pcall` is taken out so a script cannot catch the limit's
error, but `setmetatable` gives it a callback the limit never reaches.

**Corrective direction:** keep `__gc` out of reach. In `MakeSandbox`, replace
`setmetatable` with a wrapper that refuses a metatable whose raw `__gc` field is
set: Lua marks an object for finalization only if `__gc` is present when the
metatable is set. Scripting's freeze function keeps its own captured copy
(`scripting.cpp:38-49`), so it is unaffected. The alternative, taking
`setmetatable` out altogether, also removes ordinary uses from rules. Either way
it lands in `MakeSandbox`, so the Parameters script gets it too. Add
`PolicyTest` cases for a looping finalizer made in `on_tick`, at the top level,
and at `lua_close` (the Policy's destruction), each with a watchdog, so the test
fails rather than hangs.

**Verification / reproduction:** done at the Lua level as above; the program is
in the session scratchpad and is not kept in the repository. The engine-level
confirmation is the first `PolicyTest` case of the corrective direction, which
at this commit is expected to hang.

**Related:** ER-3 (fixed): this is a residual of the same sandbox boundary, by
another mechanism.

### Low

#### ER-7: On Windows, a file name outside the ANSI code page makes the capture writer throw, which terminates augustad

| Field        | Value                                                                                                                                  |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `errors/host/CaptureDirectory/narrow-path-conversion-throws`                                                                           |
| Category     | BUG                                                                                                                                    |
| Severity     | Low (impact High: the server process aborts; factors: Windows is a development platform only (NFR-04); an unusual file name is needed) |
| Confidence   | High                                                                                                                                   |
| Verification | V1 (every hop traced); the throwing call demonstrated in isolation (V3)                                                                |
| Ledger       | new                                                                                                                                    |

**Location:** `src/modules/host/capture_retention.cpp:89-93`
`CaptureDirectory::Open`; `src/modules/host/capture_retention.cpp:118-123`
`CaptureDirectory::Scan`; `src/modules/host/capturer.cpp:175` `Capturer::Writer`
(its thread); `src/modules/host/capturer.cpp:290-322` `Writer::Run`;
`src/modules/host/capturer.cpp:341-352` `Writer::OpenFile`;
`src/modules/host/replay_catalog.cpp:77, 107` `ReplayCatalog::List`, `Find`.

**Path and conditions:** a Windows augustad with `simulation.capture` set.
Simulation thread: `PrepareTick` → `StartMatchIfReady` → `Capturer::StartMatch`
(`host_match.cpp:133`) → `Writer::Open` queues the file (`capturer.cpp:538`).
Writer thread: `Run` → `Handle` → `OpenFile` → `CaptureDirectory::Open` →
`Scan`, which calls `entry->path().filename().string()` on every directory entry
(`capture_retention.cpp:123`) before filtering by name. `OpenFile` then logs
`path_.string()`, the configured directory's path (`capturer.cpp:352`).

**Evidence:**

- Observed (V3): `path_repro.cpp`, built with clang-cl and the MSVC STL on this
  machine (ACP 1252), calls
  `std::filesystem::path(L"match-中文.acap").string()`. It throws
  `std::system_error`: "No mapping for the Unicode character exists in the
  target multi-byte code page." (code 1113).
- Observed: the writer is a plain `std::thread` (`capturer.cpp:175`) whose body
  `Run` catches nothing (`capturer.cpp:290-322`; no `try` in the file). The
  metrics thread, the other unsupervised thread, wraps its body in
  `failure::Guard` (`src/server/metrics.cpp:313-315`).
- Observed: ADR-0005, Consequences: "A worker failure is reported, never a
  `std::terminate`." ADR-0005 names the capture writer as the server's fourth
  thread.
- Observed: `Scan` runs on every Match start, whether or not retention is on
  (`capture_retention.cpp:93`).
- Derived: one file in the capture directory whose name has a character the ANSI
  code page lacks, or a capture directory whose own path has one (a profile
  folder named in CJK script on a Western-locale machine), throws at the first
  Match start. The exception leaves the writer's thread function, and the
  process calls `std::terminate`: no first cause, no terminal event, the Match
  lost.
- Observed: the replay catalog makes the same call on every `.acap` file
  (`replay_catalog.cpp:77, 107`) on the Network I/O thread, unlocked, for a
  Replay list or Replay request (`replay_server.cpp:259, 280`). There the
  supervisor catches it as `worker_failed`, so a peer asking for the list stops
  a replay server that holds such a file.
- Observed: the assets module already knows the trap: `OpenValidatedMapping`
  opens the pack through `path.c_str()` because "the narrow overload takes the
  system ANSI codepage there" (`assets.cpp:406-410`).

**Failure mechanism:** on Windows, `path::string()` converts the native wide
name to the ANSI code page and throws when it cannot. The capture code converts
names it does not choose (any file in the directory, the configured directory
itself) on a thread with no exception boundary.

**Impact:** a Windows development server aborts at its first Match start, and a
Windows replay server stops on a peer's list request, both because of a file
name. Neither happens on Linux, where `string()` returns the native bytes.

**Preconditions and likelihood:** Windows, captures on, and a non-ANSI name in
or above the capture directory. Rare on this developer's machine, routine on a
machine whose user folder is named in another script.

**Why it is not prevented:** each `filesystem` call here takes an `error_code`,
but the conversion to `std::string` is not a filesystem call and has no
non-throwing overload. The writer thread was given no `Guard`, unlike the
metrics thread.

**Corrective direction:** keep paths as paths: compare file names as
`std::filesystem::path` (or their `u8string()`), log them through `u8string()`,
and keep a capture's name for the Replay list in UTF-8. Separately, give the
writer thread the metrics thread's boundary: run `Run` under `failure::Guard`
and treat an escaped exception as the capture's loss (`CaptureHealth::kStopped`
for a strict capture), so no future exception there is a `std::terminate`. Add a
`capture_retention_test` case with a non-ANSI file in the directory, run on
Windows (CI's `client-x64-windows` job runs the Windows tests).

**Verification / reproduction:** the throwing call is demonstrated as above. The
end-to-end case is the test of the corrective direction, which at this commit is
expected to terminate the test process on Windows.

**Related:** none.

#### ER-2: The server bounds neither how many peers connect nor how fast a connected peer sends what it routinely rejects

| Field        | Value                                                                                                                 |
| ------------ | --------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/server-host/PumpPeers/no-peer-or-rate-bound`                                                                |
| Category     | RISK                                                                                                                  |
| Severity     | Low (impact Medium: tick timing degraded, transport resources exhausted; factors: trusted LAN only, ARCHITECTURE §11) |
| Confidence   | Medium                                                                                                                |
| Verification | V1                                                                                                                    |
| Ledger       | recurring (first seen 2026-10-14, `129f9a81`)                                                                         |

**Location:** `src/modules/host/peer_gate.cpp:50-58` `PumpPeers` (accept);
`src/modules/host/peer_gate.cpp:75-83` `PumpPeers` (each message under the
lock); `src/modules/host/include/augusta/misbehaviour.h:46-61` `IsMisbehaviour`;
`src/modules/host/include/augusta/admission.h:23` `kAdmissionDeadline`;
`src/modules/host/host_match.cpp:161-162` `PrepareTick`;
`src/modules/networking/networking.cpp:720-746` `Server::ReceiveMessages`.

**Path and conditions:** any UDP peer on the LAN, against a live or replay
server, in any build.

**Evidence:**

- Observed: every `kConnectRequested` is still accepted before anything is known
  of the peer (`peer_gate.cpp:50-58`). Nothing in `PeerGate`, `Host`,
  `link::Server` or `networking::Server` counts connections, and the only bound
  on an unadmitted one is the 30 s deadline.
- Observed: `kStaleCommand`, `kCommandsOutsideMatch`, `kStaleReady` and
  `kJoinRefused` still never count toward a disconnect (`misbehaviour.h:54-58`).
- Observed, changed since 2026-10-14: payloads are now decoded by the Link's
  `Server::Receive` outside the Host's mutex (`link.cpp:254-265`), but each
  decoded message is still handled with the mutex held (`peer_gate.cpp:77-82`),
  the mutex `PrepareTick` takes every tick (`host_match.cpp:162`). Each
  `Receive` drains the poll group without a per-round cap
  (`networking.cpp:723-741`).
- Hypothesis: as before, a flood of stale Commands from one peer keeps the mutex
  busy enough to make `PrepareTick` wait; not measured.

**Failure mechanism:** exhaustion rather than malformed input, as on 2026-10-14.

**Impact:** a LAN peer can degrade the tick loop for everyone in the Match, or
hold many connections open for 30 s each.

**Preconditions and likelihood:** a hostile or runaway process on the trusted
LAN.

**Why it is not prevented:** unchanged: admission needs a connection, and rate
is bounded only for rejections that count as misbehaviour.

**Corrective direction:** unchanged from the 2026-10-14 report: a cap on open
connections at `kConnectRequested`, a generous per-window rate on routine
rejections, and in the longer term tick input handed over through a queue rather
than under the tick's lock (see Architectural observations, the Host's one
mutex).

**Verification / reproduction:** open, as before: a flood test under
`linux-netcode` measuring `HostMetrics`' late and overrun ticks.

**Related:** none.

## Architectural observations

These are not findings: none has a demonstrated defect. They are where the
boundaries stand at this commit, as input to v4.1 and to
`/improve-codebase-architecture`.

- **The boundary checks are real, and they check the link graph, not the include
  graph.** `checks/core_boundary.cmake` and `checks/protocol_boundary.cmake`
  were read whole. Each proves itself on a planted violation where it says it
  does, and no source outside `protocol` and `link` names `protocol::` or
  includes `augusta/protocol.h` (`grep`, this commit). But most modules give all
  their targets one include directory (`host`: 7 targets, `config` and
  `scenario` 3, `window`, `audio` and `capture` 2), so a target can include a
  sibling's header that it does not link. Linking is what
  `augusta_set_module_scope`, the Glue check and the core boundary's link scans
  see. Two targets use this on purpose: `augusta_client_content` and
  `augusta_client_frame_mapping` add `src/modules/renderer/include` to their
  include path without linking `augusta_renderer`, because `renderer.h` holds
  both the plain scene types they need and the GPU-backed `Renderer` class
  (`src/client/CMakeLists.txt:1-9, 15-34`). A per-target header directory, or a
  types-only target for `renderer.h`'s data, would make the link graph and the
  include graph the same.
- **Declared link edges drift from use.** A throwaway script compared each
  target's `#include`s with its `target_link_libraries`. It is a heuristic: it
  reads a target's sources and same-named headers only. It reports edges
  declared but unused, such as `augusta_server_liveness` and
  `augusta_server_metrics`, which link `augusta_gameplay` for `augusta/tick.h`
  alone, and `augusta_host_match`, which links `augusta_physics` without
  including it. It also reports headers used only through someone else's PUBLIC
  edge: `augusta_host` uses `augusta/policy.h` and `augusta/tick.h`, and
  `augusta_host_replay` some fourteen headers through `augusta_host`. Each
  over-declared PUBLIC edge widens what every dependent links, PhysX included.
- **The Host is the largest module, and its README needs the most "and"s.** 6.2k
  lines, 7 targets. It holds the Lobby and Match bookkeeping, the peer gate,
  command intake, two mapping layers (into the Link's types and into
  SimulationWorld's), metrics and heartbeat counts, the capture writer with its
  retention, and Replay with its catalog and server. Its own build file says
  that splitting out the Replay server "is a later decision"
  (`src/modules/host/CMakeLists.txt`). Capture writing and Replay are the seams
  already present: each depends on the Host only through `PeerGate`,
  `HostMetrics`, `host_log` and `Match`'s admission terms.
- **The Host's one mutex serialises network handling with the tick's input.** It
  guards the Lobby, the Match, every player's queue and the peer gate
  (`host_impl.h:97-118`), and both threads take it per message or per tick. This
  is ER-2's mechanism, and the place a queue handing tick input to the
  Simulation thread would change most.
- **The client and the server treat the Link's vocabulary differently.** The
  harness takes the Link's types as its own through `using` aliases
  (`harness.h:51-67`). The Host defines its own `SessionId`, `EntityId`,
  `JoinRefusal`, `MatchStart` and `MatchEnd` (`match.h`) and maps them
  (`link_mapping.cpp`). `simulation`, `policy` and `presentation` each define
  `EntityId` or `SessionId` again. About 1 250 lines of conversion code sit
  across `link_messages.cpp`, `capture_records.cpp`, `link_mapping.cpp`,
  `simulation_mapping.cpp`, `frame_mapping.cpp` and `server_view.cpp`. The enum
  mappings are exhaustive `switch`es under `-Wall -Werror` and `/W4 /WX`
  (`CMakeLists.txt:83-94`), so drift breaks the build, but a new refusal reason
  touches three enums and three switches. Choosing one rule for both sides is a
  v4.1 decision.
- **Capture and the Link depend on each other at module level.** The capture
  module owns the records' types (`augusta_capture_records`) and links the
  Link's conversions. The Link module implements the records' bytes, magic and
  version (`link/capture_records.cpp`) over those types. There is no target
  cycle, and the capture README states the split, but a change to the capture
  format is always a change to both modules.
- **ClientRuntime runs three modes through one loop.** A live player, a Captured
  player's reenactment and a Replay viewer are told apart by
  `reenactment.has_value()` and `config.replay.has_value()` in `NextCommand`,
  `CurrentAim`, `CollectOutcome`, `NextFrameInput`, `LoadCharacters` and `Run`
  (`src/client/client.cpp`). The thread boundaries hold: `Reenactment` keeps its
  cross-thread state in atomics, and the Runtime keeps what it shares behind
  mutexes, each documented. But a fourth mode would add a branch to each of
  these.
- **One error is a string.** `link::PeerMessage::type` is
  `std::expected<MessageType, std::string>` (`link.h:245`), the protocol's
  `DecodeError` turned into words so that no protocol type crosses the Link. The
  coding standards ask that errors be types.
- **ADR-0005 and ADR-0033 hold as read.** Transport callbacks publish one event
  and nothing else (`networking.cpp:385, 573`). The supervisor requests the stop
  before it publishes the first cause (`supervisor.cpp:85-109`). Both runtimes
  join their workers before what they use goes (`server.cpp`'s member order,
  `runner.h:89-90`, `client.cpp:467-469`). The capture writer is the one thread
  without an exception boundary (ER-7).
- **Divergences between docs and code.** ARCHITECTURE §8 says the server runs
  three threads, while ADR-0005 counts the capture writer as a fourth. The
  gameplay README's "Encoding any of it: protocol and wire" names the wire
  module the Link replaced. The profile's subsystem map does not list `policy`,
  `hud` or `window`.

## Verification performed

| Check                                    | Command or method                                                                                                                         | Scope it reached                                                                         | Result                                                                                                                                                                |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Identity                                 | `git rev-parse HEAD`, `git status --short` in the detached review worktree, before Phase 2 and before Phase 6                             | the worktree                                                                             | `909d1aa1…`, clean, both times                                                                                                                                        |
| Full test suites at identical code       | `gh run view 39256223171` (CI, `develop@ee6abcbe`), then `git diff --name-only ee6abcbe HEAD`                                             | `server-x64-linux`, `client-x64-windows`, `confinement`, `container-verify`              | all green; the commits differ in `docs/REQUIREMENTS.md` and `docs/ROADMAP.md` only                                                                                    |
| CI at this commit                        | `gh run view 39261395763` (`develop@909d1aa1`)                                                                                            | the change filter                                                                        | build, sanitizer and fuzz jobs skipped (documentation change)                                                                                                         |
| Sanitizers, fuzzing, netcode, coverage   | `gh run view 39221399679` (nightly 2026-10-19, which checks out `develop`, then `9d781716`); `git diff --stat 9d781716 HEAD -- src tests` | `tsan`, `fuzz` ×3, `netcode`, `coverage`, `property-tests`, `benchmarks`, `sbom-scan` ×3 | all green but `client-x64-linux`: 5 `WindowTest` cases with LeakSanitizer leaks (GLFW on X11), which #887 addressed, merged after that nightly; 67 files differ since |
| Lua finalizers and the count hook        | read `lgc.c`, `lbaselib.c` of the vcpkg Lua 5.5.1 build tree; `gc_repro.cpp` (scratchpad), clang-cl, run                                  | the sandbox's instruction bound                                                          | the hook stops a loop in the hook; a loop in a `__gc` finalizer runs past 5 s (ER-6)                                                                                  |
| MSVC STL narrow path conversion          | `path_repro.cpp` (scratchpad), clang-cl, run                                                                                              | `std::filesystem::path::string()` on ACP 1252                                            | throws `std::system_error` 1113 for `L"match-中文.acap"` (ER-7)                                                                                                       |
| Protocol and networking outside the Link | `grep` for `protocol::`, `augusta/protocol.h`, `augusta/link_messages.h` and `augusta_(protocol\|link_messages)` link edges               | `src`, `tools`, `tests` sources and listfiles                                            | only `protocol`, `link`, `capture` (its conversions, as allowed) and tests                                                                                            |
| Include graph against link graph         | a throwaway `depgraph.py` (scratchpad), `python -I`                                                                                       | every `augusta_*` library target under `src/`                                            | the over- and under-declared edges in Architectural observations; a heuristic                                                                                         |
| Prior findings' issues                   | `gh issue view 659 660 661 662`                                                                                                           | ER-1, ER-3, ER-4, ER-5                                                                   | all closed; #660 as not planned, "resolved by releasing v3.2.0 without Falcor"                                                                                        |
| GameNetworkingSockets' close contract    | read `isteamnetworkingsockets.h` in the vcpkg tree                                                                                        | `CloseConnection`                                                                        | "Any unread data on the connection is discarded" (closes an open question of 2026-10-14)                                                                              |
| Warning flags                            | read `CMakeLists.txt:83-94`, `.clang-tidy`                                                                                                | every C++ target                                                                         | `-Wall -Wextra -Werror`, `/W4 /WX`; clang-tidy `WarningsAsErrors: "*"`                                                                                                |
| Local build and tests                    | none                                                                                                                                      | none                                                                                     | not run: no local build tree is current with the commit, and a build is beyond this review's budget                                                                   |

## Not reviewed or not verified

- **L4 not run locally.** TSan, ASan/UBSan, the fuzzers and the impaired-link
  preset need Linux. CI's results for `ee6abcbe` and the nightly's for
  `9d781716` stand in (V2), with the drift stated above.
- **Engine-level confirmation of ER-6 and ER-7.** Both were demonstrated at the
  library level and traced through the engine. The `PolicyTest` and
  `capture_retention_test` cases of their corrective directions would confirm
  each end to end.
- **Authoritative tick below plan:** `gameplay` (`ballistics.cpp`, `weapon.cpp`,
  `parameters.cpp`) and `physics` (`physics.cpp`, `map.cpp`) were not read this
  time. `replication.cpp` was not read. A scoped L3 review of the Shot flow
  (critical flow 2) would close it.
- **Prediction, replication (High): L1 only.** `prediction.cpp`, the client's
  reconciliation path and `presentation` were not read. The 2026-10-14 review
  traced them at L3 at `129f9a81`.
- **Capture and replay (High):** the capture reader (`capture/capture.cpp`) and
  `host/replay.cpp` were not read. The capture reader is fuzzed nightly
  (`capture_read`).
- **Packs (Critical): L2.** `decoder.cpp`, `client_content` and the scenario
  loaders' content checks were not read. `pack_load` is fuzzed nightly.
- **Threads and failure:** `application.cpp` and the `failure` module (renamed
  `FailureLatch` in #889) were not read.
- **Pack writer, configuration, observability, supporting modules:** L1 or less.
  Read only where a finding needed them.
- **Rendering and audio, agents and tools, build and deployment:** L1. Build
  files, the chart's probe and resources, and the nightly's trigger were read.
  `chisel/`, `clusters/`, CI permissions and pinning were not re-read since
  2026-10-14.
- **Out of scope:** the k3s cluster, the published docs site, the Composer tool,
  visual and audible output.

## Open questions

- **ER-6's memory half.** Whether a finalizer that allocates during
  `Script::Call`'s unlimited window takes the state past `kMemoryLimit`. The
  `__gc` `PolicyTest` cases, with `string.rep` in place of the loop, would show
  it.
- **ER-2's magnitude.** Unchanged: the flood test under `linux-netcode`.
- **The include-graph heuristic.** `depgraph.py` reads a target's sources and
  same-named headers only, so its "uses transitively only" list may miss headers
  reached through other private headers. A `clang-scan-deps` or
  `include-what-you-use` pass would replace it.

## Residual risks and assumptions

- Lua 5.5.1's behaviour as its source reads, and as the reproduction ran it, is
  assumed to be what the vcpkg port builds on Linux. The repro ran the Windows
  build.
- Packs are memory-mapped once and read from the mapping after verification
  (`assets.cpp:643-678`). The review assumes no process writes a pack file in
  place while it is mapped. The cluster mounts packs read-only, and
  `augusta-publish` writes a new folder per version.
- GameNetworkingSockets is assumed to behave as its header documents: callbacks
  only from `RunCallbacks`, and unread data discarded on `CloseConnection`.
- The trusted-LAN assumption, and the accepted risks of ARCHITECTURE §11, stand
  as on 2026-10-14.
- Even with every finding fixed, a scenario script remains code the server runs.
  The sandbox is only as closed as the base library's surface, which grows with
  each Lua release (`warn` in 5.4, now finalizers' hook exemption).

## Comparison with prior reviews

| ID   | Fingerprint                                                     | Previous status | Status now       | Evidence                                                                                                                                                                                                                                                                                            |
| ---- | --------------------------------------------------------------- | --------------- | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ER-1 | `correctness/server-host/match_winner/stale-on-abandoned-match` | new             | fixed            | `96e1b2bc`: the `kEndedMatch` branch clears `match_winner` (`host_network.cpp:225`); `FirstMatchWonCaptureHostTest.AMatchEveryPlayerLeftEndsAsADrawWhoeverWonTheMatchBefore` (`session_test.cpp:2043`); #659 closed                                                                                 |
| ER-2 | `security/server-host/PumpPeers/no-peer-or-rate-bound`          | new             | recurring        | accept, `IsMisbehaviour` and per-message locking unchanged in mechanism; decoding moved outside the lock (`link.cpp:254-265`)                                                                                                                                                                       |
| ER-3 | `security/scripting/MakeSandbox/warn-and-memory-unbounded`      | new             | fixed            | `warn` taken out (`lua_sandbox.cpp:65`), a counting allocator bounds each state at `kMemoryLimit` (`lua_sandbox.cpp:36-58`), both tested (`policy_test.cpp:247-300`); #661 closed. Residual: ER-6                                                                                                   |
| ER-4 | `build-deploy/ci/sbom-cpes/released-sbom-names-removed-port`    | new             | not-reproducible | `sbom-scan (augustac-windows-x64)` green on the nightlies of 2026-10-18 and 2026-10-19; the latest release, `v3.3.0`, ships no Falcor. `sbom-cpes.py` still fails on a port that only the released SBOM names, so the next port removed before a release brings it back; #660 closed as not planned |
| ER-5 | `networking/protocol/ReadList/allocates-past-truncation`        | new             | fixed            | `ReadList` stops at the first read error and reserves nothing (`protocol.cpp:343-355`); #662 closed                                                                                                                                                                                                 |
| ER-6 | `security/scripting/MakeSandbox/gc-finalizer-escapes-limits`    | none            | new              | this report                                                                                                                                                                                                                                                                                         |
| ER-7 | `errors/host/CaptureDirectory/narrow-path-conversion-throws`    | none            | new              | this report                                                                                                                                                                                                                                                                                         |

## Recommended next actions

1. Close ER-6: refuse `__gc` metatables in `MakeSandbox`, with the watchdogged
   `PolicyTest` cases. Through `/triage`, into the v4.1 milestone or a hotfix:
   it breaks a guarantee ADR-0022 states.
2. Fix ER-7: keep capture and replay file names as paths or UTF-8, and put the
   capture writer's body under `failure::Guard`. Through `/triage`, with a
   Windows test.
3. Decide ER-4's latent mechanism: map a port removed since the last release in
   `sbom-cpes.py`'s `_NO_CPE`, or document that a port removal waits for a
   release. Through `/triage` if wanted.
4. For v4.1's boundaries, run `/improve-codebase-architecture` on the
   Architectural observations: the Host's capture and Replay seams, one rule for
   the Link's vocabulary on both sides, per-target include directories (or a
   types-only renderer target), and the over-declared link edges. Then
   `/grill-with-docs` the chosen moves into ADR amendments.
5. Measure ER-2 with the flood test, as recommended on 2026-10-14. Moving tick
   input off the Host's mutex fits the v4.1 work on the Host.
6. A scoped review of the Shot flow (`gameplay`, `physics`, `replication`) and
   of the client's prediction and presentation, to bring the Authoritative tick
   and the High areas back to their planned depth.
7. Amend ARCHITECTURE §8's thread count, the gameplay README's "wire", and the
   profile's subsystem map (`policy`, `hud`, `window`).
