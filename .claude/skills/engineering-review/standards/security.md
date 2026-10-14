# Security and robustness standard

**Objective:** nothing an untrusted party controls can corrupt memory, bypass
authority or integrity, read what it should not, or exhaust resources beyond a
stated bound.

## Questions

- Draw the **trust boundaries**: every input from outside the process (network,
  files, packs, configs, scripts, environment, command line) and who controls
  each. Which does the project treat as trusted, and is that documented?
- At each boundary: is every field validated (length, range, enum, count,
  cross-field consistency) before use? Is validation complete before any state
  changes?
- Can a peer cause work or memory out of proportion to what it sends
  (amplification, unbounded allocation from a length field, quadratic parsing)?
  What rate limits, caps and timeouts exist?
- Authority: can a client cause any state change the server has not validated?
  Can one peer affect another's session?
- Integrity: is signed or hashed content verified before any byte of it is used?
  Is the verified buffer the one that is used (no re-read)?
- Embedded interpreters and plugins: what can sandboxed code reach? Which host
  bindings expose I/O, unbounded allocation or unbounded time?
- Secrets: are keys, tokens and credentials kept out of the repository, logs,
  crash reports and messages to peers?
- What does an error message tell an attacker?

## Failure patterns

- A length or count from input used for allocation or indexing before a bound
  check.
- Validation of a copy, then use of the original.
- A misbehaviour counter that a peer can keep just below its threshold while
  still exhausting resources.
- A sandbox missing one dangerous global, or a binding that reaches the
  filesystem.
- Signature checked over metadata but not over the payload.

## Evidence required

The input an attacker controls, the path from the boundary to the effect, every
check on that path, and the attacker's capability required (local, LAN,
internet). Exposure sets the severity factors.

## Verification

Fuzzing the decoder or loader with sanitizers; a crafted input in a test in a
throwaway worktree; enumerating a sandbox's environment from inside a script;
dependency vulnerability scans already in CI (record their findings, do not
re-derive them).
