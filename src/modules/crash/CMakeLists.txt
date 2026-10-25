# What a crash leaves in the log: the fatal signal and a symbolized stack
# (ADR-0047). Abseil symbolizes on Linux and macOS, being async-signal-safe
# where the alternatives (std::stacktrace, Boost.Stacktrace) allocate. On
# Windows dbghelp writes the minidump (ADR-0055). crash.cpp formats the lines on every platform;
# the platform's own file catches the crash and writes them (crash_platform.h).
find_package(absl CONFIG REQUIRED)
add_library(
  augusta_crash
  STATIC
  crash.cpp
  $<$<PLATFORM_ID:Windows>:crash_windows.cpp>
  $<$<NOT:$<PLATFORM_ID:Windows>>:crash_posix.cpp>
)
target_include_directories(augusta_crash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Its lines end in logging's context, which logging keeps for it to read
# without a lock (logging::CopyContext). Its settings name the Run by its
# Boost.Uuid ID, and are kept past exit in an absl::NoDestructor. The halves
# hand each other a stack's capture as an absl::FunctionRef, which allocates
# nothing.
target_link_libraries(
  augusta_crash
  PUBLIC Boost::uuid
  PRIVATE
    augusta_logging
    absl::no_destructor
    absl::function_ref
    $<$<NOT:$<PLATFORM_ID:Windows>>:absl::symbolize>
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
# crash_windows.cpp writes through <windows.h>; keep its min/max macros out of
# the way of the standard library.
target_compile_definitions(augusta_crash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_crash SHARED)
