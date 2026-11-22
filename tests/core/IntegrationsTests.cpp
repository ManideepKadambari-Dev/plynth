#include <plynth/core/catalog/Integrations.h>

#include "SchemaSupport.h"
#include "TestSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <plynth/core/Library.h>
#include <plynth/core/catalog/AssetInfo.h>
#include <plynth/core/catalog/Importer.h>

#include <algorithm>
#include <fstream>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A test only integration with options of both uses and a setup item. It is registered the first time a test
// asks for it and sorts last.
class TestIntegration final : public Integration {
public:
    std::string_view id() const override { return "testopts"; }
    std::string_view name() const override { return "Test source"; }
    std::string_view description() const override { return "Only for tests."; }
    int order() const override { return 900; }
    std::vector<std::string> assetImporters() const override { return {"test_asset_importer"}; }
    std::vector<std::string> materialImporters() const override { return {"test_material_importer"}; }
    bool takesAddresses() const override { return false; }
    std::string_view setupItem() const override { return kCheckBlenderKit; }
    std::string_view site() const override { return "https://example.org/"; }
    Json defaultOptions() const override
    {
        Json j = Json::object();
        j["mode"] = "a";
        j["count"] = 1;
        return j;
    }
    Status checkOptions(const Json& options, OptionUse use) const override
    {
        if (!options.is_object()) return fail(ErrorCode::InvalidValue, "The options must be an object.");
        for (const auto& item : options.items()) {
            if (item.key() == "mode") {
                if (!item.value().is_string() || (item.value() != "a" && item.value() != "b")) {
                    return fail(ErrorCode::InvalidValue, "mode must be a or b.");
                }
            } else if (item.key() == "count") {
                if (!item.value().is_number_integer() || item.value() < 1 || item.value() > 5) {
                    return fail(ErrorCode::InvalidValue, "count must be 1 to 5.");
                }
            } else if (item.key() == "once" && use == OptionUse::Import) {
                continue;
            } else {
                return fail(ErrorCode::InvalidValue, "Unknown option " + item.key() + ".");
            }
        }
        return {};
    }
};

// Puts the test integration in the registry once.
const Integration& testIntegration()
{
    static const bool added = registerIntegration(std::make_unique<TestIntegration>());
    (void)added;
    return *findIntegration("testopts");
}

// A library folder inside a temporary folder.
LibraryPaths makeLibrary(const plynth::test::TempDir& tmp)
{
    const fs::path root = tmp.path() / "Library";
    fs::create_directories(root);
    return libraryPaths(root);
}

// Writes a file by hand.
void writeText(const fs::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

// The file as plain JSON.
nlohmann::json readFile(const LibraryPaths& library)
{
    return nlohmann::json::parse(plynth::test::readTextFile(integrationsFile(library)));
}

}

TEST_CASE("allIntegrations holds local first", "[core][integrations]") {
    const auto all = allIntegrations();
    REQUIRE_FALSE(all.empty());
    REQUIRE(all.front()->id() == "local");
    REQUIRE(all.front()->order() == 10);
    REQUIRE(all.front()->assetImporters() == std::vector<std::string>{"local_file"});
    REQUIRE(all.front()->materialImporters() == std::vector<std::string>{"texture_set", "blend_material"});
    REQUIRE(all.front()->setupItem().empty());
    REQUIRE(all.front()->defaultOptions().empty());
    REQUIRE_FALSE(all.front()->takesAddresses());
    for (std::size_t i = 1; i < all.size(); ++i) REQUIRE(all[i - 1]->order() <= all[i]->order());
}

