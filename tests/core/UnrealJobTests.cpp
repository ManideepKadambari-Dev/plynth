#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EngineLink.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/UnrealLink.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Writes a small file, making its folder.
void writeFile(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// One entry of build/materials.json.
nlohmann::json builtMaterial(const std::string& name, const nlohmann::json& material, const nlohmann::json& surface,
                             const fs::path& images, bool opacity)
{
    const auto image = [&](const char* channel) { return pathToUtf8(images / (name + "_" + channel + ".png")); };
    return {{"name", name},
            {"material", material},
            {"surface", surface},
            {"maps", {{"base_color", image("base")}, {"normal", image("normal")}, {"roughness", image("rough")},
                      {"metallic", image("metal")}, {"opacity", opacity ? nlohmann::json(image("alpha")) : nlohmann::json(nullptr)},
                      {"occlusion", nullptr}}},
            {"alpha_mode", opacity ? "blend" : "opaque"}};
}

// A project folder as the link leaves it after its prepare step: the unreal export in build/links/unreal/out
// with two textures, a kit and the data files, and build/materials.json with an oak floor, a glass pane and a
// furniture material.
struct JobFixture {
    JobFixture() : tmp("unreal-job")
    {
        projectFolder = tmp.path() / "My Flat";
        work = linkFolder(projectFolder, kUnrealLinkId);
        out = work / "out";
        writeFile(out / "My_Flat.fbx", "FAKEFBX");
        writeFile(out / "My_Flat_Kit.fbx", "FAKEKIT");
        writeFile(out / "My_Flat_Kit_layout.json",
                  R"({"pieces": [{"id": "Wall_50x300x20_3f9a07", "mesh": "SM_Kit_Wall_50x300x20_3f9a07"}]})");
        writeFile(out / "My_Flat_gameplay.json", "{}");
        writeFile(out / "My_Flat_licenses.json", "{}");
        for (const char* texture : {"T_OakPlanks_BaseColor.png", "T_OakPlanks_Normal.png", "T_OakPlanks_Roughness.png",
                                    "T_Glass_BaseColor.png", "T_Glass_Opacity.png", "T_ChairOak_Wood_BaseColor.jpg",
                                    "T_Handmade_scribble.png"}) {
            writeFile(out / "Textures" / texture, "PNG");
        }

        const fs::path images = tmp.path() / "images";
        nlohmann::json materials = nlohmann::json::array();
        materials.push_back(builtMaterial("Living_Floor", "oak_planks", nullptr, images, false));
        materials.push_back(builtMaterial("Glass", nullptr, "Glass", images, true));
        nlohmann::json chair = builtMaterial("Wood.001", nullptr, nullptr, images, false);
        chair["asset"] = "chair_oak";
        materials.push_back(chair);
        writeFile(projectFolder / "build" / "materials.json",
                  nlohmann::json{{"format", "plynth-build-materials"}, {"version", 1}, {"materials", materials},
                                 {"objects", nlohmann::json::array()}}
                      .dump(2));

        exported.profile = ExportProfile::Unreal;
        exported.exported = true;
        exported.reportFile = out / "export_report.json";
        exported.texturesFolder = out / "Textures";
        exported.fileOutputs.push_back({"building", "My_Flat.fbx", 12, 144, "My Flat"});
        exported.files.push_back(out / "My_Flat.fbx");
        exported.kit.asked = true;
        exported.kit.written = true;
        exported.kit.file = out / "My_Flat_Kit.fbx";
        exported.kit.layoutFile = out / "My_Flat_Kit_layout.json";
        exported.gameplay.enabled = true;
        exported.gameplay.file = out / "My_Flat_gameplay.json";
        exported.licenses.enabled = true;
        exported.licenses.written = true;
        exported.licenses.jsonFile = out / "My_Flat_licenses.json";

        delivery.projectFolder = projectFolder;
        delivery.project = squareProjectWithWalls();
        delivery.project.settings.name = "My Flat";
        delivery.editor.id = "node-1";
        delivery.editor.target = {"MyGame", "D:/Games/MyGame/", "5.8.0"};
        delivery.exported = &exported;
        delivery.workFolder = work;
        delivery.jobId = "link-3f9a07c1";
    }

    fs::path resultFile() const { return work / "unreal_result.json"; }

    // The job made from the delivery, checked against its schema.
    nlohmann::json job(const UnrealJobChecks& checks = {}) const
    {
        const nlohmann::json made = nlohmann::json::parse(makeUnrealJob(delivery, checks, resultFile()).dump());
        INFO(schemaProblem("unreal-job.schema.json", made));
        REQUIRE(schemaProblem("unreal-job.schema.json", made).empty());
        return made;
    }

    // The material entry of a slot.
    static nlohmann::json materialOf(const nlohmann::json& job, const std::string& slot)
    {
        for (const auto& m : job["materials"]) {
            if (m["slot"] == slot) return m;
        }
        return nullptr;
    }

    // The texture entry of an asset.
    static nlohmann::json textureOf(const nlohmann::json& job, const std::string& asset)
    {
        for (const auto& t : job["textures"]) {
            if (t["asset"] == asset) return t;
        }
        return nullptr;
    }

    TempDir tmp;
    fs::path projectFolder;
    fs::path work;
    fs::path out;
    ExportReport exported;
    EngineDelivery delivery;
};

// A result as the script writes it.
nlohmann::json goodResult()
{
    return nlohmann::json::parse(R"({
        "format": "plynth-unreal-result", "version": 1, "job_id": "link-3f9a07c1", "ok": true, "error": null,
        "engine_version": "5.8.0", "project_name": "MyGame", "content_path": "/Game/Plynth/My_Flat",
        "assets": [{"path": "/Game/Plynth/My_Flat/Meshes/SM_My_Flat", "kind": "static_mesh", "action": "created"}],
        "actors": [{"label": "My_Flat", "asset": "/Game/Plynth/My_Flat/Meshes/SM_My_Flat", "action": "spawned"}],
        "checks": [{"id": "scale", "status": "pass", "message": "Right size."}],
        "screenshot": "D:/Shots/flat.png", "ms": 5400})");
}

}

