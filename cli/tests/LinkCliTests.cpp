#include "../../tests/core/FakeUnrealEditor.h"
#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/UnrealRemote.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::utf8;

namespace {

// Checks a JSON value against a schema file of the repository.
void requireSchema(const std::string& file, const nlohmann::json& value) {
    std::ifstream in(fs::path(PLYNTH_SCHEMA_DIR) / file);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(in));
    REQUIRE_NOTHROW(validator.validate(value));
}

// The quoted parts of a command, in order: the script, then its arguments.
std::vector<std::string> quotedParts(const std::string& command) {
    std::vector<std::string> parts;
    std::size_t at = 0;
    while ((at = command.find('"', at)) != std::string::npos) {
        const std::size_t end = command.find('"', at + 1);
        if (end == std::string::npos) {
            break;
        }
        parts.push_back(command.substr(at + 1, end - at - 1));
        at = end + 1;
    }
    return parts;
}

// Plays the Plynth script inside the fake editor: reads the job the command names and writes a result the way the
// real script does. It remembers the assets and actors of earlier runs, so a second send updates them.
class FakeLinkScript {
public:
    // Runs one command and returns the lines the editor prints.
    std::vector<std::pair<std::string, std::string>> run(const std::string& command) {
        std::lock_guard lock(mutex_);
        ++runs_;
        const std::vector<std::string> parts = quotedParts(command);
        if (parts.size() != 2) {
            return {{"Error", "plynth_link: error bad_job"}};
        }
        const nlohmann::json job = nlohmann::json::parse(readFile(plynth::pathFromUtf8(parts[1])), nullptr, false);
        if (job.is_discarded() || !job.is_object()) {
            return {{"Error", "plynth_link: error bad_job"}};
        }
        job_ = job;
        const std::string content = job.at("content_path").get<std::string>();
        nlohmann::json result = {{"format", "plynth-unreal-result"}, {"version", 1}, {"job_id", job.at("job_id")},
                                 {"ok", true}, {"error", nullptr}, {"engine_version", "5.8.0-test"},
                                 {"project_name", "Game"}, {"content_path", content},
                                 {"assets", nlohmann::json::array()}, {"actors", nlohmann::json::array()},
                                 {"checks", nlohmann::json::array()}, {"screenshot", nullptr}, {"ms", 900}};
        const auto asset = [&](const std::string& path, const char* kind) {
            const bool known = !assets_.insert(path).second;
            result["assets"].push_back({{"path", path}, {"kind", kind}, {"action", known ? "updated" : "created"}});
        };
        for (const nlohmann::json& mesh : job.at("meshes")) {
            const std::string path = content + "/Meshes/" + mesh.at("asset").get<std::string>();
            asset(path, "static_mesh");
            const std::string label = mesh.at("actor").get<std::string>();
            const bool kept = !actors_.insert(label).second;
            result["actors"].push_back({{"label", label}, {"asset", path}, {"action", kept ? "kept" : "spawned"}});
        }
        for (const nlohmann::json& texture : job.at("textures")) {
            asset(content + "/Textures/" + texture.at("asset").get<std::string>(), "texture");
        }
        for (const nlohmann::json& material : job.at("materials")) {
            asset(content + "/Materials/" + material.at("asset").get<std::string>(), "material_instance");
        }
        if (job.at("checks").is_object()) {
            result["checks"].push_back({{"id", "scale"}, {"status", "pass"}, {"message", "The building has the size of its walls."}});
            result["checks"].push_back({{"id", "collision"}, {"status", "fail"}, {"message", "One mesh has no collision."}});
            result["screenshot"] = job.at("checks").at("screenshot");
        }
        std::ofstream(plynth::pathFromUtf8(job.at("result_file").get<std::string>()), std::ios::binary) << result.dump(2);
        return {{"Info", "plynth_link: ok"}};
    }

    // How many commands it ran.
    int runs() {
        std::lock_guard lock(mutex_);
        return runs_;
    }

    // The last job it read.
    nlohmann::json job() {
        std::lock_guard lock(mutex_);
        return job_;
    }

private:
    std::mutex mutex_;
    std::set<std::string> assets_;
    std::set<std::string> actors_;
    nlohmann::json job_;
    int runs_ = 0;
};

