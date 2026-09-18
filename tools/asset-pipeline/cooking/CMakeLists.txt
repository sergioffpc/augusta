# Two small, independent pybind11 extension modules for the asset-cooking
# pipeline (ADR-0030) - thin wrappers over meshoptimizer and DirectXTex,
# the two pieces of what was once a full C++ cooker (ADR-0016/ADR-0017)
# with no Python equivalent. Everything else (USD stage walking, the pack
# container format, key generation) is pure Python now, in ../src/
# asset_pipeline - see that project's cook.py/pack.py/keys.py.
#
# Neither module links USD. The pipeline's own USD access goes through
# usd-optimize's pip-installed pxr build instead (cook.py), specifically
# so there is only ever one OpenUSD build loaded in the process - linking
# vcpkg's own separately-built USD here too (as the old C++ cooker did)
# caused a hard DLL-name collision between the two builds (same-named USD
# DLLs, incompatible ABIs) the moment both loaded into one process.
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
