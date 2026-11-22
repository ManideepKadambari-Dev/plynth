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

    std::ofstream(flat.exportFolder() / "export_report.json", std::ios::trunc) << "{not json";
    const auto bad = flat.sample.run({"export", "report"});
    CHECK(bad.exitCode == kExitFailed);
    CHECK(errorCode(bad) == "bad_file");
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

TEST_CASE("export with the kit on writes the kit and its layout", "[cli][export][kit]") {
    ExportSample flat("export-kit-on");
    useLibrary(flat);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);

    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    const auto& kit = result.at("kit");
    CHECK(kit.at("asked") == true);
    CHECK(kit.at("written") == true);
    CHECK(kit.at("skipped").is_null());
    CHECK(kit.at("pieces").get<int>() > 0);
    CHECK(kit.at("placements").get<int>() >= kit.at("pieces").get<int>());

    const fs::path kitFile = exportedEnding(flat, "_Kit.fbx");
    const fs::path layoutFile = exportedEnding(flat, "_Kit_layout.json");
    REQUIRE_FALSE(kitFile.empty());
    REQUIRE_FALSE(layoutFile.empty());
    CHECK(kit.at("file") == utf8(kitFile));
    CHECK(kit.at("layout_file") == utf8(layoutFile));

    std::ifstream schemaFile(fs::path(PLYNTH_SCHEMA_DIR) / "kit-layout.schema.json");
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schemaFile));
    CHECK_NOTHROW(validator.validate(nlohmann::json::parse(readFile(layoutFile))));
    CHECK(fs::exists(flat.exportFolder() / "export_report.json"));
}

TEST_CASE("export with the kit on and no library skips the kit and still exits 0", "[cli][export][kit]") {
    ExportSample flat("export-kit-nolibrary");
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);

    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    CHECK(result.at("exported") == true);
    const auto& kit = result.at("kit");
    CHECK(kit.at("asked") == true);
    CHECK(kit.at("written") == false);
    REQUIRE(kit.at("skipped").is_string());
    CHECK_FALSE(kit.at("skipped").get<std::string>().empty());
    CHECK(kit.at("file").is_null());
    CHECK(kit.at("layout_file").is_null());
    CHECK(exportedEnding(flat, "_Kit.fbx").empty());
    CHECK(exportedEnding(flat, "_Kit_layout.json").empty());
    CHECK_FALSE(exportedEnding(flat, ".fbx").empty());
}

TEST_CASE("export takes the kit library from --library", "[cli][export][kit]") {
    ExportSample flat("export-kit-library-option");
    const fs::path library = useLibrary(flat);
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);

    const auto without = flat.exportWith();
    REQUIRE(without.exitCode == kExitOk);
    CHECK(without.answer.at("result").at("kit").at("written") == false);

    const auto with = flat.exportWith({"--library", utf8(library)});
    REQUIRE(with.exitCode == kExitOk);
    requireReport(with.answer.at("result"));
    CHECK(with.answer.at("result").at("kit").at("written") == true);
    CHECK_FALSE(exportedEnding(flat, "_Kit.fbx").empty());
}

TEST_CASE("export refuses a bad --library and leaves export unchanged", "[cli][export][kit]") {
    ExportSample flat("export-kit-bad-library");
    useLibrary(flat);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.exportWith().exitCode == kExitOk);
    const auto before = entries(flat.exportFolder());
    REQUIRE_FALSE(exportedEnding(flat, "_Kit.fbx").empty());

    const auto refused = flat.exportWith({"--library", utf8(flat.sample.temp.path() / "typo")});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(refused.answer.at("ok") == false);
    CHECK(refused.answer.at("error").at("code") == "no_library");
    CHECK(entries(flat.exportFolder()) == before);
}

TEST_CASE("export says why a broken configured library skipped the kit", "[cli][export][kit]") {
    ExportSample flat("export-kit-broken-library");
    const fs::path library = useLibrary(flat);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    std::ofstream(library / "materials" / "materials.db", std::ios::binary) << "not a database";
    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& kit = outcome.answer.at("result").at("kit");
    CHECK(kit.at("written") == false);
    const std::string why = kit.at("skipped").get<std::string>();
    CHECK(why.find("No library is set") == std::string::npos);
    CHECK_FALSE(why.empty());
}

