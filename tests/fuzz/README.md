# Fuzz targets

libFuzzer targets for what arrives from outside (ADR-0013). Each target is one
`<target>_fuzz.cpp` defining `LLVMFuzzerTestOneInput`, built twice by
[CMakeLists.txt](CMakeLists.txt): with libFuzzer under the `linux-fuzz` preset
(clang, ASan), and on every preset with [replay_main.cpp](replay_main.cpp), so
`ctest` runs the target's inputs without the fuzzer.

| Target            | Input                                                                                                                     | Seeds                              |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------- | ---------------------------------- |
| `protocol_decode` | one payload for `protocol::Decode`, and the same bytes as one capture record for `protocol::DecodeCaptureRecord`          | one message of each kind           |
| `capture_read`    | a Match capture file for `capture::ReadCapture`                                                                           | one capture holding every record   |
| `pack_load`       | a pack without its trailer, which the target signs with the golden packs' test key and loads through `assets::Pack::Load` | the golden client and server packs |

## Layout

- `corpus/<target>/`: the seeds, committed. The fuzzer starts from them; the
  corpus it grows is not committed.
- `regressions/<target>/`: the minimized inputs of crashes found, committed.
- `ctest` replays both through the target (`augusta_<target>_replay`), on every
  preset, MSVC included.

The `protocol_decode` seeds are what `Encode` writes for the messages in
[protocol_decode_seeds_test.cpp](protocol_decode_seeds_test.cpp), which fails
when they drift from it. After a deliberate change to the wire, rewrite them:

```bash
cmake --build --preset linux --target augusta_protocol_decode_seeds
```

Beside the corpus, never in it, so no replay reads it,
[protocol_decode_seeds.pcapng](protocol_decode_seeds.pcapng) is what the pcapng
encoder (`augusta_pcapng`, ADR-0076) writes of those seeds: the fixture of
`tools/wireshark`'s dissector test. Its Section Header Block's `shb_userappl` is
`augustad <version>`, the version of the build that wrote it. Then one Enhanced
Packet Block per file in `corpus/protocol_decode`, in name order, so the nth
packet is the seed whose name is the nth in that order and the `augusta.Message`
`oneof` member of that name. Every packet goes between `10.0.0.1:27015` (local)
and `10.0.0.2:50000` (remote), 2026-10-09T12:00:00Z plus n milliseconds, and the
nth (from 0) is outbound (`10.0.0.1` to `10.0.0.2`) when n is even and inbound
(`10.0.0.2` to `10.0.0.1`) when it is odd.
[protocol_decode_seeds_pcapng_test.cpp](protocol_decode_seeds_pcapng_test.cpp),
built only with `AUGUSTA_DEBUG_TOOLS` on, fails when the fixture drifts from the
seeds, the encoder or the engine's version (a Development build's, `+dev`).
After rewriting the seeds, a deliberate change to the encoder, or a version
bump, rewrite it with this build's version:

```bash
cmake --build --preset linux --target augusta_protocol_decode_seeds_pcapng
```

The `capture_read` seed is what a `Capturer` writes for the short Match in
[capture_read_seeds_test.cpp](capture_read_seeds_test.cpp), which fails when it
drifts from it. After a deliberate change to the capture format, rewrite it:

```bash
cmake --build --preset linux --target augusta_capture_read_seeds
```

The `pack_load` seeds are the golden packs in `tests/fixtures/example-packs`
without their trailer: a trailer the fuzzer mutated would fail verification
before `Load` parses anything, so the target writes a valid one itself.
[pack_load_seeds_test.cpp](pack_load_seeds_test.cpp) fails when they drift from
the golden packs; after regenerating those, rewrite the seeds:

```bash
cmake --build --preset linux --target augusta_pack_load_seeds
```

## Running

A pull request runs each target for about 60 seconds (CI's `fuzz` job), and a
crash fails it with the crashing input uploaded as a run artifact. The nightly
(`nightly.yml`'s `fuzz` job) runs each for about 30 minutes, starting from the
corpus earlier nights grew: it lives in the Actions cache, minimized after each
run, and never in the repository. A crash there fails the nightly, uploads the
input, and reaches the `nightly-failure` issue. Locally, in the dev container:

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
   replay test picks it up on the next configure and fails until the fix lands,
   on every preset, with no fuzzer.
3. When the bug is in a module's own logic, also add an example-based test of it
   to that module's tests (docs/agents/coding-standards.md, Testing).
