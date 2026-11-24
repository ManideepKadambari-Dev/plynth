#include <plynth/core/catalog/BlenderKit.h>

#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/MaterialImporter.h"

namespace plynth {

namespace {

// Imports a material from a BlenderKit address through the user's own add-on.
class BlenderKitMaterialImporter final : public MaterialImporter {
public:
    std::string_view id() const override { return kBlenderKitImporterId; }
    std::string_view name() const override { return "BlenderKit"; }
    std::string_view description() const override
    {
        return "A BlenderKit material address. Blender downloads it through your own BlenderKit add-on.";
    }
    std::vector<std::string> fileSuffixes() const override { return {}; }

    // Takes only an address it can read, never a file.
    int score(const ImportSource& source) const override
    {
        if (!source.file.empty() || source.url.empty()) return 0;
        return parseBlenderKitAddress(source.url).status.ok() ? 30 : 0;
    }

    // Plans a download of the material the address names, to be cleaned up like any file afterwards.
    Status plan(const ImportSource& source, const std::filesystem::path&, MaterialImportPlan& out) const override
    {
        if (!source.file.empty() || source.url.empty()) return fail(ErrorCode::InvalidValue, "BlenderKit cannot read this.");
        const BlenderKitAddress address = parseBlenderKitAddress(source.url);
        if (!address.status.ok()) return address.status;
        BlenderKitOptions options;
        if (const Status s = blenderKitOptionsFromJson(source.options, options); !s.ok()) return s;

        out = {};
        out.sourceKind = std::string(kSourceBlenderKit);
        const auto first = source.url.find_first_not_of(" \t\r\n");
        out.reference = source.url.substr(first, source.url.find_last_not_of(" \t\r\n") - first + 1);
        out.fetch = makeBlenderKitFetch(address.assetBaseId, kFetchKindMaterial, options);
        out.details = Json{{"asset_base_id", address.assetBaseId}};
        return {};
    }
};

const MaterialImporterRegistrar registrar(std::make_unique<BlenderKitMaterialImporter>());

}

}
