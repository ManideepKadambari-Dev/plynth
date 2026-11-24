// The test source for the app steps of a new store. The test runner copies this one file into
// core/src/catalog/importers/ of a scratch copy of the tree and builds only there. It is in no build list.
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/Integrations.h>

#include <memory>

namespace plynth {

namespace {

// Takes files named teststore_ and asks which model of a pair to take, in words of its own.
class AppTestImporter final : public Importer {
public:
    std::string_view id() const override { return "test_store"; }
    std::string_view name() const override { return "Test store"; }
    std::string_view description() const override { return "Test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {".obj"}; }
    // Five, so Own files wins when nothing names this importer.
    int score(const ImportSource& source) const override
    {
        return pathToUtf8(source.file.filename()).starts_with("teststore_") ? 5 : 0;
    }
    // Plans the file, or refuses a pair until one is picked.
    Status plan(const ImportSource& source, const std::filesystem::path&, ImportPlan& out) const override
    {
        out = {};
        out.details = Json{{"models", Json::array({Json{{"entry", "left"}}, Json{{"entry", "right"}}})}};
        if (!source.options.contains("model") && pathToUtf8(source.file.filename()).find("pair") != std::string::npos) {
            return fail(ErrorCode::InvalidValue, "Tell Plynth which piece you want.");
        }
        out.file = source.file;
        out.format = "obj";
        out.sourceKind = "test_store_item";
        out.reference = "test";
        return {};
    }
};

// The card of the test store, with an option of each kind.
class AppTestStore final : public Integration {
public:
    std::string_view id() const override { return "test_store"; }
    std::string_view name() const override { return "Test store"; }
    std::string_view description() const override { return "A source for the app checks."; }
    int order() const override { return 900; }
    std::vector<std::string> assetImporters() const override { return {"test_store"}; }
    std::vector<std::string> materialImporters() const override { return {}; }
    bool takesAddresses() const override { return false; }
    std::string_view setupItem() const override { return {}; }
    std::string_view site() const override { return {}; }
    Json defaultOptions() const override { return Json{{"size", "small"}, {"count", 2}}; }
    // Allows its two options, and the pick for one import.
    Status checkOptions(const Json& options, OptionUse use) const override
    {
        for (const auto& item : options.items()) {
            const bool ok = item.key() == "size" || item.key() == "count" || (use == OptionUse::Import && item.key() == "model");
            if (!ok) return fail(ErrorCode::InvalidValue, "No such option.");
        }
        return {};
    }
    std::vector<std::string> storeSourceKinds() const override { return {"test_store_item"}; }
    // One choice and one number.
    std::vector<OptionField> optionFields() const override
    {
        return {{"size", "Size", "The size to take.", OptionKind::Choice, {{"small", "Small"}, {"big", "Big"}}},
                {"count", "Count", "How many to take.", OptionKind::WholeNumber, {}, 1, 5}};
    }
};

const ImporterRegistrar importerRegistrar(std::make_unique<AppTestImporter>());
const IntegrationRegistrar integrationRegistrar(std::make_unique<AppTestStore>());

}

}
