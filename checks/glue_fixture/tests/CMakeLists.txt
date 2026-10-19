# A test target, outside any executable's own directory, as the ones under
# tests/ are: an end-to-end test links the executable's Runtime, and CASE
# decides whether it links the executable itself too.
add_library(fixture_test INTERFACE)
target_link_libraries(fixture_test INTERFACE fixture_runtime)
if(CASE STREQUAL "TEST_EXECUTABLE")
  target_link_libraries(fixture_test INTERFACE fixture_exe)
endif()
