find_package(glm CONFIG REQUIRED)

add_library(augusta_math INTERFACE)
target_include_directories(augusta_math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_math INTERFACE glm::glm)
augusta_set_module_scope(augusta_math SHARED)
