#include "FakeBlender.h"
#include "SchemaSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/BlenderJob.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Process.h>
#include <plynth/core/catalog/AssetJobs.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A fake install, a scripts folder and a work folder in one temporary folder.
struct JobFixture {
    explicit JobFixture(const std::string& tag, const std::string& mode = "") : tmp(tag), isolated(tag + "-user")
    {
        exe = plynth::test::makeFakeBlender(tmp.path() / "blender", "5.2.0", mode);
        scripts = plynth::test::makeFakeScripts(tmp.path() / "scripts");
        work = tmp.path() / "work";
        options.scriptsFolder = scripts;
    }

    // A probe job writing its result into the work folder.
    Json probeJob(const std::string& id = "probe-00000001") const
    {
        BlenderUserFolders folders;
        folders.config = tmp.path() / "user" / "config";
        folders.scripts = tmp.path() / "user" / "scripts";
        folders.extensions = tmp.path() / "user" / "extensions";
        folders.blenderKitData = tmp.path() / "user" / "blenderkit_data";
        return makeProbeJob(folders, id, work / "result.json");
    }

    plynth::test::TempDir tmp;
    plynth::test::IsolatedUser isolated;
    fs::path exe;
    fs::path scripts;
    fs::path work;
    BlenderRunOptions options;
};

// Checks a JSON value against a schema.
std::string problemWith(const char* schema, const Json& json)
{
    return plynth::test::schemaProblem(schema, nlohmann::json::parse(json.dump()));
}

// A cleanup job for the fake.
Json cleanupJob(const JobFixture& f, const std::string& format, const std::string& id)
{
    CleanupJob job;
    job.sourceFile = f.tmp.path() / ("chair." + format);
    job.format = format;
    job.assetId = "chair";
    job.outputFolder = f.work / "item";
    return makeCleanupJob(job, id, f.work / "result.json");
}

}

TEST_CASE("runBlenderJob with an ok worker gives the result and progress in order", "[core][blenderjob]") {
    JobFixture f("job-ok");
    std::vector<BlenderProgress> progress;
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options, [&](const BlenderProgress& p) { progress.push_back(p); });
    plynth::test::requireOk(run.status);
    REQUIRE(run.exitCode == 0);
    REQUIRE(run.errorCode.empty());
    REQUIRE(run.result["ok"].get<bool>());
    REQUIRE(progress.size() == 2);
    REQUIRE(progress[0].step == "import_source");
    REQUIRE(progress[0].index == 1);
    REQUIRE(progress[0].count == 2);
    REQUIRE(progress[1].step == "save_blend");
    REQUIRE(progress[1].message == "Fake save");
    REQUIRE(fs::exists(f.work / "job.json"));
    const std::string log = plynth::test::readTextFile(f.work / "blender.log");
    REQUIRE(log.find("fake blender log line") != std::string::npos);
    REQUIRE(run.logTail.find("fake blender log line") != std::string::npos);
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
    REQUIRE(problemWith("blender-job.schema.json", Json::parse(plynth::test::readTextFile(f.work / "job.json"))).empty());
}

TEST_CASE("a failing worker gives blender_failed with its message and code", "[core][blenderjob]") {
    JobFixture f("job-fail", "fail");
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.status.message == "Fake import failure.");
    REQUIRE(run.errorCode == "import_failed");
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
}

TEST_CASE("a crash gives blender_failed with the log tail", "[core][blenderjob]") {
    JobFixture f("job-crash", "crash");
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.exitCode == 9);
    REQUIRE(run.status.message.find("exit code 9") != std::string::npos);
    REQUIRE(run.status.message.find("fake blender log line") != std::string::npos);
}

TEST_CASE("a bad result gives blender_failed with the log tail", "[core][blenderjob]") {
    JobFixture f("job-bad", "bad_result");
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.status.message.find("fake blender log line") != std::string::npos);
}

TEST_CASE("a stale result file is not read", "[core][blenderjob]") {
    JobFixture f("job-stale", "crash");
    fs::create_directories(f.work);
    std::ofstream(f.work / "result.json") << R"({"ok": true})";
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.result.is_null());
}

TEST_CASE("a stop request ends a hanging job as cancelled", "[core][blenderjob]") {
    JobFixture f("job-hang", "hang");
    std::stop_source source;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        source.request_stop();
    });
    const auto begin = std::chrono::steady_clock::now();
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options, {}, source.get_token());
    stopper.join();
    REQUIRE(run.status.code == ErrorCode::Cancelled);
    REQUIRE(std::chrono::steady_clock::now() - begin < std::chrono::seconds(10));
    REQUIRE(fs::exists(f.work / "blender.log"));
}

TEST_CASE("the timeout ends a hanging job as blender_failed", "[core][blenderjob]") {
    JobFixture f("job-timeout", "hang");
    f.options.timeout = std::chrono::seconds(1);
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(), f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.status.message.find("timed out") != std::string::npos);
}

TEST_CASE("a missing scripts folder is not_found", "[core][blenderjob]") {
    JobFixture f("job-noscripts");
    f.options.scriptsFolder = f.tmp.path() / "nowhere";
    REQUIRE(runBlenderJob(f.exe, f.probeJob(), f.work, f.options).status.code == ErrorCode::NotFound);
}

