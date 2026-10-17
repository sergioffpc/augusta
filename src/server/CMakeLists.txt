# Whether the tick loop is still running, which /livez answers; pure, so tested
# without HTTP (see liveness.h).
add_library(augustad_liveness STATIC liveness.cpp)
target_include_directories(augustad_liveness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_liveness PUBLIC augusta_gameplay)
augusta_set_module_scope(augustad_liveness SERVER_ONLY)

# The HTTP endpoint serving /metrics and /livez (see metrics.h, ADR-0049):
# prometheus-cpp's core keeps and formats the metrics, Boost.Beast serves them;
# its permanent failure is a subsystem one in the shared error model (ADR-0033).
find_package(prometheus-cpp CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
add_library(augustad_metrics STATIC metrics.cpp)
target_include_directories(augustad_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_metrics
  PUBLIC augusta_failure augusta_gameplay prometheus-cpp::core
  PRIVATE augustad_liveness augusta_logging augusta_version Boost::beast
)
# std::getenv reads the commit the image stamps in; MSVC deprecates it for
# _dupenv_s, which has no portable equivalent. Asio wants the Windows version
# it targets named, or it assumes Windows 7 (and says so on every build).
target_compile_definitions(
  augustad_metrics
  PRIVATE _CRT_SECURE_NO_WARNINGS
  PUBLIC $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
)
augusta_set_module_scope(augustad_metrics SERVER_ONLY)

# Every client's Connection health as metrics (see connection_health.h,
# ADR-0049); tested by collecting it, without a network.
add_library(augustad_connection_health STATIC connection_health.cpp)
target_include_directories(augustad_connection_health PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_connection_health
  PUBLIC augusta_host augusta_metrics augusta_networking augusta_host_match
  PRIVATE augusta_limits
)
augusta_set_module_scope(augustad_connection_health SERVER_ONLY)

add_library(augustad_runtime STATIC runtime.cpp)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_runtime
  PUBLIC
    augusta_host
    augusta_host_replay
    augusta_failure
    augusta_gameplay
    augusta_networking
    augusta_physics
    augusta_simulation
  PRIVATE augusta_assets augusta_logging augusta_supervisor augustad_connection_health augustad_metrics
)
augusta_set_module_scope(augustad_runtime SERVER_ONLY)

# augustad's phases behind the application boundary - config, transport,
# content, runtime - each classifying its own failure (see application.h,
# ADR-0033); tested with the golden server pack and injected faults.
add_library(augustad_application STATIC application.cpp)
target_include_directories(augustad_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_application
  PUBLIC augusta_application augusta_config augusta_failure augusta_server_config augustad_runtime
  PRIVATE augusta_assets augusta_logging augusta_networking augusta_scenario augusta_scenario_policy_loader augusta_host
)
augusta_set_module_scope(augustad_application SERVER_ONLY)

# What a crash leaves in the log: the fatal signal and a symbolized stack
# (ADR-0047). Abseil symbolizes, being async-signal-safe where the
# alternatives (std::stacktrace, Boost.Stacktrace) allocate.
find_package(absl CONFIG REQUIRED)
add_library(augustad_crash STATIC crash.cpp)
target_include_directories(augustad_crash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_crash PRIVATE absl::stacktrace absl::symbolize)
# crash.cpp writes through <windows.h> there; keep its min/max macros out of the
# way of the standard library.
target_compile_definitions(augustad_crash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augustad_crash SERVER_ONLY)

add_executable(augustad main.cpp)
target_link_libraries(
  augustad
  PRIVATE
    augusta_application
    augusta_config
    augusta_server_config
    augustad_application
    augustad_crash
    augusta_version
    augusta_logging
    augustad_runtime
)
# The headless server itself, not only its libraries: a client-only module
# linked straight into it fails the configure (tests/server_boundary.cmake).
augusta_set_module_scope(augustad SERVER_ONLY)
# This directory is augustad's Glue: only augustad, augustad_application and
# augustad_runtime belong in it, and a new library here fails the configure
# (ADR-0060, cmake/AugustaModule.cmake). The libraries above are the
# exceptions the migration has still to move out.
augusta_set_executable_glue(augustad)

# On Linux the debug info is split off into augustad.debug beside it (ADR-0047).
if(NOT WIN32)
  augusta_split_debug_info(augustad)
endif()

# `cmake --install` puts augustad in the prefix's bin directory (DESTDIR
# honoured), which is how the Dockerfile's image gets it. On Windows the vcpkg
# DLLs it loads go beside it, as the build tree has them; CMake resolves them
# itself, rather than vcpkg's applocal helper, so they are in the install
# manifest `make uninstall` removes. On Linux vcpkg links them statically.
if(WIN32)
  install(TARGETS augustad RUNTIME_DEPENDENCY_SET augustad_dlls RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(
    RUNTIME_DEPENDENCY_SET augustad_dlls
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\][Ss]ystem32[/\\]"
    DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
else()
  install(TARGETS augustad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
