cmake_minimum_required(VERSION 3.25)

# Fully independent of the main augusta CMake project (own vcpkg.json,
# own CMakeLists.txt/CMakePresets.json) - this module is the only
# native code the asset-cooking pipeline (tools/pack, ADR-0030,
# a pure-Python project otherwise) needs, so building it never requires
# configuring the client/server runtime (or its flecs/GameNetworkingSockets/
# PhysX dependencies) at all. Only the Linux toolchain is the root's
# (cmake/LlvmToolchain.cmake and triplets/, ADR-0008), so the module and its
# dependencies build as the engine's do.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"
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

project(augusta_asset_pipeline_cooking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /EHsc)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

# One pybind11 extension module for the asset-cooking pipeline (ADR-0030),
# augusta_pack._native: the mesh pass over meshoptimizer (ADR-0016, mesh.h)
# and the texture compression over stb_image, stb_image_resize2 and libktx
# (ADR-0017, texture.h), the only two pieces of the pipeline with no Python
# equivalent, bound in module.cpp. Everything else (USD stage walking, the pack container
# format, key generation) is pure Python, in ../src/augusta_pack - see that
# project's cook.py/pack.py/keys.py.
#
# The module doesn't link USD. The pipeline's own USD access goes through
# usd-optimize's pip-installed pxr build instead (cook.py), so that there
# is only ever one OpenUSD build loaded in the process - see ADR-0030 for
# why a second, independently-built one (e.g. vcpkg's own) can't coexist
# with it (a DLL-name collision between the two builds' same-named DLLs).
find_package(meshoptimizer CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# Built straight into ../src/augusta_pack so `import augusta_pack._native`
# just works once that project is installed (editable or not) - no separate
# copy step.
set(AUGUSTA_PACK_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/augusta_pack")

pybind11_add_module(augusta_pack_py module.cpp mesh.cpp texture.cpp)
target_link_libraries(augusta_pack_py PRIVATE meshoptimizer::meshoptimizer KTX::ktx)
target_include_directories(augusta_pack_py SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
set_target_properties(
  augusta_pack_py
  PROPERTIES
    OUTPUT_NAME "_native"
    RUNTIME_OUTPUT_DIRECTORY "${AUGUSTA_PACK_PACKAGE_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${AUGUSTA_PACK_PACKAGE_DIR}"
)