TEST_CASE("a missing Blender is blender_not_found", "[core][blenderjob]") {
    JobFixture f("job-noblender");
    REQUIRE(runBlenderJob(f.tmp.path() / "none" / "blender.exe", f.probeJob(), f.work, f.options).status.code ==
            ErrorCode::BlenderNotFound);
}

TEST_CASE("a run without factory startup works", "[core][blenderjob]") {
    JobFixture f("job-factory");
    f.options.factoryStartup = false;
    plynth::test::requireOk(runBlenderJob(f.exe, cleanupJob(f, "obj", "chair-00000008"), f.work, f.options).status);
}

namespace {

// Points the private Blender folder at a temporary one for the life of the object.
struct PrivateFolderScope {
    explicit PrivateFolderScope(const fs::path& folder) { SetEnvironmentVariableW(L"PLYNTH_BLENDER_PRIVATE", folder.c_str()); }
    ~PrivateFolderScope() { SetEnvironmentVariableW(L"PLYNTH_BLENDER_PRIVATE", nullptr); }
};

// The lines the fake wrote about the variables it saw.
std::vector<std::string> fakeEnvLines(const JobFixture& f)
{
    std::vector<std::string> lines;
    std::ifstream in(f.exe.parent_path() / "fake_env.log", std::ios::binary);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

}

TEST_CASE("a factory startup job and the version probe run in the private folder", "[core][blenderjob]") {
    JobFixture f("job-private");
    const fs::path priv = f.tmp.path() / "private";
    PrivateFolderScope scope(priv);
    SetEnvironmentVariableW(L"BLENDER_USER_CONFIG", L"C:/users/me/config");
    SetEnvironmentVariableW(L"BLENDER_USER_EXTENSIONS", L"C:/users/me/extensions");
    REQUIRE(inspectBlender(f.exe).status.ok());
    plynth::test::requireOk(runBlenderJob(f.exe, f.probeJob(), f.work, f.options).status);
    SetEnvironmentVariableW(L"BLENDER_USER_CONFIG", nullptr);
    SetEnvironmentVariableW(L"BLENDER_USER_EXTENSIONS", nullptr);

    const std::string root = pathToUtf8(priv);
    const auto lines = fakeEnvLines(f);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].rfind("version|", 0) == 0);
    REQUIRE(lines[1].rfind("job|", 0) == 0);
    for (const std::string& line : lines) {
        REQUIRE(line.find("BLENDER_USER_RESOURCES=" + root + "|") != std::string::npos);
        REQUIRE(line.find("users/me") == std::string::npos);
        REQUIRE(line.find("BLENDER_USER_CONFIG=" + pathToUtf8(priv / "config") + "|") != std::string::npos);
    }
    REQUIRE(fs::is_directory(priv / "extensions"));
}

TEST_CASE("a run without factory startup keeps the user's environment", "[core][blenderjob]") {
    JobFixture f("job-normal-env");
    const fs::path priv = f.tmp.path() / "private";
    PrivateFolderScope scope(priv);
    wchar_t saved[4096] = {};
    const DWORD savedSize = GetEnvironmentVariableW(L"BLENDER_USER_RESOURCES", saved, 4096);
    SetEnvironmentVariableW(L"BLENDER_USER_RESOURCES", L"C:/users/me/resources");
    f.options.factoryStartup = false;
    plynth::test::requireOk(runBlenderJob(f.exe, cleanupJob(f, "obj", "chair-00000009"), f.work, f.options).status);
    SetEnvironmentVariableW(L"BLENDER_USER_RESOURCES", savedSize > 0 ? saved : nullptr);
    const auto lines = fakeEnvLines(f);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].find("BLENDER_USER_RESOURCES=C:/users/me/resources|") != std::string::npos);
    REQUIRE_FALSE(fs::exists(priv));
}

