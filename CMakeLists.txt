cmake_minimum_required(VERSION 3.25)

# Default to the vcpkg submodule's toolchain (pinned in third_party/vcpkg,
# see .gitmodules) unless the caller already picked one. Must run before
# project().
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

# Use sccache when it's on PATH (ADR-0008). Skip it under MSVC's default
# Visual Studio generator - sccache only works there via Ninja.
find_program(SCCACHE_PROGRAM sccache)
if (SCCACHE_PROGRAM AND (NOT CMAKE_GENERATOR MATCHES "Visual Studio"))
  set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
endif()

project(augusta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  add_compile_options(/W4 /WX /EHsc)
else()
  add_compile_options(-Wall -Wextra -Werror)
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
# actually needs.
add_library(nvtx3-cpp INTERFACE)
target_include_directories(nvtx3-cpp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvtx/c/include")

include(cmake/AugustaModule.cmake)

enable_testing()
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

add_subdirectory(src/modules/version)
add_subdirectory(src/modules/logging)
add_subdirectory(src/modules/config)
add_subdirectory(src/modules/math)
add_subdirectory(src/modules/assets)
add_subdirectory(src/modules/physics)
add_subdirectory(src/modules/command)
add_subdirectory(src/modules/parameters)
add_subdirectory(src/modules/map)
add_subdirectory(src/modules/ballistics)
add_subdirectory(src/modules/protocol)
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
if (WIN32)
  # NVIDIA Falcor (ADR-0009) is Windows-only, so augusta_renderer's real
  # implementation only builds here - nothing on Linux (only the
  # WIN32-only client links it) ever needed it in that build graph anyway.
  add_subdirectory(src/modules/renderer)
  add_subdirectory(src/client)
endif()

# Every module and both runtime targets above have now made their own
# target_link_libraries() calls, so the client/server module dependency
# graph is complete - see cmake/AugustaModule.cmake.
augusta_check_module_scopes()

add_subdirectory(tests)
