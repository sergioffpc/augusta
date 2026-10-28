// Includes a header from the fixture's own directory, one from the sysroot
// stand-in and, with FIXTURE_VISUAL_STUDIO, one from the Visual Studio
// stand-in. header_origin_fixture.cmake writes the last two.
#include "fixture.h"

#include <sysroot_header.h>

#ifdef FIXTURE_VISUAL_STUDIO
#include <visual_studio_header.h>
#endif

int fixture() { return fixture_own() + sysroot_header(); }