TEST_CASE("jobs validate against the job schema", "[core][blenderjob][schema]") {
    plynth::test::TempDir tmp("job-schema");
    const fs::path result = tmp.path() / "result.json";
    BlenderUserFolders folders;
    folders.config = tmp.path() / "config";
    folders.scripts = tmp.path() / "scripts";
    folders.extensions = tmp.path() / "extensions";
    folders.blenderKitData = tmp.path() / "blenderkit_data";
    REQUIRE(problemWith("blender-job.schema.json", makeProbeJob(folders, "probe-1a2b3c4d", result)).empty());
    REQUIRE_FALSE(problemWith("blender-job.schema.json", makeBlenderJob(kProbeTask, "probe-1a2b3c4d", result, Json::object())).empty());

    CleanupJob cleanup;
    cleanup.sourceFile = tmp.path() / "chair.obj";
    cleanup.format = "obj";
    cleanup.assetId = "chair";
    cleanup.outputFolder = tmp.path() / "item";
    cleanup.scale = 0.01;
    cleanup.rotateZ = 90;
    REQUIRE(problemWith("blender-job.schema.json", makeCleanupJob(cleanup, "chair-1a2b3c4d", result)).empty());
    cleanup.format = "zip";
    cleanup.thumbnail.render = false;
    REQUIRE(problemWith("blender-job.schema.json", makeCleanupJob(cleanup, "chair-1a2b3c4d", result)).empty());
    REQUIRE(makeCleanupJob(cleanup, "chair-1a2b3c4d", result)["params"]["bake_size"] == 1024);
    for (const int size : {256, 512, 2048, 4096}) {
        cleanup.bakeSize = size;
        const Json sized = makeCleanupJob(cleanup, "chair-1a2b3c4d", result);
        REQUIRE(sized["params"]["bake_size"] == size);
        REQUIRE(problemWith("blender-job.schema.json", sized).empty());
    }
    cleanup.bakeSize = 300;
    REQUIRE_FALSE(problemWith("blender-job.schema.json", makeCleanupJob(cleanup, "chair-1a2b3c4d", result)).empty());

    ThumbnailJob thumb;
    thumb.blendFile = tmp.path() / "asset.blend";
    thumb.outputFile = tmp.path() / "thumb.png";
    REQUIRE(problemWith("blender-job.schema.json", makeThumbnailJob(thumb, "thumb-1a2b3c4d", result)).empty());
    thumb.measure = false;
    REQUIRE(problemWith("blender-job.schema.json", makeThumbnailJob(thumb, "thumb-1a2b3c4d", result)).empty());
    thumb.fbxFile = tmp.path() / "asset.fbx";
    const Json withFbx = makeThumbnailJob(thumb, "thumb-1a2b3c4d", result);
    REQUIRE(problemWith("blender-job.schema.json", withFbx).empty());
    REQUIRE(withFbx["params"]["fbx_file"].is_string());
}

TEST_CASE("thumbnail jobs carry no engine and a null fbx_file when none is wanted", "[core][blenderjob][schema]") {
    plynth::test::TempDir tmp("job-noengine");
    const fs::path result = tmp.path() / "result.json";
    CleanupJob cleanup;
    cleanup.sourceFile = tmp.path() / "chair.obj";
    cleanup.format = "obj";
    cleanup.assetId = "chair";
    cleanup.outputFolder = tmp.path() / "item";
    const Json cleanupJson = makeCleanupJob(cleanup, "chair-1a2b3c4d", result);
    REQUIRE_FALSE(cleanupJson["params"]["thumbnail"].contains("engine"));

    ThumbnailJob thumb;
    thumb.blendFile = tmp.path() / "asset.blend";
    thumb.outputFile = tmp.path() / "thumb.png";
    const Json thumbJson = makeThumbnailJob(thumb, "thumb-1a2b3c4d", result);
    REQUIRE_FALSE(thumbJson["params"]["thumbnail"].contains("engine"));
    REQUIRE(thumbJson["params"]["fbx_file"].is_null());
    REQUIRE(problemWith("blender-job.schema.json", thumbJson).empty());
}

TEST_CASE("a job has its fields in a fixed order", "[core][blenderjob]") {
    const Json job = makeBlenderJob(kProbeTask, "probe-1", "D:/x/result.json", Json::object());
    std::vector<std::string> keys;
    for (const auto& [key, value] : job.items()) keys.push_back(key);
    REQUIRE(keys == std::vector<std::string>{"format", "version", "task", "job_id", "result_file", "params"});
    REQUIRE(job["result_file"] == "D:/x/result.json");
}

TEST_CASE("newJobId", "[core][blenderjob]") {
    const std::string a = newJobId("chair");
    const std::string b = newJobId("chair");
    REQUIRE(std::regex_match(a, std::regex("chair-[0-9a-f]{8}")));
    REQUIRE(a != b);
}

TEST_CASE("cleanup and thumbnail results are read", "[core][blenderjob]") {
    JobFixture f("job-assets");
    const std::string id = "chair-00000001";
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "obj", id), f.work, f.options);
    plynth::test::requireOk(run.status);
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
    const AssetJobOutput out = readCleanupResult(run.result, id);
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.blendFile == "asset.blend");
    REQUIRE(out.fbxFile == "asset.fbx");
    REQUIRE(fs::exists(f.work / "item" / "asset.fbx"));
    REQUIRE(out.pickedFile.empty());
    REQUIRE(out.measured);
    REQUIRE(out.size.width == 0.5);
    REQUIRE(out.size.height == 0.9);
    REQUIRE(out.triangles == 12);
    REQUIRE(out.objects == 1);
    REQUIRE(out.materials == std::vector<std::string>{"Fake"});
    REQUIRE(out.textures.size() == 4);
    REQUIRE(out.textures[0] == "textures/maps/fake/base_color.png");
    REQUIRE(out.materialMaps.size() == 1);
    REQUIRE(out.materialMaps[0].name == "Fake");
    REQUIRE(out.materialMaps[0].maps.baseColor.file == "textures/maps/fake/base_color.png");
    REQUIRE(out.materialMaps[0].maps.baseColor.origin == MapOrigin::File);
    REQUIRE(out.materialMaps[0].maps.baseColor.detail == "fake.png");
    REQUIRE(out.materialMaps[0].maps.opacity.origin == MapOrigin::None);
    REQUIRE(out.materialMaps[0].maps.opacity.file.empty());
    REQUIRE(out.materialMaps[0].previewMaps == out.materialMaps[0].maps);
    for (const MapChannel channel : {MapChannel::BaseColor, MapChannel::Normal, MapChannel::Roughness, MapChannel::Metallic}) {
        REQUIRE(fs::exists(f.work / "item" / pathFromUtf8(materialMap(out.materialMaps[0].maps, channel).file)));
    }
    REQUIRE(out.thumbnailRendered);
    REQUIRE(out.thumbnailEngine == "eevee");
    REQUIRE(out.thumbnailError.empty());
    REQUIRE(out.header.blenderVersion == "5.2.0");
    REQUIRE(fs::exists(f.work / "item" / "asset.blend"));
    REQUIRE(fs::file_size(f.work / "item" / "thumb.png") > 50);
    REQUIRE(plynth::test::readTextFile(f.work / "item" / "asset.blend") == "FAKEBLEND");
}

