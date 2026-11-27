#include "plynth/core/VariationCopies.h"

#include "plynth/core/Exporter.h"
#include "plynth/core/ExportGroups.h"
#include "plynth/core/ProjectFiles.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The text without spaces and tabs at either end.
std::string_view trimmed(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

// Reads one seed: a whole number from 0 to 4294967295 and nothing else. Empty for any other text.
std::optional<std::uint32_t> readSeed(std::string_view text)
{
    unsigned long long value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [stop, error] = std::from_chars(begin, end, value);
    if (text.empty() || error != std::errc() || stop != end || value > (std::numeric_limits<std::uint32_t>::max)()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

// The refusal for a list with more seeds than one run makes.
Status tooManySeeds()
{
    return fail(ErrorCode::InvalidValue, "At most " + std::to_string(kMaxVariationCopies) + " copies can be made at once.");
}

}

// Reads a typed list of seeds and ranges.
SeedListRead parseSeedList(std::string_view text)
{
    SeedListRead out;
    if (trimmed(text).empty()) {
        out.status = fail(ErrorCode::InvalidValue, "Give at least one seed.");
        return out;
    }
    std::set<std::uint32_t> seen;
    const auto add = [&](std::uint32_t seed) {
        if (!seen.insert(seed).second) return fail(ErrorCode::InvalidValue, "The seed " + std::to_string(seed) + " is given twice.");
        if (out.seeds.size() >= kMaxVariationCopies) return tooManySeeds();
        out.seeds.push_back(seed);
        return Status{};
    };
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::string_view part = trimmed(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        const std::size_t dash = part.find('-');
        if (dash == std::string_view::npos || dash == 0) {
            const std::optional<std::uint32_t> seed = readSeed(part);
            if (!seed) {
                out.status = fail(ErrorCode::InvalidValue, "The seed '" + std::string(part) + "' is not a whole number from 0 to 4294967295.");
                out.seeds.clear();
                return out;
            }
            if (const Status added = add(*seed); !added.ok()) {
                out.status = added;
                out.seeds.clear();
                return out;
            }
        } else {
            const std::optional<std::uint32_t> from = readSeed(trimmed(part.substr(0, dash)));
            const std::optional<std::uint32_t> to = readSeed(trimmed(part.substr(dash + 1)));
            if (!from || !to || *to < *from) {
                out.status = fail(ErrorCode::InvalidValue, "The range '" + std::string(part) +
                                                               "' must be two whole numbers from 0 to 4294967295, the first not above the second.");
                out.seeds.clear();
                return out;
            }
            // A long range is refused before it is counted out one by one.
            if (static_cast<unsigned long long>(*to) - *from + 1 > kMaxVariationCopies - out.seeds.size()) {
                out.status = tooManySeeds();
                out.seeds.clear();
                return out;
            }
            for (unsigned long long seed = *from; seed <= *to; ++seed) {
                if (const Status added = add(static_cast<std::uint32_t>(seed)); !added.ok()) {
                    out.status = added;
                    out.seeds.clear();
                    return out;
                }
            }
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

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