TEST_CASE("the building name is the export stem of the project name", "[core][link][unreal]")
{
    CHECK(unrealBuildingName("My Flat") == "My_Flat");
    CHECK(unrealBuildingName("My Flat 2") == "My_Flat_2");
    CHECK(unrealBuildingName("") == "Building");
    CHECK(unrealBuildingName("Ünï") == "Building");
}

TEST_CASE("the Unreal job lists the meshes, kit, textures, materials and data files of the export", "[core][link][unreal]")
{
    JobFixture f;
    const nlohmann::json job = f.job();
    CHECK(job["format"] == "plynth-unreal-job");
    CHECK(job["version"] == 1);
    CHECK(job["job_id"] == "link-3f9a07c1");
    CHECK(job["plynth_version"] == PLYNTH_EXPECTED_VERSION);
    CHECK(job["building"] == "My_Flat");
    CHECK(job["content_path"] == "/Game/Plynth/My_Flat");
    CHECK(pathFromUtf8(job["project_folder"].get<std::string>()) == f.projectFolder);
    CHECK(pathFromUtf8(job["source_folder"].get<std::string>()) == f.out);
    CHECK(pathFromUtf8(job["result_file"].get<std::string>()) == f.resultFile());
    CHECK(job["project_folder"].get<std::string>().find('\\') == std::string::npos);
    CHECK(job["pack_materials"] == true);
    CHECK(job["grouped"] == false);

    REQUIRE(job["meshes"].size() == 1);
    CHECK(job["meshes"][0] == nlohmann::json{{"group", "building"}, {"file", "My_Flat.fbx"}, {"asset", "SM_My_Flat"}, {"actor", "My_Flat"}});
    CHECK(job["kit"] == nlohmann::json{{"file", "My_Flat_Kit.fbx"}, {"pieces", nlohmann::json::array({"SM_Kit_Wall_50x300x20_3f9a07"})}});
    CHECK(job["data_files"] == nlohmann::json{"My_Flat_gameplay.json", "My_Flat_licenses.json", "My_Flat_Kit_layout.json"});
    CHECK(job["level"] == nlohmann::json{{"place_actors", true}, {"tag", "Plynth:My_Flat"}});
    CHECK(job["checks"].is_null());

    const nlohmann::json floor = JobFixture::materialOf(job, "Living_Floor");
    REQUIRE(floor.is_object());
    CHECK(floor["asset"] == "MI_Living_Floor");
    CHECK(floor["blend"] == "opaque");
    CHECK(floor["two_sided"] == false);
    CHECK(floor["maps"]["base_color"] == "T_OakPlanks_BaseColor");
    CHECK(floor["maps"]["normal"] == "T_OakPlanks_Normal");
    CHECK(floor["maps"]["roughness"] == "T_OakPlanks_Roughness");
    CHECK(floor["maps"]["metallic"].is_null());
    CHECK(floor["maps"]["opacity"].is_null());
    CHECK(floor["values"]["opacity"] == 1.0);

    const nlohmann::json glass = JobFixture::materialOf(job, "Glass");
    REQUIRE(glass.is_object());
    CHECK(glass["blend"] == "translucent");
    CHECK(glass["two_sided"] == true);
    CHECK(glass["maps"]["opacity"] == "T_Glass_Opacity");
    CHECK(glass["values"]["opacity"].get<double>() < 1.0);

    const nlohmann::json chair = JobFixture::materialOf(job, "ChairOak_Wood");
    REQUIRE(chair.is_object());
    CHECK(chair["asset"] == "MI_ChairOak_Wood");
    CHECK(chair["maps"]["base_color"] == "T_ChairOak_Wood_BaseColor");

    CHECK(job["textures"].size() == 6);
    const nlohmann::json base = JobFixture::textureOf(job, "T_OakPlanks_BaseColor");
    CHECK(base == nlohmann::json{{"file", "Textures/T_OakPlanks_BaseColor.png"}, {"asset", "T_OakPlanks_BaseColor"},
                                 {"channel", "base_color"}, {"srgb", true}});
    const nlohmann::json normal = JobFixture::textureOf(job, "T_OakPlanks_Normal");
    CHECK(normal["channel"] == "normal");
    CHECK(normal["srgb"] == false);
    CHECK(JobFixture::textureOf(job, "T_Glass_Opacity")["channel"] == "opacity");
    CHECK(JobFixture::textureOf(job, "T_ChairOak_Wood_BaseColor")["file"] == "Textures/T_ChairOak_Wood_BaseColor.jpg");
    // A texture whose channel cannot be told is left out rather than guessed.
    CHECK(JobFixture::textureOf(job, "T_Handmade_scribble").is_null());
}

