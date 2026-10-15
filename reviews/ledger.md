# Engineering review ledger

Next ID: ER-6

## Findings

| ID   | Fingerprint                                                     | Category    | Severity | Status | First seen | Last checked |
| ---- | --------------------------------------------------------------- | ----------- | -------- | ------ | ---------- | ------------ |
| ER-1 | `correctness/server-host/match_winner/stale-on-abandoned-match` | BUG         | Low      | new    | 2026-10-14 | 2026-10-14   |
| ER-2 | `security/server-host/PumpPeers/no-peer-or-rate-bound`          | RISK        | Low      | new    | 2026-10-14 | 2026-10-14   |
| ER-3 | `security/scripting/MakeSandbox/warn-and-memory-unbounded`      | RISK        | Low      | new    | 2026-10-14 | 2026-10-14   |
| ER-4 | `build-deploy/ci/sbom-cpes/released-sbom-names-removed-port`    | BUG         | Low      | new    | 2026-10-14 | 2026-10-14   |
| ER-5 | `networking/protocol/ReadList/allocates-past-truncation`        | IMPROVEMENT | Low      | new    | 2026-10-14 | 2026-10-14   |

## History

### ER-1

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Traced (V1) from
  `Host::Impl::HandleDisconnect`'s `kEndedMatch` branch to `Capturer::EndMatch`;
  no test covers the case.
- 2026-10-15 129f9a81 triage: filed as #659 (`ready-for-agent`).

### ER-2

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `PumpPeers`, `AdmissionDeadlines` and `IsMisbehaviour`; magnitude not
  measured.

### ER-3

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `MakeSandbox` against Lua 5.5.1's base library; not demonstrated.
- 2026-10-15 129f9a81 triage: filed as #661 (`ready-for-agent`).

### ER-4

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Observed (V2) in
  nightly runs 38379309305 and 38533136972; `falcor` absent from
  `scripts/sbom-cpes.py`, present in release `v3.1.0`.
- 2026-10-15 129f9a81 triage: filed as #660 (`ready-for-agent`).

### ER-5

Former fingerprints: none

- 2026-10-14 129f9a81 2026-10-14-full-repository.md: new. Read (V1) in
  `protocol.cpp`'s `ReadList` against the header's contract.
- 2026-10-15 129f9a81 triage: filed as #662 (`ready-for-agent`).
