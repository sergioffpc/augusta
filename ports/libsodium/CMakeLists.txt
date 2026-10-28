# libsodium's Windows build for augusta's overlay port (portfile.cmake): the
# static library of upstream's msbuild project
# (builds/msvc/vs2022/libsodium/libsodium.vcxproj), every source and the same
# definitions, built by CMake under whichever MSVC-compatible compiler, cl or
# clang-cl, the triplet names. Its version.h is the one upstream ships for that
# project, builds/msvc/version.h.
cmake_minimum_required(VERSION 3.25)
project(libsodium LANGUAGES C)

file(GLOB_RECURSE sodium_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/libsodium/*.c")

add_library(libsodium STATIC ${sodium_sources})

set(generated_include_dir "${PROJECT_BINARY_DIR}/include")
configure_file("${PROJECT_SOURCE_DIR}/builds/msvc/version.h" "${generated_include_dir}/sodium/version.h" COPYONLY)

target_include_directories(
  libsodium
  PRIVATE
    "${generated_include_dir}"
    "${generated_include_dir}/sodium"
    "${PROJECT_SOURCE_DIR}/src/libsodium/include"
    "${PROJECT_SOURCE_DIR}/src/libsodium/include/sodium"
)

# libsodium.props, LIB.props, Common.props and x64.props.
target_compile_definitions(
  libsodium
  PRIVATE SODIUM_STATIC NATIVE_LITTLE_ENDIAN inline=__inline _CRT_SECURE_NO_WARNINGS _LIB UNICODE _UNICODE
)
target_compile_options(libsodium PRIVATE /wd4146 /wd4244)

include(GNUInstallDirs)
install(TARGETS libsodium ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/src/libsodium/include/sodium.h" TYPE INCLUDE)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/libsodium/include/sodium/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sodium"
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "private" EXCLUDE
)
install(FILES "${generated_include_dir}/sodium/version.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sodium")
