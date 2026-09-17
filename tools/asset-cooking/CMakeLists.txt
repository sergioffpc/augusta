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
# augusta_assets is PUBLIC, not PRIVATE: asset_cooking.h itself includes
# augusta/assets.h (Cook()'s signing_key parameter is an
# assets::Ed25519PrivateKey), so a consumer of this library needs
# augusta_assets' own public include path too, not just this library's.
target_link_libraries(augusta_asset_cooking_lib PUBLIC augusta_assets)
target_link_libraries(augusta_asset_cooking_lib PRIVATE
  ${PXR_LIBRARIES}
  meshoptimizer::meshoptimizer
  Microsoft::DirectXTex
)

# vcpkg's usd port doesn't set PXR_PLUGINPATH_NAME for a caller, so
# cook.cpp registers this compile-time-baked plugInfo.json path itself
# before any pxr:: call (see EnsureUsdPluginPathConfigured's own comment
# for why - ADR-0030).
if (WIN32)
  set(AUGUSTA_USD_PLUGIN_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/usd")
else()
  set(AUGUSTA_USD_PLUGIN_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/usd")
endif()
target_compile_definitions(augusta_asset_cooking_lib PRIVATE AUGUSTA_USD_PLUGIN_PATH="${AUGUSTA_USD_PLUGIN_PATH}")

add_executable(augusta_asset_cooking main.cpp)
target_link_libraries(augusta_asset_cooking PRIVATE augusta_asset_cooking_lib)
