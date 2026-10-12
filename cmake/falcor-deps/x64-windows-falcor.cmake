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
