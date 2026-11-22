#include "plynth/core/catalog/CatalogNames.h"

namespace plynth {

// Makes a name from a file name stem.
std::string catalogNameFromStem(std::string_view stem)
{
    std::string name;
    bool pendingSpace = false;
    for (const char raw : stem) {
        if (raw == '_' || raw == '-' || raw == ' ') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !name.empty()) name.push_back(' ');
        pendingSpace = false;
        name.push_back(raw);
    }
    if (name.empty()) return "Asset";
    if (name.front() >= 'a' && name.front() <= 'z') name.front() = static_cast<char>(name.front() - 'a' + 'A');
    return name;
}

}
