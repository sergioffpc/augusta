# Testing standard

**Objective:** the tests would fail if the behaviour they claim to cover broke,
they cover the project's stated requirements and risky paths, and they give the
same result every run.

## Questions

- For each Critical- or High-tier area: which tests exercise it, and would they
  fail if its key invariant broke? Mentally mutate the code (flip a condition,
  drop a check) and ask whether any test notices.
- Do tests assert on outcomes, or only that nothing crashed?
- Are error paths, boundaries and concurrency tested, or only the happy path?
- Integration: is each contract between modules (writer and reader, client and
  server) tested with both real sides, or only with fakes that could drift?
- Do requirements the project marks as testable have tests that check their
  measure?
- Reproducibility: do tests depend on wall-clock time, sleeps, ports, ordering,
  shared files, network, or random seeds not recorded? Are any skipped,
  quarantined or retried?
- Which sanitizers, fuzzers and property tests run, at which stage, and on what
  platforms? What do they not reach?

## Failure patterns

- A test whose assertion is satisfied by the bug it should catch.
- A fake that accepts input the real implementation rejects.
- A timing-based test that passes by sleeping long enough.
- A fuzzer whose harness rejects most inputs before reaching the parser.
- Coverage that runs code without asserting its result.

## Evidence required

The behaviour left untested, the test that appears to cover it, and why it would
not fail (the assertion it lacks, the input it never uses). Coverage gaps are
`IMPROVEMENT` or `RISK` findings, unless the untested code is also shown to be
wrong.

## Verification

Running the suite (record the command and results); repeating a suspect test
many times; a hand mutation in a throwaway worktree to see whether a test fails;
a coverage report for the area.