TEST_CASE("a zip source reports the picked file", "[core][blenderjob]") {
    JobFixture f("job-zip");
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "zip", "chair-00000002"), f.work, f.options);
    plynth::test::requireOk(run.status);
    REQUIRE(readCleanupResult(run.result, "chair-00000002").pickedFile == "model.obj");
}

TEST_CASE("no_thumbnail mode imports with a thumbnail error", "[core][blenderjob]") {
    JobFixture f("job-nothumb", "no_thumbnail");
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "obj", "chair-00000003"), f.work, f.options);
    plynth::test::requireOk(run.status);
    const AssetJobOutput out = readCleanupResult(run.result, "chair-00000003");
    plynth::test::requireOk(out.header.status);
    REQUIRE_FALSE(out.thumbnailRendered);
    REQUIRE(out.thumbnailError == "Fake render failure");
    REQUIRE_FALSE(fs::exists(f.work / "item" / "thumb.png"));
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
}

TEST_CASE("no_fbx mode imports with fbx_file null and the fbx_failed warning", "[core][blenderjob]") {
    JobFixture f("job-nofbx", "no_fbx");
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "obj", "chair-00000005"), f.work, f.options);
    plynth::test::requireOk(run.status);
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
    const AssetJobOutput out = readCleanupResult(run.result, "chair-00000005");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.fbxFile.empty());
    REQUIRE(out.thumbnailRendered);
    REQUIRE_FALSE(fs::exists(f.work / "item" / "asset.fbx"));
    REQUIRE(out.header.warnings.size() == 1);
    REQUIRE(out.header.warnings[0].code == "fbx_failed");
}

TEST_CASE("render_thumbnail results", "[core][blenderjob]") {
    JobFixture f("job-render");
    ThumbnailJob job;
    job.blendFile = f.tmp.path() / "asset.blend";
    job.outputFile = f.work / "out" / "thumb.png";
    job.measure = true;
    BlenderJobRun run = runBlenderJob(f.exe, makeThumbnailJob(job, "thumb-00000001", f.work / "result.json"), f.work, f.options);
    plynth::test::requireOk(run.status);
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
    AssetJobOutput out = readThumbnailResult(run.result, "thumb-00000001");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.measured);
    REQUIRE(out.size.depth == 0.5);
    REQUIRE(out.thumbnailRendered);
    REQUIRE(out.thumbnailEngine == "eevee");
    REQUIRE(out.fbxFile.empty());
    REQUIRE(fs::exists(job.outputFile));

    job.fbxFile = f.work / "out" / "asset.fbx";
    run = runBlenderJob(f.exe, makeThumbnailJob(job, "thumb-00000004", f.work / "result.json"), f.work, f.options);
    plynth::test::requireOk(run.status);
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
    out = readThumbnailResult(run.result, "thumb-00000004");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.fbxFile == "asset.fbx");
    REQUIRE(fs::exists(job.fbxFile));
    job.fbxFile.clear();

    job.measure = false;
    run = runBlenderJob(f.exe, makeThumbnailJob(job, "thumb-00000002", f.work / "result.json"), f.work, f.options);
    plynth::test::requireOk(run.status);
    REQUIRE(problemWith("blender-result.schema.json", run.result).empty());
    out = readThumbnailResult(run.result, "thumb-00000002");
    plynth::test::requireOk(out.header.status);
    REQUIRE_FALSE(out.measured);
}

TEST_CASE("a failed thumbnail render fails a render_thumbnail job", "[core][blenderjob]") {
    JobFixture f("job-render-fail", "no_thumbnail");
    ThumbnailJob job;
    job.blendFile = f.tmp.path() / "asset.blend";
    job.outputFile = f.work / "thumb.png";
    const BlenderJobRun run = runBlenderJob(f.exe, makeThumbnailJob(job, "thumb-00000003", f.work / "result.json"), f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.errorCode == "render_failed");
    REQUIRE(readThumbnailResult(run.result, "thumb-00000003").header.status.code == ErrorCode::BlenderFailed);
}

