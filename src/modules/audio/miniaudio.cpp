// miniaudio is a single header (ADR-0028): its implementation is compiled once,
// here. The MA_NO_* options it builds with are the target's own compile
// definitions (CMakeLists.txt), so every file that includes it agrees on them.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
