# A test target, outside any executable's own directory, as the ones under
# tests/ are: CASE decides which Glue it links and whether it declares itself
# an end-to-end test. An INTERFACE library, like every target in this fixture,
# so it needs no compiler - the check reads a target's link dependencies and
# its tag, not how it is built.
add_library(fixture_test INTERFACE)

# Its own executable's runtime, until it has been migrated and links a module
# alone - and then on top of that, the other executable's runtime or the
# executable itself.
if(CASE STREQUAL "TEST_STALE")
  target_link_libraries(fixture_test INTERFACE fixture_exe_module)
else()
  target_link_libraries(fixture_test INTERFACE fixture_exe_runtime)
endif()
if(CASE STREQUAL "TEST_FOREIGN")
  target_link_libraries(fixture_test INTERFACE fixture_other_runtime)
elseif(CASE STREQUAL "TEST_EXECUTABLE")
  target_link_libraries(fixture_test INTERFACE fixture_exe)
endif()

if(CASE MATCHES "^TEST_(END_TO_END|FOREIGN|EXECUTABLE)$")
  augusta_set_end_to_end_test(fixture_test fixture_exe)
endif()
