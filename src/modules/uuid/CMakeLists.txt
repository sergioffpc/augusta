# Boost.Uuid (ADR-0035) takes the UUIDv7s; it stays behind uuid.cpp, so the
# modules that name an ID only see uuid.h.
find_package(boost_uuid CONFIG REQUIRED)

add_library(augusta_uuid STATIC uuid.cpp)
target_include_directories(augusta_uuid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_uuid PRIVATE Boost::uuid)
augusta_set_module_scope(augusta_uuid SHARED)
