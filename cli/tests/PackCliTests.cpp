#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

using SettingsReset = plynth::cli::testing::GuardedSettingsReset;

// Writes an own asset into a library and indexes it.
void addAsset(const std::filesystem::path& library, const std::string& id, const std::string& kind) {
    plynth::Status status;
    auto catalog = plynth::AssetCatalog::open(plynth::libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().assetItems / id;
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "asset.blend", std::ios::binary) << "x";
    plynth::AssetInfo info;
    info.id = id;
    info.name = id;
    info.license = "cc0";
    info.source.kind = kind;
    info.source.reference = kind == "blenderkit" ? "https://www.blenderkit.com/get/" + id : id + ".obj";
    info.source.importedAt = plynth::sessionNow();
    info.size = {0.5, 0.5, 0.5};
    info.triangles = 12;
    info.blendBytes = 1;
    info.blenderVersion = "5.2.0";
    REQUIRE(plynth::writeAssetInfo(folder / "info.json", info).ok());
    REQUIRE(catalog->indexAsset(info).ok());
}

// Puts an item of each asset on the project's first floor.
void placeItems(const std::filesystem::path& project, const std::vector<std::string>& assets) {
    plynth::ProjectLoad load = plynth::loadProject(project);
    REQUIRE(load.status.ok());
    int n = 0;
    for (const std::string& asset : assets) {
        plynth::Item item;
        item.id = "i" + std::to_string(++n);
        item.asset = asset;
        item.x = n;
        item.y = 1;
        item.size = {0.5, 0.5, 0.5};
        load.project.plan.floors[0].items.push_back(item);
    }
    REQUIRE(plynth::saveProject(load.project, project, project).ok());
}

}

TEST_CASE("pack and unpack move a project's items to another library", "[cli][pack]") {
    SettingsReset reset;
    Sample sample("pack-cli");
    const auto first = sample.temp.path() / "LibA";
    const auto second = sample.temp.path() / "LibB";
    REQUIRE(runChecked({"library", "init", utf8(first)}).exitCode == kExitOk);
    REQUIRE(runChecked({"library", "init", utf8(second)}).exitCode == kExitOk);
    addAsset(first, "sofa", "local_file");
    addAsset(first, "chair", "blenderkit");
    placeItems(sample.folder, {"sofa", "chair"});

    Outcome outcome = sample.run({"pack", "--library", utf8(first), "--check"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("checked_only") == true);
    CHECK(outcome.answer.at("result").at("copied") == 1);
    CHECK(outcome.answer.at("result").at("references") == 1);
    CHECK_FALSE(std::filesystem::exists(sample.folder / "pack"));

    outcome = sample.run({"pack", "--library", utf8(first)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(std::filesystem::exists(sample.folder / "pack" / "pack.json"));
    CHECK(std::filesystem::exists(sample.folder / "pack" / "assets" / "sofa" / "asset.blend"));
    CHECK_FALSE(std::filesystem::exists(sample.folder / "pack" / "assets" / "chair"));

    outcome = sample.run({"unpack", "--library", utf8(second), "--check"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("added") == 0);
    CHECK_FALSE(std::filesystem::exists(second / "assets" / "items" / "sofa"));

    outcome = sample.run({"unpack", "--library", utf8(second)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("added") == 1);
    CHECK(outcome.answer.at("result").at("complete") == false);
    REQUIRE(outcome.answer.contains("warnings"));
    CHECK(outcome.answer.at("warnings").at(0).at("code") == "needs_download");
    CHECK(std::filesystem::exists(second / "assets" / "items" / "sofa" / "asset.blend"));

    outcome = sample.run({"unpack", "--library", utf8(second)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("added") == 0);
}

TEST_CASE("pack and unpack refuse with clear codes", "[cli][pack]") {
    SettingsReset reset;
    Sample sample("pack-cli-refuse");
    const auto library = sample.temp.path() / "Lib";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);

    Outcome outcome = sample.run({"unpack", "--library", utf8(library)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");

    TempFolder empty("pack-cli-empty");
    outcome = runChecked({"pack", "--library", utf8(library), "--project", utf8(empty.path())});
    CHECK(errorCode(outcome) == "not_a_project");
    outcome = sample.run({"pack", "--library", utf8(sample.temp.path() / "nolibrary")});
    CHECK(errorCode(outcome) == "no_library");
    CHECK(plynth::cli::isCommandWord("pack"));
    CHECK(plynth::cli::isCommandWord("unpack"));
}
