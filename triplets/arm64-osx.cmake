# vcpkg's own arm64-osx, for the macOS client's development build, built by
# this project's LLVM toolchain (ADR-0008) instead of Apple's clang. vcpkg.json's
# overlay puts it in place of the built-in triplet of the same name.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/LlvmMacosToolchain.cmake")

# PhysX builds with -Weverything -Werror, as on Linux (triplets/arm64-linux.cmake),
# and on its Linux platform files (ports/physx), which test for aarch64 by
# CMake's Linux name and so miss Darwin's arm64: their -ffp-contract=off, which
# keeps its floating point what it is on Linux's arm64, is given here. Its
# vector math joins bool results with & where its NEON half for Apple returns
# bool, which its own -Wall reports: kept from failing the build, by the error
# alone, as -Wformat is.
if(PORT STREQUAL "physx")
  string(
    CONCAT VCPKG_C_FLAGS
    "-Wno-missing-include-dirs -Wno-unique-object-duplication -Wno-nrvo -Wno-error=format -ffp-contract=off"
    " -Wno-error=bitwise-instead-of-logical"
  )
  set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()
