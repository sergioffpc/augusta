# yaml-cpp reads the client's and server's startup config file (ADR-0034);
# Boost.DLL finds the directory that file sits in (the running executable's).
find_package(yaml-cpp CONFIG REQUIRED)
find_package(boost_dll CONFIG REQUIRED)

add_library(augusta_config STATIC config.cpp)
target_include_directories(augusta_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_config PRIVATE yaml-cpp::yaml-cpp Boost::dll)
augusta_set_module_scope(augusta_config SHARED)
