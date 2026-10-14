---
name: engineering-review
description:
    Evidence-driven review of a repository, subsystem or flow at a fixed commit
    (not a diff), producing a findings report and an updated findings ledger.
disable-model-invocation: true
---

# Engineering review

A review of code **as it stands at one commit**: a whole repository, a
subsystem, or one end-to-end flow. For changes since a point (a branch, a PR),
use `/code-review` instead; this skill audits standing code.

The output is a report and an updated findings ledger. Every claim in it is
traceable to evidence a reader can check.

## Read-only

The review changes no application source, build configuration, test, or runtime
behaviour. Its only writes to the repository are the report and the ledger, at
the location the project profile names. Experiments that need code (a
reproduction test, a fuzz harness, an instrumented build) live in a throwaway
worktree or the session scratchpad, are described in the report, and are
discarded; fixing what they find is a separate, explicitly authorised task.

This is enforced by these instructions alone, not by tool permissions.

## Before starting

1. Read the **project profile**: `docs/agents/engineering-review.md` in this
   repository (in another repository, the equivalent file its agent instructions
   name). It maps subsystems to risk tiers and standards, names the project's
   guarantees, its verification tools and where reports live. With no profile,
   build a provisional one in Phase 2 of the runbook and record it in the report
   as unconfirmed.
2. Follow [runbook.md](runbook.md), phase by phase. It is the procedure; this
   file is the vocabulary it uses.

## Claims

Every statement of evidence carries one tag:

- **Observed**: read directly in code, config, docs or tool output; cited as
  `path:line` with the symbol.
- **Derived**: follows from Observed facts by reasoning written out in the
  finding.
- **Hypothesis**: plausible, consistent with what was read, not established.
- **Unverified**: a concern raised but not investigated.

No test result, measurement, platform behaviour or guarantee is stated unless it
was Observed. "No defect found" in an area is reported as what was examined and
how, never as "correct".

## Depth and verification

Two independent axes. Depth is how far the code was read; verification is how
strongly a conclusion was checked. Record both for every area and finding.

| Depth | Scope                                                                                       | Deliverable                                                         | Cannot claim                                  |
| ----- | ------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- | --------------------------------------------- |
| L1    | Reconnaissance: structure, dependencies, entry points, build, docs                          | Module map, risk register, depth plan                               | Anything about behaviour inside a component   |
| L2    | Component: implementations, contracts, invariants, error paths, ownership, tests            | Per-component findings, contract notes                              | Behaviour across module or thread boundaries  |
| L3    | Critical path: end-to-end data and control flow, state transitions, failure and concurrency | Traced flows with cited hops, cross-module findings                 | That a traced path behaves as read at runtime |
| L4    | Adversarial: targeted tests, stress, fuzzing, sanitizers, static analysis, fault injection  | Reproductions or recorded failed attempts, with commands and output | That no other input breaks it                 |

| Verification | Meaning                                                                       |
| ------------ | ----------------------------------------------------------------------------- |
| V0           | Read only                                                                     |
| V1           | Traced: every relevant caller, callee and consumer inspected                  |
| V2           | Tool-checked: existing tests, static analysis or a build run, output recorded |
| V3           | Demonstrated: a targeted reproduction, sanitizer report or measurement        |