// A built square room with a fake Blender, the stand in link script and a fake Unreal editor on the test's own
// port, which the endpoint variable points the link at. Never Unreal's own group or the headless check's port.
struct LinkSample {
    LinkSample(const std::string& name, int editorPort)
        : port(editorPort),
          endpoint("PLYNTH_UNREAL_MULTICAST", std::string(plynth::test::kFakeEditorGroup) + ":" + std::to_string(editorPort)),
          sample(name) {
        REQUIRE(port != plynth::kUnrealMulticastPort);
        REQUIRE(port != 6866);
        scripts = sample.temp.path() / "unreal";
        fs::create_directories(scripts);
        std::ofstream(scripts / "plynth_link.py") << "# stand in for the link script\n";
        scriptsVariable = std::make_unique<ScopedEnvironment>("PLYNTH_UNREAL_SCRIPTS", utf8(scripts));
        sample.drawRoomSquare();
        fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0");
        REQUIRE(build().exitCode == kExitOk);
    }

    // Runs build with the fake Blender and any extra options.
    Outcome build(std::vector<std::string> extra = {}) const {
        std::vector<std::string> args = {"build", "--blender", utf8(fake)};
        args.insert(args.end(), extra.begin(), extra.end());
        return sample.run(args);
    }

    // Runs "link unreal <args>" on the project.
    Outcome link(std::vector<std::string> args) const {
        args.insert(args.begin(), {"link", "unreal"});
        return sample.run(args);
    }

    // Runs "link unreal send" with the fake Blender and any extra options.
    Outcome send(std::vector<std::string> extra = {}) const {
        std::vector<std::string> args = {"send", "--blender", utf8(fake)};
        args.insert(args.end(), extra.begin(), extra.end());
        return link(args);
    }

    // Starts a fake editor with a project open. A second call replaces the first editor.
    void openEditor(const std::string& project) {
        editor.reset();
        editor = std::make_unique<plynth::test::FakeUnrealEditor>(
            port, "node-" + project, project, [this](const std::string& command) { return script.run(command); });
        REQUIRE(editor->ready());
    }

    void closeEditor() { editor.reset(); }

    // Sets the fake Blender's mode.
    void setMode(const std::string& mode) const { std::ofstream(fake.parent_path() / "fake_mode.txt") << mode; }

    fs::path work() const { return sample.folder / "build" / "links" / "unreal"; }
    fs::path outFolder() const { return work() / "out"; }

    // The saved export-options.json, or an empty object when there is none.
    nlohmann::json options() const {
        const fs::path file = sample.folder / "export-options.json";
        return fs::exists(file) ? nlohmann::json::parse(readFile(file)) : nlohmann::json::object();
    }

    // The file in the link's output folder whose name ends with the suffix, or an empty path.
    fs::path outEnding(const std::string& suffix) const {
        std::error_code ignored;
        for (const auto& entry : fs::directory_iterator(outFolder(), ignored)) {
            if (entry.path().filename().string().ends_with(suffix)) {
                return entry.path();
            }
        }
        return {};
    }

    plynth::cli::testing::GuardedSettingsReset reset;
    int port;
    ScopedEnvironment endpoint;
    Sample sample;
    fs::path scripts;
    std::unique_ptr<ScopedEnvironment> scriptsVariable;
    fs::path fake;
    FakeLinkScript script;
    std::unique_ptr<plynth::test::FakeUnrealEditor> editor;
};