TEST_CASE("readCleanupResult refuses a bad result", "[core][blenderjob]") {
    JobFixture f("job-refuse");
    const std::string id = "chair-00000004";
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "obj", id), f.work, f.options);
    plynth::test::requireOk(run.status);

    Json negative = run.result;
    negative["output"]["size"]["width"] = -1.0;
    REQUIRE(readCleanupResult(negative, id).header.status.code == ErrorCode::BadFile);

    Json missing = run.result;
    missing["output"].erase("triangles");
    const AssetJobOutput gone = readCleanupResult(missing, id);
    REQUIRE(gone.header.status.code == ErrorCode::BadFile);
    REQUIRE(gone.header.status.where == "/output/triangles");

    REQUIRE(readCleanupResult(run.result, "other-id").header.status.code == ErrorCode::BadFile);
    REQUIRE(readThumbnailResult(run.result, id).header.status.code == ErrorCode::BadFile);
    REQUIRE(readCleanupResult(Json::object(), id).header.status.code == ErrorCode::BadFile);
    REQUIRE(readCleanupResult(Json::array(), id).header.status.code == ErrorCode::BadFile);
}

TEST_CASE("readProbeResult reads add-ons and BlenderKit", "[core][blenderjob]") {
    JobFixture f("job-probe");
    plynth::test::ScopedEnv addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.archimesh,bagapie");
    plynth::test::ScopedEnv kit("FAKE_BLENDER_BLENDERKIT", "enabled");
    const std::string id = "probe-00000005";
    const BlenderJobRun run = runBlenderJob(f.exe, f.probeJob(id), f.work, f.options);
    plynth::test::requireOk(run.status);
    const ProbeResult probe = readProbeResult(run.result, id);
    plynth::test::requireOk(probe.header.status);
    REQUIRE(probe.version == "5.2.0");
    REQUIRE(probe.python == "3.11.9");
    REQUIRE(probe.addons.size() == 3);
    REQUIRE(probe.addons[0].module == "bl_ext.blender_org.archimesh");
    REQUIRE(probe.blenderKitInstalled);
    REQUIRE(probe.blenderKitEnabled);
    REQUIRE_FALSE(probe.blenderKitSignedIn);

    Json broken = run.result;
    broken["output"]["blenderkit"].erase("enabled");
    REQUIRE(readProbeResult(broken, id).header.status.code == ErrorCode::BadFile);
}

TEST_CASE("readBlenderResultHeader checks what was asked", "[core][blenderjob]") {
    Json result = Json::object();
    result["format"] = "plynth-blender-result";
    result["version"] = 1;
    result["task"] = "probe";
    result["job_id"] = "probe-1";
    result["ok"] = true;
    result["error"] = nullptr;
    result["blender_version"] = "5.2.0";
    result["warnings"] = Json::array({Json{{"code", "scale_suspicious"}, {"message", "Very small."}}});
    result["elapsed_ms"] = 12;
    result["output"] = Json::object();

    const BlenderResultHeader good = readBlenderResultHeader(result, "probe", "probe-1");
    plynth::test::requireOk(good.status);
    REQUIRE(good.warnings.size() == 1);
    REQUIRE(good.warnings[0].code == "scale_suspicious");
    REQUIRE(good.elapsedMs == 12);

    REQUIRE(readBlenderResultHeader(result, "cleanup_asset", "probe-1").status.where == "/task");
    REQUIRE(readBlenderResultHeader(result, "probe", "probe-2").status.where == "/job_id");

    Json wrongFormat = result;
    wrongFormat["format"] = "other";
    REQUIRE(readBlenderResultHeader(wrongFormat, "probe", "probe-1").status.where == "/format");
    Json wrongVersion = result;
    wrongVersion["version"] = 2;
    REQUIRE(readBlenderResultHeader(wrongVersion, "probe", "probe-1").status.where == "/version");

    Json failed = result;
    failed["ok"] = false;
    failed["error"] = Json{{"code", "no_geometry"}, {"message", "The file holds no model."}};
    failed["output"] = nullptr;
    const BlenderResultHeader bad = readBlenderResultHeader(failed, "probe", "probe-1");
    REQUIRE(bad.status.code == ErrorCode::BlenderFailed);
    REQUIRE(bad.status.message == "The file holds no model.");
    REQUIRE(bad.errorCode == "no_geometry");
}

