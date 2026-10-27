// tbbmalloc_proxy's header, which on Windows makes the linker import its DLL,
// whose loading patches the CRT's malloc to tbbmalloc's: nothing in the
// executable calls it. On Linux it declares nothing, and linking the proxy's
// shared library is enough: its malloc interposes the C library's.
#include <oneapi/tbb/tbbmalloc_proxy.h>  // IWYU pragma: keep
