# Boost: Individual Libraries Where the Standard Library Stops

Boost is used one library at a time, each as its own vcpkg port (`boost-dll`,
`boost-interprocess`, ...), never as the umbrella `boost` port: a library is
adopted when the standard library or an existing dependency has nothing that
does the job, and each adoption pays only for that library's own dependency
closure (ADR-0025).

Two libraries are adopted now:

- **Boost.DLL** finds the running executable's directory
  (`boost::dll::program_location()`), which is where `augustac.yaml` /
  `augustad.yaml` live (ADR-0034). `std::filesystem` has no portable way to ask
  for it, and it replaces a hand-written `GetModuleFileNameW` /
  `/proc/self/exe` pair.
- **Boost.Interprocess** memory-maps pack files (`file_mapping` +
  `mapped_region`, read-only), replacing mio in `augusta::assets` (ADR-0031).
  It is header-only, has a `wchar_t` path overload so non-ASCII pack paths open
  on Windows, and drops a dependency that only did this one job.

Boost.DLL pulls in Boost.Filesystem and Boost.System, which are compiled
libraries; they ship as DLLs next to each executable on Windows like every other
vcpkg dependency.

## Considered Options

- **Keeping mio and writing the executable-directory lookup by hand**: works,
  but leaves two single-purpose pieces where one already-needed dependency
  covers both.
- **whereami** (executable path) and **Boost.Iostreams** `mapped_file_source`
  (mmap): whereami is smaller than Boost.DLL but is one more unrelated
  dependency; Boost.Iostreams is a compiled library with a much larger
  dependency closure than Interprocess for the same read-only mapping.
- **Boost.Log instead of spdlog** (ADR-0027): adopted separately, see ADR-0036.
