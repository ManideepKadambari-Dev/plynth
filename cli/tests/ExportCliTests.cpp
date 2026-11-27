#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json-schema.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;
using plynth::cli::testing::utf8;

namespace {

// Clears the settings file when a test starts and when it ends.
struct SettingsReset {
    SettingsReset() { clear(); }
    ~SettingsReset() { clear(); }
    static void clear() {
        plynth::cli::testing::removeSettingsFileSafely();
    }
};

// Checks a JSON value against the export report schema.
void requireReport(const nlohmann::json& report) {
    static nlohmann::json_schema::json_validator validator = [] {
        std::ifstream file(fs::path(PLYNTH_SCHEMA_DIR) / "export-report.schema.json");
        nlohmann::json_schema::json_validator made;
        made.set_root_schema(nlohmann::json::parse(file));
        return made;
    }();
    REQUIRE_NOTHROW(validator.validate(report));
}

// The progress lines as parsed JSON.
std::vector<nlohmann::json> progressLines(const std::string& text) {
    std::vector<nlohmann::json> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const nlohmann::json parsed = nlohmann::json::parse(line);
        REQUIRE(parsed.size() == 1);
        lines.push_back(parsed.at("progress"));
    }
    return lines;
}

// The names of the entries in a folder.
std::set<std::string> entries(const fs::path& folder) {
    std::set<std::string> names;
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(folder, ignored)) {
        names.insert(entry.path().filename().string());
    }
    return names;
}

// Writes a build lock for this process, as a running build would.
void writeFreshLock(const fs::path& file) {
    const auto process = plynth::currentProcess();
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream(file, std::ios::binary) << process.pid << " " << process.startKey << " " << epoch << "\n";
}

// A built square room with a fake Blender, ready to export.
struct ExportSample {
    explicit ExportSample(const std::string& name) : sample(name) {
        sample.drawRoomSquare();
        fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0");
        const auto built = sample.run({"build", "--blender", utf8(fake)});
        REQUIRE(built.exitCode == kExitOk);
    }

    // Runs export on the project with the fake Blender and any extra options.
    Outcome exportWith(std::vector<std::string> extra = {}) const {
        std::vector<std::string> args = {"export", "--blender", utf8(fake)};
        args.insert(args.end(), extra.begin(), extra.end());
        return sample.run(args);
    }

    // Sets the fake Blender's mode.
    void setMode(const std::string& mode) const { std::ofstream(fake.parent_path() / "fake_mode.txt") << mode; }

    fs::path exportFolder() const { return sample.folder / "export"; }

    SettingsReset reset;
    Sample sample;
    fs::path fake;
};

}

TEST_CASE("export writes the files and answers with the report", "[cli][export]") {
    ExportSample flat("export-ok");
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");

    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "export");
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    CHECK(result.at("ok") == true);
    CHECK(result.at("profile") == "plain");
    CHECK(result.at("exported") == true);
    CHECK(result.at("checked_only") == false);
    CHECK(fs::exists(flat.exportFolder() / "export_report.json"));
    CHECK(fs::exists(flat.exportFolder() / "Textures" / "T_Fake_BaseColor.png"));
    bool haveFbx = false;
    for (const auto& name : entries(flat.exportFolder())) {
        haveFbx = haveFbx || name.ends_with(".fbx");
    }
    CHECK(haveFbx);
    CHECK_FALSE(fs::exists(flat.sample.folder / "build" / "build.lock"));

    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}

TEST_CASE("export writes progress lines in stage order, none with --quiet", "[cli][export]") {
    ExportSample flat("export-progress");
    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);

    const auto lines = progressLines(outcome.progress);
    REQUIRE(lines.size() >= 3);
    CHECK(lines.front().at("stage") == "preparing");
    CHECK(lines.back().at("stage") == "done");
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
            lastIndex = line.at("index");
        }
    }
    CHECK(stages == std::vector<std::string>{"preparing", "blender", "finishing", "done"});
    CHECK(lastIndex > 0);

    const auto quiet = flat.exportWith({"--quiet"});
    REQUIRE(quiet.exitCode == kExitOk);
    CHECK(quiet.progress.empty());
}