namespace {

// An install of the fake that counts as usable, without starting it.
BlenderInstall fakeInstall(const fs::path& exe, BlenderVersion version = {5, 2, 0})
{
    BlenderInstall install;
    install.executable = exe;
    install.version = version;
    install.support = blenderSupport(version);
    return install;
}

// True when two paths name the same place, whatever their separators.
bool samePlace(const fs::path& a, const fs::path& b)
{
    return a.lexically_normal().generic_u8string() == b.lexically_normal().generic_u8string();
}

// The lines of a log the fake wrote next to its program. Empty when it never started.
std::vector<std::string> fakeLog(const fs::path& exe, const char* name)
{
    std::vector<std::string> lines;
    std::ifstream in(exe.parent_path() / name, std::ios::binary);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

// Checks that every start of the fake used factory startup and the private folder for all five variables.
void requireAllStartsPrivate(const JobFixture& f)
{
    const auto args = fakeLog(f.exe, "fake_args.log");
    const auto envs = fakeLog(f.exe, "fake_env.log");
    REQUIRE_FALSE(args.empty());
    REQUIRE(args.size() == envs.size());
    for (const std::string& line : args) REQUIRE(line.find("--factory-startup") != std::string::npos);
    const std::string priv = pathToUtf8(f.isolated.tmp.path() / "private");
    const std::string user = pathToUtf8(f.isolated.userRoot);
    for (const std::string& line : envs) {
        REQUIRE(line.find("BLENDER_USER_RESOURCES=" + priv + "|") != std::string::npos);
        REQUIRE(line.find("BLENDER_USER_CONFIG=" + priv + "/config|") != std::string::npos);
        REQUIRE(line.find("BLENDER_USER_SCRIPTS=" + priv + "/scripts|") != std::string::npos);
        REQUIRE(line.find("BLENDER_USER_EXTENSIONS=" + priv + "/extensions|") != std::string::npos);
        REQUIRE(line.find("BLENDER_USER_DATAFILES=" + priv + "/datafiles") != std::string::npos);
        REQUIRE(line.find(user) == std::string::npos);
    }
}

}

TEST_CASE("the fake writes into the user folders when it is started with them", "[core][blenderjob][safety]") {
    JobFixture f("safe-control", "write_user");
    const auto before = f.isolated.fingerprint();
    ProcessSpec spec;
    spec.program = f.exe;
    spec.arguments = {"--background", "--version"};
    spec.timeout = std::chrono::seconds(30);
    plynth::test::requireOk(runProcess(spec).status);
    REQUIRE(f.isolated.fingerprint() != before);
    REQUIRE(fs::exists(f.isolated.userRoot / "config" / "fake_marker.txt"));
    REQUIRE(fs::exists(f.isolated.userRoot / "extensions" / "fake_marker.txt"));
}

TEST_CASE("a probe leaves the user's folders alone however the run ends", "[core][blenderjob][safety]") {
    struct Outcome {
        const char* mode;
        ErrorCode code;
    };
    for (const Outcome& outcome : {Outcome{"write_user", ErrorCode::None}, Outcome{"write_user+fail", ErrorCode::BlenderFailed},
                                   Outcome{"write_user+crash", ErrorCode::BlenderFailed}, Outcome{"write_user+hang", ErrorCode::BlenderFailed},
                                   Outcome{"write_user+hang", ErrorCode::Cancelled}}) {
        DYNAMIC_SECTION(outcome.mode << " " << errorCodeName(outcome.code)) {
            JobFixture f("safe-probe", outcome.mode);
            const BlenderInstall install = inspectBlender(f.exe);
            REQUIRE(install.usable());
            const auto before = f.isolated.fingerprint();

            std::stop_source source;
            std::thread stopper;
            BlenderRunOptions options = f.options;
            options.factoryStartup = false;
            options.timeout = std::chrono::seconds(1);
            if (outcome.code == ErrorCode::Cancelled) {
                options.timeout = std::chrono::seconds(60);
                stopper = std::thread([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                    source.request_stop();
                });
            }
            const BlenderProbeRun run = runBlenderProbe(install, options, source.get_token());
            if (stopper.joinable()) stopper.join();

            REQUIRE(run.status.code == outcome.code);
            if (outcome.code == ErrorCode::None) REQUIRE(run.result.version == "5.2.0");
            REQUIRE(f.isolated.fingerprint() == before);
            REQUIRE(fs::exists(f.isolated.tmp.path() / "private" / "config" / "fake_marker.txt"));
            REQUIRE(fs::is_empty(f.isolated.tempFolder));
            requireAllStartsPrivate(f);
        }
    }
}

TEST_CASE("a probe job run directly is also forced to factory startup", "[core][blenderjob][safety]") {
    JobFixture f("safe-direct", "write_user");
    f.options.factoryStartup = false;
    const auto before = f.isolated.fingerprint();
    plynth::test::requireOk(runBlenderJob(f.exe, f.probeJob(), f.work, f.options).status);
    REQUIRE(f.isolated.fingerprint() == before);
    requireAllStartsPrivate(f);
}

TEST_CASE("a probe job without user folders is refused by the worker", "[core][blenderjob][safety]") {
    JobFixture f("safe-nofolders");
    const Json job = makeBlenderJob(kProbeTask, "probe-00000007", f.work / "result.json", Json::object());
    const BlenderJobRun run = runBlenderJob(f.exe, job, f.work, f.options);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.errorCode == "bad_job");
}

TEST_CASE("a private folder that cannot be made stops the probe before Blender starts", "[core][blenderjob][safety]") {
    JobFixture f("safe-noprivate", "write_user");
    const fs::path file = f.tmp.path() / "not-a-folder";
    std::ofstream(file) << "x";
    plynth::test::ScopedEnv priv("PLYNTH_BLENDER_PRIVATE", pathToUtf8(file / "inside"));
    const auto before = f.isolated.fingerprint();
    const BlenderProbeRun run = runBlenderProbe(fakeInstall(f.exe), f.options);
    REQUIRE(run.status.code == ErrorCode::CannotWrite);
    REQUIRE(fakeLog(f.exe, "fake_args.log").empty());
    REQUIRE(f.isolated.fingerprint() == before);
    REQUIRE(fs::is_empty(f.isolated.tempFolder));
}

