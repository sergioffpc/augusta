# The executable's Runtime module: every target in it is the executable's
# Runtime, its application among them, as the client and server modules hold
# augustac's and augustad's.
add_library(fixture_runtime INTERFACE)
add_library(fixture_runtime_application INTERFACE)
