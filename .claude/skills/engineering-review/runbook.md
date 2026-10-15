# Engineering review runbook

The procedure for every review, full or scoped. Terms (Observed, Derived, L1–L4,
V0–V3, categories, severity, confidence) are defined in [SKILL.md](SKILL.md).

A **scoped review** (one subsystem, one flow, one dimension) runs every phase
too, each bounded by the scope: Phase 2 maps only the scope and the boundaries
it crosses, Phase 5 traces only flows that enter or leave it. What sits outside
the scope is listed under "not reviewed", never silently assumed correct.

Keep a **working log** in the scratchpad while reviewing: areas read, flows
traced, commands run with their output, open questions. The report is written
from it in Phase 9.

## Phase 1: Establish identity and scope

- **Purpose:** fix what is reviewed, so the review is repeatable.
- **Actions:** record the repository, branch, full commit hash and whether the
  working tree is clean (`git status`, `git rev-parse HEAD`); record the
  platform and the tools available (compilers, sanitizers, GPU, network). State
  the scope, the objectives (what question the review answers), the constraints
  (time, platforms, no network) and the dimensions in and out of scope. Read the
  project profile.
- **Inputs:** the request, the repository, the profile.
- **Done when:** the report's metadata section can be filled completely.
- **Outputs:** report metadata; the scope statement.
- **Dig deeper when:** the tree is dirty or the commit is not on a shared
  branch; record exactly what is reviewed, or review a clean worktree of the
  commit.
- **Do not assume:** that the checkout stays on the same commit during the
  review (re-check `HEAD` before Phase 6); that a tool is installed.

## Phase 2: Reconnoitre (L1)

- **Purpose:** a map to plan depth against.
- **Actions:** list modules and their dependencies (build targets, includes,
  imports); entry points (executables, threads, external inputs, scripts); the
  project's stated guarantees (requirements, ADRs, architecture docs); the test
  layout and what runs in CI; the critical flows. Skim, do not review.
- **Inputs:** the repository tree, build files, docs, the profile's subsystem
  map.
- **Done when:** every module in scope has an owner flow, a dependency list and
  a risk tier (from the profile, or proposed with a reason).
- **Outputs:** module map; list of critical flows; risk register.
- **Dig deeper when:** the code contradicts the docs or profile (a dependency
  the architecture forbids, a module the profile does not list); record the
  divergence.
- **Do not assume:** that the docs describe the code; that a directory name
  states its contents.

## Phase 3: Assess architecture and plan depth

- **Purpose:** spend effort where failure costs most.
- **Actions:** load [standards/architecture.md](standards/architecture.md);
  check dependency direction and boundaries against the stated design; assign
  each module and flow a depth (L1–L4) and the standards to load, from its risk
  tier. Write the **depth plan**: area, tier, depth, standards, verification
  intended.
- **Inputs:** Phase 2 outputs.
- **Done when:** every in-scope area has a planned depth, and every
  Critical-tier area is planned at L3 or deeper.
- **Outputs:** depth plan; architecture observations.
- **Dig deeper when:** an area's tier is unclear; default it one tier higher and
  say why.
- **Do not assume:** that an area is low risk because it is small or rarely
  changed.

## Phase 4: Targeted component review (L2)

- **Purpose:** find defects inside components.
- **Actions:** for each area in the plan, load its standards and work their
  questions against the code: contracts, invariants, error paths, ownership,
  tests. Record each candidate finding with its evidence tags. Read callers
  before judging a callee.
- **Inputs:** depth plan, standards.
- **Done when:** every question of every loaded standard is answered for the
  area, marked not applicable, or recorded as open.
- **Outputs:** candidate findings; per-area coverage notes.
- **Dig deeper when:** a candidate depends on what happens across a module or
  thread boundary; move it to Phase 5.
- **Do not assume:** that a function is only called the way its name suggests;
  that a comment matches the code.

## Phase 5: Cross-module and end-to-end investigation (L3)

- **Purpose:** find defects that live between components.
- **Actions:** trace each critical flow in the plan end to end, citing each hop
  (`path:line`, symbol): where data enters, every transformation, every thread
  or process crossing, every state transition, where it leaves. At each hop ask
  what happens on failure, on reordering, on repetition, on shutdown. Check that
  domain concepts mean the same thing on both sides of each boundary.
- **Inputs:** candidate findings from Phase 4; critical flows.
- **Done when:** each planned flow is traced with no unexplained hop, or the gap
  is recorded.
