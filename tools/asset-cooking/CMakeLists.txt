# Offline, authoring-time tool (ARCHITECTURE.md "Tooling" section) - never
# linked into the shipped client/server binaries, so it lives under
# tools/, not src/, with its own target instead of joining src/'s
# CMakeLists tree.
add_executable(augusta_asset_cooking main.cpp)

find_package(pxr CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)

target_include_directories(augusta_asset_cooking PRIVATE ${PXR_INCLUDE_DIRS})
target_link_libraries(augusta_asset_cooking PRIVATE
  ${PXR_LIBRARIES}
  meshoptimizer::meshoptimizer
  Microsoft::DirectXTex
)
