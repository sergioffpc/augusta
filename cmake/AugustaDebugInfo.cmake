# Splits a Linux executable's debug info off into <executable>.debug beside it
# (ADR-0047): augustad's and augustac's, each published with its release. The
# executable keeps its symbol table, which augustad's crash handler symbolizes
# with, and a .gnu_debuglink naming the file. The build ID is what matches a
# core dump to the executable and its debug info.
function(augusta_split_debug_info target)
  target_link_options(${target} PRIVATE -Wl,--build-id=sha1)
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.debug
    COMMAND ${CMAKE_OBJCOPY} --strip-debug --add-gnu-debuglink=$<TARGET_FILE:${target}>.debug $<TARGET_FILE:${target}>
    VERBATIM
  )
endfunction()
