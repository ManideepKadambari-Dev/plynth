#include "FakeBlender.h"
#include "FakeUnrealEditor.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/EngineLink.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/UnrealLink.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// The quoted parts of a command, in order: the script, then its arguments.
std::vector<std::string> quotedParts(const std::string& command)
{
    std::vector<std::string> parts;
    std::size_t at = 0;
    while ((at = command.find('"', at)) != std::string::npos) {
        const std::size_t end = command.find('"', at + 1);
        if (end == std::string::npos) break;
        parts.push_back(command.substr(at + 1, end - at - 1));
        at = end + 1;
    }
    return parts;
}

// Plays the Plynth script inside the fake editor: reads the job, checks it against the schema and writes a
// result as the real script would. It remembers the assets and actors of earlier runs, so a second send updates.
struct FakeLinkScript {
    std::string mode = "ok"; // "ok", "folder_taken" or "no_result"
    std::mutex mutex;
    std::set<std::string> assets;
    std::set<std::string> actors;
    std::string schemaProblem;
    int runs = 0;

    // Runs one command and returns the lines the editor prints.
    std::vector<std::pair<std::string, std::string>> run(const std::string& command)
    {
        std::lock_guard lock(mutex);
        ++runs;
        const std::vector<std::string> parts = quotedParts(command);
        if (parts.size() != 2) return {{"Error", "plynth_link: error bad_job"}};
        const Json job = Json::parse(readTextFile(pathFromUtf8(parts[1])), nullptr, false);
        if (job.is_discarded()) return {{"Error", "plynth_link: error bad_job"}};
        schemaProblem = plynth::test::schemaProblem("unreal-job.schema.json", nlohmann::json::parse(job.dump()));
        if (mode == "no_result") return {{"Info", "starting"}, {"Error", "plynth_link: error import_failed"}};

        const std::string content = job["content_path"].get<std::string>();
        Json result = {{"format", "plynth-unreal-result"}, {"version", 1}, {"job_id", job["job_id"]}, {"ok", true},
                       {"error", nullptr}, {"engine_version", "5.8.0-test"}, {"project_name", "FakeProject"},
                       {"content_path", content}, {"assets", Json::array()}, {"actors", Json::array()},
                       {"checks", Json::array()}, {"screenshot", nullptr}, {"ms", 1200}};
        if (mode == "folder_taken") {
            result["ok"] = false;
            result["error"] = Json{{"code", "folder_taken"},
                                   {"message", "Content/Plynth/Square belongs to the Plynth project D:/Other. Rename this project."}};
        } else {
            const auto asset = [&](const std::string& path, const char* kind) {
                const bool known = !assets.insert(path).second;
                result["assets"].push_back(Json{{"path", path}, {"kind", kind}, {"action", known ? "updated" : "created"}});
            };
            for (const Json& mesh : job["meshes"]) {
                const std::string path = content + "/Meshes/" + mesh["asset"].get<std::string>();
                asset(path, "static_mesh");
                const std::string label = mesh["actor"].get<std::string>();
                const bool kept = !actors.insert(label).second;
                result["actors"].push_back(Json{{"label", label}, {"asset", path}, {"action", kept ? "kept" : "spawned"}});
            }
            for (const Json& texture : job["textures"]) asset(content + "/Textures/" + texture["asset"].get<std::string>(), "texture");
            for (const Json& material : job["materials"]) {
                asset(content + "/Materials/" + material["asset"].get<std::string>(), "material_instance");
            }
            if (job["checks"].is_object()) {
                result["checks"].push_back(Json{{"id", "scale"}, {"status", "pass"}, {"message", "The building has the size of its walls."}});
                result["screenshot"] = job["checks"]["screenshot"];
            }
        }
        std::ofstream(pathFromUtf8(job["result_file"].get<std::string>()), std::ios::binary) << result.dump(2);
        if (mode == "folder_taken") return {{"Error", "plynth_link: error folder_taken"}};
        return {{"Info", "plynth_link: ok"}};
    }
};

