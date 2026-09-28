# Fuzz targets

libFuzzer targets for what arrives from outside (ADR-0013). Each target is one
`<target>_fuzz.cpp` defining `LLVMFuzzerTestOneInput`, built twice by
[CMakeLists.txt](CMakeLists.txt): with libFuzzer under the `linux-fuzz` preset
(clang, ASan), and on every preset with [replay_main.cpp](replay_main.cpp), so
`ctest` runs the target's inputs without the fuzzer.

| Target | Input | Seeds |
|---|---|---|
| `protocol_decode` | one payload for `protocol::Decode` | one message of each kind |

## Layout

- `corpus/<target>/`: the seeds, committed. The fuzzer starts from them; the
  corpus it grows is not committed.
- `regressions/<target>/`: the minimized inputs of crashes found, committed.
- `ctest` replays both through the target (`augusta_<target>_replay`), on
  every preset, MSVC included.

The `protocol_decode` seeds are what `Encode` writes for the messages in
[protocol_decode_seeds_test.cpp](protocol_decode_seeds_test.cpp), which fails
when they drift from it. After a deliberate change to the wire, rewrite them:

```bash
cmake --build --preset linux --target augusta_protocol_decode_seeds
```

## Running

A pull request runs each target for about 60 seconds (CI's `fuzz` job), and a
crash fails it with the crashing input uploaded as a run artifact. Locally,
under WSL:

```bash
cmake --preset linux-fuzz
cmake --build --preset linux-fuzz
mkdir -p /tmp/corpus
build/x64-linux-fuzz/tests/fuzz/augusta_protocol_decode_fuzz -max_total_time=60 \
  /tmp/corpus tests/fuzz/corpus/protocol_decode
```

The first directory is where libFuzzer writes what it finds, so it is never the
committed seeds.

## A crash becomes a regression test

In the pull request that fixes it:

1. Minimize the crashing input:
   `augusta_<target>_fuzz -minimize_crash=1 -runs=100000 crash-<sha1>`.
2. Commit the smallest result as `regressions/<target>/<what-it-broke>`. The
   replay test picks it up on the next configure and fails until the fix
   lands, on every preset, with no fuzzer.
3. When the bug is in a module's own logic, also add an example-based test of
   it to that module's tests (docs/agents/coding-standards.md, Testing).
