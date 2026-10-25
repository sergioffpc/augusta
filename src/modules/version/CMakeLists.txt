# version.cpp holds the engine's version; the platform's own file reads which
# build is running from the executable's image.
add_library(
  augusta_version
  STATIC
  version.cpp
  $<$<PLATFORM_ID:Windows>:version_windows.cpp>
  $<$<PLATFORM_ID:Darwin>:version_macos.cpp>
  $<$<PLATFORM_ID:Linux>:version_linux.cpp>
)
target_include_directories(augusta_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_version SHARED)
# version_windows.cpp includes <windows.h> to read the executable's own image;
# keep its min/max macros out of the way of the standard library.
target_compile_definitions(augusta_version PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
