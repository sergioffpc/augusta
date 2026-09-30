# Removes what `cmake --install` put in place: every file listed in the build
# tree's install manifest (MANIFEST). CMake has no uninstall of its own;
# `make uninstall` runs this with
#   cmake -DMANIFEST=<build dir>/install_manifest.txt -P cmake/Uninstall.cmake
# The manifest records the prefix but not DESTDIR, so a staged install is
# removed with the same DESTDIR in the environment, prepended as
# `cmake --install` prepends it. The directories are left, as a GNU uninstall
# leaves them.
if (NOT EXISTS "${MANIFEST}")
  message(FATAL_ERROR "No install manifest at '${MANIFEST}': nothing is installed from this build tree.")
endif()

file(STRINGS "${MANIFEST}" installed_files)
foreach(installed_file IN LISTS installed_files)
  set(installed_file "$ENV{DESTDIR}${installed_file}")
  message(STATUS "Uninstalling: ${installed_file}")
  file(REMOVE "${installed_file}")
endforeach()
# Gone with what it lists, so a second uninstall says nothing is installed.
file(REMOVE "${MANIFEST}")
