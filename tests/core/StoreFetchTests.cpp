#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/StoreFetch.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Sets FAKE_BLENDERKIT for the scope of a test.
struct KitMode {
    explicit KitMode(const char* value) { _putenv_s("FAKE_BLENDERKIT", value); }
    ~KitMode() { _putenv_s("FAKE_BLENDERKIT", ""); }
};

// A fake Blender, its scripts folder and a work folder.
struct Setup {
    plynth::test::TempDir tmp{"store-fetch"};
    fs::path blender = plynth::test::makeFakeBlender(tmp.path() / "bin", "5.2.0");
    BlenderRunOptions base;
    fs::path work = tmp.path() / "work";

    Setup() { base.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "scripts"); }
};

constexpr const char* kId = "0c1d4a2e-1111-2222-3333-444455556666";

// A fetch of the fake item.
ImportFetch fetchOf(std::string_view kind = kFetchKindModel)
{
    return makeBlenderKitFetch(kId, kind, BlenderKitOptions{});
}

// A result the way the worker writes it.
Json result(const Json& output)
{
    return Json{{"format", "plynth-blender-result"}, {"version", 1}, {"task", "blenderkit_fetch"}, {"job_id", "j1"},
                {"ok", true}, {"error", nullptr}, {"blender_version", "5.2.0"}, {"warnings", Json::array()},
                {"elapsed_ms", 5}, {"output", output}};
}

Json output()
{
    return Json{{"file", "a/b.blend"}, {"format", "blend"}, {"kind", "model"}, {"name", "Chair"}, {"author", nullptr},
                {"license", "cc0"}, {"reference", "ref"}, {"material", nullptr}};
}

}

TEST_CASE("runImportFetch brings the file through the fake", "[core][fetch]") {
    KitMode mode("");
    Setup s;
    std::vector<std::string> steps;
    const FetchRun run = runImportFetch(fetchOf(), s.blender, s.base, s.work,
                                        [&](const BlenderProgress& p) { steps.push_back(p.step); });
    plynth::test::requireOk(run.status);
    REQUIRE(run.file == s.work / "fetched" / "fake_item.blend");
    REQUIRE(run.file.is_absolute());
    REQUIRE(fs::is_regular_file(s.work / "fetched" / "textures" / "fake.png"));
    REQUIRE(run.output.kind == "model");
    REQUIRE(run.output.name == "Fake chair");
    REQUIRE(run.output.license == "royalty_free");
    REQUIRE(run.output.reference == std::string("https://www.blendkit.com/asset-gallery-detail/") + kId + "/");
    REQUIRE(steps == std::vector<std::string>{"check_addon", "download", "copy"});

    const Json written = Json::parse(plynth::test::readTextFile(s.blender.parent_path() / "fake_result.json"));
    REQUIRE(plynth::test::schemaProblem("blender-result.schema.json", written) == "");
    std::string lastJob;
    std::ifstream jobs(s.blender.parent_path() / "fake_jobs.log", std::ios::binary);
    for (std::string line; std::getline(jobs, line);) lastJob = line;
    const Json job = Json::parse(lastJob);
    REQUIRE(plynth::test::schemaProblem("blender-job.schema.json", job) == "");
}

TEST_CASE("the fetch starts without factory startup and with --background", "[core][fetch]") {
    KitMode mode("");
    Setup s;
    plynth::test::requireOk(runImportFetch(fetchOf(), s.blender, s.base, s.work).status);
    const std::string args = plynth::test::readTextFile(s.blender.parent_path() / "fake_args.txt");
    REQUIRE(args.find("--factory-startup") == std::string::npos);
    REQUIRE(args.find("--background") != std::string::npos);
    REQUIRE(args.find("--window-geometry") == std::string::npos);
}

TEST_CASE("a failed fetch gives BlenderFailed with the worker's code", "[core][fetch]") {
    Setup s;
    for (const char* code : {"not_signed_in", "addon_missing", "download_failed", "download_timeout", "wrong_asset_kind"}) {
        const std::string name = std::string(code) == "download_timeout" ? "timeout" : std::string(code) == "wrong_asset_kind" ? "hdr" : code;
        KitMode mode(name.c_str());
        const FetchRun run = runImportFetch(fetchOf(), s.blender, s.base, s.work / code);
        REQUIRE(run.status.code == ErrorCode::BlenderFailed);
        REQUIRE(run.errorCode == code);
        REQUIRE_FALSE(run.status.message.empty());
        const Json written = Json::parse(plynth::test::readTextFile(s.blender.parent_path() / "fake_result.json"));
        REQUIRE(plynth::test::schemaProblem("blender-result.schema.json", written) == "");
    }
    KitMode mode("not_signed_in");
    const FetchRun run = runImportFetch(fetchOf(), s.blender, s.base, s.work / "again");
    REQUIRE(run.status.message.find("Sign in to BlenderKit inside Blender first") == 0);
}

