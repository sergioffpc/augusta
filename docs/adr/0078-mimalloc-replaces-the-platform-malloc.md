# mimalloc Replaces the Platform's malloc

`augustac` and `augustad` replace the platform's malloc/free and new/delete with
mimalloc's, for the whole process, on Windows, Linux and macOS. The allocator
benchmarks (ADR-0013) measured mimalloc among the fastest on both platforms they
ran: 1.7 to 6 times faster than the platform's allocator at freeing blocks on
another thread than the one that allocated them, 5 to 12% faster at the server
tick, replication and Replay ticks, and about 20% faster across every benchmark
by geometric mean. No other candidate ranked near the top on both Windows and
Linux, so one allocator serves every platform.

**Where it replaces malloc.** In the two executables alone. The tools and the
test executables keep the platform's allocator, apart from the allocator
benchmarks' own candidates. Every preset links it except those whose sanitizer
brings its own allocator (`linux-san`, `linux-tsan`, `linux-fuzz`), which leave
it out on their own, with no option to turn it off by hand: the debug presets
run the allocator the release runs. macOS, with no release and no measurement,
takes it too, by the same mechanism as Linux.

**How it replaces malloc.** On Linux and macOS, where vcpkg links its ports
statically, mimalloc's static library replaces the C library's malloc at link
time. On Windows, its DLL's `mimalloc-redirect.dll` redirects the CRT's malloc,
the other DLLs' included, as the process starts, which only happens when the
executable loads `mimalloc.dll` first of its DLLs; each executable's `main.cpp`
defines new and delete as mimalloc's, as mimalloc asks of a program its DLL
redirects. Both DLLs ship in the client's runtime folder (ADR-0058). mimalloc,
with its override, is one of the vcpkg manifest's base dependencies.

**It refuses to run without it.** A redirect that did not happen, or a link that
left the C library's malloc in place, fails silently, so each executable checks
first thing in `main`, before it reads its command line: the process must be
redirected (Windows) and a block malloc has just handed out must be mimalloc's.
Otherwise it writes one line to stderr and exits nonzero, before any log or Run
exists. `--version` runs the check too, so `scripts/stage-client.sh` catches a
broken redirect on every pull request. Only the two executables link mimalloc:
its static library holds its API and its override in one object, so a library
that called it would replace malloc in every test executable that links that
library. So `main` reads mimalloc's facts (redirected, a block in its heap, its
version) into a plain value, and the `application` module decides from it. Each
Runtime's startup line names the allocator and its version (`hardware`), so a
bug report says what it ran on.

**Its defaults, untuned.** mimalloc's peak memory ran about 40 MiB above the
platform's on Windows and 13 MiB above on Linux. Its options (purge delay,
arenas) stay at their defaults until a profile shows a concrete need. Flecs' and
PhysX's own allocators stay as they are; what they ask of malloc, mimalloc now
serves.

## Considered Options

- **The platform's allocator**: no dependency and no DLLs to ship, but 4 to 8
  times slower than every candidate at cross-thread frees on Linux (glibc), and
  slower than mimalloc at the server tick on both platforms.
- **jemalloc on Linux, mimalloc on Windows**: jemalloc was as fast as mimalloc
  on Linux at less memory than glibc, but it has no Windows build that replaces
  malloc, so the two platforms would run different allocators.
- **snmalloc**: the fastest on Linux, and at cross-thread frees above all, but
  it took 40% more memory than glibc and does not replace malloc on Windows.
- **oneTBB's scalable allocator**: second on Windows, but 19% slower than glibc
  at replication on Linux, and its proxy does not run on macOS.

## Consequences

- The Windows client zip holds two more DLLs; a build that drops or reorders
  them fails the zip's own `--version` check, not a player's machine.
- Allocation behaviour differs between the sanitizer presets and every other; a
  bug that depends on the allocator reproduces outside the sanitizers only.
- The allocator benchmarks keep every candidate, the platform's allocator as the
  baseline, to answer whether mimalloc is still the right choice.
