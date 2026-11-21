#pragma once

#include "plynth/core/Status.h"

#include <filesystem>
#include <string>

namespace plynth::fileio {

// Writes text to path + ".tmp". On success tmp receives that path.
Status writeTemp(const std::filesystem::path& path, const std::string& text, std::filesystem::path& tmp);

// Renames tmp over target. It is tried many times over about half a second, because another program (such as
// the app reading the file) may hold it open for a moment. The temporary file is removed when it gives up.
Status renameOver(const std::filesystem::path& tmp, const std::filesystem::path& target);

// Writes text to a temporary file next to the target and renames it over the target.
Status writeAtomic(const std::filesystem::path& path, const std::string& text);

}