// A built project folder, a fake Blender for the prepare step, the stand in link script and a fake editor on a
// port of the test's own, found through the endpoint variable.
struct LinkFixture {
    LinkFixture(const std::string& tag, int editorPort)
        : tmp(tag), port(editorPort),
          endpoint("PLYNTH_UNREAL_MULTICAST", std::string(kFakeEditorGroup) + ":" + std::to_string(editorPort))
    {
        REQUIRE(port != kUnrealMulticastPort);
        REQUIRE(port != 6866);
        projectFolder = tmp.path() / "Project";
        fs::create_directories(projectFolder / "build");
        std::ofstream(projectFolder / "build" / "building.blend", std::ios::binary) << "FAKEBLEND";
        blenderFolder = tmp.path() / "blender";
        const fs::path exe = makeFakeBlender(blenderFolder, "5.2.0");
        unrealScripts = tmp.path() / "unreal";
        fs::create_directories(unrealScripts);
        std::ofstream(unrealScripts / "plynth_link.py") << "# stand in for the link script\n";
        scriptsVariable = std::make_unique<ScopedEnv>("PLYNTH_UNREAL_SCRIPTS", pathToUtf8(unrealScripts));

        request.projectFolder = projectFolder;
        request.project = squareProjectWithWalls();
        request.project.settings.name = "Square";
        request.link = std::string(kUnrealLinkId);
        request.blender = inspectBlender(exe);
        request.run.scriptsFolder = makeFakeScripts(tmp.path() / "scripts");
    }

    // Starts a fake editor with a project open. Only one runs at a time through this fixture.
    void openEditor(const std::string& project)
    {
        editor.reset();
        editor = std::make_unique<FakeUnrealEditor>(port, "node-" + project, project,
                                                    [this](const std::string& command) { return script.run(command); });
        REQUIRE(editor->ready());
    }

    void closeEditor() { editor.reset(); }
    void setBlenderMode(const std::string& mode) const { std::ofstream(blenderFolder / "fake_mode.txt") << mode; }

    fs::path work() const { return linkFolder(projectFolder, kUnrealLinkId); }
    fs::path jobFile() const { return work() / "unreal_job.json"; }
    fs::path reportFile() const { return work() / "link_report.json"; }
    fs::path lockFile() const { return projectFolder / "build" / "build.lock"; }

    // The stored options of the Unreal link.
    EngineLinkOptions storedOptions() const
    {
        const ExportOptionsRead read = readExportOptions(projectFolder);
        requireOk(read.status);
        return engineLinkOptions(read.options, kUnrealLinkId);
    }

    // What the schema says about a written file. Empty when it passes.
    static std::string fileProblem(const std::string& schema, const fs::path& file)
    {
        const std::string text = readTextFile(file);
        if (text.empty()) return "no file " + pathToUtf8(file);
        return schemaProblemForText(schema, text);
    }

    TempDir tmp;
    int port;
    ScopedEnv endpoint;
    std::unique_ptr<ScopedEnv> scriptsVariable;
    fs::path projectFolder;
    fs::path blenderFolder;
    fs::path unrealScripts;
    EngineLinkRequest request;
    FakeLinkScript script;
    std::unique_ptr<FakeUnrealEditor> editor;
};

// A link that does nothing, for the registry tests.
class TestLink final : public EngineLink {
public:
    explicit TestLink(std::string id) : id_(std::move(id)) {}
    std::string_view id() const override { return id_; }
    std::string_view name() const override { return "Test engine"; }
    std::string_view description() const override { return "Does nothing."; }
    ExportProfile profile() const override { return ExportProfile::Plain; }
    EngineEditorSearch findEditors(std::chrono::milliseconds, std::stop_token) const override { return {}; }
    Status deliver(const EngineDelivery&, EngineLinkReport&, const std::function<void(const EngineLinkProgress&)>&,
                   std::stop_token) const override
    {
        return {};
    }

private:
    std::string id_;
};

}

TEST_CASE("the Unreal Engine link is registered and links are entries of a registry", "[core][link]")
{
    const EngineLink* unreal = findEngineLink("unreal");
    REQUIRE(unreal);
    CHECK(unreal->id() == "unreal");
    CHECK(unreal->name() == "Unreal Engine");
    CHECK(unreal->profile() == ExportProfile::Unreal);
    CHECK_FALSE(unreal->description().empty());
    CHECK(findEngineLink("unity") == nullptr);

    CHECK_FALSE(registerEngineLink(nullptr));
    CHECK_FALSE(registerEngineLink(std::make_unique<TestLink>("unreal")));
    CHECK_FALSE(registerEngineLink(std::make_unique<TestLink>("Bad-Id")));
    CHECK_FALSE(registerEngineLink(std::make_unique<TestLink>("")));
    CHECK(registerEngineLink(std::make_unique<TestLink>("zz_registry_test")));
    CHECK(findEngineLink("zz_registry_test") != nullptr);
    CHECK(findEngineLink("unreal") == unreal);

    const std::vector<const EngineLink*> all = allEngineLinks();
    REQUIRE(all.size() >= 2);
    for (std::size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1]->id() < all[i]->id());

    CHECK(engineLinkStageName(EngineLinkStage::Finding) == "finding");
    CHECK(engineLinkStageName(EngineLinkStage::Preparing) == "preparing");
    CHECK(engineLinkStageName(EngineLinkStage::Delivering) == "delivering");
    CHECK(engineLinkStageName(EngineLinkStage::Done) == "done");
    CHECK(linkFolder("D:/P", "unreal") == fs::path("D:/P") / "build" / "links" / "unreal");
}

