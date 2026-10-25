# Copies every DLL in BIN_DIR into DESTINATION with its timestamp, skipping one
# already there with the same timestamp, so a rerun copies only what changed:
#
#   cmake -DBIN_DIR=<dir> -DDESTINATION=<dir> -P CopyDlls.cmake
foreach(var BIN_DIR DESTINATION)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "CopyDlls.cmake needs -D${var}=...")
  endif()
endforeach()

file(GLOB dlls "${BIN_DIR}/*.dll")
file(COPY ${dlls} DESTINATION ${DESTINATION})