TEST_CASE("an Unreal job without materials, grouped, holds meshes only", "[core][link][unreal]")
{
    JobFixture f;
    f.delivery.options.packMaterials = false;
    f.delivery.grouped = true;
    f.exported.fileOutputs.clear();
    f.exported.fileOutputs.push_back({"structure", "My_Flat_Walls.fbx", 4, 40, "Walls"});
    f.exported.fileOutputs.push_back({"furniture_r3", "My_Flat_Furniture_Kitchen.fbx", 4, 40, "Furniture Kitchen"});
    f.exported.kit = {};
    f.exported.gameplay = {};
    f.exported.licenses = {};
    const nlohmann::json job = f.job();
    CHECK(job["pack_materials"] == false);
    CHECK(job["grouped"] == true);
    CHECK(job["textures"].empty());
    CHECK(job["materials"].empty());
    CHECK(job["kit"].is_null());
    CHECK(job["data_files"].empty());
    REQUIRE(job["meshes"].size() == 2);
    CHECK(job["meshes"][0] == nlohmann::json{{"group", "structure"}, {"file", "My_Flat_Walls.fbx"}, {"asset", "SM_My_Flat_Walls"}, {"actor", "My_Flat_Walls"}});
    CHECK(job["meshes"][1]["group"] == "furniture_r3");
    CHECK(job["meshes"][1]["asset"] == "SM_My_Flat_Furniture_Kitchen");
}

TEST_CASE("an Unreal job for the self check carries the walls' bounds and the picture", "[core][link][unreal]")
{
    JobFixture f;
    UnrealJobChecks checks;
    checks.enabled = true;
    checks.expectedBounds = expectedWallBounds(f.delivery.project);
    checks.screenshot = f.tmp.path() / "shots" / "flat.png";
    REQUIRE(checks.expectedBounds);
    const nlohmann::json job = f.job(checks);
    REQUIRE(job["checks"].is_object());
    CHECK(job["checks"]["expected_bounds"]["min"] ==
          nlohmann::json{checks.expectedBounds->min.x, checks.expectedBounds->min.y, checks.expectedBounds->min.z});
    CHECK(job["checks"]["expected_bounds"]["max"] ==
          nlohmann::json{checks.expectedBounds->max.x, checks.expectedBounds->max.y, checks.expectedBounds->max.z});
    CHECK(pathFromUtf8(job["checks"]["screenshot"].get<std::string>()) == checks.screenshot);

    checks.expectedBounds.reset();
    checks.screenshot.clear();
    const nlohmann::json bare = f.job(checks);
    CHECK(bare["checks"] == nlohmann::json{{"expected_bounds", nullptr}, {"screenshot", nullptr}});
}

