#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/MaterialImporter.h"
#include "plynth/core/catalog/MaterialInfo.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Says whether a path ends in .blend, in any letter case.
bool isBlendFile(const fs::path& path)
{
    std::string s = pathToUtf8(path.extension());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s == ".blend";
}

// Takes one material out of a .blend file, without changing the file.
class BlendMaterialImporter : public MaterialImporter {
public:
    std::string_view id() const override { return kBlendMaterialImporterId; }
    std::string_view name() const override { return "Material from a .blend file"; }
    std::string_view description() const override { return "One material of a Blender file, made into texture maps."; }
    std::vector<std::string> fileSuffixes() const override { return {".blend"}; }

    int score(const ImportSource& source) const override
    {
        if (source.file.empty() || !source.url.empty()) return 0;
        std::error_code ec;
        return isBlendFile(source.file) && !fs::is_directory(source.file, ec) ? 10 : 0;
    }

    Status plan(const ImportSource& source, const fs::path&, MaterialImportPlan& out) const override
    {
        if (source.file.empty()) return fail(ErrorCode::InvalidValue, "No file was given.");
        std::error_code ec;
        if (!fs::is_regular_file(source.file, ec)) {
            return fail(ErrorCode::NotFound, "The file " + pathToUtf8(source.file) + " was not found.");
        }
        if (!isBlendFile(source.file)) {
            return fail(ErrorCode::InvalidValue, "The file " + pathToUtf8(source.file) + " is not a .blend file.");
        }
        out = {};
        out.workerSource = std::string(kMaterialSourceBlend);
        out.path = source.file;
        out.sourceKind = std::string(kSourceBlendMaterial);
        out.reference = pathToUtf8(source.file.filename());
        return {};
    }
};

const MaterialImporterRegistrar registrar(std::make_unique<BlendMaterialImporter>());

}

}
