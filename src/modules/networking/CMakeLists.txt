add_library(augusta_networking STATIC
  networking.cpp
)
target_include_directories(augusta_networking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_networking PRIVATE
  augusta_logging
  $<IF:$<TARGET_EXISTS:GameNetworkingSockets::GameNetworkingSockets>,GameNetworkingSockets::GameNetworkingSockets,GameNetworkingSockets::GameNetworkingSockets_s>
)
