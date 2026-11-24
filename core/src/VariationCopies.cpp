#include "plynth/core/VariationCopies.h"

#include "plynth/core/Exporter.h"
#include "plynth/core/ExportGroups.h"
#include "plynth/core/ProjectFiles.h"

#include <set>
#include <string>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

// Makes the variation copies.
VariationCopies makeVariationCopies(const Project& project, const fs::path& projectFolder, std::span<const std::uint32_t> seeds,
                                    const fs::path& outFolder)
{
    VariationCopies out;
    if (!isProjectFolder(projectFolder)) {
        out.status = fail(ErrorCode::NotAProject, "Save the project before making variation copies.");
        return out;
    }
    if (seeds.empty()) {
        out.status = fail(ErrorCode::InvalidValue, "Give at least one seed.");
        return out;
    }
    if (seeds.size() > kMaxVariationCopies) {
        out.status = fail(ErrorCode::InvalidValue, "At most " + std::to_string(kMaxVariationCopies) + " copies can be made at once.");
        return out;
    }
    std::set<std::uint32_t> seen;
    for (std::uint32_t seed : seeds) {
        if (!seen.insert(seed).second) {
            out.status = fail(ErrorCode::InvalidValue, "The seed " + std::to_string(seed) + " is given twice.");
            return out;
        }
    }

    const std::string stem = exportFileStem(project.settings.name);
    std::vector<fs::path> folders;
    std::error_code ec;
    for (std::uint32_t seed : seeds) {
        const fs::path folder = outFolder / pathFromUtf8(stem + "_v" + std::to_string(seed));
        if (fs::exists(folder, ec)) {
            out.status = fail(ErrorCode::AlreadyExists, "The folder " + pathToUtf8(folder) + " is already there.");
            return out;
        }
        folders.push_back(folder);
    }

    const fs::path options = projectFolder / pathFromUtf8(kExportOptionsFileName);
    const bool hasOptions = fs::is_regular_file(options, ec);
    for (std::size_t i = 0; i < folders.size(); ++i) {
        Project copy = project;
        copy.settings.variation.seed = seeds[i];
        copy.settings.name = project.settings.name + " v" + std::to_string(seeds[i]);
        if (const Status saved = saveProject(copy, folders[i], projectFolder); !saved.ok()) {
            out.status = fail(ErrorCode::CannotWrite, "The copy " + pathToUtf8(folders[i]) + " could not be saved. " + saved.message);
            return out;
        }
        if (hasOptions) {
            std::error_code copyError;
            fs::copy_file(options, folders[i] / pathFromUtf8(kExportOptionsFileName), fs::copy_options::overwrite_existing, copyError);
            if (copyError) {
                out.status = fail(ErrorCode::CannotWrite, "The export options could not be copied to " + pathToUtf8(folders[i]) + ".");
                return out;
            }
        }
    }
    out.folders = std::move(folders);
    return out;
}

}