Choose depth **per subsystem by its risk tier**, not uniformly: L3 and L4 go to
the paths whose failure costs most (the profile's tiers), L1 or L2 to the rest.

## Classifying a finding

**Category**, by what is established:

- `BUG`: incorrect behaviour demonstrated (V3) or following from a complete
  trace with no unverified step (V1 or better). Always High confidence; anything
  less is a `RISK`.
- `RISK`: a plausible failure whose mechanism is not fully demonstrated.
- `DESIGN`: a material problem in architecture, boundaries, abstractions or
  contracts, with a named concrete consequence (a defect class it invites, a
  requirement it blocks).
- `IMPROVEMENT`: a justified change with no demonstrated defect. Severity at
  most Medium.

A preference, a style disagreement, or a concern with no mechanism is not a
finding. Style the project's formatters and linters enforce is out of scope.

**Severity** rates the consequence **if the mechanism is real**:

- **Critical**: breaks a core guarantee (authority, integrity, memory safety,
  liveness of the main loop, persistent data) with severe impact, reachable in a
  supported configuration without unusual preconditions, and no effective
  mitigation.
- **High**: serious incorrect behaviour or failure under plausible conditions;
  or Critical impact that needs meaningful preconditions.
- **Medium**: bounded impact (one session, one feature, degraded but
  recoverable), or High impact under unlikely conditions; or a design flaw that
  will predictably produce defects.
- **Low**: minor, narrow or easily recovered impact.

Start from impact, then move **down one level** for each substantial limiting
factor: exposure only to trusted or local actors, development-only builds or
platforms, an effective existing mitigation, a narrow scope. Never move up past
the impact. Record which factors were applied.

**Confidence** rates how sure the review is that the mechanism is real:

- **High**: verified (V3), or traced (V1+) with every assumption checked.
- **Medium**: follows from the code read, but depends on at least one named,
  unchecked assumption.
- **Low**: plausible from partial evidence; key steps unverified.

A severe but weakly supported concern is reported as a `RISK` with its real
confidence and the evidence that would confirm or dismiss it, after an honest
attempt to raise that confidence.

## Before a finding is written

A finding states its evidence, its failure mechanism, its impact, and why the
current code does not prevent it. Before writing one:

- Inspect every caller and consumer that the mechanism needs, and check whether
  an invariant, a contract, a caller, a type or a surrounding mechanism already
  prevents it.
- Check the build configuration, platform and runtime assumptions it depends on
  (a debug-only assert, a Linux-only path, a compile-time flag).
- Find the **root cause**: several symptoms of one cause are one finding, with
  the symptoms listed in it.
- Tie each recommendation to the project's requirements or a demonstrated
  problem, never to general good practice.
- When it remains open, state what evidence would confirm or dismiss it.

Fewer, substantiated findings beat many speculative ones.

## Performance claims

Label each performance observation as one of: **defect** (measured, or a
defensible analysis against a stated budget), **risk** (complexity or
architecture that will breach a budget at a stated scale), **opportunity**
(plausible gain, unmeasured), **hypothesis** (needs profiling to say anything).
No gain is claimed without a measurement or written analysis. A recommendation
of caching, parallelism, an abstraction or a data-structure change names the
problem it solves and its cost. Details:
[standards/performance.md](standards/performance.md).

## Standards

Load a standard when the depth plan reaches its dimension; the profile says
which ones apply to which subsystem. A dimension whose functionality the project
lacks is skipped and recorded as not applicable.

| Dimension                                                      | Standard                                     |
| -------------------------------------------------------------- | -------------------------------------------- |
| Architecture, boundaries, coupling, cross-module consistency   | [architecture.md](standards/architecture.md) |
| Logic, invariants, state machines, edge cases, API contracts   | [correctness.md](standards/correctness.md)   |
| C++ language: ownership, lifetime, UB, types                   | [cpp.md](standards/cpp.md)                   |
| Concurrency, synchronisation, shutdown                         | [concurrency.md](standards/concurrency.md)   |
| Performance and resource use                                   | [performance.md](standards/performance.md)   |
| Error handling, recovery, resources, observability             | [errors.md](standards/errors.md)             |
| Tests and reproducibility                                      | [testing.md](standards/testing.md)           |
| Security, input validation, trust boundaries, exhaustion       | [security.md](standards/security.md)         |
| Build, dependencies, configuration, deployment                 | [build-deploy.md](standards/build-deploy.md) |
| Networking, protocol, serialization                            | [networking.md](standards/networking.md)     |
| Authoritative simulation, prediction, replication, physics     | [simulation.md](standards/simulation.md)     |
| Asset pipeline, packaging, virtual filesystems, runtime assets | [assets.md](standards/assets.md)             |

Each standard holds the dimension's objectives, questions, failure patterns,
required evidence and verification techniques.

## Templates

- [templates/finding.md](templates/finding.md): one finding.
- [templates/report.md](templates/report.md): the review report.
- [templates/ledger.md](templates/ledger.md): the findings ledger that carries
  finding identity and status across reviews.

## After the review

Name the hand-offs, do not start them: `/triage` to turn chosen findings into
issues, `/diagnosing-bugs` to confirm a `RISK` that needs a live repro,
`/improve-codebase-architecture` for `DESIGN` findings.
