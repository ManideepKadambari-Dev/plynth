#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/MaterialImporter.h"
#include "plynth/core/catalog/MaterialInfo.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The ending of a path in lower case, with the dot.
std::string lowerSuffix(const fs::path& path)
{
    std::string s = pathToUtf8(path.extension());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Says whether a path ends like a picture a texture set may hold.
bool isImageFile(const fs::path& path)
{
    const std::string suffix = lowerSuffix(path);
    const auto all = textureImageSuffixes();
    return std::find(all.begin(), all.end(), suffix) != all.end();
}

// Says whether a folder holds a picture directly in it or one folder down.
bool holdsImage(const fs::path& folder)
{
    std::error_code ec;
    std::vector<fs::path> subfolders;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec)) {
            if (isImageFile(entry.path())) return true;
        } else if (entry.is_directory(ec)) {
            subfolders.push_back(entry.path());
        }
    }
    for (const fs::path& sub : subfolders) {
        for (const auto& entry : fs::directory_iterator(sub, ec)) {
            if (entry.is_regular_file(ec) && isImageFile(entry.path())) return true;
        }
    }
    return false;
}

// The name of a file or folder without a trailing separator.
std::string nameOf(const fs::path& path)
{
    fs::path p = path;
    if (!p.has_filename()) p = p.parent_path();
    return pathToUtf8(p.filename());
}

// Imports a folder, zip or picture file of images that make one material.
class TextureSetImporter : public MaterialImporter {
public:
    std::string_view id() const override { return kTextureSetImporterId; }
    std::string_view name() const override { return "Texture set"; }
    std::string_view description() const override
    {
        return "A folder, zip file or picture file of texture maps such as base color, normal and roughness.";
    }
    std::vector<std::string> fileSuffixes() const override
    {
        std::vector<std::string> out;
        for (const std::string_view s : textureImageSuffixes()) out.emplace_back(s);
        out.emplace_back(".zip");
        return out;
    }

    int score(const ImportSource& source) const override
    {
        if (source.file.empty() || !source.url.empty()) return 0;
        std::error_code ec;
        if (fs::is_directory(source.file, ec)) return holdsImage(source.file) ? 10 : 0;
        return (isImageFile(source.file) || lowerSuffix(source.file) == ".zip") ? 10 : 0;
    }

    Status plan(const ImportSource& source, const fs::path&, MaterialImportPlan& out) const override
    {
        if (source.file.empty()) return fail(ErrorCode::InvalidValue, "No file or folder was given.");
        std::error_code ec;
        if (!fs::exists(source.file, ec)) {
            return fail(ErrorCode::NotFound, "The file " + pathToUtf8(source.file) + " was not found.");
        }
        out = {};
        out.workerSource = std::string(kMaterialSourceTextureSet);
        out.path = source.file;
        out.sourceKind = std::string(kSourceTextureSet);
        const bool folder = fs::is_directory(source.file, ec);
        out.reference = nameOf(source.file);
        if (folder) {
            if (!holdsImage(source.file)) {
                return fail(ErrorCode::InvalidValue, "The folder " + pathToUtf8(source.file) + " holds no images.");
            }
            out.defaultName = catalogNameFromStem(nameOf(source.file));
            return {};
        }
        const std::string suffix = lowerSuffix(source.file);
        if (!isImageFile(source.file) && suffix != ".zip") {
            return fail(ErrorCode::InvalidValue, "The ending \"" + suffix + "\" is not an image or zip file a texture set comes in.");
        }
        out.defaultName = catalogNameFromStem(pathToUtf8(source.file.stem()));
        out.originalIsZip = suffix == ".zip";
        return {};
    }
};

const MaterialImporterRegistrar registrar(std::make_unique<TextureSetImporter>());

}

}
