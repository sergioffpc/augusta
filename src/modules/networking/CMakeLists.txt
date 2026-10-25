# free_port.cpp's socket.
find_package(boost_asio CONFIG REQUIRED)
add_library(augusta_networking STATIC free_port.cpp networking.cpp transport_events.cpp)
target_include_directories(augusta_networking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(
  augusta_networking
  # Every send and receive answers with failure.h's model, and takes faults.h's.
  PUBLIC augusta_failure
  PRIVATE
    augusta_logging
    Boost::asio
    $<IF:$<TARGET_EXISTS:GameNetworkingSockets::GameNetworkingSockets>,GameNetworkingSockets::GameNetworkingSockets,GameNetworkingSockets::GameNetworkingSockets_s>
)
# NOMINMAX WIN32_LEAN_AND_MEAN: Asio's <winsock2.h> brings in <windows.h>.
target_compile_definitions(augusta_networking PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_networking SHARED)
