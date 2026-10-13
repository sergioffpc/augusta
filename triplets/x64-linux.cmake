# vcpkg's own x64-linux, built by this project's LLVM toolchain (ADR-0008)
# instead of the system compiler. vcpkg.json's overlay puts it in place of the
# built-in triplet of the same name.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/LlvmToolchain.cmake")

# GameNetworkingSockets' debug build checks itself with libstdc++'s debug
# containers (<debug/map>), which libc++ has no counterpart of: its release
# build's paranoia level, in both.
if(PORT STREQUAL "gamenetworkingsockets")
  set(VCPKG_C_FLAGS "")
  set(VCPKG_CXX_FLAGS -DSTEAMNETWORKINGSOCKETS_SNP_PARANOIA=1)
endif()

# PhysX builds with -Weverything -Werror and puts its own flags after these, so
# each warning clang 21 adds that its code trips is turned off by name: by the
# error alone for -Wformat, which its -Wall would turn back on.
if(PORT STREQUAL "physx")
  set(VCPKG_C_FLAGS "-Wno-missing-include-dirs -Wno-unique-object-duplication -Wno-nrvo -Wno-error=format")
  set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()
