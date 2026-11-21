#include <plynth/core/Version.h>

namespace plynth {

// Returns the version string that CMake passed in at build time.
std::string_view version() {
    return PLYNTH_VERSION_STRING;
}

}
