# The server Runtime, augustad's (ADR-0060), and the pieces it is made of, each
# an internal target so its tests link only what it needs. liveness.h,
# metrics.h and connection_health.h are private to the module: its tests reach
# them through this directory, as the client module's do theirs.

# Whether the tick loop is still running, which /livez answers; pure, so tested
# without HTTP (see liveness.h).
add_library(augusta_server_liveness STATIC liveness.cpp)
target_link_libraries(augusta_server_liveness PUBLIC augusta_gameplay)
augusta_set_module_scope(augusta_server_liveness SERVER_ONLY)

# The HTTP endpoint serving /metrics and /livez (see metrics.h, ADR-0049):
# prometheus-cpp's core keeps and formats the metrics, Boost.Beast serves them;
# its permanent failure is a subsystem one in the shared error model (ADR-0033).
find_package(prometheus-cpp CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
add_library(augusta_server_metrics STATIC metrics.cpp)
target_link_libraries(
  augusta_server_metrics
  PUBLIC augusta_failure augusta_gameplay prometheus-cpp::core
  PRIVATE augusta_server_liveness augusta_logging augusta_version Boost::beast
)
# std::getenv reads the commit the image stamps in; MSVC deprecates it for
# _dupenv_s, which has no portable equivalent. Asio wants the Windows version
# it targets named, or it assumes Windows 7 (and says so on every build).
target_compile_definitions(
  augusta_server_metrics
  PRIVATE _CRT_SECURE_NO_WARNINGS
  PUBLIC $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
)
augusta_set_module_scope(augusta_server_metrics SERVER_ONLY)

# Every client's Connection health as metrics (see connection_health.h,
# ADR-0049); tested by collecting it, without a network.
add_library(augusta_server_connection_health STATIC connection_health.cpp)
target_link_libraries(
  augusta_server_connection_health
  PUBLIC augusta_host augusta_metrics augusta_link augusta_host_match
  PRIVATE augusta_limits
)
augusta_set_module_scope(augusta_server_connection_health SERVER_ONLY)

# What a crash leaves in the log: the fatal signal and a symbolized stack
# (ADR-0047). Abseil symbolizes, being async-signal-safe where the
# alternatives (std::stacktrace, Boost.Stacktrace) allocate.
find_package(absl CONFIG REQUIRED)
add_library(augusta_server_crash STATIC crash.cpp)
target_include_directories(augusta_server_crash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_server_crash PRIVATE absl::stacktrace absl::symbolize)
# crash.cpp writes through <windows.h> there; keep its min/max macros out of the
# way of the standard library.
target_compile_definitions(augusta_server_crash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_server_crash SERVER_ONLY)

# The server Runtime itself (see server.h): the Network I/O and Simulation
# threads around the Host, its clock, and the metrics endpoint beside them.
add_library(augusta_server STATIC server.cpp)
target_include_directories(augusta_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_server
  PUBLIC augusta_host augusta_host_replay augusta_failure augusta_gameplay augusta_physics augusta_simulation
  PRIVATE augusta_assets augusta_logging augusta_supervisor augusta_server_connection_health augusta_server_metrics
)
augusta_set_module_scope(augusta_server SERVER_ONLY)

# augustad's phases behind the application boundary - config, transport,
# content, runtime - each classifying its own failure (see
# server_application.h, ADR-0033); tested with the golden server pack and
# injected faults.
add_library(augusta_server_application STATIC server_application.cpp)
target_include_directories(augusta_server_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_server_application
  PUBLIC augusta_application augusta_config augusta_failure augusta_server_config augusta_server
  PRIVATE augusta_assets augusta_link augusta_logging augusta_scenario augusta_scenario_policy_loader augusta_host
)
augusta_set_module_scope(augusta_server_application SERVER_ONLY)