- **Outputs:** traced flows; cross-module candidate findings.
- **Dig deeper when:** a hop's behaviour depends on runtime state that reading
  cannot settle; plan an L4 check in Phase 6.
- **Do not assume:** that two modules agree on units, ranges, ownership or
  ordering because they share a type name.

## Phase 6: Validate (raise verification)

- **Purpose:** turn candidates into findings or discard them.
- **Actions:** apply every check in SKILL.md's "Before a finding is written".
  Then raise verification where it changes the outcome, cheapest first: run
  existing tests and static analysis (V2); write a targeted reproduction, run
  sanitizers, fuzzers, stress or fault injection (V3), in a throwaway worktree
  or the scratchpad. Record each command and its output. Re-check `HEAD`.
- **Inputs:** all candidate findings; the profile's verification tools.
- **Done when:** every candidate is a finding, discarded with the reason, or
  left open with the evidence that would settle it.
- **Outputs:** validated findings with verification levels; discarded
  candidates; verification log.
- **Dig deeper when:** a candidate is High or Critical severity at Low or Medium
  confidence; it gets at least one attempt to raise confidence.
- **Do not assume:** a tool's silence is proof (a sanitizer only sees the paths
  executed); that a test passing covers the scenario in question.

## Phase 7: Deduplicate, classify, prioritise, document

- **Purpose:** a consistent, minimal set of findings.
- **Actions:** merge findings that share a root cause, keeping symptoms as a
  list. Assign category, severity (with the factors applied) and confidence per
  SKILL.md. Write each with [templates/finding.md](templates/finding.md). Order
  by severity, then confidence.
- **Inputs:** validated findings.
- **Done when:** every finding has every applicable template field, and no two
  findings share a root cause.
- **Outputs:** the findings.
- **Dig deeper when:** the severity factors disagree with intuition; write the
  factors out and let them decide.
- **Do not assume:** that a finding's severity is its symptom's; rate the root
  cause's worst consequence.

## Phase 8: Compare with prior reviews

- **Purpose:** carry finding identity and status across reviews.
- **Actions:** open the ledger ([templates/ledger.md](templates/ledger.md) for
  its format) at the profile's location. Match each current finding to a ledger
  entry by **fingerprint and mechanism**, not by file or line. For every open
  ledger entry whose area is in scope, re-check it at this commit and set its
  status:

    | Status             | When                                                                                              |
    | ------------------ | ------------------------------------------------------------------------------------------------- |
    | `new`              | No ledger entry matches                                                                           |
    | `recurring`        | Matches an entry, and the mechanism is re-confirmed at this commit                                |
    | `fixed`            | A change that removes the mechanism is identified, and a former repro, if any, now passes         |
    | `not-reproducible` | The former evidence no longer reproduces, but no change explaining it is identified               |
    | `disputed`         | The owner, or this review, contests it with recorded counter-evidence                             |
    | `unresolved`       | Not re-checked this review (out of scope, missing capability); the prior status is kept beside it |

    An entry absent from this review's findings is never `fixed` on that ground.
    An entry outside the scope stays `unresolved`.

- **Inputs:** the ledger, the prior reports it links, the current findings.
- **Done when:** every current finding and every in-scope open ledger entry has
  a status with its evidence.
- **Outputs:** the ledger updated; the report's comparison section.
- **Dig deeper when:** a prior report's commit or scope is missing; treat its
  findings as unverified history and re-check before matching.
- **Do not assume:** that a prior report was right; a prior finding found false
  is closed as `disputed` with the refuting evidence.

With no ledger, skip the comparison and create the ledger from this review.

## Phase 9: Report

- **Purpose:** an actionable, honest account.
- **Actions:** write the report with [templates/report.md](templates/report.md)
  at the profile's location. Fill limitations from the working log: every area
  in scope below its planned depth, every check not run and why.
- **Inputs:** everything above.
- **Done when:** every template section is filled or explicitly "none", and the
  report states nothing the working log does not support.
- **Outputs:** the report; the ledger; the hand-offs (SKILL.md, "After the
  review").
- **Do not assume:** that the reader has the working log; the report stands
  alone.

## Stopping rule

Stop investigating an area when any of these holds, and record which:

1. **Satisfied:** its planned depth and verification are reached and every
   loaded standard's questions are answered.
2. **Diminishing returns:** the last few checks produced no new candidate and no
   change in confidence, and nothing in the area is above the plan's depth in
   risk.
3. **Blocked:** a missing capability (platform, hardware, credentials, a
   dependency, time) prevents the next check.

Whatever is open at the stop goes into the report as an open question or a
limitation, with what would close it.
