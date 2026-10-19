# A stand-in for augustac's Linux build, for stage_client_linux.cmake to stage
# with scripts/stage-client.sh: an executable named augustac, in src/client as
# augustac is, that prints "augustac <VERSION>" for --version, with its debug
# info split off as augustac's is and the same client install component and
# RPATH. It links a shared library of its own, which BUNDLED=ON installs beside
# it; BUNDLED=OFF leaves it out, a dependency neither in the folder nor on the
# allowlist. CORRUPT=ON also installs a file that starts as an ELF file but
# isn't one, whose libraries cannot be read. BUILD names the build, compiled
# into augustac, so two builds' build IDs differ wherever they are built. C,
# not C++, so the fixture has no C++ runtime to link.
cmake_minimum_required(VERSION 3.25)
project(augusta_stage_client_fixture C)

include(${AUGUSTA_SOURCE_DIR}/cmake/AugustaDebugInfo.cmake)

add_compile_options(-g)
add_library(fixture_bundled SHARED bundled.c)
add_executable(augustac augustac.c)
target_compile_definitions(augustac PRIVATE "FIXTURE_VERSION=\"${VERSION}\"" "FIXTURE_BUILD=\"${BUILD}\"")
target_link_libraries(augustac PRIVATE fixture_bundled)
set_target_properties(
  augustac
  fixture_bundled
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/client
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/client
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "$ORIGIN"
)
augusta_split_debug_info(augustac)

install(TARGETS augustac RUNTIME DESTINATION bin COMPONENT client EXCLUDE_FROM_ALL)
if(BUNDLED)
  install(TARGETS fixture_bundled LIBRARY DESTINATION bin COMPONENT client EXCLUDE_FROM_ALL)
endif()
if(CORRUPT)
  string(ASCII 127 delete)
  file(WRITE ${CMAKE_BINARY_DIR}/libcorrupt.so "${delete}ELF, and nothing else")
  install(FILES ${CMAKE_BINARY_DIR}/libcorrupt.so DESTINATION bin COMPONENT client EXCLUDE_FROM_ALL)
endif()