TEST_CASE("a probe of an unusable install or with no folders starts nothing", "[core][blenderjob][safety]") {
    JobFixture f("safe-unusable");
    BlenderInstall old = fakeInstall(f.exe, {3, 6, 0});
    REQUIRE(runBlenderProbe(old, f.options).status.code == ErrorCode::BlenderNotFound);
    BlenderInstall broken = fakeInstall(f.exe);
    broken.status = fail(ErrorCode::BlenderFailed, "broken");
    REQUIRE(runBlenderProbe(broken, f.options).status.code == ErrorCode::BlenderNotFound);

    plynth::test::ScopedEnv resources("BLENDER_USER_RESOURCES", "");
    plynth::test::ScopedEnv config("BLENDER_USER_CONFIG", "");
    plynth::test::ScopedEnv scripts("BLENDER_USER_SCRIPTS", "");
    plynth::test::ScopedEnv extensions("BLENDER_USER_EXTENSIONS", "");
    plynth::test::ScopedEnv appData("APPDATA", "");
    REQUIRE(runBlenderProbe(fakeInstall(f.exe), f.options).status.code == ErrorCode::NotFound);
    REQUIRE(fakeLog(f.exe, "fake_args.log").empty());
}

TEST_CASE("the user's folders come from the variables first", "[core][blenderjob][folders]") {
    plynth::test::IsolatedUser user("folders-vars");
    const BlenderInstall install = fakeInstall(user.tmp.path() / "bin" / "blender.exe");
    BlenderUserFolders folders = blenderUserFolders(install);
    REQUIRE(samePlace(folders.config, user.userRoot / "config"));
    REQUIRE(samePlace(folders.scripts, user.userRoot / "scripts"));
    REQUIRE(samePlace(folders.extensions, user.userRoot / "extensions"));
    REQUIRE(samePlace(folders.blenderKitData, user.tmp.path() / "profile" / "blenderkit_data"));

    fs::create_directories(user.tmp.path() / "other");
    {
        plynth::test::ScopedEnv config("BLENDER_USER_CONFIG", pathToUtf8(user.tmp.path() / "other"));
        folders = blenderUserFolders(install);
        REQUIRE(samePlace(folders.config, user.tmp.path() / "other"));
        REQUIRE(samePlace(folders.scripts, user.userRoot / "scripts"));
    }
    {
        plynth::test::ScopedEnv xdg("XDG_DATA_HOME", pathToUtf8(user.tmp.path() / "other"));
        REQUIRE(samePlace(blenderUserFolders(install).blenderKitData, user.tmp.path() / "other" / "blenderkit_data"));
    }
}

TEST_CASE("a variable folder that is missing is skipped like Blender does", "[core][blenderjob][folders]") {
    plynth::test::IsolatedUser user("folders-missing");
    const BlenderInstall install = fakeInstall(user.tmp.path() / "bin" / "blender.exe");
    plynth::test::ScopedEnv config("BLENDER_USER_CONFIG", pathToUtf8(user.tmp.path() / "missing"));
    BlenderUserFolders folders = blenderUserFolders(install);
    REQUIRE(samePlace(folders.config, user.userRoot / "config"));

    plynth::test::ScopedEnv resources("BLENDER_USER_RESOURCES", pathToUtf8(user.tmp.path() / "also-missing"));
    folders = blenderUserFolders(install);
    REQUIRE(samePlace(folders.config, user.tmp.path() / "appdata" / "Blender Foundation" / "Blender" / "5.2" / "config"));
    REQUIRE(samePlace(folders.scripts, user.userRoot / "scripts"));
}

TEST_CASE("portable comes before APPDATA and the version picks the folder", "[core][blenderjob][folders]") {
    plynth::test::IsolatedUser user("folders-portable");
    plynth::test::ScopedEnv resources("BLENDER_USER_RESOURCES", "");
    plynth::test::ScopedEnv config("BLENDER_USER_CONFIG", "");
    plynth::test::ScopedEnv scripts("BLENDER_USER_SCRIPTS", "");
    plynth::test::ScopedEnv extensions("BLENDER_USER_EXTENSIONS", "");
    const fs::path exe = user.tmp.path() / "bin" / "blender.exe";
    const fs::path appBase = user.tmp.path() / "appdata" / "Blender Foundation" / "Blender";

    REQUIRE(samePlace(blenderUserFolders(fakeInstall(exe)).extensions, appBase / "5.2" / "extensions"));
    REQUIRE(samePlace(blenderUserFolders(fakeInstall(exe, {4, 2, 3})).extensions, appBase / "4.2" / "extensions"));

    fs::create_directories(user.tmp.path() / "bin" / "portable");
    const BlenderUserFolders portable = blenderUserFolders(fakeInstall(exe));
    REQUIRE(samePlace(portable.config, user.tmp.path() / "bin" / "portable" / "config"));
    REQUIRE(samePlace(portable.scripts, user.tmp.path() / "bin" / "portable" / "scripts"));

    fs::create_directories(user.tmp.path() / "res");
    plynth::test::ScopedEnv again("BLENDER_USER_RESOURCES", pathToUtf8(user.tmp.path() / "res"));
    REQUIRE(samePlace(blenderUserFolders(fakeInstall(exe)).config, user.tmp.path() / "res" / "config"));
}

