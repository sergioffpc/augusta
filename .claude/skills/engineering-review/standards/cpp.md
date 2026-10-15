# C++ standard

**Objective:** C++ code free of undefined behaviour, with ownership and lifetime
visible in its types, and APIs whose signatures carry their contracts. Load only
for C++ code. Project rules (style guide, error-handling policy, ownership
defaults) are in the project's own standards; check against those, not against
general preference.

## Questions

- **Lifetime:** can any reference, pointer, `string_view`, `span` or iterator
  outlive its referent? Check returns of views into temporaries, captures in
  lambdas stored beyond the call, iterators across container mutation, `this`
  captured by a callback registered elsewhere.
- **Ownership:** is every owning raw pointer or handle wrapped? Is `shared_ptr`
  used where the lifetime is in fact unique? Are cycles of `shared_ptr`
  possible?
- **Moves:** is any object used after being moved from, other than assigned or
  destroyed? Does a move leave the source in a state its destructor handles?
- **Special members:** does a type that owns a resource define or delete copy
  and move consistently? Is a destructor that can run during unwinding
  `noexcept` in practice?
- **Undefined behaviour:** signed overflow, shifts past width, misaligned or
  type-punned access (`reinterpret_cast` on wire buffers), uninitialised reads,
  out-of-bounds indexing, data races, `std::optional`/`std::expected` access
  without a check.
- **Integers:** narrowing between sizes and signedness at API boundaries,
  especially lengths and counts that come from outside.
- **API design:** do parameter types say who owns what (`unique_ptr` by value
  transfers, `const&` borrows)? Is `[[nodiscard]]` on results that must be
  checked? Do overload sets or implicit conversions admit wrong calls?
- **Exceptions:** which functions can throw, and does every caller on a no-throw
  path know it? Is exception safety at least basic where state is mutated?
- **Static initialisation:** does any global depend on another translation
  unit's global?

## Failure patterns

- A `string_view` or `span` member stored from a temporary argument.
- A callback capturing `this` that runs after the object is destroyed.
- `vector::push_back` invalidating a pointer held elsewhere.
- `size_t` length from untrusted input converted to `int`, or added before a
  bounds check.
- An error result ignored because the function lacked `[[nodiscard]]`.
- `std::move` of a member in a loop body.

## Evidence required

The construction and destruction points of the object, or the arithmetic with
the input ranges that reach it. For UB, the exact operation and a reachable
input; "may be UB" with no reachable input is a `RISK` at most.

## Verification

ASan and UBSan builds running the relevant tests; clang-tidy checks (for example
`bugprone-*`, `cppcoreguidelines-*`) over the files; a minimal reproduction
under a sanitizer in a throwaway worktree.
