# augustad and its Runtime (ADR-0060): the server Runtime and the pieces it is
# made of, each an internal target so its tests link only what it needs, then
# the executable that runs it. liveness.h, metrics.h and connection_health.h are
# private to the Runtime: its tests reach them through this directory, as the
# client Runtime's do theirs.

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

# The server Runtime itself (see server.h): the Network I/O and Simulation
# threads around the Host, its clock, and the metrics endpoint beside them.
add_library(augusta_server STATIC server.cpp)
target_include_directories(augusta_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_server
  PUBLIC augusta_host augusta_host_replay augusta_failure augusta_gameplay augusta_physics augusta_simulation
  PRIVATE augusta_assets augusta_logging augusta_supervisor augusta_server_connection_health augusta_server_metrics
)
# In a Development build (ADR-0074), the pcapng writer the Runtime taps its
# transport with when `debug.network.pcapng` asks for one (ADR-0076), and the
# engine version its file names.
if(AUGUSTA_DEBUG_TOOLS)
  target_link_libraries(augusta_server PRIVATE augusta_pcapng augusta_version)
endif()
augusta_set_module_scope(augusta_server SERVER_ONLY)

# augustad's phases behind the application boundary - config, transport,
# content, runtime - each classifying its own failure (see
# server_application.h, ADR-0033); tested with the golden server pack and
# injected faults.
add_library(augusta_server_application STATIC server_application.cpp)
target_include_directories(augusta_server_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  augusta_server_application
  PUBLIC augusta_application augusta_config augusta_failure augusta_server_config augusta_server
  PRIVATE
    augusta_assets
    augusta_hardware
    augusta_link
    augusta_logging
    augusta_physics
    augusta_scenario
    augusta_policy_loader
    augusta_host
)
augusta_set_module_scope(augusta_server_application SERVER_ONLY)

# augustad's entry point: it runs the server Runtime above behind the
# application boundary (ADR-0033, ADR-0060).
add_executable(augustad main.cpp)
target_link_libraries(
  augustad
  PRIVATE augusta_application augusta_server_application augusta_crash augusta_version augusta_logging augusta_server
)
# The headless server itself, not only its libraries: a client-only module
# linked straight into it fails the configure (checks/server_boundary.cmake).
augusta_set_module_scope(augustad SERVER_ONLY)
# augustad is Glue: it links only the application, the server Runtime beside
# it, crash, failure, logging and version - anything else fails the configure
# (ADR-0060, cmake/AugustaModule.cmake).
augusta_set_executable_glue(augustad)
# OmniPVD's runtime beside augustad in a Debug build (cmake/PvdRuntime.cmake).
augusta_stage_pvd_runtime(augustad)

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
