# Engineering review: full repository

## Metadata

| Field          | Value                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Date           | 2026-10-26                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| Repository     | `https://github.com/sergioffpc/augusta.git`, reviewed in a dedicated worktree (`augusta-wt/review`) detached at the commit below, so parallel sessions switching the main checkout's branch could not move it                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| Branch, commit | `develop` `550cfe8839dddc0071272dac7a62b4c6deddf226`, working tree clean (re-checked before Phase 6)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Scope          | Full: every area of the profile's subsystem map, weighted towards the code new since the last review (91 merges and 444 files under `src/` and `tools/` since `909d1aa1`: the protobuf wire, the macOS client and platform split, skeletal hitboxes and the Clips' Hitbox pose, the Muzzle origin, the `.apak` pack and the pack tools' native bootstrap, and the splits of `protocol`, `networking`, `assets`, `config`, `host_metrics`, `capturer`, `physics`, `simulation` and `client` into several files). Out: the k3s cluster itself, the published docs site, the Composer tool, the renderer and audio beyond their threads, visual and audible output (ADR-0013).                                                                                                                                                                                                                                          |
| Objectives     | Can the profile's guarantees be disproved at this commit? Do the findings of 2026-10-19 still hold, and do their fixes close the mechanism or only its site? Does the new wire hold its bounds? Does prediction agree with the server (first L3 pass of that area)?                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Depth plan     | Inbound wire, admission: Critical, L3 + L4 (V3 parse measurement; fuzz as CI's, V2) / L3 + L4. Authoritative tick: Critical, L3 / L3. Threads and failure: Critical, L3 (TSan as CI's, V2; client not under TSan) / L3 + L4. Packs and integrity: Critical, L3 (V3 OS-level mapping test) / L3 + L4. Lua sandbox and policy: Critical, L3 + L4 (engine-level reproductions, V3) / L3. Prediction, replication: High, L3 (V3 reproductions) / L3. Capture and replay: High, L2 + L3 on flow 7 / L2 + L4 (fuzz not run locally). Pack writer: High, L2 / L2. Configuration: Medium, L2 / L2. Observability: Medium, L2 / L2. Supporting modules: Medium, L2 / L2. Rendering and audio: Low, L1 (threads only) / L1–L2. Agents and tools: Low, L1 (`tools/agent` dispatch guard only) / L1. Build and deployment: Medium, L2 / L2. Architecture (cross-cutting): L2, target link graph by script, boundary checks read. |
| Tools          | `git`; `gh` (read-only) for CI, nightly and issue state; the Read/Grep tools; `clang-cl` (LLVM 21, MSVC STL, through `scripts/vcenv.cmd`) and `clang++` 21.1.8 for throwaway programs in the session scratchpad, linked against `build/x64-windows/vcpkg_installed` (Lua 5.5.1, sol2, PhysX, protobuf 33.4, glm); `protoc` 33.4 on the worktree's `augusta.proto`; `py -3.12 -I` for throwaway scripts against a previously built `augusta_pack._native`. Not run locally: `cmake`/`ctest`, `clang-tidy`, sanitizers, fuzzers, the netcode preset.                                                                                                                                                                                                                                                                                                                                                                   |
| Environment    | Windows 11 Pro 10.0.26300 (ANSI code page 1252); no GPU used; network used only for GitHub reads. Missing: a local build of this commit on either platform; Linux for TSan, ASan and fuzzing; an arm64 host.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Reviewer       | Claude Code (Claude Opus 5.5), running the `engineering-review` skill, with seven read-only area reviewers whose candidates were re-checked before being written here                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| Prior review   | `reviews/2026-10-19-full-repository.md` (`909d1aa1`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |

## Executive summary

Every Critical area was read at L3 and both High runtime areas at L3 or L2, with
targeted reproductions in the scratchpad where reading could not settle a
mechanism. Prediction was traced end to end for the first time. Of the seven
earlier entries, ER-6 and ER-7 are fixed at their sites, ER-2 stands with new
symptoms, and ER-4 still does not reproduce. Twenty new findings are recorded,
none Critical or High. Four are Medium:

- **ER-8:** a rules script can escape the per-hook instruction limit by three
  routes (a replaced sol2 message handler, C library calls, `__close` handlers),
  all reproduced against the engine's own sandbox code.
- **ER-9:** reading a hook's answer copies a table with shared sub-tables once
  per path, outside both limits.
- **ER-10:** the client's prediction world has no other players in it, so a
  player pushing into another is corrected every tick.
- **ER-11 (a risk):** every CI job's token can write GHCR, including the build
  cache that the signed server image comes from.

One root cause turned up in three areas independently: `path::string()` in log
and failure text still stops the Windows client from starting under a non-ANSI
folder, and on a config error ends in `std::terminate` (ER-12). The ER-7 fix
added a private helper at each capture site but left this mechanism in place
elsewhere.

The main limitation: nothing was built or run locally against the engine.
Sanitizers, fuzzing and TSan stand on CI runs, and the nightly's TSan, long-fuzz
and netcode results predate the file splits merged on the reviewed day.

## Findings

### Critical

none

### High

none

### Medium

#### ER-8: A rules script escapes the per-hook instruction limit by three routes, so it can stall or hang the Simulation thread

| Field        | Value                                                                                                                                   |
| ------------ | --------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/scripting/LimitInstructions/hook-work-escapes-instruction-limit`                                                              |
| Category     | BUG                                                                                                                                     |
| Severity     | Medium (impact High: the tick loop stops or a Match becomes unplayable; factors: only signed, trusted pack content reaches it)          |
| Confidence   | High                                                                                                                                    |
| Verification | V3 at engine level (the worktree's `scripting.cpp` and `lua_sandbox.cpp` linked against vcpkg's Lua 5.5.1 and sol2); flow 5 traced (V1) |
| Ledger       | new                                                                                                                                     |

**Location:** `src/modules/scripting/sandbox/lua_sandbox.cpp:87-101`
`MakeSandbox`, `:103-105` `LimitInstructions`;
`src/modules/scripting/scripting.cpp:229-250` `Script::Call` (`:245`
`function.as<sol::protected_function>()`); sol2 `state.hpp:38,47`
`set_default_state`, `protected_handler.hpp:80-104`; Lua 5.5.1 `ldo.c:459`
(`luaD_hook` clears `allowhook`), `ldebug.c:947-949` (`resethookcount`),
`ldo.c:1061-1094` `luaD_closeprotected`, `ltablib.c:128-150` `tmove`,
`lstrlib.c` (the pattern matcher).

**Path and conditions:** the Simulation thread, each tick:
`SimulationWorld::Impl::OnScriptsBehaviours` (`simulation.cpp:554`) →
`PolicyBridge::OnTick` (`policy_bridge.cpp:53`) → `Policy::OnTick`
(`policy.cpp:117`) → `Script::Call` → the hook under
`LimitInstructions(kHookInstructionLimit)`. The same applies to `assign_spawns`,
to the rules' and Parameters' top level at load, and to each Replay's
`PolicyMaker`. Any build, any platform; needs a server pack whose script does
it.

**Evidence:**

- Observed: `MakeSandbox` clears `dofile`, `loadfile`, `load`, `print`, `warn`,
  `collectgarbage`, `pcall`, `xpcall`, `math.random`, `math.randomseed` and
  wraps `setmetatable` against `__gc`. It leaves the global under which sol2's
  `set_default_state` stores the default message handler
  (`"sol.\xF0\x9F\x94\xA9"`), and opens `base`, `math`, `string` and `table`
  whole (`lua_sandbox.cpp:87-99`).
- Observed: `Script::Call` builds its `protected_function` on every call, after
  the script's top level has run (`scripting.cpp:245`). The constructor reads
  the default handler from that global.
- Observed: the limit is a `LUA_MASKCOUNT` hook (`lua_sandbox.cpp:104`). Lua
  calls a hook with `allowhook = 0` (`ldo.c:459`), so the message handler that
  `luaL_error` in the hook triggers runs with no hook at all. The count is reset
  each time it trips (`ldebug.c:947-949`), and unwinding keeps calling pending
  `__close` handlers after each error (`ldo.c:1061-1076`).
- Observed (V3, route 1, replaced handler): with these rules,

    ```lua
    _G['sol.\240\159\148\169'] = function(m)
      local i = 0
      while i < 300000000 do i = i + 1 end
      return m
    end
    function on_tick() while true do end end
    ```

    `engine_repro.exe handler` printed

    ```text
    call 0: 5.495 s, the hook failed: handler ran 300000000 iterations after:
    instruction limit exceeded
    ```

    (5.591 s on call 1). With a handler of `while true do end`, a plain-Lua
    reproduction was still running at 30 s when `timeout` killed it (exit 124).
    The baseline hook takes 0.001 s.

- Observed (V3, route 2, C library work): `table.move({}, 1, 1e8, 1, {})` as the
  whole hook returned normally after 2.696 s, because the limit never fired.
  With `1e12` it was killed at 60 s.
  `string.find(("a"):rep(30), ("a*"):rep(9).."b")` took 6.496 s with the hook
  firing 0 times. `string.rep` and `gsub` stay bounded by the 32 MiB allocator,
  so they are not routes.
- Observed (V3, route 3, `__close`): 15 000 nested `local c <close>`, each
  `__close` looping, took 7.214 s per call through the engine code (7.091 s on
  call 1). The plain-Lua count showed the hook firing 15 002 times in one call.
- Derived: each route is unbounded in time. Route 1 is unbounded outright.
  Routes 2 and 3 are bounded only by the script's chosen sizes.

**Failure mechanism:** route 1 sets the global at load. Each `on_tick` then
takes it as its handler, runs to the limit, and the hook's `luaL_error` calls
the script's handler with hooks off, which never returns. Route 2 spends its
time inside one C call, which executes one VM instruction. Route 3 trips the
limit once, and every pending `__close` then gets a fresh 100 000-instruction
budget before tripping again.

**Impact:** route 1 hangs the Simulation thread for good, on the live server and
on the replay server. Routes 2 and 3 make each tick take seconds, so the Match
stalls and the liveness probe restarts the pod into the same pack.

**Preconditions and likelihood:** a signed server pack carrying such a script,
whether by mistake or by a hostile author. It takes no peer and no unusual
configuration.

**Why it is not prevented:** ER-6's fix refuses `__gc` only
(`lua_sandbox.cpp:35-50`). Removing `pcall`/`xpcall` does not reach the default
handler. The memory limit does not bound time. ADR-0022 reads the instruction
count as a time bound ("well under a millisecond"), but these routes break that
equivalence.

**Corrective direction:** give the hook's `protected_function` an explicit C++
handler (or none) and clear sol2's handler global in `MakeSandbox`. Refuse
`__close` as `__gc` is refused, or make the limit sticky: once tripped, re-raise
on every count until `Call` returns. Leave out or wrap `table.move` and the four
pattern functions, or cap pattern complexity. Amend ADR-0022 to say what the
count does and does not bound. The trade-off is fewer library functions for
rules authors.

**Verification / reproduction:** scratchpad `lua-capture/engine_repro.cpp` (the
engine's sources) and `repro.c` (plain Lua mirroring `MakeSandbox`), with
`cases.lua`, `handler.lua` and `handler_forever.lua`. Built with:

```text
scripts\vcenv.cmd clang-cl /std:c++latest /EHsc /O2 /MD
  /I<scripting includes> /I<vcpkg include>
  engine_repro.cpp scripting.cpp lua_sandbox.cpp /link lua.lib psapi.lib
```

Outputs are quoted above.

**Related:** ER-6 (same class: Lua code running outside the limits; its
mechanism is fixed). ER-17 shares route 1's fix (an explicit handler).

#### ER-9: Reading a hook's answer copies a table with shared sub-tables once per path, outside both limits

| Field        | Value                                                                                                                           |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/scripting/ToValue/shared-subtables-exponential-copy`                                                                  |
| Category     | BUG                                                                                                                             |
| Severity     | Medium (impact High: seconds per tick, or an allocation failure that stops the server; factors: only signed content reaches it) |
| Confidence   | High                                                                                                                            |
| Verification | V3 (engine code)                                                                                                                |
| Ledger       | new                                                                                                                             |

**Location:** `src/modules/scripting/scripting.cpp:133-196`
`TableToValue`/`ToValue`, `:32` `kMaxDepth`, `:231-233` (the memory limit lifted
while the answer is read).

**Path and conditions:** `Script::Call` → after the protected call returns,
`ToValue` converts the hook's result into a C++ `Value` tree on the Simulation
thread. Same paths and preconditions as ER-8.

**Evidence:**

- Observed: the only structural bound is `kMaxDepth = 16` (`scripting.cpp:32`,
  `:134`). It stops cycles, but not a directed acyclic graph that reuses one
  table at many positions.
- Observed: the `Value` tree is allocated by C++, outside the Lua allocator's
  budget, and the reads run under `MemoryLimitScope unlimited`
  (`scripting.cpp:231-233`).
- Observed (V3): rules
  `local t={} for i=1,N do t[i]=i end for d=1,15 do t={t,t} end return t` cost a
  few thousand instructions and kilobytes of Lua memory. Converting them took:

    | N    | Time    | Values     | Peak working set |
    | ---- | ------- | ---------- | ---------------- |
    | 10   | 0.140 s | 393 215    | 24 MiB           |
    | 100  | 0.893 s | 3 342 335  | 193 MiB          |
    | 1000 | 7.884 s | 32 833 535 | 1344 MiB         |

- Derived: `{t,t,t}` per level gives 3¹⁵ ≈ 1.4×10⁷ leaves before any element, so
  memory exhaustion is reachable.

**Failure mechanism:** the hook returns a small, self-sharing table. `ToValue`
expands every path into a fresh `Value`, so the work grows exponentially in
depth.

**Impact:** stalled ticks, or `std::bad_alloc` on the Simulation thread, which
the supervisor records as a worker failure and the server stops.

**Preconditions and likelihood:** as ER-8.

**Why it is not prevented:** neither limit applies. `ReadTickAction` validates
the answer only after it has been converted.

**Corrective direction:** a total node budget for one conversion (refuse with
`kNotPlainData` past it), or refuse a table already seen in this conversion. The
cost is one counter, or a set of seen tables per call.

**Verification / reproduction:** `engine_repro.exe dag N` in scratchpad
`lua-capture/`; output above.

**Related:** ER-8 (same entry point, different mechanism and fix).

#### ER-10: The client predicts its player walking through other players, whom the server's physics stops

| Field        | Value                                                                                                                               |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `simulation/prediction/World-Impl-physics/missing-remote-colliders`                                                                 |
| Category     | BUG                                                                                                                                 |
| Severity     | Medium (impact Medium: a bounded, recoverable desync against NFR-02 whenever two players touch; factors: none — contact is routine) |
| Confidence   | High                                                                                                                                |
| Verification | V3 for the PhysX behaviour with the project's capsule; the reconciliation loop traced (V1); in-game magnitude not measured          |
| Ledger       | new                                                                                                                                 |

**Location:** `src/modules/prediction/prediction.cpp:112-116` `Impl` (the
prediction `physics::World` holds one body, `local_body`);
`src/modules/simulation/simulation.cpp:615` `World::AddPlayer` (the server puts
every player in one world); `src/modules/physics/physics.cpp:385` `World::Step`
(a default `PxControllerFilters`);
`src/modules/presentation/presentation.cpp:95-97` (presentation's map world
holds no bodies).

**Path and conditions:** the second half of flow 1:
`Inbox::OnAuthoritativeState` → `prediction::World::Tick` → `OnReconciliation` →
`History::Acknowledge` / `BodyNeedsCorrection` (`prediction.cpp:174`) →
`physics::World::Restore` → `History::Replay` → presentation's `Correction`.
Needs two players in contact. Any build.

**Evidence:**

- Observed: the prediction world creates one body and nothing in `prediction`,
  `presentation` or `client` creates another (grep for `CreateBody`, `SetState`,
  `Restore`).
- Observed: `World::Step` passes a default `PxControllerFilters`
  (`physics.cpp:385`), with no `mCCTFilterCallback` anywhere in `physics`. PhysX
  documents that a null `mCCTFilterCallback` makes each controller collide with
  all others (`PxController.h:263-288`).
- Observed (V3): one controller with the project's capsule (radius 0.3, height
  1.5, step 0.3) walked at 3 m/s for 1 s reached `x=3.000` alone, and `x=0.800`
  with a second controller at `x=1.5`.
- Derived: while the local player pushes into another, the server blocks it and
  prediction does not. `BodyNeedsCorrection` fires on every acknowledgement.
  `Restore` puts the body at the blocked position, and the replay of the
  unacknowledged commands walks it through again, because the replay world still
  has no obstacle. The shown body sits about RTT × speed inside the other player
  (≈0.3 m at 100 ms and 3 m/s), and a correction is counted every tick.
- Observed: no ADR accepts players passing through each other. ARCHITECTURE §11
  accepts cross-platform PhysX divergence, not a rule difference on one
  platform.

**Failure mechanism:** two worlds with different colliders. Reconciliation
corrects towards the server, then the replay reproduces the difference, every
tick.

**Impact:** the local player is visibly inside other players, jittering along
the contact. `CorrectionTally` and the Network panel inflate, and harness
netcode numbers measure this instead of the network.

**Preconditions and likelihood:** two or more players in contact. That is
routine in any Match.

**Why it is not prevented:** ADR-0004 assumes the next acknowledgement corrects
what is left, but the replay recreates it. ADR-0024's "the client's one physics
World … Presentation sets every other pose" does not match the code (two worlds,
neither holding remote players).