// Puts a door on w2 and gives it a custom shape that only the library folder defines. The id is used by no other
// test, so nothing else in this process has registered it.
void addCustomShapeDoor(ExportSample& flat, const fs::path& shapesFolder, const std::string& id) {
    fs::create_directories(shapesFolder);
    std::ofstream(shapesFolder / (id + ".shape.json"))
        << R"({"format":"plynth-shape","version":1,"id":")" << id
        << R"(","name":"Pentagon","outline":[[0,0],[1,0],[1,0.7],[0.5,1],[0,0.7]]})";
    REQUIRE(flat.sample.run({"opening", "add", "--wall", "w2", "--offset", "2"}).exitCode == kExitOk);
    std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::size_t at = plan.find("\"shape\": \"rectangle\"");
    REQUIRE(at != std::string::npos);
    plan.replace(at, std::string("\"shape\": \"rectangle\"").size(), "\"shape\": \"" + id + "\"");
    std::ofstream(flat.sample.folder / "plan.json", std::ios::binary) << plan;
}

// Copies a library to a second folder no command in this process has registered the shapes of, which stands
// in for a fresh process.
fs::path freshCopy(const fs::path& library) {
    const fs::path copy = library.parent_path() / (library.filename().string() + "Fresh");
    fs::copy(library, copy, fs::copy_options::recursive);
    return copy;
}

TEST_CASE("the kit uses a custom opening shape from the configured library", "[cli][export][kit]") {
    ExportSample flat("export-kit-shape-settings");
    const fs::path library = useLibrary(flat);
    addCustomShapeDoor(flat, library / "shapes", "ghost_kit_s1");
    const fs::path fresh = freshCopy(library);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.sample.run({"build", "--blender", utf8(flat.fake)}).exitCode == kExitOk);

    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", utf8(fresh)}).exitCode == kExitOk);
    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& kit = outcome.answer.at("result").at("kit");
    CHECK(kit.at("written") == true);
    const fs::path layoutFile = exportedEnding(flat, "_Kit_layout.json");
    REQUIRE_FALSE(layoutFile.empty());
    CHECK(readFile(layoutFile).find("\"kind\": \"opening\"") != std::string::npos);
}

TEST_CASE("the kit uses a custom opening shape from --library", "[cli][export][kit]") {
    ExportSample flat("export-kit-shape-option");
    const fs::path library = useLibrary(flat);
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    addCustomShapeDoor(flat, library / "shapes", "ghost_kit_o1");
    const fs::path fresh = freshCopy(library);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.sample.run({"build", "--blender", utf8(flat.fake)}).exitCode == kExitOk);

    const auto outcome = flat.exportWith({"--library", utf8(fresh)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("kit").at("written") == true);
    const fs::path layoutFile = exportedEnding(flat, "_Kit_layout.json");
    REQUIRE_FALSE(layoutFile.empty());
    CHECK(readFile(layoutFile).find("\"kind\": \"opening\"") != std::string::npos);
}

TEST_CASE("export kit off removes the kit files at the next export", "[cli][export][kit]") {
    ExportSample flat("export-kit-off");
    useLibrary(flat);
    REQUIRE(flat.sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.exportWith().exitCode == kExitOk);
    REQUIRE_FALSE(exportedEnding(flat, "_Kit.fbx").empty());
    REQUIRE_FALSE(exportedEnding(flat, "_Kit_layout.json").empty());

    REQUIRE(flat.sample.run({"export", "kit", "off"}).exitCode == kExitOk);
    const auto outcome = flat.exportWith();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    requireReport(result);
    CHECK(result.at("kit").at("asked") == false);
    CHECK(result.at("kit").at("written") == false);
    CHECK(exportedEnding(flat, "_Kit.fbx").empty());
    CHECK(exportedEnding(flat, "_Kit_layout.json").empty());
    CHECK_FALSE(exportedEnding(flat, ".fbx").empty());
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
