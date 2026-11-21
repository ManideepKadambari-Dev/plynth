#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/AssetJobs.h"
#include "plynth/core/catalog/Importer.h"
#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The endings of model files, best first. A folder with several kinds uses the first kind that is there.
const std::vector<std::string>& modelSuffixes()
{
    static const std::vector<std::string> suffixes = {".blend", ".fbx", ".glb", ".gltf", ".obj"};
    return suffixes;
}

// The ending of a path in lower case, with the dot.
std::string lowerSuffix(const fs::path& path)
{
    std::string s = pathToUtf8(path.extension());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The format id of an ending.
std::string formatOf(const std::string& suffix)
{
    if (suffix == ".blend") return std::string(kFormatBlend);
    if (suffix == ".fbx") return std::string(kFormatFbx);
    if (suffix == ".obj") return std::string(kFormatObj);
    if (suffix == ".gltf" || suffix == ".glb") return std::string(kFormatGltf);
    if (suffix == ".zip") return std::string(kFormatZip);
    return {};
}

// Whether a lower case ending is one of the model endings.
bool isModelSuffix(const std::string& suffix)
{
    const auto& all = modelSuffixes();
    return std::find(all.begin(), all.end(), suffix) != all.end();
}

// The model files directly in a folder and one folder down, sorted by path.
std::vector<fs::path> modelFilesIn(const fs::path& folder)
{
    std::vector<fs::path> found;
    std::error_code ec;
    std::vector<fs::path> subfolders;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec)) {
            if (isModelSuffix(lowerSuffix(entry.path()))) found.push_back(entry.path());
        } else if (entry.is_directory(ec)) {
            subfolders.push_back(entry.path());
        }
    }
    for (const fs::path& sub : subfolders) {
        for (const auto& entry : fs::directory_iterator(sub, ec)) {
            if (entry.is_regular_file(ec) && isModelSuffix(lowerSuffix(entry.path()))) found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

// The name of a file or folder without a trailing separator.
std::string nameOf(const fs::path& path)
{
    fs::path p = path;
    if (!p.has_filename()) p = p.parent_path();
    return pathToUtf8(p.filename());
}

// Imports the user's own model files, zips and folders.
class LocalFileImporter : public Importer {
public:
    std::string_view id() const override { return kLocalFileImporterId; }
    std::string_view name() const override { return "Own files"; }
    std::string_view description() const override
    {
        return "Model files (.blend, .fbx, .obj, .gltf, .glb), zip files and folders that hold a model.";
    }
    std::vector<std::string> fileSuffixes() const override { return {".blend", ".fbx", ".glb", ".gltf", ".obj", ".zip"}; }

    int score(const ImportSource& source) const override
    {
        if (source.file.empty() || !source.url.empty()) return 0;
        std::error_code ec;
        if (fs::is_directory(source.file, ec)) return modelFilesIn(source.file).empty() ? 0 : 10;
        const std::string suffix = lowerSuffix(source.file);
        return (isModelSuffix(suffix) || suffix == ".zip") ? 10 : 0;
    }

    Status plan(const ImportSource& source, const fs::path&, ImportPlan& out) const override
    {
        std::error_code ec;
        if (source.file.empty()) return fail(ErrorCode::InvalidValue, "No file was given.");
        if (!fs::exists(source.file, ec)) {
            return fail(ErrorCode::NotFound, "The file " + pathToUtf8(source.file) + " was not found.");
        }
        out = {};
        out.sourceKind = std::string(kSourceLocalFile);
        out.reference = nameOf(source.file);

        if (fs::is_directory(source.file, ec)) {
            const std::vector<fs::path> models = modelFilesIn(source.file);
            for (const std::string& suffix : modelSuffixes()) {
                std::vector<fs::path> best;
                for (const fs::path& m : models) {
                    if (lowerSuffix(m) == suffix) best.push_back(m);
                }
                if (best.empty()) continue;
                if (best.size() > 1) {
                    return fail(ErrorCode::InvalidValue,
                                "The folder " + pathToUtf8(source.file) + " holds more than one " + suffix + " file (" +
                                    pathToUtf8(best[0].filename()) + " and " + pathToUtf8(best[1].filename()) +
                                    "). Import the one you want by itself.");
                }
                out.file = best[0];
                out.format = formatOf(suffix);
                out.defaultName = catalogNameFromStem(nameOf(source.file));
                return {};
            }
            return fail(ErrorCode::InvalidValue, "The folder " + pathToUtf8(source.file) + " holds no model file.");
        }

        const std::string suffix = lowerSuffix(source.file);
        out.format = formatOf(suffix);
        if (out.format.empty()) {
            return fail(ErrorCode::InvalidValue, "The ending \"" + suffix + "\" is not a model file this importer reads.");
        }
        out.file = source.file;
        out.defaultName = catalogNameFromStem(pathToUtf8(source.file.stem()));
        out.originalIsZip = suffix == ".zip";
        return {};
    }
};

const ImporterRegistrar registrar(std::make_unique<LocalFileImporter>());

}

}
