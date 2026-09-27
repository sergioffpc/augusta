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
| Asset pipeline check | | ✅ | | ✅ |
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
- **Nightly** runs on `develop`. A failure opens a GitHub issue labelled
  `nightly-failure` with a link to the run, or updates the one already open,
  since no pull request is waiting on it for anyone to notice.
- **The release tag** runs `ctest` and the asset pipeline check again on the
  binaries it publishes: they come from a fresh build, not the one the
  commit's push tested.
- **No stage runs after a deploy to k3s.** ADR-0026 keeps runners out of the
  cluster, so only something running inside it could test a deployed server.

## Kinds

- **Non-functional requirements as integration tests.** NFR-02, NFR-05, and
  NFR-06 need no dedicated hardware: the Harness lets a test drive a real
  server Host and real client sessions in one process, ticking both by hand,
  with the transport's own simulated latency and loss, so a full round with
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
- **Asset pipeline check.** Builds the cooker, generates a throwaway Ed25519
  keypair for the run, cooks and signs the test assets with it, and loads the
  signed pack in C++. It is the contract test between the Python writer and
  the C++ reader of the pack format; the C++ tests round-trip through the C++
  encoder only, and pytest covers the cooker's own logic (validation,
  optimization, signing). The release private key never reaches CI.
- **Fuzzing** targets what arrives from outside: the protocol's message
  decoding (the NFR-05 attack surface) and `Pack::Load`. It builds with the
  `linux-fuzz` preset (clang, libFuzzer, ASan). Seeds live in the repository
  under `tests/fuzz/corpus/<target>/`; the corpus the nightly grows lives in
  the Actions cache, being large and disposable. Every crash found is
  minimized into a fixture and a regression test that `ctest` runs without
  the fuzzer, in the pull request that fixes it.
- **Property-based tests** check an invariant over a whole input domain:
  serialization round-trips, stamina never going negative, reconciliation
  converging on the server's state. They are a tool, not a requirement:
  example-based tests stay the default. The case count comes from
  `RC_PARAMS`, so the same binary runs 100 cases per pull request and
  ~10 000 nightly. A failing case the runner shrinks is kept as an
  example-based regression test.
- **Coverage** is a report for finding untested deterministic logic, not a
  gate: a minimum percentage pushes toward tests written for the number.
- **Micro-benchmarks** are run by hand when Tracy points at a hot spot. A
  shared runner is too noisy to gate on a percentage, and NFR-01 is the only
  formal performance target.

## Out of scope

- Rendering, audio, and the client as a whole are validated by eye and ear,
  with no written checklist; CI's Windows runners have no GPU for Falcor.
- Lua gameplay scripts (ADR-0022) get a testing decision when M5 starts,
  when the first ones exist.

## Considered Options

- **Gating micro-benchmarks in CI** by a regression percentage: rejected,
  see Micro-benchmarks above.
- **Golden-image tests of the renderer**: rejected, the maintenance of
  reference images outweighs what they catch for a solo project.
- **Exchanging trajectories between the Windows and Linux jobs** for
  NFR-03, instead of a golden file: rejected, it chains the jobs for no
  more coverage than both comparing against the same file.
- **AFL++**, which builds with any compiler: not needed once Linux builds
  with clang (ADR-0008).
