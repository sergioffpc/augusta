# Engineering review ledger

Next ID: ER-8

## Findings

| ID   | Fingerprint                                                     | Category    | Severity | Status           | First seen | Last checked |
| ---- | --------------------------------------------------------------- | ----------- | -------- | ---------------- | ---------- | ------------ |
| ER-1 | `correctness/server-host/match_winner/stale-on-abandoned-match` | BUG         | Low      | fixed            | 2026-10-14 | 2026-10-19   |
| ER-2 | `security/server-host/PumpPeers/no-peer-or-rate-bound`          | RISK        | Low      | recurring        | 2026-10-14 | 2026-10-19   |
| ER-3 | `security/scripting/MakeSandbox/warn-and-memory-unbounded`      | RISK        | Low      | fixed            | 2026-10-14 | 2026-10-19   |
| ER-4 | `build-deploy/ci/sbom-cpes/released-sbom-names-removed-port`    | BUG         | Low      | not-reproducible | 2026-10-14 | 2026-10-19   |
| ER-5 | `networking/protocol/ReadList/allocates-past-truncation`        | IMPROVEMENT | Low      | fixed            | 2026-10-14 | 2026-10-19   |
| ER-6 | `security/scripting/MakeSandbox/gc-finalizer-escapes-limits`    | BUG         | Medium   | new              | 2026-10-19 | 2026-10-19   |
| ER-7 | `errors/host/CaptureDirectory/narrow-path-conversion-throws`    | BUG         | Low      | new              | 2026-10-19 | 2026-10-19   |

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

### ER-2

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `PumpPeers`, `AdmissionDeadlines` and `IsMisbehaviour`; magnitude not
  measured.
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: recurring. Re-read (V1) in
  `src/modules/host/peer_gate.cpp` and `misbehaviour.h`: accept and routine
  rejections unchanged; decoding now outside the Host's mutex, handling still
  under it.

### ER-3

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `MakeSandbox` against Lua 5.5.1's base library; not demonstrated.
- 2026-10-15 129f9a81 triage: filed as #661 (`ready-for-agent`).
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: fixed. `warn` taken out and
  a counting allocator at `kMemoryLimit` (`lua_sandbox.cpp:36-67`), both tested
  in `PolicyTest`; #661 closed. A residual by another mechanism is ER-6.

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

### ER-5

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `protocol.cpp`'s `ReadList` against the header's contract.
- 2026-10-15 129f9a81 triage: filed as #662 (`ready-for-agent`).
- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: fixed. `ReadList` stops at
  the first read error and reserves nothing (`protocol.cpp:343-355`); #662
  closed.

### ER-6

Former fingerprints: none

- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: new. Lua 5.5.1's `GCTM`
  disables debug hooks during a finalizer (`lgc.c:980`); a looping `__gc`
  finalizer ran past 5 s under the sandbox's count hook in a C-API reproduction
  (V3); the engine path from `on_tick` traced (V1).

### ER-7

Former fingerprints: none

- 2026-10-19 909d1aa1 2026-10-19-full-repository.md: new.
  `std::filesystem::path::string()` threw `std::system_error` 1113 for a
  non-ANSI name on ACP 1252 (V3); traced (V1) from `Capturer::StartMatch` to
  `CaptureDirectory::Scan` on the unguarded writer thread.
