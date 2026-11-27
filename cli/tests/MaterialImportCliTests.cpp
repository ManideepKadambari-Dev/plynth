#include "FakeBlender.h"
#include "MaterialLibrary.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::MaterialSettingsReset;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// A library in temp set in the settings, and a fake Blender. Returns the fake's path.
std::filesystem::path setUp(const TempFolder& temp, const std::string& mode = "") {
    REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == kExitOk);
    return installFakeBlender(temp.path() / "fb", "5.2.0", mode);
}

// A material folder with one image and, when id is not empty, a material.json giving it.
std::filesystem::path makeSet(const std::filesystem::path& parent, const std::string& folder, const std::string& id) {
    const auto dir = parent / folder;
    std::filesystem::create_directories(dir);
    std::ofstream(dir / (folder + "_color.png"), std::ios::binary) << "x";
    if (!id.empty()) {
        std::ofstream(dir / "material.json")
            << R"({"format": "plynth-material-source", "version": 1, "id": ")" << id << R"(", "name": ")" << id
            << R"(", "tile_size": {"width": 1.2, "height": 0.6}})";
    }
    return dir;
}

// Every job the fake Blender was given, in order.
std::vector<nlohmann::json> jobsLogged(const TempFolder& temp) {
    std::vector<nlohmann::json> jobs;
    std::istringstream lines(readFile(temp.path() / "fb" / "fake_jobs.log"));
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty()) {
            jobs.push_back(nlohmann::json::parse(line));
        }
    }
    return jobs;
}

std::vector<std::string> importedIds(const plynth::cli::testing::Outcome& outcome) {
    std::vector<std::string> ids;
    for (const auto& item : outcome.answer.at("result").at("imported")) {
        ids.push_back(item.at("id"));
    }
    return ids;
}

}

TEST_CASE("materials import takes one folder and answers with its id", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-one");
    const auto fake = setUp(temp);
    const auto set = makeSet(temp.path() / "in", "oak_set", "oak_wood");

    const auto outcome = runChecked({"materials", "import", utf8(set), "--blender", utf8(fake), "--group", "floor"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("imported").at(0);
    CHECK(item.at("id") == "oak_wood");
    CHECK(item.at("importer") == "texture_set");
    CHECK(item.at("skipped") == false);
    CHECK(item.at("thumbnail_failed") == false);
    CHECK(item.at("original_removed") == false);
    CHECK(item.at("warnings").is_array());
    CHECK(outcome.progress.find("\"progress\"") != std::string::npos);

    const auto info = runChecked({"materials", "info", "oak_wood"});
    REQUIRE(info.exitCode == kExitOk);
    CHECK(info.answer.at("result").at("tile_size").at("width") == 1.2);
    CHECK(info.answer.at("result").at("groups") == nlohmann::json::array({"floor"}));
    CHECK(std::filesystem::exists(temp.path() / "Library" / "materials" / "items" / "oak_wood" / "info.json"));
}

TEST_CASE("materials import expands a wildcard, sorted, and skips existing ids", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-many");
    const auto fake = setUp(temp);
    const auto root = temp.path() / "samples";
    makeSet(root, "b_set", "beta");
    makeSet(root, "a_set", "alpha");
    makeSet(root, "c_set", "gamma");
    std::ofstream(root / "README.md") << "notes";

    const auto first = runChecked({"materials", "import", utf8(root / "*"), "--blender", utf8(fake), "--quiet"});
    REQUIRE(first.exitCode == kExitOk);
    CHECK(importedIds(first) == std::vector<std::string>{"alpha", "beta", "gamma"});
    CHECK(first.progress.empty());

    const auto second = runChecked(
        {"materials", "import", utf8(root / "*"), "--skip-existing", "--blender", utf8(fake), "--quiet"});
    REQUIRE(second.exitCode == kExitOk);
    CHECK(importedIds(second) == std::vector<std::string>{"alpha", "beta", "gamma"});
    for (const auto& item : second.answer.at("result").at("imported")) {
        CHECK(item.at("skipped") == true);
    }
    CHECK(runChecked({"materials", "list"}).answer.at("result").at("count") == 3);

    const auto nothing = runChecked({"materials", "import", utf8(root / "zz*"), "--blender", utf8(fake)});
    CHECK(errorCode(nothing) == "not_found");
}

TEST_CASE("materials import --id with two paths is bad_arguments", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-id");
    const auto fake = setUp(temp);
    const auto one = makeSet(temp.path() / "in", "one", "one");
    const auto two = makeSet(temp.path() / "in", "two", "two");
    const auto outcome = runChecked({"materials", "import", utf8(one), utf8(two), "--id", "mine", "--blender", utf8(fake)});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "bad_arguments");

    const auto single = runChecked({"materials", "import", utf8(one), "--id", "mine", "--blender", utf8(fake)});
    REQUIRE(single.exitCode == kExitOk);
    CHECK(importedIds(single) == std::vector<std::string>{"mine"});
}

TEST_CASE("materials import stops at the first failure and says how many came in", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-stop");
    const auto fake = setUp(temp);
    const auto good = makeSet(temp.path() / "in", "good", "good");
    const auto empty = temp.path() / "in" / "empty";
    std::filesystem::create_directories(empty);
    const auto last = makeSet(temp.path() / "in", "last", "last");

    const auto outcome = runChecked(
        {"materials", "import", utf8(good), utf8(empty), utf8(last), "--blender", utf8(fake), "--quiet"});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("1 material was imported before it") !=
          std::string::npos);
    const auto list = runChecked({"materials", "list"});
    CHECK(list.answer.at("result").at("count") == 1);
}

