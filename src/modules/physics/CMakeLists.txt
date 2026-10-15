# PhysX collision and movement (ADR-0002): everything that builds and runs a
# physics::World, including a map's static collision from a verified Pack, which
# ships collision data to client and server alike (ADR-0019); see physics.h,
# map.h.
add_library(augusta_physics STATIC physics.cpp map.cpp)
target_include_directories(augusta_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_physics PUBLIC augusta_assets augusta_math PRIVATE augusta_logging)

if(MSVC)
  target_link_libraries(augusta_physics PRIVATE unofficial::omniverse-physx-sdk::sdk)
else()
  # PhysXCharacterKinematic and PhysXExtensions reference each other's
  # symbols (e.g. Cct::Controller needs PxRigidBodyExt, PxMeshOverlapUtil
  # needs Cct-side callbacks); GNU ld's single left-to-right archive pass
  # can't resolve that circular dependency in a fixed topological order
  # the way the Windows linker (lld-link) tolerates, so the whole PhysX SDK
  # needs --start-group/--end-group here (lld-link has no equivalent flag,
  # hence the branch above).
  target_link_libraries(augusta_physics PRIVATE -Wl,--start-group unofficial::omniverse-physx-sdk::sdk -Wl,--end-group)
endif()
augusta_set_module_scope(augusta_physics SHARED)
