# Boost: Individual Libraries Where the Standard Library Stops

Boost is used one library at a time, each as its own vcpkg port (`boost-dll`,
`boost-interprocess`, ...), never as the umbrella `boost` port: a library is
adopted when the standard library or an existing dependency has nothing that
does the job, and each adoption pays only for that library's own dependency
closure (ADR-0025).

Five libraries are adopted now:

- **Boost.DLL** finds the running executable's directory
  (`boost::dll::program_location()`), which is where `augustac.yaml` /
  `augustad.yaml` live (ADR-0034). `std::filesystem` has no portable way to ask
  for it, and it replaces a hand-written `GetModuleFileNameW` / `/proc/self/exe`
  pair.
- **Boost.Interprocess** memory-maps pack files (`file_mapping` +
  `mapped_region`, read-only), replacing mio in `augusta::assets` (ADR-0031). It
  is header-only, has a `wchar_t` path overload so non-ASCII pack paths open on
  Windows, and drops a dependency that only did this one job.
- **Boost.Program_options** parses the command line of `augustac` and `augustad`
  (`--config <file>`, `--help` and `--version`, ADR-0034) in `augusta_config`,
  in place of comparing `argv` by hand. It is a compiled library. Its defaults
  are looser than the config file's own strictness (prefix guessing turns
  `--conf` into `--config`; a bare argument is ignored), so the parser turns
  guessing off and registers an empty positional description to keep every other
  argument a usage error.
- **Boost.Beast** serves `augustad`'s metrics endpoint, `/metrics` and `/livez`
  (ADR-0049), on Boost.Asio. It is header-only, and its HTTP parsing and
  responses leave the endpoint only its routing to write; prometheus-cpp's own
  HTTP server cannot answer `/livez`'s `503`.
- **Boost.Uuid** takes the Run, Session and Match IDs (GLOSSARY.md) in
  `augusta_uuid`, each a UUIDv7 (RFC 9562) from its `time_generator_v7`, which
  needs Boost 1.86 or newer. A UUIDv7 is unique across every machine with no
  coordination, and starts with its time, so IDs, and the files and lines named
  by them, sort by when they were taken. One generator serves the process under
  a lock, so each ID it takes sorts after the one before. The module's own type
  holds the 16 bytes and writes the canonical lowercase hyphenated form, so no
  other translation unit includes Boost.Uuid. It is header-only.

Boost.DLL pulls in Boost.Filesystem and Boost.System, which are compiled
libraries; they ship as DLLs next to each executable on Windows like every other
vcpkg dependency.

## Considered Options

- **Keeping mio and writing the executable-directory lookup by hand**: works,
  but leaves two single-purpose pieces where one already-needed dependency
  covers both.
- **Comparing `argv` by hand, or CLI11 / cxxopts** for the command line: the
  hand-written comparison was correct for one option but does not grow; CLI11
  and cxxopts are smaller, but each is one more unrelated dependency where a
  Boost library, adopted one at a time under this ADR, already does the job.
- **whereami** (executable path) and **Boost.Iostreams** `mapped_file_source`
  (mmap): whereami is smaller than Boost.DLL but is one more unrelated
  dependency; Boost.Iostreams is a compiled library with a much larger
  dependency closure than Interprocess for the same read-only mapping.
- **Boost.Log instead of spdlog** (ADR-0027): adopted separately, see ADR-0036.
- **A random UUIDv4, or a hand-written UUIDv7**, for the IDs: a UUIDv4 is as
  unique but sorts by nothing, so a folder of Runs or a page of lines would not
  sort by time; a hand-written UUIDv7 is a few dozen lines, but its monotonic
  counter and its randomness source are where it would go wrong, and Boost.Uuid
  has both, under this ADR's one-library-at-a-time rule.
