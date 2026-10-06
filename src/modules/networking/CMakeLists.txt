add_library(augusta_networking STATIC
  free_port.cpp
  networking.cpp
  transport_events.cpp
)
target_include_directories(augusta_networking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_networking PRIVATE
  augusta_logging
  # free_port.cpp's plain socket.
  $<$<PLATFORM_ID:Windows>:ws2_32>
  $<IF:$<TARGET_EXISTS:GameNetworkingSockets::GameNetworkingSockets>,GameNetworkingSockets::GameNetworkingSockets,GameNetworkingSockets::GameNetworkingSockets_s>
)
# NOMINMAX WIN32_LEAN_AND_MEAN: free_port.cpp's <winsock2.h> brings in <windows.h>.
target_compile_definitions(augusta_networking PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_networking SHARED)