TEST_CASE("the Unreal result is read and checked against its job", "[core][link][unreal]")
{
    const nlohmann::json good = goodResult();
    REQUIRE(schemaProblem("unreal-result.schema.json", good).empty());
    const UnrealResult read = readUnrealResult(Json::parse(good.dump()), "link-3f9a07c1");
    requireOk(read.status);
    CHECK(read.ok);
    CHECK(read.errorCode.empty());
    CHECK(read.engineVersion == "5.8.0");
    CHECK(read.projectName == "MyGame");
    CHECK(read.contentPath == "/Game/Plynth/My_Flat");
    CHECK(read.assets == std::vector<EngineAssetResult>{{"/Game/Plynth/My_Flat/Meshes/SM_My_Flat", "static_mesh", "created"}});
    CHECK(read.actors == std::vector<EngineActorResult>{{"My_Flat", "/Game/Plynth/My_Flat/Meshes/SM_My_Flat", "spawned"}});
    CHECK(read.checks == std::vector<UnrealCheck>{{"scale", "pass", "Right size."}});
    CHECK(read.screenshot == fs::path("D:/Shots/flat.png"));
    CHECK(read.ms == 5400);

    nlohmann::json failed = good;
    failed["ok"] = false;
    failed["error"] = {{"code", "folder_taken"}, {"message", "Content/Plynth/My_Flat belongs to the Plynth project D:/Other. Rename this project."}};
    failed["assets"] = nlohmann::json::array();
    REQUIRE(schemaProblem("unreal-result.schema.json", failed).empty());
    const UnrealResult refused = readUnrealResult(Json::parse(failed.dump()), "link-3f9a07c1");
    requireOk(refused.status);
    CHECK_FALSE(refused.ok);
    CHECK(refused.errorCode == "folder_taken");
    CHECK(refused.errorMessage.find("belongs to") != std::string::npos);

    CHECK(readUnrealResult(Json::parse(good.dump()), "link-00000000").status.code == ErrorCode::BadFile);
    CHECK(readUnrealResult(Json::array(), "link-3f9a07c1").status.code == ErrorCode::BadFile);
    for (const char* key : {"format", "version", "ok", "error", "assets", "actors", "checks", "screenshot", "ms"}) {
        nlohmann::json broken = good;
        broken.erase(key);
        INFO(key);
        CHECK(readUnrealResult(Json::parse(broken.dump()), "link-3f9a07c1").status.code == ErrorCode::BadFile);
    }
    nlohmann::json noReason = good;
    noReason["ok"] = false;
    CHECK(readUnrealResult(Json::parse(noReason.dump()), "link-3f9a07c1").status.code == ErrorCode::BadFile);
    nlohmann::json badStatus = good;
    badStatus["checks"][0]["status"] = "maybe";
    CHECK(readUnrealResult(Json::parse(badStatus.dump()), "link-3f9a07c1").status.code == ErrorCode::BadFile);
}

TEST_CASE("the Unreal scripts folder and editor programs are found", "[core][link][unreal]")
{
    TempDir tmp("unreal-find");
    const fs::path scripts = tmp.path() / "scripts";
    writeFile(scripts / "plynth_link.py", "# stand in\n");
    {
        ScopedEnv variable("PLYNTH_UNREAL_SCRIPTS", pathToUtf8(scripts));
        CHECK(unrealScriptsFolder() == scripts);
    }
    {
        // The repository's own scripts are a valid folder too.
        const fs::path source = fs::path(PLYNTH_SOURCE_DIR) / "engines" / "unreal";
        REQUIRE(fs::is_regular_file(source / "plynth_link.py"));
        ScopedEnv variable("PLYNTH_UNREAL_SCRIPTS", pathToUtf8(source));
        CHECK(unrealScriptsFolder() == source);
    }

    const fs::path root = tmp.path() / "UE_5.8";
    CHECK(unrealInstallAt(root).editorCmd.empty());
    CHECK(unrealInstallAt(root).editor.empty());
    writeFile(root / "Engine" / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe", "MZ");
    writeFile(root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe", "MZ");
    const UnrealInstall install = unrealInstallAt(root);
    CHECK(install.root == root);
    CHECK(install.editorCmd == root / "Engine" / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe");
    CHECK(install.editor == root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe");
}