TEST_CASE("export --check writes nothing but the report", "[cli][export]") {
    ExportSample flat("export-check");
    const auto outcome = flat.exportWith({"--check"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    CHECK(result.at("checked_only") == true);
    CHECK(result.at("exported") == false);
    CHECK(entries(flat.exportFolder()) == std::set<std::string>{"export_report.json"});

    flat.setMode("quality_fail");
    const auto failed = flat.exportWith({"--check"});
    REQUIRE(failed.exitCode == kExitOk);
    requireReport(failed.answer.at("result"));
    CHECK(failed.answer.at("result").at("quality").at("passed") == false);
    REQUIRE(failed.answer.contains("warnings"));
    CHECK(failed.answer.at("warnings").at(0).at("code") == "quality_failed");
    CHECK(entries(flat.exportFolder()) == std::set<std::string>{"export_report.json"});
}

TEST_CASE("a failed check blocks export and --force exports past it", "[cli][export]") {
    ExportSample flat("export-gate");
    flat.setMode("quality_fail");

    const auto blocked = flat.exportWith();
    CHECK(blocked.exitCode == kExitFailed);
    CHECK(errorCode(blocked) == "quality_failed");
    CHECK(blocked.answer.at("error").at("message").get<std::string>().find("Naming") != std::string::npos);
    CHECK(fs::exists(flat.exportFolder() / "export_report.json"));
    CHECK(entries(flat.exportFolder()) == std::set<std::string>{"export_report.json"});

    const auto forced = flat.exportWith({"--force"});
    REQUIRE(forced.exitCode == kExitOk);
    const auto& result = forced.answer.at("result");
    requireReport(result);
    CHECK(result.at("exported") == true);
    CHECK(result.at("forced") == true);
    CHECK(result.at("overridden") == nlohmann::json::array({"naming"}));
    CHECK(fs::exists(flat.exportFolder() / "Textures"));
}

TEST_CASE("a warning does not block export", "[cli][export]") {
    ExportSample flat("export-warn");
    flat.setMode("quality_warn");
    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("exported") == true);
}

TEST_CASE("export without a build is not_found", "[cli][export]") {
    SettingsReset reset;
    const Sample sample("export-nobuild");
    sample.drawRoomSquare();
    const auto fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0");
    const auto outcome = sample.run({"export", "--blender", utf8(fake)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("Build the project first.") !=
          std::string::npos);
}

TEST_CASE("export takes the group options only for export groups", "[cli][export]") {
    ExportSample flat("export-stray-option");
    const auto outcome = flat.exportWith({"--name", "Hero"});
    CHECK(outcome.exitCode != kExitOk);
    CHECK_FALSE(fs::exists(flat.exportFolder()));
}

TEST_CASE("export is in_use while a fresh lock is held", "[cli][export]") {
    ExportSample flat("export-locked");
    const fs::path lock = flat.sample.folder / "build" / "build.lock";
    writeFreshLock(lock);
    const auto outcome = flat.exportWith();
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "in_use");
    CHECK(fs::exists(lock));
}

TEST_CASE("export without a usable Blender is blender_not_found", "[cli][export]") {
    ExportSample flat("export-noblender");
    const auto outcome =
        flat.sample.run({"export", "--blender", utf8(flat.sample.temp.path() / "none" / "blender.exe")});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "blender_not_found");
}

TEST_CASE("export answers blender_failed when the worker fails", "[cli][export]") {
    ExportSample flat("export-fail");
    flat.setMode("fail");
    const auto outcome = flat.exportWith();
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "blender_failed");
}

TEST_CASE("export report gives the last report and not_found before any", "[cli][export]") {
    ExportSample flat("export-report");
    const auto none = flat.sample.run({"export", "report"});
    CHECK(none.exitCode == kExitFailed);
    CHECK(errorCode(none) == "not_found");

    const auto exported = flat.exportWith();
    REQUIRE(exported.exitCode == kExitOk);
    const auto outcome = flat.sample.run({"export", "report"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "export");
    requireReport(outcome.answer.at("result"));
    CHECK(outcome.answer.at("result").at("job_id") == exported.answer.at("result").at("job_id"));
    CHECK(outcome.answer.at("result").at("profile") == "plain");

    std::ofstream(flat.exportFolder() / "export_report.json", std::ios::trunc) << "{not json";
    const auto bad = flat.sample.run({"export", "report"});
    CHECK(bad.exitCode == kExitFailed);
    CHECK(errorCode(bad) == "bad_file");
}

TEST_CASE("export report warns when the report comes from an older build", "[cli][export]") {
    ExportSample flat("export-report-age");
    REQUIRE(flat.exportWith().exitCode == kExitOk);
    const auto fresh = flat.sample.run({"export", "report"});
    REQUIRE(fresh.exitCode == kExitOk);
    CHECK_FALSE(fresh.answer.contains("warnings"));

    REQUIRE(flat.sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "2", "--to-y", "2"}).exitCode == kExitOk);
    REQUIRE(flat.sample.run({"build", "--blender", utf8(flat.fake)}).exitCode == kExitOk);
    const auto old = flat.sample.run({"export", "report"});
    REQUIRE(old.exitCode == kExitOk);
    requireReport(old.answer.at("result"));
    REQUIRE(old.answer.contains("warnings"));
    REQUIRE(old.answer.at("warnings").size() == 1);
    CHECK(old.answer.at("warnings").at(0).at("code") == "out_of_date");

    REQUIRE(flat.exportWith().exitCode == kExitOk);
    CHECK_FALSE(flat.sample.run({"export", "report"}).answer.contains("warnings"));
}

