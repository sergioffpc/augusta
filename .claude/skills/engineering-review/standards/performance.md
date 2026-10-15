# Performance standard

**Objective:** hot paths meet the project's stated budgets with margin, and
resource use is bounded. A finding is anchored to a stated budget (a
requirement, a frame or tick time) or to a scale the project expects.

## Label every observation

As defined in SKILL.md: **defect** (measured, or analysis against a budget),
**risk** (breaches a budget at a stated scale), **opportunity** (plausible,
unmeasured), **hypothesis** (needs profiling). A defect is a `BUG`, a risk a
`RISK`; an opportunity or hypothesis is at most an `IMPROVEMENT` or a
Low-confidence `RISK`.

## Questions

- What runs per tick, per frame, per message, per entity? What is its complexity
  in the quantities that grow (players, entities, messages, history length)?
- What allocates on those paths? Is the allocation amortised, pooled, or per
  call?
- Is data accessed in an order the cache can follow, or through scattered
  pointers?
- Which locks or atomics do hot paths share between threads, and how often?
- What grows without bound: queues, histories, caches, logs, metric label sets?
  What evicts or caps each?
- What blocks the hot path: I/O, logging, synchronous waits on another thread?
- Is there an existing benchmark for the path, and does it reflect the real
  input size?

## Failure patterns

- An O(n²) loop over entities or peers inside the tick.
- Per-message heap allocation or string formatting on the network path.
- Logging or metric registration in a hot loop.
- An unbounded queue between a fast producer and a slow consumer.
- A history buffer sized by time with no cap on entries.
- A benchmark measuring a size or state far from production's.

## Evidence required

For a defect: the measurement (tool, build type, input, numbers) or the analysis
(operation counts at a stated scale against the budget). For a recommendation:
the problem it solves, its expected effect, and its cost (complexity, memory,
coupling, a new failure mode).

## Verification

The project's micro-benchmarks at realistic sizes; a profiler capture of the
path; counting allocations under a hook. Measurements are taken on an optimised
build and the build type, hardware and run-to-run variance are recorded. A
measurement on a debug build supports nothing.