TEST_CASE("materials import passes maps, values and the tile size to the job", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-job");
    const auto fake = setUp(temp);
    const auto set = makeSet(temp.path() / "in", "wood", "");
    const auto color = temp.path() / "in" / "other.png";
    std::ofstream(color, std::ios::binary) << "x";

    const auto outcome = runChecked({"materials", "import", utf8(set), "--map", "base_color=" + utf8(color),
                                     "--value", "metallic=1", "--value", "roughness=0.5", "--tile", "2x1",
                                     "--normal", "directx", "--blender", utf8(fake), "--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto jobs = jobsLogged(temp);
    const nlohmann::json* cleanup = nullptr;
    for (const auto& job : jobs) {
        if (job.at("task") == "cleanup_material") {
            cleanup = &job;
        }
    }
    REQUIRE(cleanup != nullptr);
    const auto& params = cleanup->at("params");
    CHECK(params.at("source").at("maps").at("base_color").get<std::string>().find("other.png") != std::string::npos);
    CHECK(params.at("source").at("values").at("metallic") == 1.0);
    CHECK(params.at("source").at("values").at("roughness") == 0.5);
    CHECK(params.at("tile_size").at("width") == 2.0);
    CHECK(params.at("tile_size").at("height") == 1.0);
    CHECK(params.at("source").at("normal") == "directx");
}

TEST_CASE("materials import refuses bad option values before running Blender", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-bad");
    const auto fake = setUp(temp);
    const auto set = makeSet(temp.path() / "in", "wood", "wood");
    for (const std::vector<std::string>& extra :
         {std::vector<std::string>{"--tile", "0x1"}, {"--tile", "abc"}, {"--map", "nonsense=a.png"},
          {"--map", "base_color"}, {"--value", "metallic=high"}, {"--value", "shiny=1"}, {"--normal", "sideways"},
          {"--value", "metallic=2"}}) {
        std::vector<std::string> args = {"materials", "import", utf8(set), "--blender", utf8(fake)};
        args.insert(args.end(), extra.begin(), extra.end());
        const auto outcome = runChecked(args);
        CHECK(outcome.exitCode != kExitOk);
        CHECK(errorCode(outcome) == "invalid_value");
    }
    CHECK(!std::filesystem::exists(temp.path() / "fb" / "fake_jobs.log"));
}

TEST_CASE("materials import answers no_library without a library", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-nolib");
    const auto set = makeSet(temp.path() / "in", "wood", "wood");
    CHECK(errorCode(runChecked({"materials", "import", utf8(set)})) == "no_library");
    CHECK(errorCode(runChecked({"materials", "inbox"})) == "no_library");
    CHECK(errorCode(runChecked({"materials", "thumbnail", "wood"})) == "no_library");
}

TEST_CASE("materials import reports a failed thumbnail and a blender failure", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    {
        TempFolder temp("mimport-nothumb");
        const auto fake = setUp(temp, "no_thumbnail");
        const auto set = makeSet(temp.path() / "in", "wood", "wood");
        const auto outcome = runChecked({"materials", "import", utf8(set), "--blender", utf8(fake), "--quiet"});
        REQUIRE(outcome.exitCode == kExitOk);
        CHECK(outcome.answer.at("result").at("imported").at(0).at("thumbnail_failed") == true);
    }
    MaterialSettingsReset::clear();
    {
        TempFolder temp("mimport-fail");
        const auto fake = setUp(temp, "fail");
        const auto set = makeSet(temp.path() / "in", "wood", "wood");
        const auto outcome = runChecked({"materials", "import", utf8(set), "--blender", utf8(fake), "--quiet"});
        CHECK(outcome.exitCode != kExitOk);
        CHECK(errorCode(outcome) == "blender_failed");
    }
}

TEST_CASE("materials import of a .blend with several materials asks for the name", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-blend");
    const auto fake = setUp(temp, "several_materials");
    const auto blend = temp.path() / "in" / "pack.blend";
    std::filesystem::create_directories(blend.parent_path());
    std::ofstream(blend, std::ios::binary) << "BLEND";
    const auto outcome = runChecked({"materials", "import", utf8(blend), "--blender", utf8(fake), "--quiet"});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("several materials") != std::string::npos);
}

TEST_CASE("materials inbox imports what waits there", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-inbox");
    const auto fake = setUp(temp);
    const auto inbox = temp.path() / "Library" / "materials" / "inbox";
    makeSet(inbox, "tile_set", "tile_set");

    const auto outcome = runChecked({"materials", "inbox", "--blender", utf8(fake), "--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("imported") == nlohmann::json::array({"tile_set"}));
    CHECK(result.at("failed").empty());
    CHECK(result.at("remaining") == 0);

    const auto idle = runChecked({"materials", "inbox", "--blender", utf8(fake)});
    REQUIRE(idle.exitCode == kExitOk);
    CHECK(idle.answer.at("result").at("imported").empty());
}

TEST_CASE("materials thumbnail cleans a material again and answers with its record", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-thumb");
    const auto fake = setUp(temp);
    const auto set = makeSet(temp.path() / "in", "wood", "wood");
    REQUIRE(runChecked({"materials", "import", utf8(set), "--blender", utf8(fake), "--quiet"}).exitCode == kExitOk);

    const auto outcome = runChecked({"materials", "thumbnail", "wood", "--blender", utf8(fake), "--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("id") == "wood");
    CHECK(outcome.answer.at("result").at("thumbnail").at("state") == "ok");

    CHECK(errorCode(runChecked({"materials", "thumbnail", "ghost", "--blender", utf8(fake)})) == "not_found");
    CHECK(errorCode(runChecked({"materials", "thumbnail", "../x", "--blender", utf8(fake)})) == "invalid_value");
}

TEST_CASE("materials rescan renders changed materials again unless --no-refresh", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-rescan");
    const auto fake = setUp(temp);
    makeSet(temp.path() / "Library" / "materials" / "items", "fresh", "");

    const auto listed = runChecked({"materials", "rescan", "--no-refresh", "--blender", utf8(fake)});
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("needs_refresh") == nlohmann::json::array({"fresh"}));
    CHECK(listed.answer.at("result").at("refreshed").empty());
    CHECK(jobsLogged(temp).empty());

    const auto refreshed = runChecked({"materials", "rescan", "--blender", utf8(fake)});
    REQUIRE(refreshed.exitCode == kExitOk);
    const auto& result = refreshed.answer.at("result");
    CHECK(result.at("refreshed") == nlohmann::json::array({"fresh"}));
    CHECK(result.at("needs_refresh").empty());
    CHECK(result.at("refresh_failed").empty());
    CHECK(jobsLogged(temp).size() == 1);

    const auto quiet = runChecked({"materials", "rescan", "--blender", utf8(fake)});
    REQUIRE(quiet.exitCode == kExitOk);
    CHECK(quiet.answer.at("result").at("refreshed").empty());
    CHECK(jobsLogged(temp).size() == 1);
}

TEST_CASE("materials rescan lists what it could not render when Blender is missing", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-rescan-noblender");
    setUp(temp);
    makeSet(temp.path() / "Library" / "materials" / "items", "fresh", "");
    REQUIRE(runChecked({"materials", "rescan", "--no-refresh"}).exitCode == kExitOk);

    const auto outcome = runChecked({"materials", "rescan", "--blender", utf8(temp.path() / "none" / "blender.exe")});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& failed = outcome.answer.at("result").at("refresh_failed");
    REQUIRE(failed.size() == 1);
    CHECK(failed.at(0).at("id") == "fresh");
    CHECK(failed.at(0).at("code") == "blender_not_found");
    CHECK(outcome.answer.at("result").at("refreshed").empty());
}

TEST_CASE("library add-sample-materials gives the samples license and keeps the sample files", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-samples");
    const auto fake = setUp(temp);
    const auto samples = temp.path() / "samples";
    std::filesystem::create_directories(samples);
    std::ofstream(samples / "samples.json", std::ios::binary)
        << R"({"format": "plynth-samples", "version": 1, "license": "cc0", "assets": [], "materials": [], "projects": []})";
    const auto set = makeSet(samples / "materials", "slate", "slate");
    const ScopedEnvironment variable("PLYNTH_SAMPLES", utf8(samples));

    const auto added = runChecked({"library", "add-sample-materials", "--blender", utf8(fake), "--quiet"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("added") == nlohmann::json::array({"slate"}));
    CHECK(added.answer.at("result").at("skipped").empty());
    CHECK(std::filesystem::is_regular_file(set / "slate_color.png"));
    const auto info = runChecked({"materials", "info", "slate"});
    REQUIRE(info.exitCode == kExitOk);
    CHECK(info.answer.at("result").at("license") == "cc0");

    const auto again = runChecked({"library", "add-sample-materials", "--blender", utf8(fake), "--quiet"});
    REQUIRE(again.exitCode == kExitOk);
    CHECK(again.answer.at("result").at("added").empty());
    CHECK(again.answer.at("result").at("skipped") == nlohmann::json::array({"slate"}));
}

TEST_CASE("materials list shows a library filled by hand", "[cli][materials][import]") {
    MaterialSettingsReset reset;
    TempFolder temp("mimport-handfilled");
    REQUIRE(runChecked({"library", "init", utf8(temp.path() / "Library"), "--use"}).exitCode == kExitOk);
    makeSet(temp.path() / "Library" / "materials" / "items", "handmade", "");

    const auto listed = runChecked({"materials", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    REQUIRE(listed.answer.at("result").at("count") == 1);
    CHECK(listed.answer.at("result").at("materials").at(0).at("id") == "handmade");
}

TEST_CASE("materials importers lists the texture set and blend importers", "[cli][materials][import]") {
    const auto outcome = runChecked({"materials", "importers"});
    REQUIRE(outcome.exitCode == kExitOk);
    std::vector<std::string> ids;
    for (const auto& importer : outcome.answer.at("result").at("importers")) {
        ids.push_back(importer.at("id"));
        CHECK(importer.at("suffixes").is_array());
        CHECK(!importer.at("description").get<std::string>().empty());
    }
    CHECK(std::ranges::find(ids, "blend_material") != ids.end());
    CHECK(std::ranges::find(ids, "texture_set") != ids.end());
}

TEST_CASE("help lists the material import commands", "[cli][materials][import]") {
    const auto outcome = runChecked({"help"});
    REQUIRE(outcome.exitCode == kExitOk);
    const std::string text = outcome.answer.dump();
    for (const char* words : {"materials import", "materials inbox", "materials thumbnail", "materials importers"}) {
        CHECK(text.find(words) != std::string::npos);
    }
}
