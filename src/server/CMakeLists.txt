# Reads the Parameters script through Lua (ADR-0022, ADR-0039) into the shared
# Parameters; pure, so tested with scripts as strings (see parameters_loader.h).
# Server-only: a client never reads the script, it is sent the result, so it
# never links the interpreter.
add_library(augustad_parameters_loader STATIC
  parameters_loader.cpp
)
target_include_directories(augustad_parameters_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_parameters_loader
  PUBLIC augusta_parameters
  PRIVATE augusta_lua_sandbox
)
augusta_set_module_scope(augustad_parameters_loader SERVER_ONLY)

# Reads the scenario's Game policy scripts out of the server pack into the
# engine SimulationWorld runs them in (ADR-0022); tested with packs built in the
# test (see policy_loader.h).
add_library(augustad_policy_loader STATIC
  policy_loader.cpp
)
target_include_directories(augustad_policy_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_policy_loader PUBLIC augusta_assets augusta_scripting)
augusta_set_module_scope(augustad_policy_loader SERVER_ONLY)

# What the server loads from its verified pack at startup: the scenario, the
# Parameters and the Game policy (see content.h).
add_library(augustad_content STATIC
  content.cpp
)
target_include_directories(augustad_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_content
  PUBLIC augusta_assets augusta_math augusta_parameters augusta_physics augusta_scripting
  PRIVATE augusta_logging augusta_map augustad_parameters_loader augustad_policy_loader
)
augusta_set_module_scope(augustad_content SERVER_ONLY)

# Who is in the match and whether a join is admitted; pure, so tested without a
# network (see match.h).
add_library(augustad_match STATIC
  match.cpp
)
target_include_directories(augustad_match PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_match PUBLIC augusta_assets augusta_math augusta_networking augusta_physics)
augusta_set_module_scope(augustad_match SERVER_ONLY)

# The sanity gate and per-player queue for what clients send to move; pure, so
# tested without a network (see command_queue.h).
add_library(augustad_commands STATIC
  command_queue.cpp
)
target_include_directories(augustad_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_commands PUBLIC augusta_command)
augusta_set_module_scope(augustad_commands SERVER_ONLY)

# The count of each peer's misbehaviour that decides when it is disconnected;
# pure, so tested without a network (see misbehaviour.h).
add_library(augustad_misbehaviour STATIC
  misbehaviour.cpp
)
target_include_directories(augustad_misbehaviour PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
augusta_set_module_scope(augustad_misbehaviour SERVER_ONLY)

# When a peer that has not been admitted is past its deadline; pure, so tested
# without a network (see admission.h).
add_library(augustad_admission STATIC
  admission.cpp
)
target_include_directories(augustad_admission PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_admission PUBLIC augusta_networking)
augusta_set_module_scope(augustad_admission SERVER_ONLY)

# The server's socket and SimulationWorld without threads or a clock, so a test
# can drive a match by hand (see host.h), and the conversions Host makes at its
# edges: between the protocol's plain types and the engine's (see wire.h), and
# into SimulationWorld's (see simulation_mapping.h).
add_library(augustad_host STATIC
  host.cpp
  simulation_mapping.cpp
  wire.cpp
)
target_include_directories(augustad_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_host
  PUBLIC augusta_ballistics augusta_networking augusta_parameters augusta_physics augusta_simulation augustad_content
         augustad_commands augustad_match augustad_misbehaviour augustad_admission augusta_assets augusta_protocol augusta_replication
         augusta_tick
  PRIVATE augusta_logging augusta_map augusta_version
)
augusta_set_module_scope(augustad_host SERVER_ONLY)

add_library(augustad_runtime STATIC
  runtime.cpp
)
target_include_directories(augustad_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(augustad_runtime
  PUBLIC augustad_host augusta_networking augusta_parameters augusta_physics augusta_simulation augusta_supervisor
  PRIVATE augusta_assets augusta_logging augusta_tick
)
augusta_set_module_scope(augustad_runtime SERVER_ONLY)

add_executable(augustad main.cpp)
target_link_libraries(augustad PRIVATE augusta_assets augusta_config augustad_content augusta_parameters augusta_version augusta_logging augusta_networking augustad_runtime)

# `cmake --install` puts augustad in the prefix's bin directory (DESTDIR
# honoured), which is how the Dockerfile's image gets it. On Windows the vcpkg
# DLLs it loads go beside it, as the build tree has them; CMake resolves them
# itself, rather than vcpkg's applocal helper, so they are in the install
# manifest `make uninstall` removes. On Linux vcpkg links them statically.
if (WIN32)
  install(TARGETS augustad RUNTIME_DEPENDENCY_SET augustad_dlls RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(RUNTIME_DEPENDENCY_SET augustad_dlls
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\][Ss]ystem32[/\\]"
    DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
else()
  install(TARGETS augustad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
