# augusta's overlay of vcpkg's libsodium port, at the baseline's version: the
# upstream portfile, with Windows built by CMake and Ninja, statically, so that
# it needs no msbuild and builds under clang-cl as under cl (ADR-0079;
# ports/README.md). Linux and macOS take upstream's make path unchanged.
# Changes from upstream are marked "augusta:".

# libsodium has a special branching/tagging scheme, where regular version tags can actually be moved
# as new patches are applied to that version. This means that we may get unexpected hash mismatches
# when the upstream tag points to a new commit. To avoid this, we must make sure that we always
# use a '-RELEASE' tag, since those seem to be fixed to a single commit.
# See https://github.com/jedisct1/libsodium/issues/1373#issuecomment-2135172301 for more info.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jedisct1/libsodium
    REF "${VERSION}-RELEASE"
    SHA512 f8e11ad193037b7b885a40b832da331105f4e5943b74c0297fe07e02a313786016c777aebb094ad1fcf16398e54af545c2d55404c28b371eae5922d1e164ba00
    HEAD_REF master
    PATCHES
        001-mingw-i386.patch
)

# augusta: on Windows, CMake and Ninja through CMakeLists.txt, whatever
# compiler the triplet names, and always static, in place of upstream's msbuild
# solution under cl and its make path, which refuses a shared build, under any
# other compiler.
if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
  vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

  file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
  vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
  vcpkg_cmake_install()
  file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

  block(SCOPE_FOR VARIABLES)
    set(PACKAGE_NAME "libsodium")
    set(PACKAGE_VERSION "${VERSION}")
    set(prefix [[unused]])
    set(exec_prefix [[${prefix}]])
    set(includedir [[${prefix}/include]])
    set(libdir [[${prefix}/lib]])
    set(PKGCONFIG_LIBS_PRIVATE "")
    configure_file("${SOURCE_PATH}/libsodium.pc.in" "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libsodium.pc" @ONLY)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libsodium.pc" " -lsodium" " -llibsodium")
    if(NOT VCPKG_BUILD_TYPE)
      set(includedir [[${prefix}/../include]])
      configure_file("${SOURCE_PATH}/libsodium.pc.in" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libsodium.pc" @ONLY)
      vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libsodium.pc" " -lsodium" " -llibsodium")
    endif()
  endblock()
else()
  if(VCPKG_TARGET_IS_EMSCRIPTEN)
    list(APPEND OPTIONS "--disable-ssp" "--disable-asm")
  endif()
  if(NOT VCPKG_TARGET_IS_MINGW)
    list(APPEND OPTIONS --disable-pie)
  endif()
  # augusta: upstream's make-path settings for Windows compilers other than
  # MSVC's are dropped: the CMake build above serves every Windows compiler.

  vcpkg_make_configure(
        AUTORECONF
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS ${OPTIONS}
  )
  vcpkg_make_install()

  file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
endif()

vcpkg_fixup_pkgconfig()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
  vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/sodium/export.h" "#ifdef SODIUM_STATIC" "#if 1" IGNORE_UNCHANGED
  )
endif()

# vcpkg legacy
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/sodiumConfig.cmake.in"
  "${CURRENT_PACKAGES_DIR}/share/unofficial-sodium/unofficial-sodiumConfig.cmake"
  @ONLY
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
