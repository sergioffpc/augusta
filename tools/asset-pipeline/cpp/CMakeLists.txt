cmake_minimum_required(VERSION 3.25)

# Fully independent of the main augusta CMake project (own vcpkg.json,
# own CMakeLists.txt/CMakePresets.json) - these two modules are the only
# native code the asset-cooking pipeline (tools/asset-pipeline, ADR-0030,
# a pure-Python project otherwise) needs, so building them never requires
# configuring the client/server runtime (or its flecs/GameNetworkingSockets/
# PhysX dependencies) at all.
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

# Use sccache when it's on PATH (ADR-0008). Skip it under MSVC's default
# Visual Studio generator - sccache only works there via Ninja.
find_program(SCCACHE_PROGRAM sccache)
if (SCCACHE_PROGRAM AND (NOT CMAKE_GENERATOR MATCHES "Visual Studio"))
  set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
endif()

project(augusta_asset_pipeline_cooking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  add_compile_options(/W4 /WX /EHsc)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

# Two small, independent pybind11 extension modules for the asset-cooking
# pipeline (ADR-0030) - thin wrappers over meshoptimizer (ADR-0016) and
# DirectXTex (ADR-0017), the only two pieces of the pipeline with no Python
# equivalent. Everything else (USD stage walking, the pack container
# format, key generation) is pure Python, in ../src/asset_pipeline - see
# that project's cook.py/pack.py/keys.py.
#
# Neither module links USD. The pipeline's own USD access goes through
# usd-optimize's pip-installed pxr build instead (cook.py), so that there
# is only ever one OpenUSD build loaded in the process - see ADR-0030 for
# why a second, independently-built one (e.g. vcpkg's own) can't coexist
# with it (a DLL-name collision between the two builds' same-named DLLs).
find_package(meshoptimizer CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# Both build straight into ../src/asset_pipeline so `import asset_pipeline.
# _meshoptimizer` / `_textconv` just work once that project is installed
# (editable or not) - no separate copy step.
set(AUGUSTA_ASSET_PIPELINE_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/asset_pipeline")

pybind11_add_module(augusta_meshoptimizer_py meshoptimizer_bindings.cpp)
target_link_libraries(augusta_meshoptimizer_py PRIVATE meshoptimizer::meshoptimizer)
set_target_properties(augusta_meshoptimizer_py PROPERTIES
  OUTPUT_NAME "_meshoptimizer"
  RUNTIME_OUTPUT_DIRECTORY "${AUGUSTA_ASSET_PIPELINE_PACKAGE_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${AUGUSTA_ASSET_PIPELINE_PACKAGE_DIR}"
)

pybind11_add_module(augusta_textconv_py textconv_bindings.cpp)
target_link_libraries(augusta_textconv_py PRIVATE Microsoft::DirectXTex)
set_target_properties(augusta_textconv_py PROPERTIES
  OUTPUT_NAME "_textconv"
  RUNTIME_OUTPUT_DIRECTORY "${AUGUSTA_ASSET_PIPELINE_PACKAGE_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${AUGUSTA_ASSET_PIPELINE_PACKAGE_DIR}"
)