**Corrective direction:** decide the rule. Either prediction's world carries
kinematic stand-ins for remote players at their interpolated or extrapolated
poses, which the replay sees, or the server filters controller-vs-controller
collisions (`mCCTFilterCallback`) so that players do not block each other.
Record the choice in ADR-0004 or ADR-0024. Stand-ins cost a body per remote
player and their own misprediction; filtering changes gameplay.

**Verification / reproduction:** scratchpad `prediction/cct.cpp`, built with
`clang-cl /std:c++20 /EHsc /MD /O2 /DNDEBUG -imsvc <vcpkg>/include/physx cct.cpp`
against PhysX's libraries. Output above. Confirm in the engine with a netcode
test of two Agents walking into each other, counting `CorrectionTally` per tick.

**Related:** none.

#### ER-11: Every CI job's token can write GHCR, including the build cache the signed server image is built from

| Field        | Value                                                                                                                                                                         |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/ci/workflow-permissions/registry-cache-writable-by-every-job`                                                                                                       |
| Category     | RISK                                                                                                                                                                          |
| Severity     | Medium (impact Critical: an attacker-built augustad carrying a valid signature; factors: needs a compromised package that a CI job resolves; most third-party code is pinned) |
| Confidence   | Medium                                                                                                                                                                        |
| Verification | V2 (job log shows the token's scopes)                                                                                                                                         |
| Ledger       | new                                                                                                                                                                           |

**Location:** `.github/workflows/ci.yml:13-17` (workflow-level
`packages: write`, `actions: write`); `ci.yml:115-136` `format` (no
`permissions:` of its own;
`uv run --no-project --with pyyaml==6.0.3 --with pytest==9.1.1 …`);
`.github/actions/build-server-image/action.yml:22,51,144-145` (`cache-from` /
`cache-to` a registry `buildcache` ref); `ci.yml:943-1033` (publish and sign);
the Kyverno policy `verify-image-signatures.yaml:65` (trusts
`ci.yml@refs/heads/(develop|main)`).

**Path and conditions:** a push to `develop`. Every job inherits
`packages: write`. The `format` job resolves pytest's transitive dependencies
unpinned at run time. `container-verify` builds from the registry cache, and the
publish job signs what was built.

**Evidence:**

- Observed: `permissions:` at workflow level grants `packages: write` and
  `actions: write` to every job (`ci.yml:13-17`).
- Observed: run 40357984952, job `format` (121695564496) logs "GITHUB_TOKEN
  Permissions: Actions: write, Contents: read, Packages: write".
- Observed: `persist-credentials` is never set, so checkout leaves the token in
  git's config for the job.
- Observed: pytest 9.1.1 is pinned, but its dependencies are resolved at run
  time.
- Hypothesis: a job token with `packages: write` can overwrite the
  `augustad:buildcache` tag, and BuildKit reuses a planted layer whose cache key
  matches. Not tried.

**Failure mechanism:** a compromised transitive PyPI package runs in `format`,
reads the token, and pushes a poisoned cache manifest. The next
`container-verify` build reuses the layer. The publish job signs the image, and
Kyverno admits it.

**Impact:** code of an attacker's choosing in the cluster's augustad, signed by
the identity admission trusts.

**Preconditions and likelihood:** an upstream package compromise, which is rare
but recurrent in the ecosystem; no action by the repository owner.

**Why it is not prevented:** signing proves which workflow built the image, not
what entered its cache. Actions are pinned by SHA, but the run-time Python
resolution is not, and no job narrows its token.

**Corrective direction:** workflow-level `contents: read`, with write scopes
only on the jobs that publish, prune or sign; `persist-credentials: false`;
hash-pinned Python dependencies (`uv` with a lock or `--require-hashes`); a
cache ref that only the publishing job writes. The cost is a few lines per job.

**Verification / reproduction:** open. Overwrite a `type=registry` cache tag
with a non-publishing job's token in a throwaway repository, then check whether
a later build reuses it.

**Related:** none.

### Low

#### ER-12: On Windows, `path::string()` in log and failure text stops augustac starting under a non-ANSI folder, and a config error there ends in `std::terminate`

| Field        | Value                                                                                                                |
| ------------ | -------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `errors/application/path-text/narrow-path-conversion-throws`                                                         |
| Category     | BUG                                                                                                                  |
| Severity     | Low (impact Medium: one install unusable, recoverable by moving it; factors: narrow precondition on the folder name) |
| Confidence   | High                                                                                                                 |
| Verification | V3 for the conversion and for the unguarded terminate; V1 for the traces                                             |
| Ledger       | new                                                                                                                  |

**Location:** `src/modules/config/config.cpp:292-294` `DescribeConfigError`
(`error.file.string()`); `src/client/main.cpp:57-61`; `src/server/main.cpp:69`;
`src/client/client_phases.cpp:28,105` `LoadClient`;
`src/client/client_content.cpp:34`; `src/modules/scenario/content.cpp:32`;
`src/modules/assets/pack_reader.cpp:544,546` `DescribeVerifiedPackError`;
`src/server/server_application.cpp:40,92`. Development only:
`src/modules/physics/recording.cpp:113`,
`src/client/debug_profiling_panel.cpp:43`.

**Path and conditions:** Windows, an executable directory (or a configured path)
with a character outside the active ANSI code page, for example a Cyrillic or
CJK folder on code page 1252, and the shipped `base_dir: .`.

**Evidence:**

- Observed: the config keeps paths wide on purpose ("so non-ASCII directories
  survive", `config.cpp:148-149`; `RequirePath` from UTF-8, `:189-192`), and the
  pack opens through `c_str()` (`pack_reader.cpp:235`), so verification succeeds
  on such a path.
- Observed: `LoadClient` logs `pack->Path().string()` at INFO on every
  successful start (`client_phases.cpp:105`), and INFO is compiled into
  non-Debug builds (`logging/CMakeLists.txt:14`).
- Observed: `DescribeConfigError` formats `error.file.string()`, and both mains
  call it from `transform_error` outside `application::Execute`'s guards
  (`client/main.cpp:57-61`, `server/main.cpp:69`).
- Observed (V3): MSVC's `path::string()` on a UTF-8-built `C:/Users/张三/…`
  threw
  `No mapping for the Unicode character exists in the target multi-byte code page.`
  on ACP 1252, where `u8string()` returned 37 bytes. A program mirroring `main`
  → `DescribeConfigError` with no try ended with `0xC0000409`
  (`std::terminate`).
- Observed: ER-7's fix (`c68fc627`) added a private `Utf8()` helper. The same
  helper is now copied in about a dozen files (`application.cpp`,
  `capture_writer.cpp`, `capture_retention.cpp`, `replay_catalog.cpp`,
  `replay_server.cpp`, `pcapng/file_writer.cpp`, `client_run.cpp`,
  `run_files.cpp`, `logging.cpp`, `host.cpp`, `vulkan_gpu_crash_dumps.cpp`),
  with no shared function and no check.

**Failure mechanism:** with a valid config, the pack verifies, and then the
`pack_verified` log line throws inside the construct phase.
`application::detail::Phase` reports `dependency_init_failed phase=construct`
with the code-page text, so every launch fails. With a missing or invalid
config, `DescribeConfigError` throws in `main`, outside any guard:
`std::terminate`, and a crash report instead of the config error, against
ADR-0033's "no unclassified exception leaves `main`". Any later content error is
misclassified the same way.

**Impact:** the Windows client cannot run from such a folder. A Windows augustad
(development only) is affected at `server_application.cpp:92`. Linux is
unaffected.

**Preconditions and likelihood:** a player whose user or install folder holds
characters outside their ANSI code page. Uncommon, but ordinary for some
locales.

**Why it is not prevented:** the rule (paths in text are UTF-8) lives in copies
of a private helper. Nothing flags `path::string()`, and CI runs from ASCII
paths.

**Corrective direction:** one shared path-to-UTF-8 text function for every path
written to a log, a failure context or a message, and a grep or clang-tidy check
banning `path::string()` in `src/`. Move the config read inside a guard. Add a
Windows CI startup test from a `u8"中文"` directory. A UTF-8 `activeCodePage`
manifest is an alternative with wider effects.

**Verification / reproduction:** scratchpad `threads/pack_path_repro.cpp`,
`threads/config_path_repro.cpp`, `build-obs/path_string.cpp`, each built with
`scripts\vcenv.cmd clang-cl /std:c++latest /EHsc /utf-8 <file>`. To confirm end
to end, run augustac from `C:\…\张三\` on ACP 1252.

**Related:** ER-7 (same mechanism; its sites are fixed).

#### ER-13: A verified pack stays memory-mapped and writable by others, and the client resolves characters from it after verification

| Field        | Value                                                                                                                                                                                                         |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/assets/Pack-Load/mapped-pack-read-after-verify`                                                                                                                                                     |
| Category     | BUG                                                                                                                                                                                                           |
| Severity     | Low (impact Medium: NFR-08's "no byte trusted before it verifies" fails for content resolved after startup; factors: needs local write access during a session, by an actor who already controls that client) |
| Confidence   | High                                                                                                                                                                                                          |
| Verification | V3 for the OS and Boost mechanism (an older `_native` build); the client's lazy resolves traced (V1)                                                                                                          |
| Ledger       | new                                                                                                                                                                                                           |

**Location:** `src/modules/assets/pack_reader.cpp:218-249`
`OpenValidatedMapping`, `:351-386` `Pack::Load`, `:174-176` `BlobBytes`;
`src/client/client_application.cpp:60-65,107` `ClientProcess::Parts`;
`src/client/client_content.cpp:111-131` `CharacterLoaderFor`;
`src/client/client.cpp:434-474` (Lobby and Replay loads).

**Path and conditions:** client: `LoadVerifiedPack` maps the file, hashes and
verifies it once, then keeps the `Pack` for the process. A Lobby roster or a
Replay's MatchStart later calls `load_character`, which reads blobs from the
mapping. The server resolves everything inside `ConstructRuntime` and mounts its
pack read-only, so it is out of scope for this finding.

**Evidence:**

- Observed: resolves read straight from the mapping (`BlobBytes`); only the
  index is copied out.
- Observed: Boost opens the file with
  `file_share_read | file_share_write | file_share_delete` on Windows
  (`win32_api.hpp:833-836`), and maps `MAP_SHARED` on POSIX
  (`mapped_region.hpp:675-678`).
- Observed: the Join check uses the hash computed at load
  (`client_application.cpp:94`), so it still passes after an edit.
- Observed (V3): a signed pack loaded through `augusta_pack._native`, then
  edited in place by another handle while loaded, showed the edit through a
  read-only view (`a read-only view now sees b'return { max_players 9 8 }'`).
  The still-loaded `Pack` kept reporting its original hash, and a fresh load
  failed with `content hash mismatch`. Truncation was refused on Windows. The
  build used reads format 4, not this commit's format 7. The mapping code and
  its flags are what the test exercises.

**Failure mechanism:** load and Join. Another process rewrites, say, a character
mesh in place. The next roster resolves the never-verified bytes. On Linux,
truncating instead raises SIGBUS on access.

**Impact:** unverified content reaches the renderer after a successful Join.
Decoders stay inside the blob span, so memory safety holds apart from SIGBUS.

**Preconditions and likelihood:** write access to the client's pack during a
session. In practice that is the player, who already controls the client.

**Why it is not prevented:** the 2026-10-19 report listed "no process writes a
pack in place while it is mapped" as an assumption. NFR-08's Source ("anyone who
can change a pack file on disk") does not grant it. Atomic writes protect only
against the project's own writer.

**Corrective direction:** resolve from bytes the process owns: read the file
into a private buffer once, hash that buffer, and serve from it (the cost is the
pack's size in memory). Alternatively, resolve every scenario character at
startup and drop the mapping, as the server does.

**Verification / reproduction:** scratchpad `packs/scripts/toctou.py`, run as
`py -3.12 -I scripts/toctou.py <mod> <data>`; output above. To confirm with this
commit, write a test in a throwaway worktree that rewrites a mesh blob in place
and calls `ResolveMesh`.

**Related:** ER-22 (NFR-08's other gap, in deployment).

#### ER-14: A reconciliation jump of 2 m or more is slid instead of shown at once, because BlendTicks splits it across frames

| Field        | Value                                                                             |
| ------------ | --------------------------------------------------------------------------------- |
| Fingerprint  | `simulation/presentation/Correction-Update/snap-threshold-sees-blended-increment` |
| Category     | BUG                                                                               |
| Severity     | Low (impact Low: cosmetic, large mispredictions are rare)                         |
| Confidence   | High                                                                              |
| Verification | V3 (the real `correction.cpp` driven with BlendTicks' lerp)                       |
| Ledger       | new                                                                               |

**Location:** `src/modules/presentation/local_view.cpp:50` `BlendTicks`;
`src/modules/presentation/correction.cpp:53-64` `Correction::Update`;
`src/modules/presentation/presentation.cpp:189-190`.

**Path and conditions:** a correction of at least 2 m arriving between two
rendered frames, at any frame rate above the tick rate.

**Evidence:**

- Observed: ADR-0004 says a jump of at least 2 m is shown at once.
- Observed: `BlendTicks` lerps `total_correction` by the frame's fraction, and
  `Correction::Update` snaps only when one frame's increment reaches
  `kSnapDistance`.
- Observed (V3): at 60 fps a 2.5 m jump arrived as −0.925 then −2.224 and never
  snapped. At 144 fps a 4.0 m jump never snapped, and the shown body was still
  1.3 m off two ticks later.
- Observed: `tests/correction_test.cpp` feeds `Correction` whole jumps.
  `tests/local_view_test.cpp:78-86` checks only an invariant of the blend.

**Failure mechanism:** the threshold is applied to the blended per-frame
increment, not to the jump the tick made.

**Impact:** the player slides across a large correction instead of seeing it at
once.

**Preconditions and likelihood:** a misprediction of 2 m or more. Rare: there is
no respawn within a Match.

**Why it is not prevented:** the unit tests exercise each piece apart.

**Corrective direction:** decide the snap on the tick's own jump (the latest
tick's total against the previous tick's) before blending.

**Verification / reproduction:** scratchpad `prediction/snap.cpp`, built with:

```text
clang-cl /std:c++latest /O2 -imsvc <vcpkg>/include /I<math include>
  /I<presentation> snap.cpp correction.cpp
```

**Related:** none.

#### ER-15: The sandbox exposes allocation addresses, so a rules decision can differ between a live Match and its Replay

| Field        | Value                                                                                                                        |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `simulation/scripting/MakeSandbox/address-dependent-values`                                                                  |
| Category     | BUG                                                                                                                          |
| Severity     | Low (impact Medium: NFR-09 divergence; factors: needs a script that relies on addresses or on `pairs` order over table keys) |
| Confidence   | High                                                                                                                         |
| Verification | V3                                                                                                                           |
| Ledger       | new                                                                                                                          |

**Location:** `src/modules/scripting/sandbox/lua_sandbox.cpp:89` (base and
string opened whole: `tostring`, `string.format("%p")`, `pairs`);
`src/modules/policy/policy_loader.cpp:49` (`*Policy::Load(text)` assumes the
second load succeeds).

**Path and conditions:** a rules script that prints or keys on table identity.
Live server and replay server.

**Evidence:**

- Observed (V3): three runs of the same script gave three different
  `tostring({})` and `%p` values, and three different `pairs` orders over 8
  table keys (`8,4,5,1,2,6,7,3` / `5,2,4,8,7,1,6,3` / `1,2,8,6,3,7,4,5`). sol2
  seeds string hashing with 0 (`state.hpp:45`), so string-key order is stable.
- Observed: ADR-0022 promises "same rules, same views, same decisions". ADR-0051
  promises each Death and the Match end come out as captured.
- Derived: `LoadPolicyMaker` dereferences a second `Policy::Load` unchecked. A
  load whose result depends on an address breaks that assumption, which is
  undefined behaviour on the replay server's Network I/O thread.

**Failure mechanism:** a decision made by iteration order over table keys, or on
an address, differs from run to run.

**Impact:** `replay_diverged`, or a Replay ending differently from the captured
Match.

**Preconditions and likelihood:** the shipped `last_man_standing.lua` does not
do this. A rules author would have to.

**Why it is not prevented:** randomness was removed (`math.random`), but
address-derived values were not.

**Corrective direction:** document it as an obligation of rules scripts in
ADR-0022, or override `tostring` and `%p`. In either case, check
`Policy::Load`'s result in `LoadPolicyMaker`.

**Verification / reproduction:** `repro.exe cases.lua addr`, run three times, in
scratchpad `lua-capture/`.

**Related:** ER-24 (another way a Replay's policy sees a different world).

#### ER-16: A capture with more Joins than the protocol carries passes the reader, then stops the replay server on the next listing

| Field        | Value                                                                                                                                                      |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `errors/host/ReplayCatalog/oversized-capture-breaks-encoding-invariant`                                                                                    |
| Category     | BUG                                                                                                                                                        |
| Severity     | Low (impact Medium: the replay server stops; factors: needs a foreign or hand-made file in the captures directory, which only trusted local writers reach) |
| Confidence   | High                                                                                                                                                       |
| Verification | V1                                                                                                                                                         |
| Ledger       | new                                                                                                                                                        |

**Location:** `src/modules/capture/capture.cpp:120-126` `RecordOrder` (admits
Joins up to player 255); `src/modules/host/replay_catalog.cpp:41-62`
`ListingOf`; `src/modules/link/link.cpp:350-365` `ListedOf`;
`src/modules/protocol/encoder.cpp:99` (`Fits(characters, kMaxPlayers)`);
`src/modules/link/link_messages.cpp:142-145` (`kInvariantViolated`);
`src/modules/host/replay_server.cpp:180-186,275-282`;
`src/server/server.cpp:142` `ServedResult`.

**Path and conditions:** the replay server lists or replays a capture with 9 or
more Joins.

**Evidence:**

- Observed: the reader accepts up to 255 players. The listing copies every
  character. The encoder refuses more than `kMaxPlayers` (8), and that refusal
  is recorded as an invariant failure, which `ServedResult` turns into a stop.
- Observed: the fuzz target exercises `ReadCapture` only.

**Failure mechanism:** a `ReplayList` request is answered by `List()` →
`Encode(listed)` → `kInvariantViolated` → the runtime stops. Replaying such a
capture fails the same way in `ViewerMessages`.

**Impact:** any LAN peer's list request takes the replay server down while the
file sits in its directory.

**Preconditions and likelihood:** the engine's own writer never produces such a
capture. It needs a hand-made or foreign file. Unverified: whether `kMaxPlayers`
was ever above 8 for an older capture format still readable.

**Why it is not prevented:** the reader's bound and the wire's bound differ, and
nothing between them reconciles the two.

**Corrective direction:** refuse a capture with more Joins than the protocol
carries in `RecordOrder` or `ListingOf`, so it is never listed.

**Verification / reproduction:** open. A test that writes a 9-Join capture into
a catalog and requests the listing.

**Related:** none.

#### ER-17: A failing policy hook's WARN line carries a multi-line, script-controlled traceback

| Field        | Value                                                                            |
| ------------ | -------------------------------------------------------------------------------- |
| Fingerprint  | `errors/scripting/Script-Call/hook-error-traceback-multiline-log`                |
| Category     | BUG                                                                              |
| Severity     | Low (impact Low: logfmt lines split and can be forged; factors: trusted content) |
| Confidence   | Medium                                                                           |
| Verification | V2 (engine repro output); log escaping read by grep only                         |
| Ledger       | new                                                                              |

**Location:** `src/modules/scripting/scripting.cpp:245` (sol2's default
handler); `src/modules/simulation/policy_bridge.cpp:37-45` `UndecidedLine`;
`src/modules/scripting/include/augusta/scripting.h:55`.

**Evidence:**

- Observed: the default handler appends `\nstack traceback:\n\t…` (the `plain`
  case of the engine reproduction).
- Observed: `UndecidedLine` puts the message inside `error="…"`, and no escaping
  was found in `src/modules/logging` (grep).
- Observed: `scripting.h:55` says the subject "is Lua's message".

**Failure mechanism:** a failing hook writes one WARN line that spans several
lines, with text chosen by the script.

**Impact:** ADR-0029's one-event-per-line logs break in Loki, and a pack can
forge log lines.

**Preconditions and likelihood:** any hook error.

**Why it is not prevented:** the handler is sol2's default (see ER-8).

**Corrective direction:** the explicit handler from ER-8, returning the message
only, escaped.

**Verification / reproduction:** `engine_repro.exe plain` in scratchpad
`lua-capture/`.

**Related:** ER-8 (shared fix).

#### ER-18: The server parses a whole inbound payload of up to 512 KiB into protobuf objects before any list bound applies

| Field        | Value                                                                                                                                                                                                                              |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `networking/protocol/DecodeEnvelope/parse-allocates-before-list-bound`                                                                                                                                                             |
| Category     | RISK                                                                                                                                                                                                                               |
| Severity     | Low (impact Medium: every player's commands arrive late while it lasts; factors: trusted LAN, ARCHITECTURE §11. The per-connection misbehaviour threshold is not counted as a mitigation, because reconnecting is free under ER-2) |
| Confidence   | High on the per-decode cost; Medium on the Match-level effect                                                                                                                                                                      |
| Verification | V3 (per-decode cost); Match-level effect not measured                                                                                                                                                                              |
| Ledger       | new                                                                                                                                                                                                                                |

**Location:** `src/modules/protocol/protocol.cpp:176-193` `DecodeEnvelope`
(`ParseFromArray`); `src/modules/protocol/decoder.cpp:174-185` `ReadList`,
`:281-288`; `src/modules/link/link.cpp:368-378` `ReadFrom`, `:415-426`
`Server::Receive`; `src/modules/networking/server.cpp:91-109` (listen socket
options); `src/modules/protocol/include/augusta/protocol.h:124`
`kMaxPayloadSize`.

**Path and conditions:** flow 1 on the Network I/O thread:
`Server::ReceiveMessages` → `Server::Receive` → `Decode` → `ReadList`. Any
build; live or replay server.

**Evidence:**

- Observed: protobuf builds every repeated element before `ReadList` checks
  `Fits`. ER-5's fix bounds only `ReadList`'s own vector.
- Observed: `kMaxPayloadSize` appears only at its definition and in one test
  (grep). No receive limit (`RecvMaxMessageSize`) is set, so GNS's documented
  512 KiB default applies.
- Observed: legitimate client messages are under about 150 bytes.
- Observed (V3): a 524 288-byte `Message{commands{262 142 × empty Command}}`
  parsed into a 21 229 680-byte arena (×40.5) in 13.1 ms per decode. A valid
  8-command message is 18 bytes and parses in under 1 µs. `ReadList` then
  refuses the large one.
- Derived: `ReadFrom` decodes server-only envelope types too, so every repeated
  field is a route.

**Failure mechanism:** a LAN peer sends reliable 512 KiB payloads of empty
elements. Each one costs about 13 ms on the Network I/O thread before it is
judged, and expulsion comes only after 16 such messages in 5 s. Then the peer
reconnects.

**Impact:** commands from every player queue late (held or idle ticks). Decoding
is outside the Host's mutex, so the tick itself is not blocked.

**Preconditions and likelihood:** an untrusted peer on the LAN.

**Why it is not prevented:** every check runs after the parse. There is no
transport cap below 512 KiB, and connections are not capped (ER-2).

**Corrective direction:** set `RecvMaxMessageSize` on the listen socket to a
client-message cap of a few KiB, or refuse an oversized payload in `ReadFrom`
before `Decode`. Optionally refuse server-only envelope members on the server.

**Verification / reproduction:** scratchpad `wire/amp.cpp` with `protoc`
33.4-generated code, built with:

```text
scripts\vcenv.cmd cl /std:c++20 /O2 /EHsc /MD /DPROTOBUF_USE_DLLS …
  amp.cpp gen\augusta.pb.cc /link libprotobuf.lib abseil_dll.lib
```

Confirm the Match-level effect with a `linux-netcode` flood counting late ticks.
The nightly fuzz runs with `-max_len 4096`, so it never reaches this size.

**Related:** ER-2 (an amplifier of the same exposure).

#### ER-19: "The rifle is compared exactly" does not hold against the arm64 server, where clang fuses multiply-adds

| Field        | Value                                                                                                                                         |
| ------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `simulation/gameplay/weapon-Step/fp-contraction-differs-across-arch`                                                                          |
| Category     | RISK                                                                                                                                          |
| Severity     | Low (impact Low: a spurious restore and replay and a `rifle_corrections` count, with an invisible one-grid-step recoil change; factors: none) |
| Confidence   | Medium (assumes the arm64 release uses clang's default `-ffp-contract=on`)                                                                    |
| Verification | V3 for the code generation and a modelled rate                                                                                                |
| Ledger       | new                                                                                                                                           |

**Location:** `src/modules/gameplay/weapon.cpp:41` `Recovered`
(`pitch*pitch + yaw*yaw`); `src/modules/prediction/prediction.cpp:173`
(`rifle_differs`, an exact compare).

**Evidence:**

- Observed: NFR-04 lists Linux arm64 as a server target. The client is x64. The
  Dockerfile installs clang-21, and no `-ffp-contract`, `-ffp-model` or `/fp:`
  flag appears in `CMakeLists.txt`, `cmake/`, the presets or any module's build
  file (grep).
- Observed (V3): clang 21.1.8 `-O2` emits `fmadd` for `--target=aarch64`, and
  `mulss` + `addss` for `x86_64`.
- Observed (V3): over 10 million random recoil states on `kAngleGrid`, the fused
  and unfused lengths differ in 11.7% of cases, and the snapped rifle differs on
  0.0072% of recovery ticks. A sequence model of the example rifle showed no
  divergence in 1990 steps.

**Failure mechanism:** the same inputs give a rifle state one grid step apart on
the two architectures, so the client corrects.

**Impact:** rare spurious corrections against an arm64 server. The same
contraction affects any other claim of identical results across server
architectures (NFR-03 golden trajectories, NFR-09 replays on arm64).

**Why it is not prevented:** ADR-0004 assumes `weapon::Step` is the same on both
sides. ARCHITECTURE §11 accepts PhysX divergence across platforms, not the
engine's own arithmetic.

**Corrective direction:** `-ffp-contract=off` for the shared core (at least
`weapon` and `physics`), or state in ADR-0004 that the rifle compare tolerates
one grid step.

**Verification / reproduction:** scratchpad `prediction/fma.cpp`, `rnd.cpp`,
`recoil_div.cpp`. Confirm with the released arm64 image's compile flags.

**Related:** none.

#### ER-20: The SIGINT/SIGTERM handler can call `Stop()` on a ServerRuntime that `Execute` is releasing

| Field        | Value                                                                                                      |
| ------------ | ---------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `concurrency/server/HandleShutdownSignal/runtime-released-under-handler`                                   |
| Category     | RISK                                                                                                       |
| Severity     | Low (impact Medium: a crash at exit after a clean stop; factors: a signal within a few-instruction window) |
| Confidence   | Medium (which thread runs the handler is platform behaviour, not observed)                                 |
| Verification | V1                                                                                                         |
| Ledger       | new                                                                                                        |

**Location:** `src/server/main.cpp:24` `g_runtime`, `:28-32`
`HandleShutdownSignal`, `:44` `~SignalTarget`;
`src/modules/application/include/augusta/application.h:118-120`
(`runtime->reset()` right after `run`).

**Evidence:**

- Observed: the handler loads `g_runtime` and then calls `runtime->Stop()`.
  Nothing keeps the runtime alive between the two steps.
- Observed: `~SignalTarget` nulls `g_runtime` when `run` returns, and `Execute`
  destroys the runtime immediately afterwards.
- Hypothesis: on Windows the CRT runs SIGINT handlers on a console control
  thread. On POSIX a process-directed SIGTERM can land on any thread that does
  not block it, including GNS's service thread.

**Failure mechanism:** the handler thread loads a non-null pointer and is
preempted. The main thread nulls the pointer and destroys the runtime. The
handler resumes and calls `Stop()` on freed memory, or makes a pure-virtual call
and aborts.

**Impact:** a crash report and a missing terminal event for a stop that had
already succeeded.

**Preconditions and likelihood:** a shutdown signal arriving exactly while the
run ends. Very unlikely.

**Why it is not prevented:** ADR-0005 makes the stop signal-safe (a lock-free
CAS), but not the lifetime of the object it reaches.

**Corrective direction:** have the handler set a process-lifetime flag that the
runtime's supervisor watches, so the handler never dereferences a runtime.

**Verification / reproduction:** open. An injected delay between the load and
`Stop()` under ASan, with the signal raised while `Execute` releases the
runtime.

**Related:** none.

#### ER-21: The Hitbox history composes every Joint for every target per bullet per tick, and the benchmark keeps one Joint

| Field        | Value                                                                                                                |
| ------------ | -------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `performance/simulation/HitboxesFor/history-compose-before-cull`                                                     |
| Category     | RISK                                                                                                                 |
| Severity     | Low (impact Medium: missed ticks against NFR-01, but only near the Joint cap under sustained fire)                   |
| Confidence   | Medium                                                                                                               |
| Verification | V3 for the per-Joint cost; analysis for the total (a **risk** at the stated scale, not a defect for current content) |
| Ledger       | new                                                                                                                  |

**Location:** `src/modules/simulation/simulation.cpp:393-411` `HitboxesFor`;
`src/modules/simulation/hitbox_history.cpp:50-84` `PoseHistory::At`;
`tests/benchmark_match.h:66-73,147-153`.

**Evidence:**

- Observed: `At()` composes every kept Joint (assign, lerp, slerp, `ToMat4` and
  a multiply) for each target, for each bullet in flight, every tick, all before
  the `Reaches` cull.
- Observed: the benchmark binds every hitbox to Root, so it keeps one Joint, and
  7 of its 8 rounds resolve on the first tick. `kMaxJoints` is 128. The example
  soldier has 12 Joints.
- Observed (V3): a copy of `At`'s inner loop (clang 21.1.8 `-O2`) took 1162,
  2266, 4473 and 9334 ns per call at 16, 32, 64 and 128 Joints, about 71 ns per
  Joint.
- Observed: the nightly's `BM_SimulationTick_mean` is 53.4 µs.
- Derived: with 8 players at 600 rpm on an open map (about 100 rounds in
  flight), the cost is about 0.5 ms per tick for the example soldier, 1.6 ms at
  32 Joints, and 6.6 ms at 128, about 40% of the 16.67 ms budget at 60 Hz.

**Failure mechanism:** the cost scales with bullets × targets × Joints. The
benchmark exercises none of the three at production values.

**Impact:** ticks overrun with richer skeletons under heavy fire.

**Why it is not prevented:** the benchmark does not represent the cost.

**Corrective direction:** a benchmark with a skeletal character binding hitboxes
across its Joints and rounds that fly their full range. Optionally, cull on the
interpolated root with a pose-independent reach before composing; the cost is a
conservative bound per Character.

**Verification / reproduction:** scratchpad `tick/at_cost.cpp`,
`clang++ -std=c++2b -O2 -DNDEBUG -I<vcpkg include> at_cost.cpp`. Confirm with
the extended benchmark at 32, 64 and 128 Joints.

**Related:** none.

#### ER-22: The server's verification key is published in the same folder, by the same credential, as the pack it verifies

| Field        | Value                                                                                                                                                                         |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `security/deploy/augustad-config/public-key-beside-pack`                                                                                                                      |
| Category     | DESIGN                                                                                                                                                                        |
| Severity     | Low (impact High: for NFR-08's "on the node" Source, Ed25519 adds nothing over BLAKE3; factors: needs the FIDO2 publish credential or node root, both trusted; a lab cluster) |
| Confidence   | High                                                                                                                                                                          |
| Verification | V1                                                                                                                                                                            |
| Ledger       | new                                                                                                                                                                           |

**Location:** `charts/augustad/templates/_helpers.tpl:100-107`
(`base_dir: /srv/augusta/pack`, `public_key: signing.pub`);
`charts/augustad/values.yaml:66-77`;
`clusters/onprem/node/augusta-publish-receive.sh:12,120-170`;
`tools/pack/src/augusta_pack/publish.py:132-170`;
`docs/ARCHITECTURE.md:570-577`.

**Evidence:**

- Observed: augustad's key is the `signing.pub` in the mounted version folder
  beside `server.apak`. `augusta-publish` uploads both, and the receive script
  accepts any key for a new folder.
- Observed: ARCHITECTURE §7 documents this ("copies a server pack and its
  `signing.pub`") and also says "Each environment signs with its own key".
  Nothing pins which key that is. augustad does not compare its pack hash with
  the `packVersion` that Git names.
- Derived: `publish` re-reads the files after `prepare` verified them, so the
  bytes sent may not be the bytes verified.

**Consequence:** whoever can create a pack folder can supply a pack and the key
that verifies it. The per-environment key separation (develop's key against the
release key for staging) is enforced nowhere, and a requirement that names "on
the node" as its Source is met only by the BLAKE3 hash.

**Corrective direction:** pin the trust anchor in Git: put the environment's
public key, or its fingerprint, in the HelmRelease values rendered into the
ConfigMap, and/or have augustad check its pack hash against the configured
`packVersion`. This contradicts ARCHITECTURE §7's current wording, so amend it
together with ADR-0026's.

**Verification / reproduction:** confirm that no admission or Flux check binds
`signing.pub` per environment.

**Related:** ER-13.

#### ER-23: The Shooter's delay is whatever the client claims within the cap, never bounded by its measured latency

| Field        | Value                                                                                                                           |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `simulation/simulation/ShootersDelay/claimed-delay-unbounded-by-rtt`                                                            |
| Category     | DESIGN                                                                                                                          |
| Severity     | Low (impact Medium: unfair hits within a Match; factors: the window is the 300 ms that ADR-0044 already asks victims to accept) |
| Confidence   | High                                                                                                                            |
| Verification | V1                                                                                                                              |
| Ledger       | new                                                                                                                             |

**Location:** `src/modules/simulation/combat.cpp:20-26` `ShootersDelay`;
`src/modules/host/command_queue.cpp:40-53` `Validate`;
`src/modules/simulation/simulation.cpp:257`.

**Evidence:**

- Observed: `Validate` checks that `seen_fraction` is finite and does not check
  `seen_tick`. `ShootersDelay` clamps the Seen time to no later than now − 1,
  and the delay to `max_delay`.
- Observed: ADR-0044 bounds the claim only by the cap. Its Considered Options do
  not discuss a per-peer bound.
- Observed: the server already samples each peer's `ping_ms`
  (`Host::SampleConnections`).

**Consequence:** a modified client with a 5 ms RTT can pick, per shot, any of
the target's poses over the last 300 ms, for example before the target reached
cover. This is the standard's "rewind window a client can extend by lying about
its delay", within the cap. It follows ADR-0044's decision and does not
contradict it.

**Corrective direction:** amend ADR-0044 to bound each peer's delay by its
measured RTT + Interpolation delay + queued ticks + slack, or at least to count
claims beyond that bound as misbehaviour. The cost is false positives under
jitter, plus a per-peer input into the tick.

**Verification / reproduction:** a harness test with a 5 ms-RTT peer claiming
the cap, showing the hit.

**Related:** none.

#### ER-24: A Replay gives Game policy a different history from the live Match

| Field        | Value                                                                                          |
| ------------ | ---------------------------------------------------------------------------------------------- |
| Fingerprint  | `simulation/host/Replay/policy-history-differs-from-live`                                      |
| Category     | DESIGN                                                                                         |
| Severity     | Low (impact Medium: NFR-09 fails for stateful rules; factors: the shipped rules are stateless) |
| Confidence   | High that the inputs differ; Medium that real scripts will hit it                              |
| Verification | V1                                                                                             |
| Ledger       | new                                                                                            |

**Location:** `src/modules/host/replay.cpp:53,143-154`;
`src/modules/host/replay_server.cpp:166`; `src/modules/host/host.cpp:104-106`;
`src/modules/host/match.cpp:152`; `src/modules/policy/policy.cpp:41-46`.

**Evidence:**

- Observed: a Replay calls `AddPlayer`, never `World::StartMatch`, so
  `assign_spawns` never runs.
- Observed: the live server keeps one `Policy`, one Lua state, for its whole
  life. Each Replay gets a fresh one.
- Observed: live Entity IDs continue across Matches (`next_entity_++`), but
  Replay IDs restart at 1 (`BodyOf(number)`). The hook's view carries `entity`
  and `killer`.

**Consequence:** a rules script that keeps state from `assign_spawns`, or across
Matches, or keys on Entity IDs, ends its Replay differently from the live Match.
ADR-0051 states that "the World is handed what the live World was handed".

**Corrective direction:** give each live Match a fresh `Policy` (or document
rules as stateless), number Entity IDs per Match from 1, and record and replay
the `assign_spawns` call.

**Verification / reproduction:** a capture-and-replay test with a rules script
that counts Matches or keys on Entity IDs.

**Related:** ER-15.

#### ER-25: The server config accepts settings whose effect is silently nothing

| Field        | Value                                                                     |
| ------------ | ------------------------------------------------------------------------- |
| Fingerprint  | `correctness/config/ServerConfigFrom/inert-settings-accepted`             |
| Category     | IMPROVEMENT                                                               |
| Severity     | Low (local and hand-written configs only; the chart renders neither case) |
| Confidence   | Medium                                                                    |
| Verification | V1 for the capture keys; V0 (Hypothesis) for the port-0 bind              |
| Ledger       | new                                                                       |

**Location:** `src/modules/config/server_config.cpp:106-119,152-167`;
`src/modules/host/host.cpp:53`; `src/modules/networking/server.cpp:94,129-133`.

**Evidence:**

- Observed: `simulation.capture_mode: strict` and `capture_retention.*` are
  accepted without `simulation.capture`, and then no Capturer is built. A strict
  playtest whose `capture` key was commented out runs uncaptured.
- Observed: `network.listen_address` without a port parses with port 0
  (`steamnetworkingtypes.h:227-228`). Hypothesis: GNS then binds an ephemeral
  port, and the log prints the requested address.

**Consequence:** an operator mistake that ADR-0034 says should never silently
fall back does fall back.

**Corrective direction:** cross-key checks in `ServerConfigFrom`, as
`ReadReplay` already does, and require an explicit non-zero port.

**Verification / reproduction:** a config test for each case.

**Related:** none.

#### ER-26: augustad is built with no binary hardening flags

| Field        | Value                                                                                                          |
| ------------ | -------------------------------------------------------------------------------------------------------------- |
| Fingerprint  | `build-deploy/cmake/compile-options/no-binary-hardening`                                                       |
| Category     | IMPROVEMENT                                                                                                    |
| Severity     | Low (defence in depth; factors: ADR-0057's confinement)                                                        |
| Confidence   | High                                                                                                           |
| Verification | V3 for the compiler defaults (Ubuntu 26.04 clang 21.1.8 with the project's link flags); V1 for the build files |
| Ledger       | new                                                                                                            |

**Location:** `CMakeLists.txt:93-111`; `cmake/LlvmToolchain.cmake:27-34`;
`triplets/x64-linux.cmake:16-25`; the `linux` and `linux-shipping` presets.

**Evidence:**

- Observed: no `-fstack-protector`, `_FORTIFY_SOURCE`, `-z now`,
  `-fcf-protection`, `-fstack-clash-protection` or `_LIBCPP_HARDENING_MODE`
  appears in the build (grep).
- Observed (V3): the result is PIE with RELRO, with no BIND_NOW, no
  `__stack_chk_fail` and no `__*_chk` symbols. libc++'s hardening mode defaults
  to none.
- Derived: Release defines `NDEBUG`, so the shipped server runs no standard
  library bounds checks.

**Consequence:** a memory-safety bug in the inbound path would meet no
compiler-inserted mitigation. ADR-0057's confinement (user namespace, seccomp,
no capabilities, read-only root, no egress) is the only layer.

**Corrective direction:** add the flags above, with
`_LIBCPP_HARDENING_MODE_FAST` in every build, and check the tick budget with the
existing benchmarks before and after.

**Verification / reproduction:** `readelf -d` and `nm` on
`augustad:sha-550cfe8839dd`.

**Related:** none.

#### ER-27: The pack fuzz target never decodes the animation blobs or the ozz archives the server loads

| Field        | Value                                                   |
| ------------ | ------------------------------------------------------- |
| Fingerprint  | `testing/fuzz/pack_load_fuzz/animation-blobs-unreached` |
| Category     | IMPROVEMENT                                             |
| Severity     | Low (only signed content reaches these parsers)         |
| Confidence   | High                                                    |
| Verification | V1                                                      |
| Ledger       | new                                                     |

**Location:** `tests/fuzz/pack_load_fuzz.cpp:105-123` `ResolveEverything`;
`tests/fuzz/README.md`; `src/modules/animation/clips.cpp:69-81`;
`src/modules/scenario/content.cpp:67`.

**Evidence:**

- Observed: the target calls none of `ResolveClip`, `ResolveAnimationSkeleton`,
  `ResolveStanceTransitions` and `ResolveWeaponSocket`, and never runs
  `animation::LoadClips` or ozz's `IArchive`. Its seeds hold 31 Clip blobs and
  `Skeleton.ozz`, but no Texture.
- Observed: the README claims "every asset of the example scenario resolved".

**Consequence:** the deepest third-party parser in the server's pack load goes
unfuzzed. A malformed but signed archive, such as one from a writer bug, reaches
it.

**Corrective direction:** add the four resolvers and `LoadClips` to
`ResolveEverything`, and add a texture seed.

**Verification / reproduction:** coverage of `clips.cpp` under the fuzz target.

**Related:** none.

## Architectural observations

- **Boundaries held.** The core check (`checks/core_boundary.cmake:44-55`) globs
  only the public headers of `physics` and `simulation`. A grep of every source
  in both modules for `protocol::`, `input::` and their headers found nothing,
  and neither build file links them. A script over every module's
  `target_link_libraries` found no target-level cycle. The only directory-level
  loop (`capture` ↔ `link`) runs through the INTERFACE target
  `augusta_capture_records`. `checks/protocol_boundary.cmake` looks for protobuf
  includes in headers only. A grep of `.cpp` files found none, but the check
  would not catch one.
- **Strengths relied on (checked):**
    - Every client float travels as an integer grid count, range-checked before
      conversion (`decoder.h:36-46`).
    - Sticky-error decoding drops a message whole.
    - Transport callbacks only publish events (`server.cpp:87-88`,
      `networking/client.cpp:86-87`).
    - The supervisor's one-way state machine records the first failure and stops
      once (`supervisor.cpp:71-109`).
    - Destructors run in join-safe order on both runtimes.
    - Pack blobs and indexes stay inside the signed range with overflow-free
      bounds (`pack_reader.cpp:156`).
    - Replication is full-state, so there are no delta baselines to lose.
    - Hook answers are validated into typed actions (`answers.cpp:25-131`).
    - `/livez` advances only after a whole tick (`server.cpp:188-195`).
- **File splits preserved behaviour as read.** `git diff e430ddae HEAD` of
  `simulation.cpp`, and a removed-lines against added-lines comparison for
  `physics`, show only moved code and includes. The phase order is unchanged.
- **Doc-vs-code divergences (not findings):**
    - ADR-0031 says the signature is checked before the header is trusted, but
      `ParsePackHeader` runs first. It is bounded, and two tests depend on the
      order. Carried from 2026-10-14.
    - ADR-0005 and ARCHITECTURE §8 still call the client's second thread
      "Simulation" (the code says Prediction, `runner.cpp:208`), and neither
      lists the pcapng writer or the audio callback thread. This was
      recommendation 7 of 2026-10-19 and is still open.
    - ADR-0024's "one physics World" does not match the code (see ER-10).
    - `command_stream.h:59-60` says "32 bits outlast any session", but
      `tick::Sequence` is 64-bit.
    - ADR-0044's Consequences say the client's Debug overlay draws hitboxes
      "where the server tested them", but `client_character_loader.cpp:117-121`
      draws them in rest pose.
    - ADR-0077 leaves kubectl and helm to a package manager, but
      `scripts/bootstrap/linux.sh:360-375` pipes unverified installers into a
      root shell (operator workstations, not CI).
- **Rewind pose vs drawn pose.** Remote players are drawn unposed
  (`renderer.h:150-158`) but judged against the animated Hitbox pose, so a
  crouched target is drawn standing and judged low. ROADMAP M26 plans this.
- **The ER-7 fix pattern.** A dozen private copies of `Utf8()` show a rule with
  no single owner (see ER-12). `docs/agents/coding-standards.md` says nothing
  about path-to-text conversion.
- **Dropped candidate kept as a note.** `ReadPackLayout`'s duplicate-path check
  is quadratic before verification (`pack_reader.cpp:160`). It is reached only
  by the `augusta-inspect` developer tool, and its cost was not measured.

## Verification performed

| Check                                               | Command or method                                                                           | Scope it reached                                                                                                                                                     | Result                                                                |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| CI on push                                          | `gh run view 40357984952`                                                                   | 550cfe88: changes, format, container-verify, server-x64-linux, client-x64-windows, confinement, publish                                                              | Green; `sanitizers` and `fuzz` skipped on push (`ci.yml:442,467`)     |
| CI on the last PR (same tree)                       | `gh run view 40351708971`                                                                   | Merge ref 68f72bf9, tree equal to 550cfe88: linux-san (ASan/UBSan), short fuzz, both platforms                                                                       | Green                                                                 |
| CodeQL                                              | `gh run view 40357985023`                                                                   | 550cfe88                                                                                                                                                             | Success                                                               |
| Nightly                                             | `gh run view 40290941153`                                                                   | develop at 0077eff9 (predates #1317–#1325): TSan (client off, `nightly-jobs.yml:189`), fuzz ×3 for 1800 s, netcode, coverage, benchmarks, macOS client, sbom-scan ×3 | All green. Protocol fuzz: 105 858 556 runs, `-max_len 4096`, no crash |
| Architecture: link graph                            | Throwaway Python over `src/modules/*/CMakeLists.txt`                                        | Every module's targets                                                                                                                                               | No target-level cycle                                                 |
| Architecture: core boundary beyond headers          | Grep of all `physics`/`simulation` sources                                                  | Both modules                                                                                                                                                         | No protocol or input use                                              |
| Lua limits (ER-8, ER-9, ER-15, ER-17)               | `engine_repro.exe` (engine sources + Lua 5.5.1 + sol2), `repro.exe` (plain Lua)             | `MakeSandbox`, `Script::Call`, `ToValue`                                                                                                                             | Reproduced; outputs in each finding                                   |
| ER-6 fix                                            | Read `lua_sandbox.cpp:35-50` against `luaC_checkfinalizer`; tests `policy_test.cpp:337-433` | `__gc` via `setmetatable`                                                                                                                                            | Holds                                                                 |
| PhysX controller contact (ER-10)                    | `prediction/cct.cpp`                                                                        | The project's capsule                                                                                                                                                | Reproduced                                                            |
| Correction snap (ER-14)                             | `prediction/snap.cpp` with the real `correction.cpp`                                        | 60 and 144 fps                                                                                                                                                       | Reproduced                                                            |
| FP contraction (ER-19)                              | `prediction/fma.cpp`, `rnd.cpp`, `recoil_div.cpp`                                           | Code generation; a modelled rate                                                                                                                                     | Reproduced                                                            |
| Inbound parse cost (ER-18)                          | `wire/amp.cpp` with protoc 33.4 output                                                      | `ParseFromArray` on the worktree's schema                                                                                                                            | Reproduced on Windows; Linux not measured                             |
| Pack mapping (ER-13)                                | `packs/scripts/toctou.py` against an older `_native` build                                  | Boost mapping flags, in-place edit                                                                                                                                   | Reproduced at the OS level; `ResolveMesh` with this commit not run    |
| Path text (ER-12)                                   | `threads/pack_path_repro.cpp`, `threads/config_path_repro.cpp`, `build-obs/path_string.cpp` | MSVC STL on ACP 1252                                                                                                                                                 | Reproduced; the end-to-end augustac launch not run                    |
| Hitbox history cost (ER-21)                         | `tick/at_cost.cpp`                                                                          | `At`'s inner loop                                                                                                                                                    | Measured                                                              |
| Hardening (ER-26)                                   | Grep of the build; a test link on Ubuntu 26.04 clang 21.1.8                                 | Compiler defaults with the project's flags                                                                                                                           | Confirmed; the published image itself not inspected                   |
| CI token scope (ER-11)                              | Job log of run 40357984952 `format`                                                         | Token permissions                                                                                                                                                    | `Packages: write` confirmed; cache overwrite not tried                |
| SBOM (ER-4)                                         | `git diff 909d1aa1 HEAD -- scripts/sbom-cpes.py`; nightly sbom-scan                         | Rule and latest scan                                                                                                                                                 | Rule unchanged; scans green                                           |
| Local build, ctest, clang-tidy, sanitizers, fuzzing | not run                                                                                     | —                                                                                                                                                                    | No local build of this commit (see limitations)                       |

## Not reviewed or not verified

- **Local build and tests:** none was run. A `make test PRESET=windows-debug`
  run, and a Linux run of the `linux-san`, `linux-tsan` and `linux-fuzz` presets
  at this commit, would close this.
- **TSan, long fuzz and netcode at this commit:** the nightly predates the file
  splits (#1317–#1325, 242 files under `src/` and `tests/`). The next scheduled
  nightly closes this. TSan never runs the client (`-DAUGUSTA_CLIENT=OFF`), so
  ClientRuntime's cross-thread state stands on review alone (read at L3, V1).
- **Capture and replay:** L2 plus flow 7. The capture reader and fuzz harness
  were read, but no fuzzing ran locally. `capture_metrics.cpp` was not read.
- **Pack writer:** L2. `encoder.cpp`, `cook.py` beyond ordering, and the native
  animation, mesh and texture code were not read. CI's byte-for-byte golden
  packs on both platforms are the evidence (V2).
- **Inbound wire:** `encoder.cpp`, the server→client conversions in
  `link_messages.cpp`, `raw_link.cpp` and the GNS internals (receive buffer
  defaults; purge on close) were not read.
- **Authoritative tick:** `recording.cpp`, `map.cpp`, `replay.cpp` (from the
  tick side) and `tick_messages.cpp` were not read in this area.
- **Rendering and audio:** threads and lifetimes only (the audio callback lock).
  The renderer itself was not read.
- **Agents and tools:** L1 only. `tools/composer` and `tools/docs` were not
  read.
- **Build and deployment:** `chisel/` slices, the seccomp script, the `Makefile`
  body, `release.yml`'s job bodies, `codeql.yml` and `docs.yml` were not read.
  The Windows client's `/guard:cf` and `/CETCOMPAT` were not checked.
- **Arm64:** no arm64 build or run. ER-19 rests on code generation and a model.

## Open questions

- **ER-11:** can a job's `GITHUB_TOKEN` overwrite the `augustad:buildcache` tag,
  and would BuildKit reuse a planted layer? A throwaway-repository test settles
  it.
- **ER-13:** is a player tampering with their own client pack in scope for
  NFR-08, or covered by an accepted risk? The answer sets its severity.
- **ER-10:** is player-against-player blocking intended on the server? The
  answer picks the fix.
- **ER-16:** was `kMaxPlayers` ever above 8 under a capture format still
  readable? If so, genuine captures reach the stop.
- **ER-18:** what are GNS's receive buffer defaults at the vcpkg version, and
  does `CloseConnection` purge a poll group's queued messages? Both shape the
  per-round cost.
- **Replay server memory:** each viewer holds its own full `Capture` copy, for
  up to 255 viewers. ADR-0051 budgets CPU, not memory. Not measured.
- **Windows augustad:** no console control handler is installed, so closing its
  window ends it without `FinishCapture` or a terminal event. Is that acceptable
  for local and lab use?
- **Stack overflow:** no `sigaltstack` is installed, so a stack overflow writes
  no crash lines (NFR-10). No deep-recursion path was found.

## Residual risks and assumptions

- Peers are unauthenticated on a trusted LAN (ARCHITECTURE §11). Every inbound
  finding is rated with that factor.
- Signed pack content is trusted. ER-8, ER-9, ER-15 and ER-17 are rated down by
  it. A signing-key or publish-credential compromise turns them into a remote
  stop of the server (see ER-22).
- PhysX divergence across platforms remains accepted. ER-19 shows that the
  engine's own arithmetic diverges across server architectures too.
- The arm64 release is assumed to be built with clang's default FP contraction.
- CI's results at the identical tree (run 40351708971) stand in for local
  sanitizer and test runs.
- Reproductions that used vcpkg libraries assume the same versions as this
  commit's manifest: Lua 5.5.1, protobuf 33.4, the installed PhysX. ER-13's
  OS-level test used an older pack format build.

## Comparison with prior reviews

| ID   | Fingerprint                                                     | Previous status  | Status now       | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| ---- | --------------------------------------------------------------- | ---------------- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ER-1 | `correctness/server-host/match_winner/stale-on-abandoned-match` | fixed            | fixed            | `host_network.cpp:259` still clears `match_winner` in `HandleDisconnect` (V1)                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| ER-2 | `security/server-host/PumpPeers/no-peer-or-rate-bound`          | recurring        | recurring        | Unchanged: every connection is accepted (`peer_gate.cpp:50-58`), handling runs per message under the Host mutex (`:75-83`), `IsMisbehaviour` is unchanged, the drain is unbounded (`networking/server.cpp:249-281`). New symptoms: an unthrottled INFO `join_refused` line per refused Join or Replay request (`host_network.cpp:56,193`; `replay_server.cpp:247`), against ADR-0038's "Untrusted input"; when full, the replay server reads and decodes a whole capture before refusing (`replay_server.cpp:288` before `:294`). ER-18 amplifies it. |
| ER-3 | `security/scripting/MakeSandbox/warn-and-memory-unbounded`      | fixed            | fixed            | `warn` still removed; allocator limit at `lua_sandbox.cpp:61-83` (V1)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| ER-4 | `build-deploy/ci/sbom-cpes/released-sbom-names-removed-port`    | not-reproducible | not-reproducible | The rule in `scripts/sbom-cpes.py` is unchanged (only `ozz-animation` added to `_NO_CPE`); nightly 40290941153's three `sbom-scan` jobs are green; no port removed since `909d1aa1`; the latent mechanism stays                                                                                                                                                                                                                                                                                                                                       |
| ER-5 | `networking/protocol/ReadList/allocates-past-truncation`        | fixed            | fixed            | `ReadList` reserves nothing past the bound (`decoder.cpp:174-185`, V1). Protobuf's own parse before it is ER-18, a different mechanism                                                                                                                                                                                                                                                                                                                                                                                                                |
| ER-6 | `security/scripting/MakeSandbox/gc-finalizer-escapes-limits`    | new              | fixed            | `f5e8651b` (#900) wraps `setmetatable` and refuses a raw `__gc` field, `__gc = false` included (`lua_sandbox.cpp:35-50,97-99`), tested in `policy_test.cpp:337-433` and `parameters_loader_test.cpp:410-417`; #898 closed. Other routes out of the limits are ER-8                                                                                                                                                                                                                                                                                    |
| ER-7 | `errors/host/CaptureDirectory/narrow-path-conversion-throws`    | new              | fixed            | `c68fc627` (#902): capture and replay paths in UTF-8 (`capture_retention.cpp:30-33`, `capture_writer.cpp:30-33`, `replay_catalog.cpp`, `replay_server.cpp`), the writer under `failure::Guard` (`capture_writer.cpp:195-201`), tests with `u8"中文"` (`capture_retention_test.cpp:78`, `replay_catalog_test.cpp:202`); #899 closed. The same mechanism at startup sites is ER-12                                                                                                                                                                      |

## Recommended next actions

1. **Close the Lua limit escapes (ER-8, ER-9, ER-17).** Use an explicit hook
   handler, refuse `__close`, trim or wrap `table.move` and the pattern
   functions, and add a node budget to `ToValue`. Hand off with `/triage`; the
   reproductions above serve as the tests.
2. **Decide player-against-player collision (ER-10).** It is a design choice
   before a fix, so take it through `/grill-with-docs` or `/triage`, then amend
   ADR-0004 or ADR-0024.
3. **Narrow CI token scopes and pin Python resolution (ER-11).** Hand off with
   `/triage`. Settle the open question first with a throwaway test.
4. **One path-to-UTF-8 text function plus a ban on `path::string()` (ER-12).**
   Hand off with `/triage`; a Windows CI test from a non-ANSI folder goes with
   it.
5. **Bound inbound size before parsing, and decide connection and rate bounds
   (ER-18, ER-2).** Use `/triage`; `/diagnosing-bugs` with a `linux-netcode`
   flood to measure the Match-level effect.
6. **Serve client resolves from owned, hashed bytes (ER-13), and pin the
   environment's pack key in Git (ER-22).** Use `/triage`, after deciding
   ER-13's scope under NFR-08.
7. **Low-cost correctness fixes:** ER-14 (snap on the tick's own jump), ER-16
   (refuse oversized captures before listing), ER-20 (a process-lifetime stop
   flag), ER-25 (cross-key config checks). Use `/triage`.
8. **Determinism across architectures and replays (ER-19, ER-15, ER-24).**
   Decide `-ffp-contract=off` for the shared core and whether rules are
   stateless; `/improve-codebase-architecture` for ER-24's per-Match `Policy`.
9. **Test and build hardening:** extend the pack fuzz target (ER-27), the tick
   benchmark (ER-21), and add binary hardening flags measured against the tick
   budget (ER-26).
10. **Amend the docs** named under "Architectural observations" (ADR-0005 thread
    names, ADR-0024, ADR-0031, ADR-0044, ARCHITECTURE §7).
11. **Deeper review:** re-run the Linux checks at this tree once the next
    nightly passes. Bring Capture and replay and the Pack writer to their full
    planned depth.
