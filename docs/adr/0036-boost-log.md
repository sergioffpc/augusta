# Logging Library: Boost.Log Replaces spdlog

`augusta::logging` writes through Boost.Log instead of spdlog, as part of using
Boost wherever it covers a need (ADR-0035). ADR-0027's other decisions stand:
one process-wide, thread-safe, console-only sink (no file sink; the platform
captures stdout), and a compile-time level tied to the build type. ADR-0029's
line format and levels are unchanged.

What changes underneath:

- The module is a small compiled library. Boost.Log stays behind `logging.cpp`,
  so `logging.h` includes only the standard library and no other translation unit
  pays for Boost.Log's headers.
- The macros (`LT`/`LD`/`LI`/`LW`/`LE`/`LC`) keep their `std::format`-style
  `{}` call syntax, so no call site changed. Each first checks the runtime
  floor, then formats with `std::format` and hands the finished line to
  `augusta::logging::Write`.
- The runtime floor (`augusta::logging::SetLogLevel`, driven by the config
  file's `logging.level`, ADR-0034) is one atomic inside `logging.cpp`, read
  before formatting rather than kept as a Boost.Log core filter, so a call under
  it never evaluates its arguments. `LW_LIMITED` checks it before its throttle,
  so a filtered warning takes no throttle slot.
- Boost.Log has no compile-time level stripping of its own, so
  `AUGUSTA_LOG_ACTIVE_LEVEL` (0 in Debug, 2 otherwise, set publicly by the
  `augusta_logging` target) makes each macro expand to nothing under that level,
  the way `SPDLOG_ACTIVE_LEVEL` did.
- The line is built by a pure function, `FormatLine`, with the level name and its
  ANSI color; Boost.Log's own formatter just calls it. Colors are used only when
  stdout is a terminal (on Windows, the console is switched to interpret them).
- Every record is flushed as written, as spdlog's stdout sink did in practice.

## Considered Options

- **Keeping spdlog**: lighter, faster per message and with compile-time level
  stripping built in. Chosen against here to standardise on Boost (ADR-0035);
  the cost is a much larger dependency closure (Boost.Log pulls in Asio,
  Phoenix, Spirit, Regex, DateTime, ...) and a longer first build.
- **Boost.Log's own `expressions::stream` formatter**: it can print the level
  and time, but not the ANSI colors, and would leave the line format untestable
  without building Boost.Log records; a pure `FormatLine` avoids both.

## Consequences

- The Boost.Log DLLs ship next to each executable on Windows like the other
  vcpkg dependencies.
- Two filters stand in front of the sink: the compile-time level strips a call
  outright, and the runtime floor drops one that compiled in before it formats.
  A per-packet `TRACE` line under the floor costs one atomic read, not a
  formatted string thrown away.
