#pragma once

#include "plynth/core/ProjectJson.h"

#include <string>
#include <string_view>

namespace plynth {

// The sentence for a project file saved by a newer Plynth, with the file name and its version filled in.
inline std::string newerFileMessage(std::string_view fileName, long long version)
{
    std::string text(kNewerFileMessage);
    const auto put = [&text](std::string_view mark, const std::string& value) {
        if (const auto at = text.find(mark); at != std::string::npos) text.replace(at, mark.size(), value);
    };
    put("%1", std::string(fileName));
    put("%2", std::to_string(version));
    return text;
}

}