TEST_CASE("a send prepares the unreal export, runs the script in the editor and stores the target", "[core][link]")
{
    LinkFixture f("link-send", 6921);
    f.openEditor("Cottage");
    std::vector<EngineLinkStage> stages;
    const EngineLinkReport report = runEngineLink(f.request, [&](const EngineLinkProgress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
    });
    requireOk(report.status);
    CHECK(stages == std::vector<EngineLinkStage>{EngineLinkStage::Finding, EngineLinkStage::Preparing,
                                                 EngineLinkStage::Delivering, EngineLinkStage::Done});

    CHECK_FALSE(report.skipped);
    CHECK(report.link == "unreal");
    CHECK(report.jobId.rfind("link-", 0) == 0);
    REQUIRE(report.target);
    CHECK(report.target->projectName == "Cottage");
    CHECK(report.contentPath == "/Game/Plynth/Square");
    CHECK(report.packMaterials);
    CHECK_FALSE(report.grouped);

    CHECK(report.exported.profile == ExportProfile::Unreal);
    CHECK(report.exported.exported);
    CHECK(fs::is_regular_file(f.work() / "out" / "Square.fbx"));
    CHECK(fs::is_regular_file(f.work() / "out" / "Textures" / "T_Fake_BaseColor.png"));
    CHECK_FALSE(fs::exists(f.projectFolder / "export"));
    CHECK_FALSE(fs::exists(f.lockFile()));

    REQUIRE(report.assets.size() == 2);
    CHECK(report.assets[0] == EngineAssetResult{"/Game/Plynth/Square/Meshes/SM_Square", "static_mesh", "created"});
    CHECK(report.assets[1] == EngineAssetResult{"/Game/Plynth/Square/Textures/T_Fake_BaseColor", "texture", "created"});
    CHECK(report.actors == std::vector<EngineActorResult>{{"Square", "/Game/Plynth/Square/Meshes/SM_Square", "spawned"}});
    CHECK(report.checks.empty());

    REQUIRE(f.editor->commands() == 1);
    const std::vector<std::string> parts = quotedParts(f.editor->lastCommand());
    REQUIRE(parts.size() == 2);
    CHECK(pathFromUtf8(parts[0]) == f.unrealScripts / "plynth_link.py");
    CHECK(pathFromUtf8(parts[1]) == f.jobFile());
    INFO(f.script.schemaProblem);
    CHECK(f.script.schemaProblem.empty());
    INFO(LinkFixture::fileProblem("unreal-job.schema.json", f.jobFile()));
    CHECK(LinkFixture::fileProblem("unreal-job.schema.json", f.jobFile()).empty());

    CHECK(report.reportFile == f.reportFile());
    INFO(LinkFixture::fileProblem("link-report.schema.json", f.reportFile()));
    CHECK(LinkFixture::fileProblem("link-report.schema.json", f.reportFile()).empty());
    CHECK(report.logFile == f.work() / "editor.log");
    CHECK(readTextFile(report.logFile).find("plynth_link: ok") != std::string::npos);

    const EngineLinkOptions stored = f.storedOptions();
    REQUIRE(stored.target);
    CHECK(stored.target->projectName == "Cottage");
    CHECK(stored.target->projectRoot == fs::path("C:/Fake/Projects/Cottage/"));
    CHECK(stored.target->engineVersion == "5.8.0-test");
    CHECK(stored.packMaterials);
    CHECK_FALSE(fs::exists(f.projectFolder / "plan.json"));
}

