# What a crash leaves in the log: the fatal signal and a symbolized stack
# (ADR-0047). Abseil symbolizes, being async-signal-safe where the
# alternatives (std::stacktrace, Boost.Stacktrace) allocate.
find_package(absl CONFIG REQUIRED)
add_library(augusta_crash STATIC crash.cpp)
target_include_directories(augusta_crash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_crash PRIVATE absl::stacktrace absl::symbolize)
# crash.cpp writes through <windows.h> there; keep its min/max macros out of the
# way of the standard library.
target_compile_definitions(augusta_crash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_crash SHARED)
