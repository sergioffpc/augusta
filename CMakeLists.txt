cmake_minimum_required(VERSION 3.25)

# Default to the vcpkg submodule's toolchain (pinned in third_party/vcpkg,
# see .gitmodules) unless the caller already picked one. Must run before
# project().
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING
    "Vcpkg toolchain file"
  )
endif()

# Use sccache when it's on PATH (ADR-0008). Skip it under MSVC's default
# Visual Studio generator - sccache only works there via Ninja.
find_program(SCCACHE_PROGRAM sccache)
if(SCCACHE_PROGRAM AND (NOT CMAKE_GENERATOR MATCHES "Visual Studio"))
  set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
endif()

# The server is on every build's graph, so a platform augustad doesn't support
# (NFR-04) stops the configure: by its host OS here, before vcpkg's toolchain
# installs the dependencies in project(), and by its target right after.
include(cmake/AugustaPlatform.cmake)
augusta_require_server_host()

# The tools (tools/agent) and their tests. On by default, so every
# build compiles them and their C++ tests join augusta_tests; the server image,
# which builds augustad alone, turns them off. Before project(): with them,
# vcpkg installs what the Agents' Python module needs.
option(AUGUSTA_TOOLS "Build the tools (tools/agent) and their tests" ON)
if(AUGUSTA_TOOLS)
  include(cmake/AugustaAgentPython.cmake)
  augusta_find_agent_python(AUGUSTA_AGENT_PYTHON)
  if(AUGUSTA_AGENT_PYTHON)
    list(APPEND VCPKG_MANIFEST_FEATURES agent)
  endif()
endif()

# The client (src/client) and its Renderer (src/modules/renderer), on Windows
# and Linux (ADR-0006): on by default, so `cmake --preset linux` builds augustac
# and its tests too. With it, vcpkg installs the manifest's client feature,
# whose GLFW needs the system's X11, Wayland and xkbcommon development files on
# Linux (scripts/bootstrap/linux.sh). The server image, and every Linux CI job
# but the nightly's client-linux, turn it off: they build and test what
# augustad is built from, and the Linux client is the nightly's alone
# (ADR-0013). Before project(), as AUGUSTA_TOOLS is.
option(AUGUSTA_CLIENT "Build the client (augustac), its Renderer and their tests" ON)
if(AUGUSTA_CLIENT)
  list(APPEND VCPKG_MANIFEST_FEATURES client)
endif()

# Mesa's lavapipe, the software Vulkan driver the renderer smoke test draws on
# (tests/CMakeLists.txt), on Windows, where vcpkg builds it: the manifest's
# lavapipe feature, for tests only. Off by default, so a release's build
# installs none of it and its SBOM names none; CI's client job and the
# windows-debug preset turn it on. Its first build compiles LLVM, which needs
# Visual Studio's ATL; off, the smoke test is not built. Before project(), as
# AUGUSTA_TOOLS is.
if(CMAKE_HOST_WIN32)
  option(AUGUSTA_LAVAPIPE "Install Mesa's lavapipe for the renderer smoke test" OFF)
  if(AUGUSTA_LAVAPIPE)
    list(APPEND VCPKG_MANIFEST_FEATURES lavapipe)
  endif()
endif()

project(augusta LANGUAGES CXX)
augusta_require_server_platform()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The GNU install directories (CMAKE_INSTALL_BINDIR, ...) under the prefix, for
# the targets that `cmake --install` puts in place.
include(GNUInstallDirs)

if(MSVC)
  # clang-cl, which sets MSVC (ADR-0008): its /W4 is clang's -Wall -Wextra and
  # /WX its -Werror, Linux's warning set below. Not /Wall, which clang-cl takes
  # for -Weverything.
  add_compile_options(/W4 /WX /EHsc)
  # Every configuration keeps its debug info, embedded in each object file
  # (/Z7), which sccache caches - not /Zi's PDB shared between compiles, which
  # it cannot. Every target, as on Linux: augustac's Release link gathers it
  # into augustac.pdb, published with each release (ADR-0055). A normal
  # variable, not a cache one, so a -D cannot drop the published symbols.
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
else()
  add_compile_options(-Wall -Wextra -Werror)
  # Release keeps its debug info, which src/server splits off augustad: what a
  # crash's core dump is read with (ADR-0047). Every target, not just augustad's
  # own: a core's frames are mostly in the module libraries it links, and
  # without theirs lldb has no file, line or local for them.
  add_compile_options($<$<CONFIG:Release>:-g>)
endif()

# The Agents' Python module (tools/agent) is a shared library that links the
# engine's static ones, which on Linux must then be position-independent: the
# whole build, so the tools link what augustad does. Windows needs nothing.
if(AUGUSTA_TOOLS AND NOT WIN32)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Every binary of an ASan build (linux-san, linux-fuzz) starts with the options
# cmake/asan_default_options.cpp gives it (ADR-0008): when ctest runs it, when
# the build lists its tests, and when it is run by hand.
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
  add_library(augusta_asan_default_options OBJECT cmake/asan_default_options.cpp)
  link_libraries(augusta_asan_default_options)
