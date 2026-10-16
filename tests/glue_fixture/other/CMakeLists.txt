# A second executable, for the cases about a test that reaches across: its own
# three are Glue to it, and Glue a test of the first executable may not link
# whatever kind it declares itself.
add_library(fixture_other INTERFACE)
add_library(fixture_other_application INTERFACE)
add_library(fixture_other_runtime INTERFACE)

augusta_set_executable_glue(fixture_other)