TEST_CASE("a second send updates the assets in place and keeps the actors", "[core][link]")
{
    LinkFixture f("link-twice", 6922);
    f.openEditor("Cottage");
    const EngineLinkReport first = runEngineLink(f.request);
    requireOk(first.status);
    const EngineLinkReport second = runEngineLink(f.request);
    requireOk(second.status);

    CHECK(second.jobId != first.jobId);
    CHECK(f.editor->commands() == 2);
    REQUIRE(second.assets.size() == 2);
    CHECK(second.assets[0].path == "/Game/Plynth/Square/Meshes/SM_Square");
    CHECK(second.assets[0].action == "updated");
    REQUIRE(second.actors.size() == 1);
    CHECK(second.actors[0].action == "kept");
    REQUIRE(second.target);
    CHECK(second.target->projectName == "Cottage");
    CHECK(f.storedOptions().target == first.target);

    const EngineLinkReportRead read = readEngineLinkReport(f.projectFolder, kUnrealLinkId);
    requireOk(read.status);
    CHECK(read.report.jobId == second.jobId);
}

TEST_CASE("a sync goes into the stored editor and is skipped without an error when it is not open", "[core][link][sync]")
{
    LinkFixture f("link-sync-none", 6923);
    CHECK(syncEngineLinks(f.request).empty());

    f.openEditor("Cottage");
    const EngineLinkReport sent = runEngineLink(f.request);
    requireOk(sent.status);

    const std::vector<EngineLinkReport> synced = syncEngineLinks(f.request);
    REQUIRE(synced.size() == 1);
    requireOk(synced[0].status);
    CHECK_FALSE(synced[0].skipped);
    CHECK(f.editor->commands() == 2);
    CHECK(synced[0].assets[0].action == "updated");

    f.closeEditor();
    const std::vector<EngineLinkReport> skipped = syncEngineLinks(f.request);
    REQUIRE(skipped.size() == 1);
    requireOk(skipped[0].status);
    CHECK(skipped[0].skipped);
    CHECK(skipped[0].link == "unreal");
    CHECK(skipped[0].skippedReason == "Unreal Engine is not open, so the building was not updated.");
    CHECK(skipped[0].exported.jobId.empty());
    CHECK(skipped[0].assets.empty());
    REQUIRE(skipped[0].target);
    CHECK(skipped[0].target->projectName == "Cottage");
    CHECK(schemaProblem("link-report.schema.json", nlohmann::json::parse(engineLinkReportToJson(skipped[0]).dump())).empty());

    // A skipped run leaves the report of the last real one.
    const EngineLinkReportRead read = readEngineLinkReport(f.projectFolder, kUnrealLinkId);
    requireOk(read.status);
    CHECK(read.report.jobId == synced[0].jobId);
    CHECK(f.storedOptions().target == sent.target);
}

TEST_CASE("a sync with another project open is skipped and sends nothing", "[core][link][sync]")
{
    LinkFixture f("link-sync-other", 6924);
    f.openEditor("Cottage");
    requireOk(runEngineLink(f.request).status);

    f.openEditor("Castle");
    const std::vector<EngineLinkReport> synced = syncEngineLinks(f.request);
    REQUIRE(synced.size() == 1);
    requireOk(synced[0].status);
    CHECK(synced[0].skipped);
    CHECK(synced[0].skippedReason.find("another project open") != std::string::npos);
    CHECK(synced[0].skippedReason.find("Cottage") != std::string::npos);
    CHECK(f.editor->commands() == 0);
    REQUIRE(f.storedOptions().target);
    CHECK(f.storedOptions().target->projectName == "Cottage");

    // A sync is skipped too when the link's sync is switched off, because it is not run at all.
    ExportOptionsRead read = readExportOptions(f.projectFolder);
    requireOk(read.status);
    read.options.links[std::string(kUnrealLinkId)].syncAfterBuild = false;
    requireOk(writeExportOptions(f.projectFolder, read.options));
    CHECK(syncEngineLinks(f.request).empty());
}

