# Asset pipeline and runtime resources standard

**Objective:** offline tools produce exactly what the runtime reads, packaged
content is verified before use, and runtime resources (files, GPU objects,
loaded assets) have bounded, well-defined lifetimes. Load for asset cooking,
pack or archive formats, virtual filesystems, and runtime asset loading.

## Questions

- **Contract:** is the format defined once, or written by one implementation and
  read by another? What keeps writer and reader in step (shared schema, golden
  files, round-trip tests)? How is a version mismatch detected?
- **Reproducibility:** does cooking the same source give byte-identical output?
  Which inputs (tool versions, timestamps, ordering, platform) leak into the
  output?
- **Integrity:** is every byte the runtime uses covered by verification, and is
  verification done before parsing? What happens on failure?
- **Parsing:** are offsets, sizes and counts in the container checked against
  the file before use? Can entries overlap or point outside the file?
- **Paths:** how are asset paths normalised? Can a path escape its root (`..`,
  absolute paths, case or separator differences across platforms)?
- **Lifetime:** who owns a loaded asset, when is it released, and can a user
  hold it past release? Are GPU or audio resources released on the right thread
  and before their device?
- **Failure:** does a missing or corrupt asset stop the load cleanly, or leave a
  half-initialised state?

## Failure patterns

- Writer and reader disagreeing on alignment or an enum value after one side
  changed.
- Verification of the table of contents but not of the payloads.
- An offset plus size overflowing before the bounds check.
- Platform-dependent ordering (directory iteration, hash maps) making cooked
  output non-reproducible.
- A path lookup that is case-sensitive on one platform and not another.

## Evidence required

The writer and reader code side by side, or the container field and the missing
check, with a concrete malformed input.

## Verification

Golden-file tests between writer and reader; cooking twice and comparing bytes;
fuzzing the loader with sanitizers; a corrupted pack in a test.