TEST_CASE("export asks for a build before it looks for Blender", "[cli][export]") {
    SettingsReset reset;
    const Sample sample("export-blocked-first");
    sample.drawRoomSquare();
    const auto outcome =
        sample.run({"export", "--check", "--blender", utf8(sample.temp.path() / "none" / "blender.exe")});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(outcome.answer.at("error").at("message") == "Build the project first.");
    CHECK_FALSE(fs::exists(sample.folder / "export"));
}

TEST_CASE("export --check is in_use while a fresh lock is held", "[cli][export]") {
    ExportSample flat("export-check-locked");
    const fs::path lock = flat.sample.folder / "build" / "build.lock";
    writeFreshLock(lock);
    const auto outcome = flat.exportWith({"--check"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "in_use");
    CHECK(outcome.answer.at("error").at("message") == "A build or an export of this project is running.");
    CHECK(fs::exists(lock));
}

TEST_CASE("export runs while the app has the project open", "[cli][export]") {
    ExportSample flat("export-app-open");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(flat.sample.folder, state).ok());
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
}

TEST_CASE("help lists export", "[cli][export]") {
    const auto outcome = plynth::cli::testing::runChecked({"help"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.text.find("\"export\"") != std::string::npos);
}

namespace {

// The file in the export folder whose name ends with the suffix, or an empty path.
fs::path exportedEnding(const ExportSample& flat, const std::string& suffix) {
    for (const auto& name : entries(flat.exportFolder())) {
        if (name.ends_with(suffix)) {
            return flat.exportFolder() / name;
        }
    }
    return {};
}

// Makes a library in the sample's folder and sets it in the settings.
fs::path useLibrary(const ExportSample& flat) {
    const fs::path library = flat.sample.temp.path() / "Library";
    REQUIRE(plynth::cli::testing::runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    return library;
}

}

TEST_CASE("plain export leaves the kit out even with the kit switch on", "[cli][export][kit]") {
    ExportSample flat("export-kit-on");
    useLibrary(flat);
    const auto on = flat.sample.run({"export", "kit", "on"});
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("result").at("enabled") == true);

    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    CHECK(result.at("profile") == "plain");
    CHECK(result.at("exported") == true);
    const auto& kit = result.at("kit");
    CHECK(kit.at("asked") == false);
    CHECK(kit.at("written") == false);
    CHECK(kit.at("skipped").is_null());
    CHECK(kit.at("file").is_null());
    CHECK(kit.at("layout_file").is_null());
    CHECK(exportedEnding(flat, "_Kit.fbx").empty());
    CHECK(exportedEnding(flat, "_Kit_layout.json").empty());
    CHECK_FALSE(exportedEnding(flat, ".fbx").empty());
}

TEST_CASE("export refuses a bad --library and leaves export unchanged", "[cli][export][kit]") {
    ExportSample flat("export-kit-bad-library");
    useLibrary(flat);
    REQUIRE(flat.exportWith().exitCode == kExitOk);
    const auto before = entries(flat.exportFolder());
    REQUIRE_FALSE(exportedEnding(flat, ".fbx").empty());

    const auto refused = flat.exportWith({"--library", utf8(flat.sample.temp.path() / "typo")});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(refused.answer.at("ok") == false);
    CHECK(refused.answer.at("error").at("code") == "no_library");
    CHECK(entries(flat.exportFolder()) == before);
}

TEST_CASE("the kit commands never change plan.json or project.json", "[cli][export][kit]") {
    ExportSample flat("export-kit-files");
    useLibrary(flat);
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.exportWith().exitCode == kExitOk);
    REQUIRE(flat.sample.run({"export", "kit", "off"}).exitCode == kExitOk);
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}
