#pragma once

#include <string_view>

namespace plynth {

// Returns the Plynth version, for example "0.0.1". It comes from the version set in the root CMakeLists.txt.
std::string_view version();

}
