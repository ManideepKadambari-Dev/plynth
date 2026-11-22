#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Settings.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using Catch::Approx;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

using SettingsReset = plynth::cli::testing::GuardedSettingsReset;

// A library in temp set in the settings, and a fake Blender. Returns the fake's path.
std::filesystem::path setUp(const TempFolder& temp, const std::string& mode = "") {
    REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == kExitOk);
    return installFakeBlender(temp.path() / "fb", "5.2.0", mode);
}

// Writes a small file the fake Blender never reads.
std::filesystem::path makeFile(const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "o thing\n";
    return file;
}

}

TEST_CASE("assets import runs the cleanup and answers with the asset entry", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-ok");
    const auto fake = setUp(temp);
    const auto model = makeFile(temp.path() / "in" / "chair.obj");

    const auto outcome = runChecked({"assets", "import", utf8(model), "--category", "chair", "--tag", "oak",
                                     "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("importer") == "local_file");
    CHECK(result.at("thumbnail_failed") == false);
    CHECK(result.at("original_removed") == false);
    const auto& asset = result.at("asset");
    CHECK(asset.at("id") == "chair");
    CHECK(asset.at("name") == "Chair");
    CHECK(asset.at("categories") == nlohmann::json::array({"chair"}));
    CHECK(asset.at("tags") == nlohmann::json::array({"oak"}));
    CHECK(asset.at("source").at("kind") == "local_file");
    CHECK(asset.at("size").at("height") == Approx(0.9));
    CHECK(asset.at("triangles") == 12);
    CHECK(asset.at("state") == "ready");
    CHECK(asset.at("thumbnail").at("state") == "ok");

    const auto folder = temp.path() / "Library" / "assets" / "items" / "chair";
    CHECK(std::filesystem::exists(folder / "asset.blend"));
    CHECK(std::filesystem::exists(folder / "thumb.png"));
    CHECK(std::filesystem::exists(folder / "info.json"));
    CHECK(std::filesystem::exists(model));

    const auto again = runChecked({"assets", "import", utf8(model), "--blender", utf8(fake)});
    CHECK(again.answer.at("result").at("asset").at("id") == "chair_2");

    const auto found = runChecked({"assets", "search", "chair"});
    CHECK(found.answer.at("result").at("count") == 2);
}

TEST_CASE("a zip imported with --keep-original stays where it was", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-zip");
    const auto fake = setUp(temp);
    const auto zip = makeFile(temp.path() / "in" / "lamp.zip");

    const auto outcome = runChecked({"assets", "import", utf8(zip), "--keep-original", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("original_removed") == false);
    CHECK(outcome.answer.at("result").at("asset").at("id") == "lamp");
    CHECK(std::filesystem::exists(zip));
}

TEST_CASE("assets import refuses bad requests", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-bad");
    const auto fake = setUp(temp);
    const auto text = makeFile(temp.path() / "in" / "notes.txt");
    const auto model = makeFile(temp.path() / "in" / "chair.obj");

    const auto ending = runChecked({"assets", "import", utf8(text), "--blender", utf8(fake)});
    CHECK(errorCode(ending) == "invalid_value");
    CHECK(ending.answer.at("error").at("message").get<std::string>().find(".obj") != std::string::npos);

    const auto rotate = runChecked({"assets", "import", utf8(model), "--rotate", "45", "--blender", utf8(fake)});
    CHECK(errorCode(rotate) == "invalid_value");
    const auto scale = runChecked({"assets", "import", utf8(model), "--scale", "0", "--blender", utf8(fake)});
    CHECK(errorCode(scale) == "invalid_value");
    const auto category =
        runChecked({"assets", "import", utf8(model), "--category", "nothing", "--blender", utf8(fake)});
    CHECK(errorCode(category) == "not_found");

    const auto noBlender =
        runChecked({"assets", "import", utf8(model), "--blender", utf8(temp.path() / "none" / "blender.exe")});
    CHECK(errorCode(noBlender) == "blender_not_found");

    CHECK(runChecked({"assets", "search"}).answer.at("result").at("count") == 0);
}

TEST_CASE("assets import in fail mode is blender_failed and leaves no asset", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-fail");
    const auto fake = setUp(temp, "fail");
    const auto model = makeFile(temp.path() / "in" / "chair.obj");

    const auto outcome = runChecked({"assets", "import", utf8(model), "--blender", utf8(fake)});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "blender_failed");
    CHECK(runChecked({"assets", "search"}).answer.at("result").at("count") == 0);
}

TEST_CASE("assets import without a library is no_library", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-nolib");
    const auto model = makeFile(temp.path() / "in" / "chair.obj");
    const auto outcome = runChecked({"assets", "import", utf8(model)});
    CHECK(errorCode(outcome) == "no_library");
}

TEST_CASE("assets inbox imports what it can and lists what failed", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-inbox");
    const auto fake = setUp(temp);
    const auto inbox = temp.path() / "Library" / "assets" / "inbox";
    makeFile(inbox / "chair.obj");
    makeFile(inbox / "bad" / "a.obj");
    makeFile(inbox / "bad" / "b.obj");

    const auto outcome = runChecked({"assets", "inbox", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("imported") == nlohmann::json::array({"chair"}));
    REQUIRE(result.at("failed").size() == 1);
    CHECK(result.at("failed").at(0).at("entry") == "bad");
    CHECK(result.at("remaining") == 0);
    CHECK(std::filesystem::exists(inbox / "failed" / "bad"));

    const auto empty = runChecked({"assets", "inbox", "--blender", utf8(fake)});
    CHECK(empty.answer.at("result").at("imported").empty());
}

TEST_CASE("rescan refreshes a changed asset.blend, and --no-blender only lists it", "[cli][import]") {
    SettingsReset reset;
    TempFolder temp("import-rescan");
    const auto fake = setUp(temp);
    const auto model = makeFile(temp.path() / "in" / "chair.obj");
    REQUIRE(runChecked({"assets", "import", utf8(model), "--blender", utf8(fake)}).exitCode == kExitOk);
    const auto blend = temp.path() / "Library" / "assets" / "items" / "chair" / "asset.blend";

    const auto quiet = runChecked({"assets", "rescan", "--blender", utf8(fake)});
    CHECK(quiet.answer.at("result").at("unchanged") == 1);

    std::ofstream(blend, std::ios::binary | std::ios::app) << "more bytes";
    const auto listed = runChecked({"assets", "rescan", "--no-blender"});
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("needs_refresh") == nlohmann::json::array({"chair"}));
    CHECK(listed.answer.at("result").at("refreshed").empty());

    const auto refreshed = runChecked({"library", "rescan", "--blender", utf8(fake)});
    REQUIRE(refreshed.exitCode == kExitOk);
    CHECK(refreshed.answer.at("result").at("assets").at("refreshed") == nlohmann::json::array({"chair"}));
    CHECK(refreshed.answer.at("result").at("assets").at("refresh_failed").empty());

    const auto thumbnail = runChecked({"assets", "thumbnail", "chair", "--blender", utf8(fake)});
    REQUIRE(thumbnail.exitCode == kExitOk);
    CHECK(thumbnail.answer.at("result").at("thumbnail").at("state") == "ok");
    CHECK(errorCode(runChecked({"assets", "thumbnail", "missing", "--blender", utf8(fake)})) == "not_found");
}

TEST_CASE("assets importers lists the local file importer", "[cli][import]") {
    SettingsReset reset;
    const auto outcome = runChecked({"assets", "importers"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& importers = outcome.answer.at("result").at("importers");
    REQUIRE(importers.size() >= 1);
    CHECK(importers.at(0).at("id") == "local_file");
    CHECK(importers.at(0).at("suffixes") ==
          nlohmann::json::array({".blend", ".fbx", ".glb", ".gltf", ".obj", ".zip"}));
}

TEST_CASE("the sample chair imports with the real Blender", "[cli][import][.blender]") {
    SettingsReset reset;
    TempFolder temp("import-real");
    REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == kExitOk);
    const std::filesystem::path samples = PLYNTH_SAMPLE_CHAIR_FOLDER;
    const auto in = temp.path() / "in";
    std::filesystem::create_directories(in);
    for (const auto& entry : std::filesystem::directory_iterator(samples)) {
        std::filesystem::copy_file(entry.path(), in / entry.path().filename());
    }

    const auto imported = runChecked({"assets", "import", utf8(in / "chair.obj")});
    REQUIRE(imported.exitCode == kExitOk);
    const auto found = runChecked({"assets", "search", "chair"});
    REQUIRE(found.answer.at("result").at("count") == 1);
    const auto& size = found.answer.at("result").at("assets").at(0).at("size");
    CHECK(size.at("width").get<double>() == Approx(0.45).margin(0.001));
    CHECK(size.at("depth").get<double>() == Approx(0.5).margin(0.001));
    CHECK(size.at("height").get<double>() == Approx(0.9).margin(0.001));
}
