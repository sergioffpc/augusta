# The cooker's native part (ADR-0030): one pybind11 module,
# augusta_pack._native, bound in module.cpp - the mesh pass over meshoptimizer
# (ADR-0016, mesh.h) and the texture compression over stb_image,
# stb_image_resize2 and libktx (ADR-0017, texture.h). Built by the root build
# with AUGUSTA_TOOLS, beside the Agents' module, and only with the tools
# environment's Python (the root CMakeLists.txt), which the vcpkg manifest's
# pack feature installs these dependencies for. The cooker's bootstrap builds
# this target alone (`make pack-native`).
#
# The module doesn't link USD. The pipeline's own USD access goes through
# usd-optimize's pip-installed pxr build instead (cook.py), so that there is
# only ever one OpenUSD build loaded in the process - see ADR-0030 for why a
# second, independently-built one (e.g. vcpkg's own) can't coexist with it (a
# DLL-name collision between the two builds' same-named DLLs).
if(NOT AUGUSTA_TOOLS_PYTHON)
  return()
endif()

find_package(meshoptimizer CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Stb REQUIRED)

# Built straight into the package's source directory, so `import
# augusta_pack._native` works once the package is installed editable (uv sync
# in tools/, or the cooker's bootstrap), as the Agents' module is
# (tools/agent/native).
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
