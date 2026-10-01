# Testing Strategy: Test Kinds and the Pipeline Stage Each Runs At

Tests use GoogleTest, property-based tests use RapidCheck (through its
GoogleTest integration, so `ctest` discovers them like any other), fuzz
targets use libFuzzer, and micro-benchmarks of hot-path code use Google
Benchmark. The asset cooker (`tools/pack`, Python) uses pytest. What decides
where a kind of test runs is the stage of the pipeline, not the milestone:
each kind has one or more stages it runs at, chosen by how long it takes and
how noisy its result is.

## Stages

| Kind | Pre-push | Pull request | Nightly | Release tag |
|---|---|---|---|---|
| `clang-tidy` | changed `src/*.cpp` | ✅ | | |
| Unit, integration, and boundary tests (`ctest`) | | ✅ Windows + Linux | | ✅ |
| Property-based tests (RapidCheck) | | ✅ 100 cases | ✅ ~10 000 cases | |
| pytest (`tools/pack`) | | ✅ | | |
| Pack contract (golden packs) | | ✅ | | ✅ |
| ASan + UBSan | | ✅ | | |
| Fuzzing | | ✅ ~60 s per target | ✅ ~30 min per target | |
| TSan | | | ✅ | |
| Coverage report (`llvm-cov`) | | | ✅ | |
| NFR-01 (tick rate under load) | | | | manual, on the r630 cluster |
| Micro-benchmarks | by hand | | | |

- **Pre-push** is a `.githooks/pre-push` hook running `clang-tidy` only:
  it is what MSVC does not catch and CI would, and a hook that builds and
  runs the whole suite would take minutes and get skipped with
  `--no-verify`.
- **The push** that lands a merged pull request on `develop`/`main` repeats
  the pull request's build and tests, without the sanitizers; it adds no
  kind of its own.
- **Nightly** runs on `develop`, on a schedule or started by hand. A failure
  opens a GitHub issue labelled `nightly-failure` with a link to the run, or
  updates the one already open, since no pull request is waiting on it for
  anyone to notice.
- **The release tag** runs `ctest` and the asset pipeline check again on the
  binaries it publishes: they come from a fresh build, not the one the
  commit's push tested.
- **No stage runs after a deploy to k3s.** ADR-0026 keeps runners out of the
  cluster, so only something running inside it could test a deployed server.

## Kinds

- **Non-functional requirements as integration tests.** NFR-02, NFR-05, and
  NFR-06 need no dedicated hardware: the Harness lets a test drive a real
  server Host and real client sessions in one process, ticking both by hand,
  with the transport's own simulated latency and loss, so a full Match with
  8 players and a correction under 100 ms of latency run on any machine.
  They run wherever `ctest` does. NFR-01 alone is about
  wall-clock time and cannot be measured on a shared CI runner, so it is
  checked by hand on the r630 cluster before each release; once Flux runs the
  `develop` release (ADR-0026), it becomes a CronJob of 8 Harness clients in
  that namespace.
- **NFR-03 through a golden file.** Reference trajectories live in the
  repository, and the test, on both the Windows (MSVC) and Linux (clang)
  runners, compares what it computes against them within a tolerance defined
  in the test itself. If the two compilers disagree, at least one runner
  fails, without either job needing the other's output. A deliberate change
  to the ballistics model regenerates the file with one build target, and
  the diff is reviewed like code.
- **Pack contract through golden packs.** The pack format has two
  implementations, the Python cooker writing it and `augusta_assets` reading
  it, so both are held to the same committed files: the example scenario's
  client and server packs, cooked with a committed test key (never the release
  key). The cooker's pytest suite cooks the example and requires those packs
  byte for byte, which works because a cook is deterministic; the C++ tests
  load and resolve them, wherever `ctest` runs. Neither job needs the other's
  output. A deliberate change to the format or the example regenerates the
  golden packs with one command. pytest also covers the cooker's own logic
  (validation, optimization, signing).
- **Lua gameplay scripts** (ADR-0022) are tested by running them inside the
  real engine, through `simulation::World`, never through a fake Lua harness:
  a shipped policy script is loaded out of a pack as the server loads it, and
  its rules are checked on what each tick's State says, across the Player
  counts a scenario may have.
- **Fuzzing** targets what arrives from outside: the protocol's message
  decoding (the NFR-05 attack surface) and `Pack::Load`. It builds with the
  `linux-fuzz` preset (clang, libFuzzer, ASan). Seeds live in the repository
  under `tests/fuzz/corpus/<target>/`; the corpus the nightly grows lives in
  the Actions cache, being large and disposable. Every crash found is
  minimized into a fixture under `tests/fuzz/regressions/<target>/`, in the
  pull request that fixes it: each target also builds without libFuzzer on
  every preset, and `ctest` replays its seeds and fixtures through it, so the
  fixture is the regression test (`tests/fuzz/README.md`).
- **Property-based tests** check an invariant over a whole input domain:
  serialization round-trips, stamina never going negative, reconciliation
  converging on the server's state. They are a tool, not a requirement:
  example-based tests stay the default. The case count comes from
  `RC_PARAMS`, so the same binary runs 100 cases per pull request and
  ~10 000 nightly; every property test carries the `ctest` label `property`,
  which is how the nightly selects them. A failing case the runner shrinks
  is kept as an example-based regression test.
- **Coverage** is a report for finding untested deterministic logic, not a
  gate: a minimum percentage pushes toward tests written for the number.
- **Micro-benchmarks** are run by hand when a profile (NVTX in Nsight
  Systems) points at a hot spot. A shared runner is too noisy to gate on a
  percentage, and NFR-01 is the only formal performance target.

## Out of scope

- Rendering, audio, and the client as a whole are validated by eye and ear,
  with no written checklist; CI's Windows runners have no GPU for Falcor.

## Considered Options

- **Gating micro-benchmarks in CI** by a regression percentage: rejected,
  see Micro-benchmarks above.
- **Golden-image tests of the renderer**: rejected, the maintenance of
  reference images outweighs what they catch for a solo project.
- **Exchanging trajectories between the Windows and Linux jobs** for
  NFR-03, instead of a golden file: rejected, it chains the jobs for no
  more coverage than both comparing against the same file.
- **Handing freshly cooked packs from the cooker's job to the C++ job** as
  an artifact, instead of golden packs: rejected, it makes the C++ build wait
  on the cooker's for no more coverage than both checking the same files.
- **AFL++**, which builds with any compiler: not needed once Linux builds
  with clang (ADR-0008).
