#include <plynth/core/catalog/FabZip.h>

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/AssetJobs.h"
#include "plynth/core/catalog/CatalogNames.h"
#include "plynth/core/catalog/Importer.h"
#include "plynth/core/catalog/Integrations.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// True for a word that looks like a Fab item id: six to ten lower case letters and digits.
bool looksLikeFabId(const std::string& word)
{
    if (word.size() < 6 || word.size() > 10) return false;
    return std::all_of(word.begin(), word.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c); });
}

// A name from a model stem. A trailing item id ("Tan_Stool_03_abcdefg") is dropped when the words before it
// start with a capital, so the name reads "Tan stool 03".
std::string nameFromStem(const std::string& stem)
{
    const auto cut = stem.rfind('_');
    if (cut != std::string::npos && looksLikeFabId(stem.substr(cut + 1)) &&
        std::isupper(static_cast<unsigned char>(stem[0])) != 0) {
        return catalogNameFromStem(stem.substr(0, cut));
    }
    return catalogNameFromStem(stem);
}

// Writes the worker's source file.
Status writeSource(const fs::path& file, const Json& json)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << json.dump(2) << "\n";
    out.flush();
    if (!out) return fail(ErrorCode::CannotWrite, "The Fab source file " + pathToUtf8(file) + " could not be written.");
    return {};
}

// True when the source is a file with the ending .zip, in any letter case.
bool isZipFile(const ImportSource& source)
{
    if (source.file.empty() || !source.url.empty()) return false;
    std::string ending = pathToUtf8(source.file.extension());
    std::transform(ending.begin(), ending.end(), ending.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ending == ".zip";
}

// Imports a model from a zip downloaded from Fab.
class FabZipImporter final : public Importer {
public:
    std::string_view id() const override { return kFabImporterId; }
    std::string_view name() const override { return "Fab zip"; }
    std::string_view description() const override
    {
        return "A zip file downloaded from fab.com. Picks the model version and the textures that go with it.";
    }
    std::vector<std::string> fileSuffixes() const override { return {".zip"}; }

    int score(const ImportSource& source) const override
    {
        if (!isZipFile(source)) return 0;
        const ZipListing listing = listZipEntries(source.file);
        if (!listing.status.ok()) return 0;
        const FabZipContents contents = classifyFabZip(listing.entries);
        if (!contents.models.empty()) return looksLikeFabModelZip(contents) ? 20 : 5;
        if (!contents.unreadableModels.empty() || !contents.innerZips.empty()) return 20;
        return 0;
    }

    Status plan(const ImportSource& source, const fs::path& workFolder, ImportPlan& out) const override
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
        const FabChoice choice = chooseFabModel(contents, options);
        if (!choice.status.ok()) {
            out = {};
            out.details = fabChoiceDetails(contents, choice);
            return choice.status;
        }

        const fs::path sourceFile = workFolder / pathFromUtf8(kFabSourceFileName);
        const Json json = fabSourceJson(choice, "model", source.file, workFolder / pathFromUtf8(kFabUnpackFolder), options);
        if (const Status s = writeSource(sourceFile, json); !s.ok()) return s;

        out = {};
        out.file = sourceFile;
        out.format = std::string(kFormatFab);
        out.sourceKind = std::string(kSourceFabZip);
        out.reference = pathToUtf8(source.file.filename());
        out.defaultName = nameFromStem(choice.model->stem);
        out.license = options.license;
        out.originalIsZip = true;
        out.details = fabChoiceDetails(contents, choice);
        return {};
    }
};

// The Fab entry of the Integrations tab.
class FabIntegration final : public Integration {
public:
    std::string_view id() const override { return kFabIntegrationId; }
    std::string_view name() const override { return "Fab"; }
    std::string_view description() const override
    {
        return "Models and materials from zip files you download from fab.com in your browser. Plynth never downloads from Fab itself.";
    }
    int order() const override { return 30; }
    std::vector<std::string> assetImporters() const override { return {std::string(kFabImporterId)}; }
    std::vector<std::string> materialImporters() const override { return {std::string(kFabImporterId)}; }
    bool takesAddresses() const override { return false; }
    std::string_view setupItem() const override { return {}; }
    std::string_view site() const override { return kFabSite; }
    Json defaultOptions() const override { return fabOptionsToJson(FabOptions{}); }
    Status checkOptions(const Json& options, OptionUse use) const override
    {
        FabOptions scratch;
        return fabOptionsFromJson(options, use == OptionUse::Import, scratch);
    }
};

const ImporterRegistrar importerRegistrar(std::make_unique<FabZipImporter>());
const IntegrationRegistrar integrationRegistrar(std::make_unique<FabIntegration>());

}

}