TEST_CASE("the job validates and folders that cannot be worked out are empty", "[core][blenderjob][folders]") {
    plynth::test::IsolatedUser user("folders-validate");
    const fs::path exe = user.tmp.path() / "bin" / "blender.exe";
    const BlenderUserFolders folders = blenderUserFolders(fakeInstall(exe));
    const Json job = makeProbeJob(folders, "probe-0000000a", user.tmp.path() / "work" / "result.json");
    REQUIRE(problemWith("blender-job.schema.json", job).empty());
    REQUIRE(job["params"]["user_folders"]["config"].get<std::string>().find('\\') == std::string::npos);

    plynth::test::ScopedEnv resources("BLENDER_USER_RESOURCES", "");
    plynth::test::ScopedEnv config("BLENDER_USER_CONFIG", "");
    plynth::test::ScopedEnv scripts("BLENDER_USER_SCRIPTS", "");
    plynth::test::ScopedEnv extensions("BLENDER_USER_EXTENSIONS", "");
    plynth::test::ScopedEnv appData("APPDATA", "");
    plynth::test::ScopedEnv profile("USERPROFILE", "");
    const BlenderUserFolders none = blenderUserFolders(fakeInstall(exe));
    REQUIRE(none.config.empty());
    REQUIRE(none.scripts.empty());
    REQUIRE(none.extensions.empty());
    REQUIRE(none.blenderKitData.empty());
}

TEST_CASE("readCleanupResult reads constant maps, previews and the material_approximated warning", "[core][blenderjob]") {
    JobFixture f("job-maps");
    const std::string id = "chair-00000005";
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "obj", id), f.work, f.options);
    plynth::test::requireOk(run.status);

    Json result = run.result;
    result["warnings"].push_back(Json{{"code", "material_approximated"}, {"message", "Paint was baked over its UV square."}});
    Json& entry = result["output"]["material_maps"][0];
    entry["name"] = "Paint";
    entry["maps"]["base_color"] = Json{{"file", "textures/maps/paint/base_color.png"}, {"from", "value"}, {"detail", "0.70, 0.38, 0.35"}};
    entry["maps"]["normal"] = Json{{"file", "textures/maps/paint/normal.png"}, {"from", "value"}, {"detail", "flat"}};
    entry["maps"]["roughness"] = Json{{"file", "textures/maps/paint/roughness.png"}, {"from", "value"}, {"detail", "0.3"}};
    entry["maps"]["metallic"] = Json{{"file", "textures/maps/paint/metallic.png"}, {"from", "baked"}, {"detail", "eevee"}};
    entry["preview_maps"] = entry["maps"];
    entry["preview_maps"]["base_color"] = Json{{"file", "textures/preview/paint_base_color.png"}, {"from", "file"}, {"detail", "paint.png"}};
    REQUIRE(problemWith("blender-result.schema.json", result).empty());

    const AssetJobOutput out = readCleanupResult(result, id);
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.header.warnings.size() == 1);
    REQUIRE(out.header.warnings[0].code == "material_approximated");
    const AssetMaterial& paint = out.materialMaps[0];
    REQUIRE(paint.maps.baseColor.origin == MapOrigin::Value);
    REQUIRE(paint.maps.baseColor.detail == "0.70, 0.38, 0.35");
    REQUIRE(paint.maps.normal.detail == "flat");
    REQUIRE(paint.maps.roughness.detail == "0.3");
    REQUIRE(paint.maps.metallic.origin == MapOrigin::Baked);
    REQUIRE(paint.previewMaps.baseColor.file == "textures/preview/paint_base_color.png");
    REQUIRE(paint.previewMaps.roughness == paint.maps.roughness);
}

TEST_CASE("readCleanupResult refuses bad material maps", "[core][blenderjob]") {
    JobFixture f("job-bad-maps");
    const std::string id = "chair-00000006";
    const BlenderJobRun run = runBlenderJob(f.exe, cleanupJob(f, "obj", id), f.work, f.options);
    plynth::test::requireOk(run.status);

    Json missing = run.result;
    missing["output"].erase("material_maps");
    const AssetJobOutput gone = readCleanupResult(missing, id);
    REQUIRE(gone.header.status.code == ErrorCode::BadFile);
    REQUIRE(gone.header.status.where == "/output/material_maps");

    Json outside = run.result;
    outside["output"]["material_maps"][0]["maps"]["normal"]["file"] = "elsewhere/normal.png";
    const AssetJobOutput bad = readCleanupResult(outside, id);
    REQUIRE(bad.header.status.code == ErrorCode::BadFile);
    REQUIRE(bad.header.status.where == "/output/material_maps/0/maps/normal/file");

    Json up = run.result;
    up["output"]["material_maps"][0]["preview_maps"]["metallic"]["file"] = "textures/maps/../../x.png";
    REQUIRE(readCleanupResult(up, id).header.status.code == ErrorCode::BadFile);

    Json noFile = run.result;
    noFile["output"]["material_maps"][0]["maps"]["base_color"] = Json{{"file", nullptr}, {"from", "none"}, {"detail", ""}};
    const AssetJobOutput none = readCleanupResult(noFile, id);
    REQUIRE(none.header.status.code == ErrorCode::BadFile);
    REQUIRE(none.header.status.where == "/output/material_maps/0/maps/base_color/from");

    Json noPreview = run.result;
    noPreview["output"]["material_maps"][0].erase("preview_maps");
    REQUIRE(readCleanupResult(noPreview, id).header.status.code == ErrorCode::BadFile);
}
