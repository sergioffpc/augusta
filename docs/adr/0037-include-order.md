# Include Order: Main Header, Standard Library, Third-Party, Project

Refines [ADR-0012](0012-coding-style.md), which adopts the Google C++ Style Guide.
Every `.cpp` and `.h` orders its `#include`s in four blocks, separated by one blank
line, each block sorted alphabetically:

1. **Main header** — a `.cpp`'s own header (`networking.cpp` → `"augusta/networking.h"`;
   for a `*_test.cpp`, the header under test). Alone in its block, so a header that
   isn't self-contained fails to compile in its own translation unit first. Files with
   no main header (`main.cpp`, tests of several headers) start at block 2.
2. **C++ standard library** — `<vector>`, `<cstdint>`. Use the `<c…>` wrappers, never
   `<stdint.h>`-style C headers.
3. **Third-party and system** — everything else in angle brackets: `<boost/…>`,
   `<steam/…>`, `<Falcor.h>`, `<nvtx3/…>`, `<gtest/gtest.h>`, `<flecs.h>`.
4. **Project** — everything in this repo, in quotes: `"augusta/<module>.h"` for a
   module's public header, `"decoder.h"` for one private to the module or executable.

Rules `clang-format` can't check:

- **Angle brackets mean "not ours", quotes mean "ours".** Public module headers are
  always spelled `"augusta/<module>.h"` — no `../` paths.
- **Include what you use.** A file includes the header of every symbol it names
  directly and doesn't lean on another header's transitive includes.
- **Platform-conditional includes** (`#ifdef _WIN32 … #include <windows.h>`) go in
  their own block after all unconditional ones; `clang-format` doesn't reorder
  across `#if`.
- **A trailing comment on an include** is for a non-obvious *why* (a header included
  only for an inline body it defines), as in `networking.cpp`.

## Why not Google's stock order

Google's own `.clang-format` sorts by the `.h` suffix — `<*.h>` first, then every
`<…>` without one. Here that put `<yaml-cpp/yaml.h>` and `<steam/…>` ahead of the
standard library but `<boost/…hpp>` and `<nvtx3/nvtx3.hpp>` among it, so the order
depended on a file extension. The standard library block is instead recognised by
having no extension and no `/`, which puts every third-party header in one group
whatever it is called. Standard library before third-party follows the style guide
itself.

## Enforcement

`.clang-format` (`IncludeBlocks: Regroup` plus three `IncludeCategories`) sorts and
groups; CI already runs `clang-format --dry-run --Werror`, so a misordered include
fails the build. Include-what-you-use and conditional-include placement are left to
review, like the rest of `docs/agents/coding-standards.md`.
