# The startup config files (ADR-0034), split by side so the headless server
# never depends on client Input (ARCHITECTURE.md §5).
#
# augusta_config is the mechanism both share: yaml-cpp reads the file;
# Boost.DLL finds the directory it sits in (the running executable's);
# Boost.Program_options parses the command line that can name another file;
# augusta_logging validates the `logging.level` key against ParseSeverity.
find_package(yaml-cpp CONFIG REQUIRED)
find_package(boost_dll CONFIG REQUIRED)
find_package(boost_program_options CONFIG REQUIRED)

add_library(augusta_config STATIC config.cpp)
target_include_directories(augusta_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_config PRIVATE yaml-cpp::yaml-cpp Boost::dll Boost::program_options augusta_logging)
augusta_set_module_scope(augusta_config SHARED)

# augustac.yaml: augusta_input names the keys and controls its `input.keys`
# section binds. Here rather than in src/client, which only Windows builds, so
# its tests run on every platform.
add_library(augusta_client_config STATIC client_config.cpp)
target_include_directories(augusta_client_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_client_config PUBLIC augusta_config augusta_input)
augusta_set_module_scope(augusta_client_config CLIENT_ONLY)

# augustad.yaml: server-owned keys only.
add_library(augusta_server_config STATIC server_config.cpp)
target_include_directories(augusta_server_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_server_config PUBLIC augusta_config)
augusta_set_module_scope(augusta_server_config SERVER_ONLY)
