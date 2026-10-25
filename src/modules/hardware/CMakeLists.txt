# The machine each Runtime logs at startup (augusta/hardware.h): the OS asked
# through its registry, Win32 and DXGI on Windows (hardware_windows.cpp), its
# /proc, /etc and /sys files on Linux (hardware_linux.cpp), sysctl and Mach on
# macOS (hardware_macos.cpp), behind hardware_platform.h. proc_files.h is
# private to the module: its tests reach it through this directory.
add_library(
  augusta_hardware
  STATIC
  hardware.cpp
  $<$<PLATFORM_ID:Windows>:hardware_windows.cpp>
  $<$<PLATFORM_ID:Darwin>:hardware_macos.cpp>
  $<$<PLATFORM_ID:Linux>:hardware_linux.cpp>
)
target_include_directories(augusta_hardware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(augusta_hardware PRIVATE $<$<PLATFORM_ID:Windows>:advapi32> $<$<PLATFORM_ID:Windows>:dxgi>)
# hardware_windows.cpp includes <windows.h>; keep its min/max macros out of the
# way of the standard library.
target_compile_definitions(augusta_hardware PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
augusta_set_module_scope(augusta_hardware SHARED)