TEST_CASE("a second local integration is refused", "[core][integrations]") {
    class Other final : public Integration {
    public:
        std::string_view id() const override { return "local"; }
        std::string_view name() const override { return "Other"; }
        std::string_view description() const override { return ""; }
        int order() const override { return 1; }
        std::vector<std::string> assetImporters() const override { return {}; }
        std::vector<std::string> materialImporters() const override { return {}; }
        bool takesAddresses() const override { return false; }
        std::string_view setupItem() const override { return {}; }
        std::string_view site() const override { return {}; }
        Json defaultOptions() const override { return Json::object(); }
        Status checkOptions(const Json&, OptionUse) const override { return {}; }
    };
    REQUIRE_FALSE(registerIntegration(std::make_unique<Other>()));
    REQUIRE(findIntegration("local")->name() == "Own files");
    REQUIRE(findIntegration("nope") == nullptr);
}

TEST_CASE("integrationForImporter finds the owner", "[core][integrations]") {
    const Integration* local = integrationForImporter("local_file", false);
    REQUIRE(local != nullptr);
    REQUIRE(local->id() == "local");
    REQUIRE(integrationForImporter("texture_set", true) == local);
    REQUIRE(integrationForImporter("blend_material", true) == local);
    REQUIRE(integrationForImporter("texture_set", false) == nullptr);
    REQUIRE(integrationForImporter("local_file", true) == nullptr);
}

TEST_CASE("isStoreSource knows store kinds", "[core][integrations]") {
    REQUIRE(isStoreSource("blenderkit"));
    REQUIRE(isStoreSource("fab_zip"));
    REQUIRE_FALSE(isStoreSource("local_file"));
    REQUIRE_FALSE(isStoreSource(""));
}

TEST_CASE("a missing integrations file gives defaults", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-missing");
    const LibraryPaths library = makeLibrary(tmp);
    const IntegrationOptionsRead read = readIntegrationOptions(library);
    plynth::test::requireOk(read.status);
    REQUIRE_FALSE(read.exists);
    REQUIRE(read.saved.empty());
    const Integration& test = testIntegration();
    const Json options = integrationOptions(test, read.saved);
    REQUIRE(options["mode"] == "a");
    REQUIRE(options["count"] == 1);
}

TEST_CASE("setIntegrationOptions writes a valid file and keeps unknown entries", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-set");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();
    writeText(integrationsFile(library),
              R"({"format": "plynth-integrations", "version": 1, "integrations": {"future": {"x": 1}}})");
    plynth::test::requireOk(setIntegrationOptions(library, "testopts", Json{{"mode", "b"}}));
    plynth::test::requireOk(setIntegrationOptions(library, "testopts", Json{{"count", 3}}));
    const nlohmann::json file = readFile(library);
    REQUIRE(plynth::test::schemaProblem("integrations.schema.json", file).empty());
    REQUIRE(file["integrations"]["future"]["x"] == 1);
    REQUIRE(file["integrations"]["testopts"]["mode"] == "b");
    REQUIRE(file["integrations"]["testopts"]["count"] == 3);

    const IntegrationOptionsRead read = readIntegrationOptions(library);
    plynth::test::requireOk(read.status);
    REQUIRE(read.exists);
    const Json options = integrationOptions(*findIntegration("testopts"), read.saved);
    REQUIRE(options["mode"] == "b");
    REQUIRE(options["count"] == 3);
}

TEST_CASE("a value equal to the default is still saved", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-default");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();
    plynth::test::requireOk(setIntegrationOptions(library, "testopts", Json{{"mode", "a"}}));
    REQUIRE(readFile(library)["integrations"]["testopts"]["mode"] == "a");
}

TEST_CASE("a refused option writes nothing", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-refused");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();
    REQUIRE(setIntegrationOptions(library, "testopts", Json{{"mode", "z"}}).code == ErrorCode::InvalidValue);
    REQUIRE(setIntegrationOptions(library, "testopts", Json{{"once", true}}).code == ErrorCode::InvalidValue);
    REQUIRE(setIntegrationOptions(library, "local", Json{{"x", 1}}).code == ErrorCode::InvalidValue);
    REQUIRE(setIntegrationOptions(library, "nope", Json::object()).code == ErrorCode::NotFound);
    REQUIRE_FALSE(fs::exists(integrationsFile(library)));
}