TEST_CASE("a blocked export sends nothing and stores no target", "[core][link]")
{
    LinkFixture f("link-blocked", 6925);
    f.openEditor("Cottage");
    f.setBlenderMode("quality_fail");
    const EngineLinkReport report = runEngineLink(f.request);
    CHECK(report.status.code == ErrorCode::QualityFailed);
    CHECK(report.exported.status.code == ErrorCode::QualityFailed);
    CHECK_FALSE(report.exported.exported);
    CHECK_FALSE(report.skipped);
    CHECK(report.jobId.empty());
    CHECK(report.assets.empty());
    CHECK(f.editor->commands() == 0);
    CHECK_FALSE(fs::exists(f.jobFile()));
    CHECK_FALSE(f.storedOptions().target);
    CHECK_FALSE(fs::exists(f.lockFile()));

    INFO(LinkFixture::fileProblem("link-report.schema.json", f.reportFile()));
    CHECK(LinkFixture::fileProblem("link-report.schema.json", f.reportFile()).empty());
    const nlohmann::json written = nlohmann::json::parse(readTextFile(f.reportFile()));
    CHECK(written["status"]["code"] == "quality_failed");
    CHECK(written["export"]["error"]["code"] == "quality_failed");
    CHECK(written["assets"].empty());

    // Forcing goes past the failed check and sends.
    f.request.settings.force = true;
    requireOk(runEngineLink(f.request).status);
    CHECK(f.editor->commands() == 1);
}

TEST_CASE("a send with no editor, or with several and none chosen, names the problem", "[core][link]")
{
    LinkFixture f("link-no-editor", 6926);
    const EngineLinkReport none = runEngineLink(f.request);
    CHECK(none.status.code == ErrorCode::EngineNotFound);
    CHECK(none.status.message.find("Unreal Engine is not open") != std::string::npos);
    CHECK_FALSE(none.skipped);
    CHECK_FALSE(fs::exists(f.reportFile()));
    CHECK_FALSE(fs::exists(f.work() / "out"));

    f.openEditor("Cottage");
    FakeUnrealEditor castle(6926, "node-Castle", "Castle", [&f](const std::string& command) { return f.script.run(command); });
    REQUIRE(castle.ready());
    const EngineLinkReport several = runEngineLink(f.request);
    CHECK(several.status.code == ErrorCode::EngineNotFound);
    CHECK(several.status.message.find("Cottage (node-Cottage)") != std::string::npos);
    CHECK(several.status.message.find("Castle (node-Castle)") != std::string::npos);

    f.request.editor = "node-Castle";
    const EngineLinkReport chosen = runEngineLink(f.request);
    requireOk(chosen.status);
    CHECK(castle.commands() == 1);
    CHECK(f.editor->commands() == 0);
    REQUIRE(f.storedOptions().target);
    CHECK(f.storedOptions().target->projectName == "Castle");

    // With a stored target, a send by hand goes to that editor even when others are open.
    f.request.editor.clear();
    requireOk(runEngineLink(f.request).status);
    CHECK(castle.commands() == 2);
    CHECK(f.editor->commands() == 0);
}

TEST_CASE("a building folder owned by another project is not written and the editor's reason is given", "[core][link]")
{
    LinkFixture f("link-taken", 6927);
    f.openEditor("Cottage");
    f.script.mode = "folder_taken";
    const EngineLinkReport report = runEngineLink(f.request);
    CHECK(report.status.code == ErrorCode::EngineFailed);
    CHECK(report.status.message == "Content/Plynth/Square belongs to the Plynth project D:/Other. Rename this project.");
    CHECK(report.assets.empty());
    CHECK_FALSE(f.storedOptions().target);
    const nlohmann::json written = nlohmann::json::parse(readTextFile(f.reportFile()));
    CHECK(written["status"]["code"] == "engine_failed");

    f.script.mode = "no_result";
    const EngineLinkReport silent = runEngineLink(f.request);
    CHECK(silent.status.code == ErrorCode::EngineFailed);
    CHECK(silent.status.message.find("wrote no result") != std::string::npos);
    CHECK(silent.status.message.find("plynth_link: error import_failed") != std::string::npos);
}

TEST_CASE("the link's own switches give a grouped send without materials", "[core][link]")
{
    LinkFixture f("link-switches", 6928);
    ExportOptions options;
    options.links[std::string(kUnrealLinkId)].packMaterials = false;
    options.links[std::string(kUnrealLinkId)].grouped = true;
    requireOk(writeExportOptions(f.projectFolder, options));
    REQUIRE(f.request.project.settings.exportMode == ExportMode::Single);

    f.openEditor("Cottage");
    const EngineLinkReport report = runEngineLink(f.request);
    requireOk(report.status);
    CHECK(report.grouped);
    CHECK_FALSE(report.packMaterials);
    CHECK(report.exported.exporter == "grouped");

    const nlohmann::json job = nlohmann::json::parse(readTextFile(f.jobFile()));
    CHECK(schemaProblem("unreal-job.schema.json", job).empty());
    CHECK(job["grouped"] == true);
    CHECK(job["pack_materials"] == false);
    CHECK(job["textures"].empty());
    CHECK(job["materials"].empty());
    REQUIRE(job["meshes"].size() >= 2);
    bool structure = false;
    for (const auto& mesh : job["meshes"]) structure = structure || mesh["group"] == "structure";
    CHECK(structure);
    for (const EngineAssetResult& asset : report.assets) CHECK(asset.kind == "static_mesh");
    CHECK(report.actors.size() == job["meshes"].size());
}

