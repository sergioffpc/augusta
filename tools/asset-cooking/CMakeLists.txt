# Offline, authoring-time tool (ARCHITECTURE.md "Tooling" section) - never
# linked into the shipped client/server binaries, so it lives under
# tools/, not src/, with its own target instead of joining src/'s
# CMakeLists tree.
find_package(pxr CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)

# The cooker as a library, single public entry point (Cook()), so it can
# be linked directly into tests without spawning a subprocess. The
# augusta_asset_cooking executable below is a thin CLI wrapper over it.
add_library(augusta_asset_cooking_lib STATIC
  cook.cpp
)
target_include_directories(augusta_asset_cooking_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(augusta_asset_cooking_lib PRIVATE ${PXR_INCLUDE_DIRS})
target_link_libraries(augusta_asset_cooking_lib PRIVATE
  augusta_assets
  ${PXR_LIBRARIES}
  meshoptimizer::meshoptimizer
  Microsoft::DirectXTex
)

# vcpkg's usd port installs each Pxr module's plugInfo.json under a
# "usd/" subtree next to that module's library (bin/usd for Windows'
# flat-DLL layout, lib/usd for Linux's .so layout) - OpenUSD's
# PlugRegistry has no way to find these on its own (no PXR_PLUGINPATH_NAME
# is set for a caller by default), and without it any pxr:: call that
# touches the plugin registry (e.g. UsdStage::Open, since opening a stage
# needs a registered Sdf file format) aborts. cook.cpp points
# PXR_PLUGINPATH_NAME here itself before making any pxr:: call, using this
# compile-time-baked path - fine since augusta_asset_cooking never ships
# outside a full repo checkout (ARCHITECTURE.md "Tooling": offline-only),
# so there's no "installed elsewhere" case to support.
if (WIN32)
  set(AUGUSTA_USD_PLUGIN_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/usd")
else()
  set(AUGUSTA_USD_PLUGIN_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/usd")
endif()
target_compile_definitions(augusta_asset_cooking_lib PRIVATE AUGUSTA_USD_PLUGIN_PATH="${AUGUSTA_USD_PLUGIN_PATH}")

add_executable(augusta_asset_cooking main.cpp)
target_link_libraries(augusta_asset_cooking PRIVATE augusta_asset_cooking_lib)