TEST_CASE("resetIntegrationOptions removes the entry", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-reset");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();
    plynth::test::requireOk(resetIntegrationOptions(library, "testopts"));
    REQUIRE_FALSE(fs::exists(integrationsFile(library)));
    plynth::test::requireOk(setIntegrationOptions(library, "testopts", Json{{"mode", "b"}}));
    plynth::test::requireOk(resetIntegrationOptions(library, "testopts"));
    const nlohmann::json file = readFile(library);
    REQUIRE_FALSE(file["integrations"].contains("testopts"));
    REQUIRE(plynth::test::schemaProblem("integrations.schema.json", file).empty());
    REQUIRE(resetIntegrationOptions(library, "nope").code == ErrorCode::NotFound);
}

TEST_CASE("a newer or broken integrations file is reported", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-bad");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();

    writeText(integrationsFile(library), R"({"format": "plynth-integrations", "version": 2, "integrations": {}})");
    REQUIRE(readIntegrationOptions(library).status.code == ErrorCode::UnsupportedVersion);

    writeText(integrationsFile(library),
              R"({"format": "plynth-integrations", "version": 1, "integrations": {"testopts": {"count": 9}}})");
    const IntegrationOptionsRead broken = readIntegrationOptions(library);
    REQUIRE(broken.status.code == ErrorCode::BadFile);
    REQUIRE(broken.status.where == "/integrations/testopts/count");
    REQUIRE(setIntegrationOptions(library, "testopts", Json{{"mode", "b"}}).code == ErrorCode::BadFile);

    writeText(integrationsFile(library), "{ not json");
    REQUIRE(readIntegrationOptions(library).status.code == ErrorCode::BadFile);
}

TEST_CASE("optionsForImporter puts request over saved over defaults", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-for-importer");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();
    const ImporterOptions plainOptions = optionsForImporter(library, "test_asset_importer", false, Json::object());
    plynth::test::requireOk(plainOptions.status);
    REQUIRE(plainOptions.options["mode"] == "a");

    plynth::test::requireOk(setIntegrationOptions(library, "testopts", Json{{"mode", "b"}, {"count", 2}}));
    const ImporterOptions saved = optionsForImporter(library, "test_asset_importer", false, Json::object());
    REQUIRE(saved.options["mode"] == "b");
    REQUIRE(saved.options["count"] == 2);

    const ImporterOptions request =
        optionsForImporter(library, "test_material_importer", true, Json{{"count", 4}, {"once", true}});
    plynth::test::requireOk(request.status);
    REQUIRE(request.options["mode"] == "b");
    REQUIRE(request.options["count"] == 4);
    REQUIRE(request.options["once"] == true);

    REQUIRE(optionsForImporter(library, "test_asset_importer", false, Json{{"count", 9}}).status.code == ErrorCode::InvalidValue);
}

TEST_CASE("optionsForImporter refuses options for an importer without any", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-no-options");
    const LibraryPaths library = makeLibrary(tmp);
    REQUIRE(optionsForImporter(library, "local_file", false, Json{{"x", 1}}).status.code == ErrorCode::InvalidValue);
    const ImporterOptions none = optionsForImporter(library, "local_file", false, Json::object());
    plynth::test::requireOk(none.status);
    REQUIRE(none.options.empty());
    REQUIRE(optionsForImporter(library, "unlisted", false, Json{{"x", 1}}).status.code == ErrorCode::InvalidValue);
    plynth::test::requireOk(optionsForImporter(library, "unlisted", false, Json::object()).status);
}

TEST_CASE("optionsForImporter reports a broken file", "[core][integrations]") {
    plynth::test::TempDir tmp("integ-broken-for-importer");
    const LibraryPaths library = makeLibrary(tmp);
    testIntegration();
    writeText(integrationsFile(library), "[]");
    REQUIRE(optionsForImporter(library, "test_asset_importer", false, Json::object()).status.code == ErrorCode::BadFile);
}

