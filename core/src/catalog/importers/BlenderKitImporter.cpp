#include <plynth/core/catalog/BlenderKit.h>

#include "plynth/core/SetupCheck.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/Importer.h"
#include "plynth/core/catalog/Integrations.h"
#include "plynth/core/catalog/MaterialImporter.h"

namespace plynth {

namespace {

// The address without spaces and line ends at its ends.
std::string trimmedAddress(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// Imports a model from a BlenderKit address through the user's own add-on.
class BlenderKitImporter final : public Importer {
public:
    std::string_view id() const override { return kBlenderKitImporterId; }
    std::string_view name() const override { return "BlenderKit"; }
    std::string_view description() const override
    {
        return "A BlenderKit item address. Blender downloads the model through your own BlenderKit add-on.";
    }
    std::vector<std::string> fileSuffixes() const override { return {}; }

    // Takes only an address it can read, never a file.
    int score(const ImportSource& source) const override
    {
        if (!source.file.empty() || source.url.empty()) return 0;
        return parseBlenderKitAddress(source.url).status.ok() ? 30 : 0;
    }

    // Plans a download of the model the address names, to be cleaned up like any file afterwards.
    Status plan(const ImportSource& source, const std::filesystem::path&, ImportPlan& out) const override
    {
        if (!source.file.empty() || source.url.empty()) return fail(ErrorCode::InvalidValue, "BlenderKit cannot read this.");
        const BlenderKitAddress address = parseBlenderKitAddress(source.url);
        if (!address.status.ok()) return address.status;
        BlenderKitOptions options;
        if (const Status s = blenderKitOptionsFromJson(source.options, options); !s.ok()) return s;

        out = {};
        out.sourceKind = std::string(kSourceBlenderKit);
        out.reference = trimmedAddress(source.url);
        out.fetch = makeBlenderKitFetch(address.assetBaseId, kFetchKindModel, options);
        out.details = Json{{"asset_base_id", address.assetBaseId}};
        return {};
    }
};

// The BlenderKit entry of the Integrations tab.
class BlenderKitIntegration final : public Integration {
public:
    std::string_view id() const override { return kBlenderKitIntegrationId; }
    std::string_view name() const override { return "BlenderKit"; }
    std::string_view description() const override
    {
        return "Models and materials from BlenderKit. Blender downloads them through the BlenderKit add-on, signed in with your own account.";
    }
    int order() const override { return 20; }
    std::vector<std::string> assetImporters() const override { return {std::string(kBlenderKitImporterId)}; }
    std::vector<std::string> materialImporters() const override { return {std::string(kBlenderKitImporterId)}; }
    bool takesAddresses() const override { return true; }
    std::string_view setupItem() const override { return kCheckBlenderKit; }
    std::string_view site() const override { return kBlenderKitSite; }
    std::string_view note() const override
    {
        return "Downloads run Blender with your own settings and enabled add-ons.";
    }
    Json defaultOptions() const override
    {
        const BlenderKitOptions defaults;
        return Json{{"resolution", defaults.resolution},
                    {"wait_seconds", defaults.waitSeconds}};
    }
    // Refuses options for a single import and checks the saved ones.
    Status checkOptions(const Json& options, OptionUse use) const override
    {
        if (use == OptionUse::Import && options.is_object() && !options.empty()) {
            return fail(ErrorCode::InvalidValue,
                        "BlenderKit has no options for a single import. Change them with plynth library integration set blenderkit.");
        }
        BlenderKitOptions scratch;
        return blenderKitOptionsFromJson(options, scratch);
    }
    // Items from BlenderKit are store items, never bundled.
    std::vector<std::string> storeSourceKinds() const override { return {std::string(kSourceBlenderKit)}; }
    // The two saved options as the app draws them.
    std::vector<OptionField> optionFields() const override
    {
        OptionField resolution;
        resolution.key = "resolution";
        resolution.label = "Texture resolution";
        resolution.help = "The size of the textures BlenderKit downloads. Original takes them as the author uploaded them.";
        resolution.kind = OptionKind::Choice;
        resolution.choices = {{"0.5k", "0.5K"}, {"1k", "1K"}, {"2k", "2K"}, {"4k", "4K"}, {"8k", "8K"}, {"original", "Original"}};

        OptionField wait;
        wait.key = "wait_seconds";
        wait.label = "Wait for a download (seconds)";
        wait.help = "How long a download may take before Plynth stops waiting for it.";
        wait.kind = OptionKind::WholeNumber;
        wait.minimum = 60;
        wait.maximum = 3600;
        return {resolution, wait};
    }
    // The sentence for a pasted text that is not a BlenderKit address.
    Status checkAddress(std::string_view text) const override { return parseBlenderKitAddress(text).status; }
};

const ImporterRegistrar importerRegistrar(std::make_unique<BlenderKitImporter>());
const IntegrationRegistrar integrationRegistrar(std::make_unique<BlenderKitIntegration>());

}

}
