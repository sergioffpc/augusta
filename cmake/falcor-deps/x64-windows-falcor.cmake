# The triplet of Falcor's dependencies (vcpkg.json beside this file), linked as
# Falcor's external/CMakeLists.txt imports them: these ports as DLLs, the rest
# static, as in the packman bundle they replace.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
if(PORT MATCHES "^(assimp|blosc|freeimage|openexr|openvdb|tbb)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
  set(VCPKG_LIBRARY_LINKAGE static)
endif()
# OpenVDB 12 at its previous ABI, 11: Falcor converts grids with packman's
# NanoVDB 32.3 (Scene/Volume/Grid.cpp, nanovdb::openToNanoVDB), which reads
# Tree::nodeCount() as the std::vector<Index32> ABI 12 widened to Index64.
if(PORT STREQUAL "openvdb")
  set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DOPENVDB_ABI_VERSION_NUMBER=11 -DOPENVDB_USE_DEPRECATED_ABI_11=ON)
endif()