TEST_CASE("integrationStatus follows the setup report", "[core][integrations]") {
    const Integration* local = findIntegration("local");
    const IntegrationStatus noItem = integrationStatus(*local, nullptr);
    REQUIRE(noItem.state == IntegrationState::Ready);
    REQUIRE(noItem.detail == "Ready.");
    REQUIRE(integrationStateName(noItem.state) == "ready");

    const Integration& test = testIntegration();
    REQUIRE(integrationStatus(test, nullptr).state == IntegrationState::NotChecked);
    REQUIRE(integrationStateName(IntegrationState::NotChecked) == "not_checked");

    SetupReport report;
    REQUIRE(integrationStatus(test, &report).state == IntegrationState::NotChecked);

    SetupItem item;
    item.id = std::string(kCheckBlenderKit);
    item.state = CheckState::NotChecked;
    report.items.push_back(item);
    REQUIRE(integrationStatus(test, &report).state == IntegrationState::NotChecked);

    report.items[0].state = CheckState::Found;
    report.items[0].detail = "BlenderKit is signed in.";
    const IntegrationStatus ready = integrationStatus(test, &report);
    REQUIRE(ready.state == IntegrationState::Ready);
    REQUIRE(ready.detail == "BlenderKit is signed in.");

    report.items[0].state = CheckState::NeedsAttention;
    report.items[0].detail = "Not signed in.";
    report.items[0].action = "Sign in inside Blender.";
    report.items[0].link = "https://example.org/help";
    const IntegrationStatus needs = integrationStatus(test, &report);
    REQUIRE(needs.state == IntegrationState::NeedsSetup);
    REQUIRE(needs.action == "Sign in inside Blender.");
    REQUIRE(needs.link == "https://example.org/help");
    REQUIRE(integrationStateName(needs.state) == "needs_setup");

    report.items[0].state = CheckState::Missing;
    REQUIRE(integrationStatus(test, &report).state == IntegrationState::NeedsSetup);
    report.items[0].state = CheckState::TooOld;
    REQUIRE(integrationStatus(test, &report).state == IntegrationState::NeedsSetup);
}

TEST_CASE("integrationToJson has the listed shape", "[core][integrations]") {
    const Integration* local = findIntegration("local");
    const Json json = integrationToJson(*local, integrationStatus(*local, nullptr), local->defaultOptions());
    REQUIRE(json["id"] == "local");
    REQUIRE(json["name"] == "Own files");
    REQUIRE(json["order"] == 10);
    REQUIRE(json["takes_addresses"] == false);
    REQUIRE(json["asset_importers"] == Json::array({"local_file"}));
    REQUIRE(json["material_importers"] == Json::array({"texture_set", "blend_material"}));
    REQUIRE(json["state"] == "ready");
    REQUIRE(json["detail"] == "Ready.");
    REQUIRE(json["action"] == "");
    REQUIRE(json["link"] == "");
    REQUIRE(json["site"] == "");
    REQUIRE(json["options"].is_object());
    REQUIRE(json["defaults"].is_object());

    const auto& suffixes = json["suffixes"];
    REQUIRE(suffixes.is_array());
    REQUIRE(std::is_sorted(suffixes.begin(), suffixes.end()));
    REQUIRE(std::find(suffixes.begin(), suffixes.end(), ".obj") != suffixes.end());
    REQUIRE(std::find(suffixes.begin(), suffixes.end(), ".blend") != suffixes.end());
    REQUIRE(std::adjacent_find(suffixes.begin(), suffixes.end()) == suffixes.end());

    const Integration& test = testIntegration();
    const Json withOptions = integrationToJson(test, integrationStatus(test, nullptr), Json{{"mode", "b"}, {"count", 1}});
    REQUIRE(withOptions["options"]["mode"] == "b");
    REQUIRE(withOptions["defaults"]["mode"] == "a");
    REQUIRE(withOptions["state"] == "not_checked");
    REQUIRE(withOptions["site"] == "https://example.org/");
}
