# Finding template

Copy the block below into the report once per finding. Fill every field that
applies; write "n/a" with a reason for one that does not. Definitions of
category, severity, confidence, claim tags and V0–V3 are in
[../SKILL.md](../SKILL.md).

## Identity

- **ID** is `ER-<n>`, the ledger's next number, assigned once and never reused
  or renumbered. A finding matching a ledger entry keeps that entry's ID.
- **Fingerprint** is `<dimension>/<module>/<anchor>/<mechanism>`: the standard's
  file name, the module, the most stable symbol or concept involved (a type, a
  function, a message), and a short slug of the failure mechanism. Example:
  `concurrency/networking/PumpEvents/callback-mutates-peer-state`. It contains
  no line numbers or file paths, so it survives moves and edits; when the anchor
  is renamed, the ledger records the old fingerprint beside the new.

## Block

```markdown
### ER-<n>: <one-line statement of the defect or problem>

| Field        | Value                                                     |
| ------------ | --------------------------------------------------------- |
| Fingerprint  | <dimension>/<module>/<anchor>/<mechanism>                 |
| Category     | BUG / RISK / DESIGN / IMPROVEMENT                         |
| Severity     | Critical / High / Medium / Low (factors: <applied, or none>) |
| Confidence   | High / Medium / Low                                       |
| Verification | V0 / V1 / V2 / V3                                         |
| Ledger       | new / recurring (first seen <date, commit>)               |

**Location:** `<path>:<start>-<end>` `<symbol>`; one line per site.

**Path and conditions:** the execution path, entry to effect, as cited hops; the
build configuration, platform and runtime state it needs.

**Evidence:**

- Observed: <fact> (`<path>:<line>`).
- Derived: <conclusion> from <which Observed facts>, because <reasoning>.
- Hypothesis / Unverified: <what is not established>.

**Failure mechanism:** how the defect produces the failure, step by step (for
concurrency, the interleaving).

**Impact:** what goes wrong for whom, actual or potential.

**Preconditions and likelihood:** who or what must act, and how often that
happens in a supported configuration.

**Why it is not prevented:** the mitigations, invariants, callers and checks
inspected, and why none stops it.

**Corrective direction:** the shape of a fix and its trade-offs; not a patch.

**Verification / reproduction:** the commands, test or input that shows it, with
the output observed; or, when open, what evidence would confirm or dismiss it.

**Related:** root cause shared with or symptoms listed under ER-<n>; prior
reports.
```
