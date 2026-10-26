# vcpkg's own x64-windows, with LLVM and Mesa built release-only (ADR-0008).
# vcpkg.json's overlay puts it in place of the built-in triplet of the same
# name.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

# Mesa is loaded only as a Vulkan ICD, lavapipe, from its own DLL, so a release
# Mesa serves a debug augusta with no two CRTs in one image; LLVM is there only
# to build Mesa, which links it statically. Their debug builds, ~8 GB of static
# libs and PDBs, would never be loaded.
if(PORT MATCHES "^(llvm|mesa)$")
  set(VCPKG_BUILD_TYPE release)
endif()