endif()
# And every binary of a UBSan build (linux-san) with cmake/ubsan_default_options.cpp's,
# so a UBSan report fails it however it is run (ADR-0013).
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*undefined")
  add_library(augusta_ubsan_default_options OBJECT cmake/ubsan_default_options.cpp)
  link_libraries(augusta_ubsan_default_options)
endif()

# flecs/GameNetworkingSockets/PhysX back the client/server shared runtime
# (ECS worlds, networking transport, physics - ADR-0001/0002/0003).
find_package(flecs CONFIG REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)
find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

# NVTX (github.com/NVIDIA/NVTX) is header-only and not on vcpkg, so it's
# vendored as a pinned git submodule (third_party/nvtx) instead. Not
# added via add_subdirectory: its own CMakeLists.txt pulls in a compiled
# loader lib, tests, and install rules this repo has no use for - a plain
# INTERFACE target for the header path is all the nvtx3::scoped_range API
# actually needs. SYSTEM, like every other third-party include: its
# headers' own diagnostics are not this repo's to fix, for the compiler or
# for clang-tidy.
add_library(nvtx3-cpp INTERFACE)
target_include_directories(nvtx3-cpp SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvtx/c/include")

include(cmake/AugustaModule.cmake)
include(cmake/AugustaDebugInfo.cmake)

enable_testing()
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(rapidcheck CONFIG REQUIRED)

add_subdirectory(src/modules/version)
add_subdirectory(src/modules/logging)
add_subdirectory(src/modules/config)
add_subdirectory(src/modules/math)
add_subdirectory(src/modules/primitives)
add_subdirectory(src/modules/tick)
add_subdirectory(src/modules/supervisor)
add_subdirectory(src/modules/failure)
add_subdirectory(src/modules/application)
add_subdirectory(src/modules/assets)
add_subdirectory(src/modules/physics)
add_subdirectory(src/modules/command)
add_subdirectory(src/modules/parameters)
add_subdirectory(src/modules/map)
add_subdirectory(src/modules/ballistics)
add_subdirectory(src/modules/weapon)
add_subdirectory(src/modules/protocol)
add_subdirectory(src/modules/shared_wire)
add_subdirectory(src/modules/capture_file)
add_subdirectory(src/modules/networking)
add_subdirectory(src/modules/scripting)
add_subdirectory(src/modules/audio)
add_subdirectory(src/modules/input)
add_subdirectory(src/modules/animation)
add_subdirectory(src/modules/simulation)
add_subdirectory(src/modules/prediction)
add_subdirectory(src/modules/harness)
add_subdirectory(src/modules/presentation)
add_subdirectory(src/modules/replication)
add_subdirectory(src/server)
# The tools, built with the runtime they link on every platform: the Agents
# (ADR-0052), whose Python module is built against the interpreter
# augusta_find_agent_python found - pybind11 in its classic mode reads it from
# PYTHON_EXECUTABLE - and left out without one.
if(AUGUSTA_TOOLS)
  if(AUGUSTA_AGENT_PYTHON)
    set(PYTHON_EXECUTABLE "${AUGUSTA_AGENT_PYTHON}")
    # Classic mode, not FindPython: vcpkg's python3 port, which pybind11's
    # depends on, wraps FindPython to give a module its own Python's headers
    # and libraries, a 3.14 where the tools environment's is the 3.12 its USD
    # wheels need. pybind11 warns that CMP0148 is not set, whatever this
    # project sets it to, and goes on with the FindPythonInterp/FindPythonLibs
    # it ships.
    find_package(pybind11 CONFIG REQUIRED)
  endif()
  add_subdirectory(tools/agent)
endif()
# The linux-coverage preset's report as build targets, for an editor that runs
# one before and one after its tests (VS Code's CMake Tools, "Run Tests with
# Coverage"): coverage-clean drops the last run's profiles, so the report is of
# this run alone; coverage-report turns this run's into it.
option(AUGUSTA_COVERAGE "Add the coverage-clean and coverage-report targets (linux-coverage preset)" OFF)
if(AUGUSTA_COVERAGE)
  add_custom_target(
    coverage-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/profiles
  )
  add_custom_target(
    coverage-report
    COMMAND bash scripts/coverage-report.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
  )
endif()
# The client renders through NVRHI on Vulkan, on Windows and Linux (ADR-0059).
# What its Renderer decides apart from the GPU builds and is tested in every
# build; the Renderer itself and the client that links it, with AUGUSTA_CLIENT.
add_subdirectory(src/modules/renderer)
if(AUGUSTA_CLIENT)
  add_subdirectory(src/client)
endif()

# Every module and both runtime targets above have now made their own
# target_link_libraries() calls, so the client/server module dependency
# graph is complete - see cmake/AugustaModule.cmake.
augusta_check_module_scopes()

# Tests, benchmarks and fuzzers build only on request, through the
# augusta_tests target (tests/CMakeLists.txt) that `make test` builds before
# ctest: building the binaries never waits on them (ADR-0008).
add_subdirectory(tests EXCLUDE_FROM_ALL)
