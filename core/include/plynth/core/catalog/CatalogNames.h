#pragma once

#include <string>
#include <string_view>

namespace plynth {

// A name made from a file name stem: "_" and "-" become spaces, the first letter upper case.
// "red_chair-02" gives "Red chair 02". Shared by the asset and material catalogs.
std::string catalogNameFromStem(std::string_view stem);

}
