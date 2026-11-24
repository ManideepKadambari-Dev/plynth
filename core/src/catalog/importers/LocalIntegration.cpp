#include "plynth/core/catalog/Importer.h"
#include "plynth/core/catalog/Integrations.h"
#include "plynth/core/catalog/MaterialImporter.h"

namespace plynth {

namespace {

// The entry for the user's own files. It has no options and needs no setup.
class LocalIntegration final : public Integration {
public:
    std::string_view id() const override { return "local"; }
    std::string_view name() const override { return "Own files"; }
    std::string_view description() const override
    {
        return "Models, texture sets and Blender materials from files and folders on this computer. Nothing to set up.";
    }
    int order() const override { return 10; }
    std::vector<std::string> assetImporters() const override { return {std::string(kLocalFileImporterId)}; }
    std::vector<std::string> materialImporters() const override
    {
        return {std::string(kTextureSetImporterId), std::string(kBlendMaterialImporterId)};
    }
    bool takesAddresses() const override { return false; }
    std::string_view setupItem() const override { return {}; }
    std::string_view site() const override { return {}; }
    Json defaultOptions() const override { return Json::object(); }
    Status checkOptions(const Json& options, OptionUse) const override
    {
        if (options.is_object() && options.empty()) return {};
        return fail(ErrorCode::InvalidValue, "Own files has no options.");
    }
};

const IntegrationRegistrar registrar(std::make_unique<LocalIntegration>());

}

}
