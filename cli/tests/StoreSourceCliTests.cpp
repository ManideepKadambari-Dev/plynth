#include "FakeBlender.h"
#include "MaterialLibrary.h"

#include "../../tests/core/ZipTestWriter.h"

#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using plynth::cli::testing::MaterialSettingsReset;
using plynth::cli::testing::PrivateUser;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

const char* const kShelfAddress = "testshelf-42";

// Takes an address that only its own registry entry knows.
class ShelfImporter final : public plynth::Importer {
public:
    std::string_view id() const override { return "test_shelf"; }
    std::string_view name() const override { return "Test shelf"; }
    std::string_view description() const override { return "Test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {}; }
    // Reads its own address only.
    int score(const plynth::ImportSource& source) const override {
        return source.file.empty() && source.url == kShelfAddress ? 30 : 0;
    }
    // Plans without doing anything.
    plynth::Status plan(const plynth::ImportSource& source, const std::filesystem::path&,
                        plynth::ImportPlan& out) const override {
        out = {};
        out.format = "obj";
        out.sourceKind = "test_shelf";
        out.reference = source.url;
        return {};
    }
};

// The same for materials.
class ShelfMaterialImporter final : public plynth::MaterialImporter {
public:
    std::string_view id() const override { return "test_shelf"; }
    std::string_view name() const override { return "Test shelf"; }
    std::string_view description() const override { return "Test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {}; }
    // Reads its own address only.
    int score(const plynth::ImportSource& source) const override {
        return source.file.empty() && source.url == kShelfAddress ? 30 : 0;
    }
    // Plans without doing anything.
    plynth::Status plan(const plynth::ImportSource& source, const std::filesystem::path&,
                        plynth::MaterialImportPlan& out) const override {
        out = {};
        out.workerSource = "texture_set";
        out.sourceKind = "test_shelf";
        out.reference = source.url;
        return {};
    }
};

// A library in temp set in the settings.
struct Setup {
    // Makes the library.
    explicit Setup(const std::string& name) : temp(name), user(temp) {
        REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == plynth::cli::kExitOk);
    }

    MaterialSettingsReset reset;
    TempFolder temp;
    PrivateUser user;
};

}

TEST_CASE("an address of a new importer needs no change of the CLI", "[cli][store_source]") {
    plynth::registerImporter(std::make_unique<ShelfImporter>());
    plynth::registerMaterialImporter(std::make_unique<ShelfMaterialImporter>());
    for (const char* catalog : {"assets", "materials"}) {
        CAPTURE(catalog);
        Setup setup("sc");
        const auto outcome = runChecked({catalog, "import", kShelfAddress, "--dry-run"});
        CAPTURE(outcome.text);
        CHECK(outcome.answer.at("ok") == true);
        if (outcome.answer.at("ok") != true) {
            continue;
        }
        CHECK(outcome.answer.at("result").at("importer") == "test_shelf");
        CHECK(outcome.answer.at("result").at("plan").at("reference") == kShelfAddress);
    }
}

TEST_CASE("the CLI dry run shows the license of how a Fab zip was chosen", "[cli][fab_license]") {
    Setup setup("sl");
    const auto zip = setup.temp.path() / "chair.zip";
    plynth::test::writeZipFile(zip, plynth::test::zipEntriesNamed({"Chair/FBX/Low/Chair.fbx", "Chair/FBX/Medium/Chair.fbx"}));
    const std::vector<std::pair<std::vector<std::string>, std::string>> runs{
        {{}, "unknown"},
        {{"--option", "quality=high"}, "unknown"},
        {{"--importer", "fab_zip"}, "royalty_free"},
        {{"--option", "license=cc0"}, "cc0"}};
    for (const auto& [extra, expected] : runs) {
        std::vector<std::string> args{"assets", "import", utf8(zip), "--dry-run"};
        args.insert(args.end(), extra.begin(), extra.end());
        CAPTURE(args);
        const auto outcome = runChecked(args);
        REQUIRE(outcome.answer.at("ok") == true);
        CHECK(outcome.answer.at("result").at("plan").at("license") == expected);
    }
}
