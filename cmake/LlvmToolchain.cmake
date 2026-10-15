# The Linux toolchain, LLVM's alone (ADR-0008): clang compiles against libc++,
# lld links, compiler-rt and libunwind are the runtime, and LLVM's binary tools
# replace binutils. vcpkg's toolchain chainloads it twice: for this project
# (the linux presets) and for every port (triplets/), so the dependencies are
# built with the same compiler and standard library they are linked with.
#
# libc++, libc++abi and libunwind link statically: augustad then needs nothing
# from the system beyond glibc (ADR-0054).

# CMake reads a toolchain more than once in a configure; the flags below are
# appended once.
include_guard(GLOBAL)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Found before CMake looks for binutils, which then keeps these.
foreach(
  tool
  IN
  ITEMS AR RANLIB NM OBJCOPY OBJDUMP READELF STRIP ADDR2LINE
)
  string(TOLOWER "${tool}" name)
  find_program(CMAKE_${tool} llvm-${name} REQUIRED)
endforeach()

string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
foreach(kind IN ITEMS EXE SHARED MODULE)
  string(
    APPEND CMAKE_${kind}_LINKER_FLAGS_INIT
    " -fuse-ld=lld -stdlib=libc++ -rtlib=compiler-rt -unwindlib=libunwind"
    " -static-libstdc++ -static-libgcc"
  )
endforeach()

# A port's build (vcpkg passes it the triplet's architecture) also gets what
# vcpkg's own Linux toolchain, which this one stands in for, sets up: the
# target processor, position-independent code and the triplet's flags.
if(DEFINED VCPKG_TARGET_ARCHITECTURE)
  include("${CMAKE_CURRENT_LIST_DIR}/../third_party/vcpkg/scripts/toolchains/linux.cmake")
endif()
