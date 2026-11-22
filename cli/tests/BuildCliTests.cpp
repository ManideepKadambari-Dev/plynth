#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;
using plynth::cli::testing::utf8;

namespace {

using SettingsReset = plynth::cli::testing::GuardedSettingsReset;

// Checks a JSON value against the build report schema.
void requireReport(const nlohmann::json& report) {
    static nlohmann::json_schema::json_validator validator = [] {
        std::ifstream file(std::filesystem::path(PLYNTH_SCHEMA_DIR) / "build-report.schema.json");
        nlohmann::json_schema::json_validator made;
        made.set_root_schema(nlohmann::json::parse(file));
        return made;
    }();
    REQUIRE_NOTHROW(validator.validate(report));
}

// Writes one asset folder by hand and indexes it.
void makeAsset(const std::filesystem::path& library, const std::string& id, const std::string& category,
               plynth::AssetSize size) {
    plynth::Status status;
    auto catalog = plynth::AssetCatalog::open(plynth::libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().assetItems / id;
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "asset.blend", std::ios::binary) << "x";
    plynth::AssetInfo info;
    info.id = id;
    info.name = id;
    info.categories = {category};
    info.source.kind = "local_file";
    info.source.reference = id + ".obj";
    info.source.importedAt = plynth::sessionNow();
    info.size = size;
    info.triangles = 12;
    info.blendBytes = 1;
    info.blenderVersion = "5.2.0";
    REQUIRE(plynth::writeAssetInfo(folder / "info.json", info).ok());
    REQUIRE(catalog->indexAsset(info).ok());
}

// The warning codes of an answer.
std::vector<std::string> warningCodes(const Outcome& outcome) {
    std::vector<std::string> codes;
    if (outcome.answer.contains("warnings")) {
        for (const auto& warning : outcome.answer.at("warnings")) {
            codes.push_back(warning.at("code").get<std::string>());
        }
    }
    return codes;
}

// Says whether a code is in a list.
bool has(const std::vector<std::string>& codes, const std::string& code) {
    return std::ranges::find(codes, code) != codes.end();
}

// The codes of the problems in a build report or a check result.
std::vector<std::string> problemCodes(const nlohmann::json& result) {
    std::vector<std::string> codes;
    for (const auto& problem : result.at("problems")) {
        codes.push_back(problem.at("code").get<std::string>());
    }
    return codes;
}

// The progress lines as parsed JSON, each checked to be one object with a progress entry.
std::vector<nlohmann::json> progressLines(const std::string& text) {
    std::vector<nlohmann::json> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const nlohmann::json parsed = nlohmann::json::parse(line);
        REQUIRE(parsed.size() == 1);
        const auto& entry = parsed.at("progress");
        REQUIRE(entry.at("stage").is_string());
        REQUIRE(entry.at("step").is_string());
        REQUIRE(entry.at("index").is_number_integer());
        REQUIRE(entry.at("count").is_number_integer());
        REQUIRE(entry.at("message").is_string());
        lines.push_back(entry);
    }
    return lines;
}

// A square room (walls w1 to w4, room r1) with a door o1 and a window o2 that reaches past its wall, a library
// with a sofa and a chair, a sofa and a chair placed, and the chair's folder removed afterwards.
struct BuildSample {
    explicit BuildSample(const std::string& name, const std::string& mode = "") : sample(name) {
        sample.drawRoomSquare();
        REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--type", "door"}).exitCode ==
                kExitOk);
        REQUIRE(sample.run({"opening", "add", "--wall", "w3", "--offset", "2", "--type", "window"}).exitCode ==
                kExitOk);
        library = sample.temp.path() / "Library";
        REQUIRE(plynth::cli::testing::runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
        makeAsset(library, "sofa", "sofa", {2.0, 0.9, 0.85});
        makeAsset(library, "chair", "chair", {0.5, 0.5, 0.9});
        REQUIRE(sample.run({"place", "sofa", "--x", "3", "--y", "6"}).exitCode == kExitOk);
        REQUIRE(sample.run({"place", "chair", "--x", "4", "--y", "4"}).exitCode == kExitOk);
        std::filesystem::remove_all(library / "assets" / "items" / "chair");
        REQUIRE(plynth::cli::testing::runChecked({"assets", "rescan", "--no-blender", "--library", utf8(library)})
                    .exitCode == kExitOk);
        pushWindowPastWall();
        fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0", mode);
    }

