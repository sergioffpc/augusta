# Report template

The report for one review, saved as `<YYYY-MM-DD>-<scope-slug>.md` at the
location the project profile names. Fill every section, or write "none" in it.
The report contains no overall score, and never states or implies that what it
did not find is absent.

```markdown
# Engineering review: <scope>

## Metadata

| Field          | Value                                                         |
| -------------- | ------------------------------------------------------------- |
| Date           | <YYYY-MM-DD>                                                  |
| Repository     | <remote or path>                                              |
| Branch, commit | <branch> <full hash>, working tree <clean / dirty: what>      |
| Scope          | <full / subsystem / flow: what is in, what is out>            |
| Objectives     | <the questions this review answers>                           |
| Depth plan     | <area: tier, depth reached / planned>, one per area           |
| Tools          | <tool and version, preset or flags>, one per tool             |
| Environment    | <OS, compiler, hardware; capabilities missing>                |
| Reviewer       | <agent and model>                                             |
| Prior review   | <path of the previous report used in comparison, or none>     |

## Executive summary

Three to six sentences: what was reviewed and how deeply, the most important
confirmed findings, the most important open risks (labelled as risks), and the
main limitation.

## Findings

### Critical

<findings, by confidence descending, from templates/finding.md>

### High

### Medium

### Low

## Architectural observations

Material observations that are not findings: confirmed strengths the review
relied on (with how they were checked), divergences between docs and code,
areas whose design shapes the risk register.

## Verification performed

| Check | Command or method | Scope it reached | Result |
| ----- | ----------------- | ---------------- | ------ |

Include checks that found nothing and checks that failed to run.

## Not reviewed or not verified

Each area in scope that is below its planned depth or verification, and each
area out of scope, with the reason and what would close it.

## Open questions

Each unresolved candidate: the concern, current evidence, and the evidence that
would confirm or dismiss it.

## Residual risks and assumptions

The assumptions conclusions rest on (platform, configuration, trusted parties,
third-party behaviour), and the risks that remain even if every finding is
fixed.

## Comparison with prior reviews

| ID | Fingerprint | Previous status | Status now | Evidence |
| -- | ----------- | --------------- | ---------- | -------- |

"None": no ledger existed; it was created from this review.

## Recommended next actions

Ordered; each names the findings it addresses and the hand-off (a fix task,
`/triage`, `/diagnosing-bugs`, `/improve-codebase-architecture`, a deeper
review of an area).
```