TEST_CASE("a reported file that is missing is BlenderFailed", "[core][fetch]") {
    KitMode mode("missing_file");
    Setup s;
    const FetchRun run = runImportFetch(fetchOf(), s.blender, s.base, s.work);
    REQUIRE(run.status.code == ErrorCode::BlenderFailed);
    REQUIRE(run.status.message.find("fake_item.blend") != std::string::npos);
}

TEST_CASE("readFetchResult checks the output", "[core][fetch]") {
    const FetchOutput good = readFetchResult(result(output()), "blenderkit_fetch", "j1");
    plynth::test::requireOk(good.header.status);
    REQUIRE(good.file == "a/b.blend");
    REQUIRE(good.name == "Chair");
    REQUIRE(good.author.empty());
    REQUIRE(good.license == "cc0");

    for (const char* file : {"/abs.blend", "../x.blend", "a/../../x.blend", "C:/x.blend", "C:x.blend", "a\\b.blend", ""}) {
        Json out = output();
        out["file"] = file;
        REQUIRE(readFetchResult(result(out), "blenderkit_fetch", "j1").header.status.code == ErrorCode::BadFile);
    }
    Json kind = output();
    kind["kind"] = "scene";
    REQUIRE(readFetchResult(result(kind), "blenderkit_fetch", "j1").header.status.code == ErrorCode::BadFile);
    Json format = output();
    format.erase("format");
    REQUIRE(readFetchResult(result(format), "blenderkit_fetch", "j1").header.status.code == ErrorCode::BadFile);
    REQUIRE(readFetchResult(result(output()), "blenderkit_fetch", "other").header.status.code == ErrorCode::BadFile);
}

TEST_CASE("applyFetch fills an asset plan only where it is empty", "[core][fetch]") {
    FetchRun run;
    run.file = "D:/w/fetched/a/b.blend";
    run.output = readFetchResult(result(output()), "blenderkit_fetch", "j1");

    ImportPlan plan;
    plan.reference = "the address";
    plynth::test::requireOk(applyFetch(run, fetchOf(), plan));
    REQUIRE(plan.file == run.file);
    REQUIRE(plan.format == "blend");
    REQUIRE(plan.defaultName == "Chair");
    REQUIRE(plan.license == "cc0");
    REQUIRE(plan.reference == "the address");

    ImportPlan named;
    named.defaultName = "Mine";
    named.license = "royalty_free";
    plynth::test::requireOk(applyFetch(run, fetchOf(), named));
    REQUIRE(named.defaultName == "Mine");
    REQUIRE(named.license == "royalty_free");
    REQUIRE(named.reference == "ref");
}

TEST_CASE("applyFetch refuses the other kind with a sentence", "[core][fetch]") {
    FetchRun material;
    Json out = output();
    out["kind"] = "material";
    out["material"] = "Oak";
    material.file = "D:/w/fetched/a/b.blend";
    material.output = readFetchResult(result(out), "blenderkit_fetch", "j1");

    ImportPlan asset;
    const Status refused = applyFetch(material, fetchOf(), asset);
    REQUIRE(refused.code == ErrorCode::InvalidValue);
    REQUIRE(refused.message == "This BlenderKit item is a material. Import it in the Materials tab or with plynth materials import.");
    REQUIRE(asset.file.empty());

    MaterialImportPlan plan;
    plynth::test::requireOk(applyFetch(material, fetchOf(kFetchKindMaterial), plan));
    REQUIRE(plan.path == material.file);
    REQUIRE(plan.workerSource == "blend");
    REQUIRE(plan.blendMaterial == "Oak");
    REQUIRE(plan.defaultName == "Chair");
    REQUIRE(plan.license == "cc0");
    REQUIRE(plan.reference == "ref");

    FetchRun model;
    model.file = "D:/w/fetched/a/b.blend";
    model.output = readFetchResult(result(output()), "blenderkit_fetch", "j1");
    MaterialImportPlan other;
    const Status wrong = applyFetch(model, fetchOf(kFetchKindMaterial), other);
    REQUIRE(wrong.code == ErrorCode::InvalidValue);
    REQUIRE(wrong.message == "This BlenderKit item is a model. Import it in the Assets tab or with plynth assets import.");
    REQUIRE(other.path.empty());
}

TEST_CASE("the fake material fetch fills a material plan", "[core][fetch]") {
    KitMode mode("material");
    Setup s;
    const ImportFetch fetch = fetchOf(kFetchKindMaterial);
    const FetchRun run = runImportFetch(fetch, s.blender, s.base, s.work);
    plynth::test::requireOk(run.status);
    MaterialImportPlan plan;
    plynth::test::requireOk(applyFetch(run, fetch, plan));
    REQUIRE(plan.blendMaterial == "Fake material");
    ImportPlan asset;
    REQUIRE(applyFetch(run, fetchOf(), asset).code == ErrorCode::InvalidValue);
}