TEST_CASE("a self check send asks the script for checks and keeps them in the report", "[core][link]")
{
    LinkFixture f("link-self-check", 6929);
    f.openEditor("Cottage");
    f.request.selfCheck = true;
    f.request.screenshot = f.tmp.path() / "shot.png";
    const EngineLinkReport report = runEngineLink(f.request);
    requireOk(report.status);

    const nlohmann::json job = nlohmann::json::parse(readTextFile(f.jobFile()));
    CHECK(schemaProblem("unreal-job.schema.json", job).empty());
    REQUIRE(job["checks"].is_object());
    REQUIRE(job["checks"]["expected_bounds"].is_object());
    CHECK(job["checks"]["expected_bounds"]["max"][0].get<double>() > 3.9);
    CHECK(pathFromUtf8(job["checks"]["screenshot"].get<std::string>()) == f.request.screenshot);

    REQUIRE(report.checks.size() == 1);
    CHECK(report.checks[0] == EngineCheckResult{"scale", "pass", "The building has the size of its walls."});
    CHECK(report.screenshot == f.request.screenshot);
    CHECK(LinkFixture::fileProblem("link-report.schema.json", f.reportFile()).empty());

    const EngineLinkReportRead read = readEngineLinkReport(f.projectFolder, kUnrealLinkId);
    requireOk(read.status);
    CHECK(read.report.checks == report.checks);
    CHECK(read.report.screenshot == report.screenshot);
}

TEST_CASE("the link report reads back, and unlink forgets the target", "[core][link]")
{
    LinkFixture f("link-report", 6930);
    CHECK(readEngineLinkReport(f.projectFolder, kUnrealLinkId).status.code == ErrorCode::NotFound);
    requireOk(unlinkEngine(f.projectFolder, kUnrealLinkId));

    f.openEditor("Cottage");
    const EngineLinkReport sent = runEngineLink(f.request);
    requireOk(sent.status);

    const EngineLinkReportRead read = readEngineLinkReport(f.projectFolder, kUnrealLinkId);
    requireOk(read.status);
    const EngineLinkReport& back = read.report;
    CHECK(back.status.ok());
    CHECK(back.link == "unreal");
    CHECK(back.jobId == sent.jobId);
    CHECK(back.startedAt == sent.startedAt);
    CHECK(back.elapsedMs == sent.elapsedMs);
    CHECK(back.target == sent.target);
    CHECK(back.contentPath == sent.contentPath);
    CHECK(back.packMaterials == sent.packMaterials);
    CHECK(back.grouped == sent.grouped);
    CHECK(back.assets == sent.assets);
    CHECK(back.actors == sent.actors);
    CHECK(back.reportFile == sent.reportFile);
    CHECK(back.logFile == sent.logFile);
    CHECK(back.exported.jobId == sent.exported.jobId);
    CHECK(back.exported.profile == ExportProfile::Unreal);
    CHECK(back.exported.exported);
    CHECK(back.exported.fileOutputs.size() == sent.exported.fileOutputs.size());
    CHECK(back.exported.files == sent.exported.files);

    std::ofstream(f.reportFile(), std::ios::binary) << R"({"format": "plynth-link-report", "version": 2})";
    CHECK(readEngineLinkReport(f.projectFolder, kUnrealLinkId).status.code == ErrorCode::BadFile);

    requireOk(unlinkEngine(f.projectFolder, kUnrealLinkId));
    CHECK_FALSE(f.storedOptions().target);
    CHECK(syncEngineLinks(f.request).empty());
    // Nothing else was set, so the options file is gone again.
    CHECK_FALSE(fs::exists(f.projectFolder / "export-options.json"));
    requireOk(unlinkEngine(f.projectFolder, kUnrealLinkId));
}
