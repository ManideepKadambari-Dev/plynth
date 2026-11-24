#pragma once

#include "Project.h"
#include "Status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace plynth {

// Variation copies (DESIGN.md section 7.15, "to fill a building or street quickly"): the project saved once per
// seed as outFolder/<Stem>_v<seed>, where Stem is exportFileStem of the project name, with that seed set and the
// name "<name> v<seed>", so the exported FBX names of a street never clash. Each copy gets the plan images and
// export-options.json; it points to library assets by id and copies none.

inline constexpr std::size_t kMaxVariationCopies = 100;

struct VariationCopies {
    Status status;
    std::vector<std::filesystem::path> folders; // in the order of the seeds, empty unless all were made
};

// Makes the copies. NotAProject when projectFolder holds no saved project (save it first). InvalidValue for no
// seeds, more than kMaxVariationCopies or a seed given twice. AlreadyExists, before anything is written, when
// one of the folders is already there. CannotWrite when a copy could not be saved. The source project folder is
// never changed.
VariationCopies makeVariationCopies(const Project& project, const std::filesystem::path& projectFolder,
                                    std::span<const std::uint32_t> seeds, const std::filesystem::path& outFolder);

}