// Makes a library in the sample's folder and sets it in the settings.
fs::path useLibrary(const LinkSample& flat) {
    const fs::path library = flat.sample.temp.path() / "Library";
    REQUIRE(plynth::cli::testing::runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    return library;
}

// Puts a door on w2 and gives it a custom shape that only the library folder defines. The id is used by no other
// test, so nothing else in this process has registered it.
void addCustomShapeDoor(const LinkSample& flat, const fs::path& shapesFolder, const std::string& id) {
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

// Copies a library to a second folder no command in this process has registered the shapes of, which stands in
// for a fresh process.
fs::path freshCopy(const fs::path& library) {
    const fs::path copy = library.parent_path() / (library.filename().string() + "Fresh");
    fs::copy(library, copy, fs::copy_options::recursive);
    return copy;
}

}

TEST_CASE("help lists the link commands", "[cli][link]") {
    const auto outcome = plynth::cli::testing::runChecked({"help"});
    REQUIRE(outcome.exitCode == kExitOk);
    for (const char* words : {"link list", "link unreal status", "link unreal send", "link unreal set",
                              "link unreal unlink", "link unreal report"}) {
        INFO(words);
        CHECK(outcome.text.find(std::string("\"") + words + "\"") != std::string::npos);
    }
}

TEST_CASE("link list names the Unreal Engine link and the project's settings", "[cli][link]") {
    Sample sample("link-list");
    const auto outside = plynth::cli::testing::runChecked({"link", "list"}, sample.temp.path());
    REQUIRE(outside.exitCode == kExitOk);
    const auto& links = outside.answer.at("result").at("links");
    REQUIRE(links.size() >= 1);
    const auto unreal = std::find_if(links.begin(), links.end(), [](const auto& l) { return l.at("id") == "unreal"; });
    REQUIRE(unreal != links.end());
    CHECK(unreal->at("name") == "Unreal Engine");
    CHECK(unreal->at("profile") == "unreal");
    CHECK(unreal->at("settings").is_null());

    const auto inside = sample.run({"link", "list"});
    REQUIRE(inside.exitCode == kExitOk);
    const auto& settings = inside.answer.at("result").at("links").at(0).at("settings");
    CHECK(settings.at("pack_materials") == true);
    CHECK(settings.at("grouped").is_null());
    CHECK(settings.at("kit") == false);
    CHECK(settings.at("gameplay") == false);
    CHECK(settings.at("sync_after_build") == true);
    CHECK(settings.at("target").is_null());
}

TEST_CASE("link unreal set changes only the settings given and refuses bad values", "[cli][link]") {
    LinkSample flat("link-set", 7041);
    const auto set = flat.link({"set", "--pack-materials", "off", "--grouped", "on", "--kit", "on"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("command") == "link unreal set");
    const auto& settings = set.answer.at("result").at("settings");
    CHECK(settings.at("pack_materials") == false);
    CHECK(settings.at("grouped") == true);
    CHECK(settings.at("grouped_now") == true);
    CHECK(settings.at("kit") == true);
    CHECK(settings.at("gameplay") == false);
    nlohmann::json saved = flat.options();
    requireSchema("export-options.schema.json", saved);
    CHECK(saved.at("links").at("unreal").at("pack_materials") == false);
    CHECK(saved.at("links").at("unreal").at("grouped") == true);
    CHECK(flat.sample.run({"export", "kit"}).answer.at("result").at("enabled") == true);

    const auto automatic = flat.link({"set", "--grouped", "auto", "--sync", "off"});
    REQUIRE(automatic.exitCode == kExitOk);
    CHECK(automatic.answer.at("result").at("settings").at("grouped").is_null());
    CHECK(automatic.answer.at("result").at("settings").at("grouped_now") == false);
    CHECK(automatic.answer.at("result").at("settings").at("pack_materials") == false);
    CHECK(automatic.answer.at("result").at("settings").at("sync_after_build") == false);

    const std::string before = readFile(flat.sample.folder / "export-options.json");
    const auto nothing = flat.link({"set"});
    CHECK(nothing.exitCode == kExitBadArguments);
    CHECK(errorCode(nothing) == "bad_arguments");
    const auto maybe = flat.link({"set", "--kit", "maybe", "--gameplay", "on"});
    CHECK(maybe.exitCode == kExitBadArguments);
    CHECK(readFile(flat.sample.folder / "export-options.json") == before);

    REQUIRE(flat.sample.run({"export", "gameplay", "on"}).exitCode == kExitOk);
    CHECK(flat.link({"status"}).answer.at("result").at("settings").at("gameplay") == true);
}

TEST_CASE("link unreal send puts the building into the open editor and links it", "[cli][link]") {
    LinkSample flat("link-send", 7042);
    flat.openEditor("Game");
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    CHECK(sent.answer.at("command") == "link unreal send");
    const auto& report = sent.answer.at("result");
    requireSchema("link-report.schema.json", report);
    CHECK(report.at("status").at("code") == "ok");
    CHECK(report.at("skipped") == false);
    CHECK(report.at("target").at("project_name") == "Game");
    CHECK(report.at("export").at("profile") == "unreal");
    CHECK(report.at("content_path").get<std::string>().starts_with("/Game/Plynth/"));
    REQUIRE_FALSE(report.at("assets").empty());
    CHECK(report.at("assets").at(0).at("action") == "created");
    CHECK(report.at("actors").at(0).at("action") == "spawned");
    CHECK(flat.script.runs() == 1);
    CHECK(fs::exists(flat.work() / "unreal_job.json"));
    CHECK(fs::exists(flat.work() / "link_report.json"));
    CHECK(sent.progress.find("\"stage\":\"delivering\"") != std::string::npos);
    CHECK(flat.options().at("links").at("unreal").at("target").at("project_name") == "Game");

    const auto status = flat.link({"status"});
    REQUIRE(status.exitCode == kExitOk);
    const auto& shown = status.answer.at("result");
    CHECK(shown.at("link") == "unreal");
    CHECK(shown.at("target_open") == true);
    CHECK(shown.at("search_problem").is_null());
    REQUIRE(shown.at("editors").size() == 1);
    CHECK(shown.at("editors").at(0).at("id") == "node-Game");
    CHECK(shown.at("editors").at(0).at("is_target") == true);
    CHECK(shown.at("settings").at("target").at("project_name") == "Game");
    CHECK(shown.at("last_report").at("job_id") == report.at("job_id"));

    const auto again = flat.link({"report"});
    REQUIRE(again.exitCode == kExitOk);
    requireSchema("link-report.schema.json", again.answer.at("result"));
    CHECK(again.answer.at("result").at("job_id") == report.at("job_id"));
}

TEST_CASE("a second send updates the assets in place and keeps the actors", "[cli][link]") {
    LinkSample flat("link-twice", 7043);
    flat.openEditor("Game");
    REQUIRE(flat.send().exitCode == kExitOk);
    const auto second = flat.send({"--quiet"});
    REQUIRE(second.exitCode == kExitOk);
    CHECK(second.progress.empty());
    for (const auto& asset : second.answer.at("result").at("assets")) {
        CHECK(asset.at("action") == "updated");
    }
    for (const auto& actor : second.answer.at("result").at("actors")) {
        CHECK(actor.at("action") == "kept");
    }
    CHECK(flat.script.runs() == 2);
}

TEST_CASE("a send with no editor, or with several and none chosen, names the problem", "[cli][link]") {
    LinkSample flat("link-no-editor", 7044);
    const auto none = flat.send();
    CHECK(none.exitCode == kExitFailed);
    CHECK(errorCode(none) == "engine_not_found");
    CHECK_FALSE(fs::exists(flat.work() / "link_report.json"));
    CHECK_FALSE(flat.options().contains("links"));

    const auto status = flat.link({"status"});
    REQUIRE(status.exitCode == kExitOk);
    CHECK(status.answer.at("result").at("editors").empty());
    CHECK(status.answer.at("result").at("target_open") == false);
    CHECK(status.answer.at("result").at("last_report").is_null());
    const auto report = flat.link({"report"});
    CHECK(report.exitCode == kExitFailed);
    CHECK(errorCode(report) == "not_found");

    flat.openEditor("Game");
    FakeLinkScript other;
    plynth::test::FakeUnrealEditor castle(flat.port, "node-Castle", "Castle",
                                          [&other](const std::string& command) { return other.run(command); });
    REQUIRE(castle.ready());
    const auto several = flat.send();
    CHECK(several.exitCode == kExitFailed);
    CHECK(errorCode(several) == "engine_not_found");
    const std::string message = several.answer.at("error").at("message").get<std::string>();
    CHECK(message.find("node-Game") != std::string::npos);
    CHECK(message.find("node-Castle") != std::string::npos);

    const auto chosen = flat.send({"--editor", "node-Castle"});
    REQUIRE(chosen.exitCode == kExitOk);
    CHECK(chosen.answer.at("result").at("target").at("project_name") == "Castle");
    CHECK(other.runs() == 1);
    CHECK(flat.script.runs() == 0);
    const auto unknown = flat.send({"--editor", "node-Nowhere"});
    CHECK(errorCode(unknown) == "engine_not_found");
}

TEST_CASE("a blocked link export sends nothing and --force sends past it", "[cli][link]") {
    LinkSample flat("link-blocked", 7045);
    flat.openEditor("Game");
    flat.setMode("quality_fail");
    const auto blocked = flat.send();
    CHECK(blocked.exitCode == kExitFailed);
    CHECK(errorCode(blocked) == "quality_failed");
    CHECK(flat.script.runs() == 0);
    CHECK_FALSE(flat.options().contains("links"));
    const auto report = flat.link({"report"});
    REQUIRE(report.exitCode == kExitOk);
    CHECK(report.answer.at("result").at("status").at("code") == "quality_failed");

    const auto forced = flat.send({"--force"});
    REQUIRE(forced.exitCode == kExitOk);
    CHECK(forced.answer.at("result").at("export").at("forced") == true);
    CHECK(flat.script.runs() == 1);
}

TEST_CASE("a self check send keeps the checks and warns about the failed ones", "[cli][link]") {
    LinkSample flat("link-self-check", 7046);
    flat.openEditor("Game");
    const auto refused = flat.send({"--screenshot", "shot.png"});
    CHECK(refused.exitCode == kExitBadArguments);
    CHECK(errorCode(refused) == "bad_arguments");
    CHECK(flat.script.runs() == 0);

    const fs::path shot = flat.sample.temp.path() / "shot.png";
    const auto checked = flat.send({"--self-check", "--screenshot", utf8(shot)});
    REQUIRE(checked.exitCode == kExitOk);
    const auto& report = checked.answer.at("result");
    requireSchema("link-report.schema.json", report);
    REQUIRE(report.at("checks").size() == 2);
    CHECK(report.at("checks").at(0).at("id") == "scale");
    CHECK(report.at("screenshot") == utf8(shot));
    CHECK(flat.script.job().at("checks").at("screenshot") == utf8(shot));
    REQUIRE(checked.answer.contains("warnings"));
    CHECK(checked.answer.at("warnings").at(0).at("code") == "check_failed");

    REQUIRE(flat.send().exitCode == kExitOk);
    CHECK(flat.script.job().at("checks").is_null());
}

TEST_CASE("build updates the linked editor, and --no-sync or a closed editor leaves it", "[cli][link][sync]") {
    LinkSample flat("link-build-sync", 7047);
    flat.openEditor("Game");
    REQUIRE(flat.send().exitCode == kExitOk);

    const auto built = flat.build();
    REQUIRE(built.exitCode == kExitOk);
    const auto& result = built.answer.at("result");
    REQUIRE(result.contains("links"));
    REQUIRE(result.at("links").size() == 1);
    const auto& synced = result.at("links").at(0);
    requireSchema("link-report.schema.json", synced);
    CHECK(synced.at("link") == "unreal");
    CHECK(synced.at("skipped") == false);
    CHECK(synced.at("assets").at(0).at("action") == "updated");
    CHECK(flat.script.runs() == 2);
    CHECK(built.progress.find("\"stage\":\"link_delivering\"") != std::string::npos);
    nlohmann::json report = result;
    report.erase("links");
    requireSchema("build-report.schema.json", report);

    const auto quiet = flat.build({"--no-sync"});
    REQUIRE(quiet.exitCode == kExitOk);
    CHECK_FALSE(quiet.answer.at("result").contains("links"));
    CHECK(flat.script.runs() == 2);

    flat.closeEditor();
    const auto closed = flat.build();
    REQUIRE(closed.exitCode == kExitOk);
    const auto& skipped = closed.answer.at("result").at("links").at(0);
    CHECK(skipped.at("skipped") == true);
    CHECK(skipped.at("status").at("code") == "ok");
    CHECK_FALSE(skipped.at("skipped_reason").get<std::string>().empty());
    REQUIRE(closed.answer.contains("warnings"));
    bool warned = false;
    for (const auto& warning : closed.answer.at("warnings")) {
        warned = warned || warning.at("code") == "link_skipped";
    }
    CHECK(warned);
}

TEST_CASE("a build syncs only into the linked project", "[cli][link][sync]") {
    LinkSample flat("link-build-other", 7048);
    flat.openEditor("Game");
    REQUIRE(flat.send().exitCode == kExitOk);
    flat.openEditor("Castle");
    const auto built = flat.build();
    REQUIRE(built.exitCode == kExitOk);
    CHECK(built.answer.at("result").at("links").at(0).at("skipped") == true);
    CHECK(flat.script.runs() == 1);
}

TEST_CASE("unlink and --sync off stop the sync after a build", "[cli][link][sync]") {
    LinkSample flat("link-unlink", 7049);
    flat.openEditor("Game");
    REQUIRE(flat.send().exitCode == kExitOk);

    REQUIRE(flat.link({"set", "--sync", "off"}).exitCode == kExitOk);
    CHECK_FALSE(flat.build().answer.at("result").contains("links"));
    CHECK(flat.script.runs() == 1);
    REQUIRE(flat.link({"set", "--sync", "on"}).exitCode == kExitOk);

    const auto unlinked = flat.link({"unlink"});
    REQUIRE(unlinked.exitCode == kExitOk);
    CHECK(unlinked.answer.at("result").at("unlinked") == true);
    CHECK(unlinked.answer.at("result").at("target").at("project_name") == "Game");
    CHECK(unlinked.answer.at("result").at("settings").at("target").is_null());
    CHECK_FALSE(flat.options().contains("links"));
    CHECK_FALSE(flat.build().answer.at("result").contains("links"));
    CHECK(flat.script.runs() == 1);

    const auto again = flat.link({"unlink"});
    REQUIRE(again.exitCode == kExitOk);
    CHECK(again.answer.at("result").at("unlinked") == false);
    CHECK(again.answer.at("result").at("target").is_null());
}

TEST_CASE("pack materials off sends no textures and grouped on sends every group", "[cli][link]") {
    LinkSample flat("link-switches", 7050);
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--pack-materials", "off", "--grouped", "on"}).exitCode == kExitOk);
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    CHECK(sent.answer.at("result").at("pack_materials") == false);
    CHECK(sent.answer.at("result").at("grouped") == true);
    const nlohmann::json job = flat.script.job();
    CHECK(job.at("pack_materials") == false);
    CHECK(job.at("textures").empty());
    CHECK(job.at("materials").empty());
    CHECK(job.at("meshes").size() > 1);
}

TEST_CASE("the link sends the gameplay data file with gameplay on", "[cli][link][gameplay]") {
    LinkSample flat("link-gameplay", 7051);
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--gameplay", "on"}).exitCode == kExitOk);
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    const auto& gameplay = sent.answer.at("result").at("export").at("gameplay");
    CHECK(gameplay.at("enabled") == true);
    REQUIRE(gameplay.at("file").is_string());
    const fs::path file = plynth::pathFromUtf8(gameplay.at("file").get<std::string>());
    CHECK(file.parent_path().filename() == "out");
    CHECK(fs::exists(file));
    bool listed = false;
    for (const auto& name : flat.script.job().at("data_files")) {
        listed = listed || name.get<std::string>().ends_with("_gameplay.json");
    }
    CHECK(listed);
}

TEST_CASE("the link sends the kit and its layout with the kit on", "[cli][link][kit]") {
    LinkSample flat("link-kit-on", 7052);
    useLibrary(flat);
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    const auto& kit = sent.answer.at("result").at("export").at("kit");
    CHECK(kit.at("asked") == true);
    CHECK(kit.at("written") == true);
    CHECK(kit.at("skipped").is_null());
    CHECK(kit.at("pieces").get<int>() > 0);
    const fs::path kitFile = flat.outEnding("_Kit.fbx");
    const fs::path layoutFile = flat.outEnding("_Kit_layout.json");
    REQUIRE_FALSE(kitFile.empty());
    REQUIRE_FALSE(layoutFile.empty());
    CHECK(kit.at("file") == utf8(kitFile));
    CHECK(kit.at("layout_file") == utf8(layoutFile));
    requireSchema("kit-layout.schema.json", nlohmann::json::parse(readFile(layoutFile)));
    CHECK(flat.script.job().at("kit").is_object());
}

TEST_CASE("a send with the kit on and no library leaves the kit out and still succeeds", "[cli][link][kit]") {
    LinkSample flat("link-kit-nolibrary", 7053);
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    const auto& kit = sent.answer.at("result").at("export").at("kit");
    CHECK(kit.at("asked") == true);
    CHECK(kit.at("written") == false);
    REQUIRE(kit.at("skipped").is_string());
    CHECK_FALSE(kit.at("skipped").get<std::string>().empty());
    CHECK(flat.outEnding("_Kit.fbx").empty());
    CHECK_FALSE(flat.outEnding(".fbx").empty());
    CHECK(flat.script.job().at("kit").is_null());
}

TEST_CASE("link send takes the kit library from --library and refuses a bad one", "[cli][link][kit]") {
    LinkSample flat("link-kit-library-option", 7054);
    const fs::path library = useLibrary(flat);
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);

    const auto without = flat.send();
    REQUIRE(without.exitCode == kExitOk);
    CHECK(without.answer.at("result").at("export").at("kit").at("written") == false);

    const auto with = flat.send({"--library", utf8(library)});
    REQUIRE(with.exitCode == kExitOk);
    CHECK(with.answer.at("result").at("export").at("kit").at("written") == true);
    CHECK_FALSE(flat.outEnding("_Kit.fbx").empty());

    const int runs = flat.script.runs();
    const auto refused = flat.send({"--library", utf8(flat.sample.temp.path() / "typo")});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "no_library");
    CHECK(flat.script.runs() == runs);
    CHECK_FALSE(flat.outEnding("_Kit.fbx").empty());
}

