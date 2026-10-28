# The Windows toolchain, the pinned LLVM's (ADR-0008, ADR-0079): clang-cl
# compiles, lld-link links, llvm-lib archives, llvm-rc compiles resources and
# llvm-mt merges manifests. vcpkg's toolchain chainloads it twice, as
# cmake/LlvmToolchain.cmake on Linux: for this project (the windows presets)
# and for every port (triplets/x64-windows.cmake), so the dependencies are
# built by the compiler our code is. Both build against the MSVC sysroot that
# `make toolchain` links into .toolchain/sysroot (the STL, the CRT, ATL and the
# Windows SDK), never a Visual Studio's, installed or not.

# CMake reads a toolchain more than once in a configure; the lookups below are
# made once.
include_guard(GLOBAL)

# vcpkg builds each port in an environment of its own, whose PATH is not the
# user's. The configure that finds LLVM on PATH, the project's, names its
# directory in AUGUSTA_LLVM_BIN before vcpkg installs the ports, and the triplet
# passes that variable through to their builds: every port is built by the
# LLVM the project is. PATH alone otherwise, never CMake's own search paths:
# vcpkg's tools or another LLVM installed under Program Files.
find_program(
  AUGUSTA_CLANG_CL
  clang-cl
  HINTS
  ENV AUGUSTA_LLVM_BIN
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  REQUIRED
)
cmake_path(GET AUGUSTA_CLANG_CL PARENT_PATH augusta_llvm_bin)
set(ENV{AUGUSTA_LLVM_BIN} "${augusta_llvm_bin}")

set(CMAKE_C_COMPILER "${AUGUSTA_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${AUGUSTA_CLANG_CL}")

# Found before CMake looks for MSVC's link, lib, rc and mt, which then keeps
# these: the ones beside clang-cl, of the same LLVM.
find_program(CMAKE_LINKER lld-link HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_AR llvm-lib HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_RC_COMPILER llvm-rc HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_MT llvm-mt HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
# MASM for a port that assembles .asm (blake3), as ml64 would, which only a
# Visual Studio has.
find_program(CMAKE_ASM_MASM_COMPILER llvm-ml64 HINTS "${augusta_llvm_bin}" NO_DEFAULT_PATH REQUIRED)

# The MSVC sysroot (ADR-0079), the checkout's: a port's build reads this file
# from the checkout too, through the triplet. Named to clang-cl and lld-link
# with /winsysroot, which also keeps them from %INCLUDE%, %LIB% and a Visual
# Studio they would otherwise find; llvm-rc takes no /winsysroot, so the
# headers a resource script includes are named one by one. Unquoted in the
# flags, which vcpkg hands on to Meson and to make as well as to CMake, so the
# checkout's path cannot hold a space.
cmake_path(ABSOLUTE_PATH CMAKE_CURRENT_LIST_DIR NORMALIZE OUTPUT_VARIABLE augusta_source_dir)
cmake_path(GET augusta_source_dir PARENT_PATH augusta_source_dir)
set(AUGUSTA_WINSYSROOT "${augusta_source_dir}/.toolchain/sysroot")
if(NOT EXISTS "${AUGUSTA_WINSYSROOT}/.installed")
  message(FATAL_ERROR "${AUGUSTA_WINSYSROOT} not found: run `make toolchain` first")
endif()
if(AUGUSTA_WINSYSROOT MATCHES " ")
  message(FATAL_ERROR "The checkout's path holds a space, which the sysroot's flags cannot: ${AUGUSTA_WINSYSROOT}")
endif()
# Its version, for the triplet's port ABI: vcpkg hashes this file and the
# compiler, not the sysroot they read, so a sysroot bump rebuilds every port
# only through this variable (triplets/x64-windows.cmake).
file(STRINGS "${AUGUSTA_WINSYSROOT}/.installed" augusta_winsysroot_version LIMIT_COUNT 1)
set(ENV{AUGUSTA_WINSYSROOT_VERSION} "${augusta_winsysroot_version}")
# The debug CRT's DLLs, which no Windows has without a Visual Studio: what a
# Debug build's executables load, tests/CMakeLists.txt copies beside its tests.
file(GLOB AUGUSTA_WINSYSROOT_DEBUG_RUNTIME "${AUGUSTA_WINSYSROOT}/VCR/*/bin/x86_64")

file(GLOB augusta_rc_includes "${AUGUSTA_WINSYSROOT}/VC/Tools/MSVC/*/include")
file(GLOB augusta_sdk_include "${AUGUSTA_WINSYSROOT}/Windows Kits/10/Include/*")
list(TRANSFORM augusta_sdk_include APPEND "/um" OUTPUT_VARIABLE augusta_sdk_um)
list(TRANSFORM augusta_sdk_include APPEND "/shared" OUTPUT_VARIABLE augusta_sdk_shared)
list(TRANSFORM augusta_sdk_include APPEND "/ucrt" OUTPUT_VARIABLE augusta_sdk_ucrt)
list(APPEND augusta_rc_includes ${augusta_sdk_um} ${augusta_sdk_shared} ${augusta_sdk_ucrt})
set(CMAKE_RC_STANDARD_INCLUDE_DIRECTORIES ${augusta_rc_includes})

foreach(kind EXE SHARED MODULE)
  string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " /winsysroot:${AUGUSTA_WINSYSROOT} /lldignoreenv")
endforeach()

# A port's build (vcpkg passes it the triplet's architecture) also gets what
# vcpkg's own Windows toolchain, which this one stands in for, sets up: the
# CRT, the MSVC flags and the triplet's. It leaves out /MP, which clang-cl does
# not take, as the compiler's path ends in clang-cl.exe. It caches the compile
# flags it makes, which an _INIT would never reach, from the triplet's.
if(DEFINED VCPKG_TARGET_ARCHITECTURE)
  string(APPEND VCPKG_C_FLAGS " /winsysroot${AUGUSTA_WINSYSROOT}")
  string(APPEND VCPKG_CXX_FLAGS " /winsysroot${AUGUSTA_WINSYSROOT}")
  include("${CMAKE_CURRENT_LIST_DIR}/../third_party/vcpkg/scripts/toolchains/windows.cmake")
else()
  string(APPEND CMAKE_C_FLAGS_INIT " /winsysroot${AUGUSTA_WINSYSROOT}")
  string(APPEND CMAKE_CXX_FLAGS_INIT " /winsysroot${AUGUSTA_WINSYSROOT}")
endif()
