# A path as UTF-8 text (see utf8.h), in a module of its own that depends on
# nothing: header-only, so every module and Runtime that writes a path can link
# it.
add_library(augusta_utf8 INTERFACE)
target_include_directories(augusta_utf8 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
augusta_set_module_scope(augusta_utf8 SHARED)