TEST_CASE("a send says why a broken configured library left the kit out", "[cli][link][kit]") {
    LinkSample flat("link-kit-broken-library", 7055);
    const fs::path library = useLibrary(flat);
    std::ofstream(library / "materials" / "materials.db", std::ios::binary) << "not a database";
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    const auto& kit = sent.answer.at("result").at("export").at("kit");
    CHECK(kit.at("written") == false);
    const std::string why = kit.at("skipped").get<std::string>();
    CHECK_FALSE(why.empty());
    CHECK(why.find("No library is set") == std::string::npos);
}

TEST_CASE("the link's kit uses a custom opening shape from the configured library", "[cli][link][kit]") {
    LinkSample flat("link-kit-shape-settings", 7056);
    const fs::path library = useLibrary(flat);
    addCustomShapeDoor(flat, library / "shapes", "ghost_link_s1");
    const fs::path fresh = freshCopy(library);
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.build().exitCode == kExitOk);

    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", utf8(fresh)}).exitCode == kExitOk);
    flat.openEditor("Game");
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    CHECK(sent.answer.at("result").at("export").at("kit").at("written") == true);
    const fs::path layoutFile = flat.outEnding("_Kit_layout.json");
    REQUIRE_FALSE(layoutFile.empty());
    CHECK(readFile(layoutFile).find("\"kind\": \"opening\"") != std::string::npos);
}

