# vcpkg's own x64-windows, built by this project's LLVM toolchain (ADR-0008,
# ADR-0079) instead of MSVC's cl, with LLVM and Mesa built release-only.
# vcpkg.json's overlay puts it in place of the built-in triplet of the same
# name.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/LlvmWindowsToolchain.cmake")

# No Visual Studio developer environment, the default of a chainload, said
# here: a port builds against the MSVC sysroot the toolchain names (ADR-0079),
# and vcpkg then looks for no Visual Studio either.
set(VCPKG_LOAD_VCVARS_ENV OFF)

# The LLVM the project's configure found (cmake/LlvmWindowsToolchain.cmake),
# through the clean environment vcpkg builds a port in. Its path is no part of
# a port's ABI: vcpkg hashes the compiler itself.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED AUGUSTA_LLVM_BIN)
# The sysroot's version, which the toolchain sets, is: vcpkg hashes neither
# the sysroot nor .toolchain/, so only this rebuilds the ports on a bump.
set(VCPKG_ENV_PASSTHROUGH AUGUSTA_WINSYSROOT_VERSION)

# Mesa is loaded only as a Vulkan ICD, lavapipe, from its own DLL, so a release
# Mesa serves a debug augusta with no two CRTs in one image; LLVM is there only
# to build Mesa, which links it statically. Their debug builds, ~8 GB of static
# libs and PDBs, would never be loaded.
if(PORT MATCHES "^(llvm|mesa)$")
  set(VCPKG_BUILD_TYPE release)
endif()

# PhysX builds with /W4 /WX, clang's -Wall -Wextra -Werror under clang-cl, and
# puts these flags after its own (ports/physx): each warning clang 21 adds that
# its code trips is turned off by name, as in triplets/x64-linux.cmake.
if(PORT STREQUAL "physx")
  set(VCPKG_C_FLAGS "-Wno-missing-include-dirs -Wno-unique-object-duplication -Wno-nrvo -Wno-error=format")
  set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()

# Boost.Context, which boost-asio's default spawn feature pulls in and nothing
# of ours calls, switches contexts in MASM that only Visual Studio's ml64
# assembles (llvm-ml64 takes no PROC ... EXPORT): on Windows' fibers instead,
# with no assembly.
if(PORT STREQUAL "boost-context")
  set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DBOOST_CONTEXT_IMPLEMENTATION=winfib)
endif()
