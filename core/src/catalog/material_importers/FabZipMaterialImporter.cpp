#include <plynth/core/catalog/FabZip.h>

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/CatalogNames.h"
#include "plynth/core/catalog/MaterialImporter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The text in lower case.
std::string lowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// A name from a zip's file stem, without a trailing quality word and item id ("Muddy_Cloth_abcdefghi_High").
std::string nameFromZipStem(std::string stem)
{
    auto cut = stem.rfind('_');
    if (cut != std::string::npos && fabQualityFromName(lowerCase(stem.substr(cut + 1)))) stem.erase(cut);
    cut = stem.rfind('_');
    if (cut != std::string::npos && std::isupper(static_cast<unsigned char>(stem[0])) != 0) {
        const std::string last = stem.substr(cut + 1);
        const bool idLike = last.size() >= 6 && last.size() <= 10 &&
                            std::all_of(last.begin(), last.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c); });
        if (idLike) stem.erase(cut);
    }
    return catalogNameFromStem(stem);
}

// True when the source is a file with the ending .zip, in any letter case.
bool isZipFile(const ImportSource& source)
{
    if (source.file.empty() || !source.url.empty()) return false;
    return lowerCase(pathToUtf8(source.file.extension())) == ".zip";
}

// Imports a material from a texture zip downloaded from Fab.
class FabZipMaterialImporter final : public MaterialImporter {
public:
    std::string_view id() const override { return kFabImporterId; }
    std::string_view name() const override { return "Fab zip"; }
    std::string_view description() const override
    {
        return "A zip file of texture maps downloaded from fab.com. Takes the largest resolution the options allow.";
    }
    std::vector<std::string> fileSuffixes() const override { return {".zip"}; }

    int score(const ImportSource& source) const override
    {
        if (!isZipFile(source)) return 0;
        const ZipListing listing = listZipEntries(source.file);
        if (!listing.status.ok()) return 0;
        const FabZipContents contents = classifyFabZip(listing.entries);
        if (!contents.models.empty() || contents.textures.empty()) return 0;
        return looksLikeFabMaterialZip(contents) ? 20 : 5;
    }

    Status plan(const ImportSource& source, const fs::path& workFolder, MaterialImportPlan& out) const override
    {
        if (source.file.empty()) return fail(ErrorCode::InvalidValue, "No file was given.");
        std::error_code ec;
        if (!fs::exists(source.file, ec)) {
            return fail(ErrorCode::NotFound, "The file " + pathToUtf8(source.file) + " was not found.");
        }
        const ZipListing listing = listZipEntries(source.file);
        if (!listing.status.ok()) return listing.status;

        FabOptions options;
        if (const Status s = fabOptionsFromJson(source.options, true, options); !s.ok()) return s;

        const FabZipContents contents = classifyFabZip(listing.entries);
        const FabChoice choice = chooseFabMaterial(contents, options);
        if (!choice.status.ok()) {
            out = {};
            out.details = fabChoiceDetails(contents, choice);
            return choice.status;
        }

        const fs::path sourceFile = workFolder / pathFromUtf8(kFabSourceFileName);
        const Json json = fabSourceJson(choice, "material", source.file, workFolder / pathFromUtf8(kFabUnpackFolder), options);
        std::ofstream file(sourceFile, std::ios::binary | std::ios::trunc);
        file << json.dump(2) << "\n";
        file.flush();
        if (!file) return fail(ErrorCode::CannotWrite, "The Fab source file " + pathToUtf8(sourceFile) + " could not be written.");

        out = {};
        out.workerSource = std::string(kMaterialSourceFab);
        out.path = sourceFile;
        out.sourceKind = std::string(kSourceFabZip);
        out.reference = pathToUtf8(source.file.filename());
        out.defaultName = nameFromZipStem(pathToUtf8(source.file.stem()));
        out.license = options.license;
        out.originalIsZip = true;
        out.details = fabChoiceDetails(contents, choice);
        return {};
    }
};

const MaterialImporterRegistrar registrar(std::make_unique<FabZipMaterialImporter>());

}

}
