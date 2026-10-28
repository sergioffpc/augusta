# vcpkg's own x64-windows, built by this project's LLVM toolchain (ADR-0008,
# ADR-0079) instead of MSVC's cl, with LLVM and Mesa built release-only.
# vcpkg.json's overlay puts it in place of the built-in triplet of the same
# name.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/LlvmWindowsToolchain.cmake")

# A chainloaded toolchain turns off the Visual Studio developer environment
# vcpkg builds a port in, whose MSVC STL, CRT and Windows SDK clang-cl still
# compiles against (ADR-0079).
set(VCPKG_LOAD_VCVARS_ENV ON)

# The LLVM the project's configure found (cmake/LlvmWindowsToolchain.cmake),
# through the clean environment vcpkg builds a port in. Its path is no part of
# a port's ABI: vcpkg hashes the compiler itself.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED AUGUSTA_LLVM_BIN)

# Mesa is loaded only as a Vulkan ICD, lavapipe, from its own DLL, so a release
# Mesa serves a debug augusta with no two CRTs in one image; LLVM is there only
# to build Mesa, which links it statically. Their debug builds, ~8 GB of static
# libs and PDBs, would never be loaded.
if(PORT MATCHES "^(llvm|mesa)$")
  set(VCPKG_BUILD_TYPE release)
endif()

# PhysX builds with /W4 /WX, clang's -Wall -Wextra -Werror under clang-cl, and
# puts these flags after its own (ports/physx): each warning its code trips is
# turned off by name, as in triplets/x64-linux.cmake. In this order: those clang
# 21 adds; those PhysX's own Linux platform files turn off for clang too; and
# those its Windows code trips, which only cl had built.
if(PORT STREQUAL "physx")
  set(
    VCPKG_C_FLAGS
    "-Wno-missing-include-dirs -Wno-unique-object-duplication -Wno-nrvo -Wno-error=format \
-Wno-deprecated -Wno-inconsistent-missing-override -Wno-invalid-offsetof -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-private-field \
-Wno-bitwise-instead-of-logical -Wno-missing-field-initializers -Wno-nan-infinity-disabled -Wno-reorder-ctor \
-Wno-return-type-c-linkage -Wno-sign-compare -Wno-undefined-bool-conversion -Wno-unknown-pragmas -Wno-unused-variable"
  )
  set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()