    // Moves the window along its wall so that it reaches past the corner.
    void pushWindowPastWall() {
        const auto file = sample.folder / "plan.json";
        nlohmann::json plan = nlohmann::json::parse(readFile(file));
        auto& openings = plan.at("floors").at(0).at("openings");
        for (auto& opening : openings) {
            if (opening.at("type") == "window") {
                opening["offset"] = 3.95;
            }
        }
        std::ofstream(file, std::ios::binary | std::ios::trunc) << plan.dump(2);
    }

    // Runs build on the project with the fake Blender and any extra options.
    Outcome build(std::vector<std::string> extra = {}) const {
        std::vector<std::string> args = {"build", "--blender", utf8(fake)};
        args.insert(args.end(), extra.begin(), extra.end());
        return sample.run(args);
    }

    std::filesystem::path buildFolder() const { return sample.folder / "build"; }

    SettingsReset reset;
    Sample sample;
    std::filesystem::path library;
    std::filesystem::path fake;
};

}

TEST_CASE("build runs the worker and answers with the report", "[cli][build]") {
    BuildSample flat("build-ok");
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");

    const auto outcome = flat.build();
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "build");
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    CHECK(result.at("ok") == true);
    CHECK(std::filesystem::exists(flat.buildFolder() / "building.blend"));
    CHECK(std::filesystem::exists(flat.buildFolder() / "building.fbx"));
    CHECK(std::filesystem::exists(flat.buildFolder() / "build.json"));
    CHECK_FALSE(std::filesystem::exists(flat.buildFolder() / "build.lock"));

    const auto codes = warningCodes(outcome);
    CHECK(has(codes, "opening_does_not_fit"));
    CHECK(has(codes, "missing_asset"));
    CHECK(result.at("warning_count").get<int>() + result.at("error_count").get<int>() ==
          static_cast<int>(result.at("problems").size()));
    CHECK(codes.size() == result.at("problems").size());

    const auto report = nlohmann::json::parse(readFile(flat.buildFolder() / "build.json"));
    requireReport(report);

    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}

TEST_CASE("build writes progress lines in stage order on the progress stream", "[cli][build]") {
    BuildSample flat("build-progress");
    const auto outcome = flat.build();
    REQUIRE(outcome.exitCode == kExitOk);

    const auto lines = progressLines(outcome.progress);
    REQUIRE(lines.size() >= 3);
    CHECK(lines.front().at("stage") == "preparing");
    CHECK(lines.front().at("index") == 0);
    CHECK(lines.front().at("count") == 0);
    CHECK(lines.front().at("step") == "");
    CHECK(lines.back().at("stage") == "done");
    CHECK(lines[lines.size() - 2].at("stage") == "finishing");

    std::vector<std::string> stages;
    int lastIndex = 0;
    for (const auto& line : lines) {
        const std::string stage = line.at("stage");
        if (stages.empty() || stages.back() != stage) {
            stages.push_back(stage);
        }
        if (stage == "blender") {
            CHECK(line.at("index").get<int>() == lastIndex + 1);
            CHECK(line.at("count").get<int>() >= line.at("index").get<int>());
            CHECK_FALSE(line.at("step").get<std::string>().empty());
            lastIndex = line.at("index");
        }
    }
    CHECK(stages == std::vector<std::string>{"preparing", "blender", "finishing", "done"});
    CHECK(lastIndex > 0);
    CHECK(lastIndex == lines[lines.size() - 3].at("count").get<int>());
}

