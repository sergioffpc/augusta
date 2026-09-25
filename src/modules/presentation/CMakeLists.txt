add_library(augusta_presentation STATIC
  correction.cpp
  interpolation.cpp
  presentation.cpp
)
target_include_directories(augusta_presentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_presentation
  PUBLIC augusta_prediction augusta_audio
  PRIVATE augusta_animation nvtx3-cpp $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
# NOMINMAX: nvtx3 pulls in <windows.h> on Windows, whose min/max macros would
# break std::ranges::max in presentation.cpp.
target_compile_definitions(augusta_presentation PRIVATE NOMINMAX)
augusta_set_module_scope(augusta_presentation CLIENT_ONLY)