TEST_CASE("the link's kit uses a custom opening shape from --library", "[cli][link][kit]") {
    LinkSample flat("link-kit-shape-option", 7057);
    const fs::path library = useLibrary(flat);
    REQUIRE(plynth::cli::testing::runChecked({"settings", "set", "--library", "none"}).exitCode == kExitOk);
    addCustomShapeDoor(flat, library / "shapes", "ghost_link_o1");
    const fs::path fresh = freshCopy(library);
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.build().exitCode == kExitOk);

    flat.openEditor("Game");
    const auto sent = flat.send({"--library", utf8(fresh)});
    REQUIRE(sent.exitCode == kExitOk);
    CHECK(sent.answer.at("result").at("export").at("kit").at("written") == true);
    const fs::path layoutFile = flat.outEnding("_Kit_layout.json");
    REQUIRE_FALSE(layoutFile.empty());
    CHECK(readFile(layoutFile).find("\"kind\": \"opening\"") != std::string::npos);
}

TEST_CASE("link unreal set --kit off removes the kit files at the next send", "[cli][link][kit]") {
    LinkSample flat("link-kit-off", 7058);
    useLibrary(flat);
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.send().exitCode == kExitOk);
    REQUIRE_FALSE(flat.outEnding("_Kit.fbx").empty());
    REQUIRE_FALSE(flat.outEnding("_Kit_layout.json").empty());

    REQUIRE(flat.link({"set", "--kit", "off"}).exitCode == kExitOk);
    const auto sent = flat.send();
    REQUIRE(sent.exitCode == kExitOk);
    const auto& kit = sent.answer.at("result").at("export").at("kit");
    CHECK(kit.at("asked") == false);
    CHECK(kit.at("written") == false);
    CHECK(flat.outEnding("_Kit.fbx").empty());
    CHECK(flat.outEnding("_Kit_layout.json").empty());
    CHECK_FALSE(flat.outEnding(".fbx").empty());
    CHECK(flat.script.job().at("kit").is_null());
}

TEST_CASE("the link commands never change plan.json or project.json", "[cli][link]") {
    LinkSample flat("link-files", 7059);
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");
    flat.openEditor("Game");
    REQUIRE(flat.link({"set", "--kit", "on", "--grouped", "on"}).exitCode == kExitOk);
    REQUIRE(flat.send().exitCode == kExitOk);
    REQUIRE(flat.link({"unlink"}).exitCode == kExitOk);
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}
