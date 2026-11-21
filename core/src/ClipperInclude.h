#pragma once

// The one place Clipper2 is included. Its header has unreachable code that MSVC warns about at /W4, so the
// warning is switched off around it.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