TEST_CASE("build --quiet writes no progress", "[cli][build]") {
    BuildSample flat("build-quiet");
    const auto outcome = flat.build({"--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.progress.empty());
}

TEST_CASE("build --check lists the problems and writes nothing", "[cli][build]") {
    BuildSample flat("build-check");
    const std::string plan = readFile(flat.sample.folder / "plan.json");

    const auto outcome = flat.sample.run({"build", "--check", "--blender", utf8(flat.sample.temp.path() / "none.exe")});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    const auto codes = problemCodes(result);
    CHECK(has(codes, "opening_does_not_fit"));
    CHECK(has(codes, "missing_asset"));
    CHECK(result.at("error_count").get<int>() >= 2);
    CHECK(result.at("warning_count").get<int>() >= 0);
    CHECK(result.at("plan_fingerprint").get<std::string>().size() == 16);
    CHECK(outcome.progress.empty());
    CHECK_FALSE(std::filesystem::exists(flat.buildFolder()));
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
}

TEST_CASE("build refuses a plan without walls and writes nothing", "[cli][build]") {
    SettingsReset reset;
    const Sample sample("build-empty");
    const auto fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0");
    const auto outcome = sample.run({"build", "--blender", utf8(fake)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(outcome.answer.at("error").at("message") == "The plan has no walls to build.");
    CHECK_FALSE(std::filesystem::exists(sample.folder / "build"));
}

TEST_CASE("build refuses a folder that is not a project", "[cli][build]") {
    SettingsReset reset;
    const plynth::cli::testing::TempFolder temp("build-none");
    const auto outcome = plynth::cli::testing::runChecked({"build", "--project", utf8(temp.path())});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_a_project");
}

TEST_CASE("build answers blender_failed when the worker fails and keeps a report", "[cli][build]") {
    BuildSample flat("build-fail", "fail");
    const auto outcome = flat.build();
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "blender_failed");
    const auto report = nlohmann::json::parse(readFile(flat.buildFolder() / "build.json"));
    requireReport(report);
    CHECK(report.at("ok") == false);
    CHECK(report.at("error").at("code") == "blender_failed");
    CHECK_FALSE(std::filesystem::exists(flat.buildFolder() / "build.lock"));
}

TEST_CASE("build without a usable Blender is blender_not_found", "[cli][build]") {
    BuildSample flat("build-noblender");
    const auto outcome =
        flat.sample.run({"build", "--blender", utf8(flat.sample.temp.path() / "none" / "blender.exe")});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "blender_not_found");
    CHECK_FALSE(std::filesystem::exists(flat.buildFolder()));
}

TEST_CASE("build without a library reports no_library and still builds", "[cli][build]") {
    BuildSample flat("build-nolibrary");
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    const auto outcome = flat.build();
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(has(problemCodes(outcome.answer.at("result")), "no_library"));
    CHECK(has(warningCodes(outcome), "no_library"));
    requireReport(outcome.answer.at("result"));

    const auto checked = flat.sample.run({"build", "--check"});
    REQUIRE(checked.exitCode == kExitOk);
    CHECK(has(problemCodes(checked.answer.at("result")), "no_library"));
}

TEST_CASE("build takes the library from --library", "[cli][build]") {
    BuildSample flat("build-library-option");
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    const auto outcome = flat.build({"--library", utf8(flat.library)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto codes = problemCodes(outcome.answer.at("result"));
    CHECK_FALSE(has(codes, "no_library"));
    CHECK(has(codes, "missing_asset"));
}

TEST_CASE("build runs while the app has the project open and changes no project file", "[cli][build]") {
    BuildSample flat("build-app-open");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(flat.sample.folder, state).ok());
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");

    const auto built = flat.build();
    CHECK(built.exitCode == kExitOk);
    const auto checked = flat.sample.run({"build", "--check"});
    CHECK(checked.exitCode == kExitOk);
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}

TEST_CASE("build is listed by help", "[cli][build]") {
    const auto outcome = plynth::cli::testing::runChecked({"help"});
    bool found = false;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        if (command.at("command").get<std::string>() == "build") {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("build is in_use while a fresh lock is held", "[cli][build]") {
    BuildSample flat("build-locked");
    std::filesystem::create_directories(flat.buildFolder());
    std::ofstream(flat.buildFolder() / "build.lock", std::ios::binary) << "held";
    const std::string plan = readFile(flat.sample.folder / "plan.json");

    const auto outcome = flat.build();
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "in_use");
    CHECK(std::filesystem::exists(flat.buildFolder() / "build.lock"));
    CHECK_FALSE(std::filesystem::exists(flat.buildFolder() / "building.blend"));
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
}
