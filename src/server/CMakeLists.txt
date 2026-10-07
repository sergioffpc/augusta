# Reads the Parameters script through Lua (ADR-0022, ADR-0039) into the shared
# Parameters; pure, so tested with scripts as strings (see parameters_loader.h).
# Server-only: a client never reads the script, it is sent the result, so it
# never links the interpreter.
add_library(augustad_parameters_loader STATIC parameters_loader.cpp)
target_include_directories(augustad_parameters_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_parameters_loader PUBLIC augusta_parameters PRIVATE augusta_lua_sandbox)
augusta_set_module_scope(augustad_parameters_loader SERVER_ONLY)

# Reads the scenario's Game policy scripts out of the server pack into the
# engine SimulationWorld runs them in (ADR-0022); tested with packs built in the
# test (see policy_loader.h).
add_library(augustad_policy_loader STATIC policy_loader.cpp)
target_include_directories(augustad_policy_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_policy_loader PUBLIC augusta_assets augusta_scripting)
augusta_set_module_scope(augustad_policy_loader SERVER_ONLY)

# What the server loads from its verified pack at startup: the scenario, the
# Parameters and the Game policy (see content.h).
add_library(augustad_content STATIC content.cpp)
target_include_directories(augustad_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_content
  PUBLIC augusta_assets augusta_math augusta_parameters augusta_physics augusta_scripting
  PRIVATE augusta_logging augusta_map augustad_parameters_loader augustad_policy_loader
)
augusta_set_module_scope(augustad_content SERVER_ONLY)

# Who is in the match and whether a join is admitted; pure, so tested without a
# network (see match.h).
add_library(augustad_match STATIC match.cpp)
target_include_directories(augustad_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_match PUBLIC augusta_assets augusta_math augusta_networking augusta_physics)
augusta_set_module_scope(augustad_match SERVER_ONLY)

# The sanity gate and per-player queue for what clients send to move; pure, so
# tested without a network (see command_queue.h).
add_library(augustad_commands STATIC command_queue.cpp)
target_include_directories(augustad_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_commands PUBLIC augusta_command)
augusta_set_module_scope(augustad_commands SERVER_ONLY)

# The count of each peer's misbehaviour that decides when it is disconnected;
# pure, so tested without a network (see misbehaviour.h).
add_library(augustad_misbehaviour STATIC misbehaviour.cpp)
target_include_directories(augustad_misbehaviour PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
augusta_set_module_scope(augustad_misbehaviour SERVER_ONLY)

# When a peer that has not been admitted is past its deadline; pure, so tested
# without a network (see admission.h).
add_library(augustad_admission STATIC admission.cpp)
target_include_directories(augustad_admission PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_admission PUBLIC augusta_networking)
augusta_set_module_scope(augustad_admission SERVER_ONLY)

# The counts behind the server's once-a-second heartbeat line; pure, so tested
# without a network (see heartbeat.h).
add_library(augustad_heartbeat STATIC heartbeat.cpp)
target_include_directories(augustad_heartbeat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
augusta_set_module_scope(augustad_heartbeat SERVER_ONLY)

# Whether the tick loop is still running, which /livez answers; pure, so tested
# without HTTP (see liveness.h).
add_library(augustad_liveness STATIC liveness.cpp)
target_include_directories(augustad_liveness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_liveness PUBLIC augusta_tick)
augusta_set_module_scope(augustad_liveness SERVER_ONLY)

# The lock-free counter, gauge and histogram the server's threads write in
# place, and their Prometheus form (see lock_free_metrics.h, ADR-0049).
find_package(prometheus-cpp CONFIG REQUIRED)
add_library(augustad_lock_free_metrics STATIC lock_free_metrics.cpp)
target_include_directories(augustad_lock_free_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_lock_free_metrics PUBLIC prometheus-cpp::core)
augusta_set_module_scope(augustad_lock_free_metrics SERVER_ONLY)

# The HTTP endpoint serving /metrics and /livez (see metrics.h, ADR-0049):
# prometheus-cpp's core keeps and formats the metrics, Boost.Beast serves them.
find_package(prometheus-cpp CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
add_library(augustad_metrics STATIC metrics.cpp)
target_include_directories(augustad_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_metrics
  PUBLIC augusta_tick prometheus-cpp::core
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
  PUBLIC augusta_networking augustad_match augustad_lock_free_metrics
  PRIVATE augusta_protocol
)
augusta_set_module_scope(augustad_connection_health SERVER_ONLY)

# The server's socket and SimulationWorld without threads or a clock, so a test
# can drive a match by hand (see host.h), and the conversions Host makes at its
# edges: between the protocol's plain types and the engine's (see wire.h), and
# into SimulationWorld's (see simulation_mapping.h); with the match recording
# Host writes (see recording.h, ADR-0048), which tools/replay replays, and what
# it counts for the metrics endpoint (see host_metrics.h, ADR-0049).
add_library(
  augustad_host
  STATIC
  host.cpp
  host_log.cpp
  host_match.cpp
  host_metrics.cpp
  host_network.cpp
  recording.cpp
  simulation_mapping.cpp
  tick_messages.cpp
  wire.cpp
)
target_include_directories(augustad_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_host
  PUBLIC
    augusta_ballistics
    augusta_networking
    augusta_parameters
    augusta_physics
    augusta_simulation
    augustad_content
    augustad_commands
    augustad_match
    augustad_misbehaviour
    augustad_admission
    augustad_heartbeat
    augusta_assets
    augusta_protocol
    augusta_replication
    augusta_tick
    augustad_lock_free_metrics
    prometheus-cpp::core
  PRIVATE augusta_logging augusta_map augusta_version
)
augusta_set_module_scope(augustad_host SERVER_ONLY)

add_library(augustad_runtime STATIC runtime.cpp)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augustad_runtime
  PUBLIC augustad_host augusta_networking augusta_parameters augusta_physics augusta_simulation augusta_supervisor
  PRIVATE augusta_assets augusta_logging augusta_tick augustad_connection_health augustad_metrics
)
augusta_set_module_scope(augustad_runtime SERVER_ONLY)

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
    augusta_assets
    augusta_config
    augusta_server_config
    augustad_content
    augustad_crash
    augusta_parameters
    augusta_version
    augusta_logging
    augusta_networking
    augustad_runtime
)
# The headless server itself, not only its libraries: a client-only module
# linked straight into it fails the configure (tests/server_boundary.cmake).
augusta_set_module_scope(augustad SERVER_ONLY)

# On Linux the debug info is split off into augustad.debug beside it (ADR-0047):
# the binary keeps its symbol table, which the crash handler symbolizes with,
# and a .gnu_debuglink naming the file. The build ID is what matches a core
# dump to the binary and its debug info.
if(NOT WIN32)
  target_link_options(augustad PRIVATE -Wl,--build-id=sha1)
  add_custom_command(
    TARGET augustad
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:augustad> $<TARGET_FILE:augustad>.debug
    COMMAND ${CMAKE_OBJCOPY} --strip-debug --add-gnu-debuglink=$<TARGET_FILE:augustad>.debug $<TARGET_FILE:augustad>
    VERBATIM
  )
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
