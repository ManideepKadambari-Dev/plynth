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
#include <utility>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// A name from a model's item. Its last word is dropped only when it is the zip's item id ("Tan_Stool_abcdefg").
std::string nameFromItem(const std::string& item, const std::string& itemId)
{
    const auto cut = item.find_last_of("_- ");
    if (!itemId.empty() && cut != std::string::npos && cut > 0 && item.substr(cut + 1) == itemId) {
        return catalogNameFromStem(item.substr(0, cut));
    }
    return catalogNameFromStem(item);
}

// The labels and help of the Fab options, for the app's fields.
std::vector<OptionField> fabOptionFields()
{
    OptionField formats{"formats", "Preferred format", "Which model format to take when a zip has several.", OptionKind::Choice, {}, 0, 0};
    const std::pair<std::string_view, const char*> firsts[] = {{"fbx", "FBX first"}, {"gltf", "glTF first"}, {"obj", "OBJ first"},
                                                               {"blend", "Blend first"}};
    for (const auto& [first, label] : firsts) {
        Json order = Json::array({std::string(first)});
        for (const auto format : kFabFormats)
            if (format != first) order.push_back(std::string(format));
        formats.choices.push_back(OptionChoice{order, label});
    }

    OptionField quality{"quality", "Model quality", "Which quality to take when a zip has several; the nearest one is taken when it is missing.",
                        OptionKind::Choice, {}, 0, 0};
    const std::pair<FabQuality, const char*> qualities[] = {
        {FabQuality::Low, "Low"}, {FabQuality::Medium, "Medium"}, {FabQuality::High, "High"}, {FabQuality::Raw, "Raw"}};
    for (const auto& [q, label] : qualities) quality.choices.push_back(OptionChoice{std::string(fabQualityName(q)), label});

    OptionField size{"max_texture_size", "Largest texture", "The largest texture resolution to take when a zip has several.",
                     OptionKind::Choice, {}, 0, 0};
    for (const int pixels : {512, 1024, 2048, 4096, 8192, 16384})
        size.choices.push_back(OptionChoice{pixels, std::to_string(pixels) + " pixels"});
    size.choices.push_back(OptionChoice{nullptr, "No limit"});

    OptionField normal{"normal", "Normal maps", "Whether the normal maps are OpenGL or DirectX. Automatic lets Plynth tell from the zip.",
                       OptionKind::Choice, {}, 0, 0};
    normal.choices.push_back(OptionChoice{"auto", "Automatic"});
    normal.choices.push_back(OptionChoice{"opengl", "OpenGL"});
    normal.choices.push_back(OptionChoice{"directx", "DirectX"});

    OptionField license{"license", "License",
                        "The license recorded for zips you pick as Fab by hand. A zip that Plynth only guesses is from Fab gets Unknown.",
                        OptionKind::License, {}, 0, 0};
    return {formats, quality, size, normal, license};
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

    // How well a zip fits: 20 for one that looks like Fab or that only Fab can explain, 5 for another zip
    // with a model, 0 for anything else.
    int score(const ImportSource& source) const override
    {
        if (!isZipFile(source)) return 0;
        const ZipListing listing = listZipEntries(source.file);
        if (listing.badName) return 20;
        if (!listing.status.ok()) return 0;
        const FabZipContents contents = classifyFabZip(listing.entries);
        if (!contents.models.empty()) return looksLikeFabModelZip(contents) ? 20 : 5;
        if (!contents.unreadableModels.empty() || !contents.innerZips.empty()) return 20;
        return 0;
    }

    // Picks the model version and its textures and writes the worker's source file.
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
        out.defaultName = nameFromItem(choice.model->item, fabItemId(contents));
        out.license = source.guessed ? std::string(kUnknownLicenseId) : options.license;
        out.originalIsZip = true;
        out.details = fabChoiceDetails(contents, choice);
        return {};
    }
};

// The Fab entry of the Sources window.
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
    // Checks options as the Fab importers read them.
    Status checkOptions(const Json& options, OptionUse use) const override
    {
        FabOptions scratch;
        return fabOptionsFromJson(options, use == OptionUse::Import, scratch);
    }
    std::vector<std::string> storeSourceKinds() const override { return {std::string(kSourceFabZip)}; }
    std::vector<OptionField> optionFields() const override { return fabOptionFields(); }
};

const ImporterRegistrar importerRegistrar(std::make_unique<FabZipImporter>());
const IntegrationRegistrar integrationRegistrar(std::make_unique<FabIntegration>());

}

}
