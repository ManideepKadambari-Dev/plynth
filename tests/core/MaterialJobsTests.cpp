#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/catalog/MaterialJobs.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Checks a job against the job schema.
std::string jobProblem(const Json& json)
{
    return plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(json.dump()));
}

// Checks a result against the result schema.
std::string resultProblem(const Json& json)
{
    return plynth::test::schemaProblem("blender-result.schema.json", nlohmann::json::parse(json.dump()));
}

// One map entry of a result.
Json mapEntry(const std::string& channel, const std::string& from = "file", const std::string& detail = "x.png")
{
    return Json{{"file", from == "none" ? Json(nullptr) : Json("textures/" + channel + ".png")}, {"from", from}, {"detail", detail}};
}

// A good result for a job.
Json goodResult(const std::string& jobId)
{
    Json maps = Json::object();
    for (const char* channel : {"base_color", "normal", "roughness", "metallic"}) maps[channel] = mapEntry(channel);
    maps["opacity"] = mapEntry("opacity", "none", "");
    maps["occlusion"] = mapEntry("occlusion", "baked", "");
    Json output = Json::object();
    output["maps"] = maps;
    output["texture_size"] = Json{{"width", 1024}, {"height", 512}};
    output["tile_size"] = Json{{"width", 1.2}, {"height", 0.6}};
    output["picked_material"] = "Wood";
    output["source_info"] = Json{{"id", "oak_planks"}, {"name", "Oak planks"}, {"groups", Json::array({"floor"})}, {"tags", Json::array({"wood"})}, {"license", "own_work"}};
    output["thumbnail"] = Json{{"file", "thumb.png"}, {"engine", "eevee"}, {"error", nullptr}};
    Json result = Json::object();
    result["format"] = "plynth-blender-result";
    result["version"] = 1;
    result["task"] = "cleanup_material";
    result["job_id"] = jobId;
    result["ok"] = true;
    result["error"] = nullptr;
    result["blender_version"] = "5.2.0";
    result["warnings"] = Json::array({Json{{"code", "visible_seams"}, {"message", "Seams."}}});
    result["elapsed_ms"] = 40;
    result["output"] = output;
    return result;
}

}

TEST_CASE("a texture set job validates, with and without tile size, maps and values", "[core][materialjobs][schema]") {
    const fs::path result = fs::path("D:/work/result.json");
    MaterialCleanupJob job;
    job.workerSource = "texture_set";
    job.path = "D:/in/oak";
    job.outputFolder = "D:/work/item";
    const Json plain = makeMaterialCleanupJob(job, "oak-1a2b3c4d", result);
    REQUIRE(jobProblem(plain).empty());
    REQUIRE(plain["task"] == "cleanup_material");
    REQUIRE(plain["params"]["source"]["kind"] == "texture_set");
    REQUIRE(plain["params"]["source"]["material"].is_null());
    REQUIRE(plain["params"]["source"]["normal"] == "auto");
    REQUIRE(plain["params"]["tile_size"].is_null());
    REQUIRE(plain["params"]["bake_size"] == 1024);
    REQUIRE(plain["params"]["source"]["maps"].empty());

    job.tileSize = TileSize{1.2, 0.6};
    job.maps = {{MapChannel::BaseColor, "D:/in/oak/oak_col.jpg"}, {MapChannel::Normal, "D:/in/oak/n.png"}};
    job.values = {{MapChannel::Metallic, 0.0}, {MapChannel::Opacity, 0.25}};
    job.normal = NormalConvention::DirectX;
    job.bakeSize = 2048;
    job.thumbnail.render = false;
    job.blendMaterial = "ignored";
    const Json full = makeMaterialCleanupJob(job, "oak-1a2b3c4d", result);
    REQUIRE(jobProblem(full).empty());
    REQUIRE(full["params"]["tile_size"]["width"] == 1.2);
    REQUIRE(full["params"]["source"]["maps"]["base_color"] == "D:/in/oak/oak_col.jpg");
    REQUIRE(full["params"]["source"]["values"]["opacity"] == 0.25);
    REQUIRE(full["params"]["source"]["normal"] == "directx");
    REQUIRE(full["params"]["source"]["material"].is_null());
    REQUIRE(full["params"]["thumbnail"]["render"] == false);
}

TEST_CASE("a blend job carries the material name or null", "[core][materialjobs][schema]") {
    const fs::path result = fs::path("D:/work/result.json");
    MaterialCleanupJob job;
    job.workerSource = "blend";
    job.path = "D:/in/wood.blend";
    job.outputFolder = "D:/work/item";
    const Json any = makeMaterialCleanupJob(job, "wood-1a2b3c4d", result);
    REQUIRE(jobProblem(any).empty());
    REQUIRE(any["params"]["source"]["material"].is_null());
    job.blendMaterial = "Wood";
    const Json named = makeMaterialCleanupJob(job, "wood-1a2b3c4d", result);
    REQUIRE(jobProblem(named).empty());
    REQUIRE(named["params"]["source"]["material"] == "Wood");
}

TEST_CASE("normal convention names round trip", "[core][materialjobs]") {
    for (const NormalConvention c : {NormalConvention::Auto, NormalConvention::OpenGl, NormalConvention::DirectX}) {
        REQUIRE(normalConventionFromName(normalConventionName(c)) == c);
    }
    REQUIRE_FALSE(normalConventionFromName("flip").has_value());
    REQUIRE(normalConventionName(NormalConvention::DirectX) == "directx");
}

TEST_CASE("a good cleanup_material result reads in full", "[core][materialjobs]") {
    const Json json = goodResult("oak-1a2b3c4d");
    REQUIRE(resultProblem(json).empty());
    const MaterialJobOutput out = readMaterialCleanupResult(json, "oak-1a2b3c4d");
    plynth::test::requireOk(out.header.status);
    REQUIRE(out.header.blenderVersion == "5.2.0");
    REQUIRE(out.header.warnings.size() == 1);
    REQUIRE(out.maps.baseColor.file == "textures/base_color.png");
    REQUIRE(out.maps.baseColor.origin == MapOrigin::File);
    REQUIRE(out.maps.opacity.origin == MapOrigin::None);
    REQUIRE(out.maps.opacity.file.empty());
    REQUIRE(out.maps.occlusion.origin == MapOrigin::Baked);
    REQUIRE(out.textureSize == TextureSize{1024, 512});
    REQUIRE(out.tileSize == TileSize{1.2, 0.6});
    REQUIRE(out.pickedMaterial == "Wood");
    REQUIRE(out.sourceInfo.has_value());
    REQUIRE(out.sourceInfo->id == "oak_planks");
    REQUIRE(out.sourceInfo->groups == std::vector<std::string>{"floor"});
    REQUIRE(out.sourceInfo->tags == std::vector<std::string>{"wood"});
    REQUIRE(out.sourceInfo->license == "own_work");
    REQUIRE(out.thumbnailRendered);
    REQUIRE(out.thumbnailEngine == "eevee");
}

TEST_CASE("a result with no material.json and no thumbnail reads", "[core][materialjobs]") {
    Json json = goodResult("a-1");
    json["output"]["source_info"] = nullptr;
    json["output"]["picked_material"] = nullptr;
    json["output"]["thumbnail"] = Json{{"file", nullptr}, {"engine", nullptr}, {"error", "Render failed."}};
    REQUIRE(resultProblem(json).empty());
    const MaterialJobOutput out = readMaterialCleanupResult(json, "a-1");
    plynth::test::requireOk(out.header.status);
    REQUIRE_FALSE(out.sourceInfo.has_value());
    REQUIRE(out.pickedMaterial.empty());
    REQUIRE_FALSE(out.thumbnailRendered);
    REQUIRE(out.thumbnailError == "Render failed.");
}

TEST_CASE("bad cleanup_material results are BadFile", "[core][materialjobs]") {
    const auto status = [](const Json& json) { return readMaterialCleanupResult(json, "a-1").header.status; };

    Json outside = goodResult("a-1");
    outside["output"]["maps"]["normal"]["file"] = "../normal.png";
    REQUIRE(status(outside).code == ErrorCode::BadFile);
    outside["output"]["maps"]["normal"]["file"] = "elsewhere/normal.png";
    REQUIRE(status(outside).code == ErrorCode::BadFile);
    outside["output"]["maps"]["normal"]["file"] = "textures/sub/normal.png";
    REQUIRE(status(outside).code == ErrorCode::BadFile);

    Json none = goodResult("a-1");
    none["output"]["maps"]["roughness"] = mapEntry("roughness", "none", "");
    const Status noneStatus = status(none);
    REQUIRE(noneStatus.code == ErrorCode::BadFile);
    REQUIRE(noneStatus.where == "/output/maps/roughness/from");

    Json nullFile = goodResult("a-1");
    nullFile["output"]["maps"]["base_color"]["file"] = nullptr;
    REQUIRE(status(nullFile).code == ErrorCode::BadFile);

    Json optionalWithFile = goodResult("a-1");
    optionalWithFile["output"]["maps"]["opacity"]["file"] = "textures/opacity.png";
    REQUIRE(status(optionalWithFile).code == ErrorCode::BadFile);

    Json badOrigin = goodResult("a-1");
    badOrigin["output"]["maps"]["metallic"]["from"] = "magic";
    REQUIRE(status(badOrigin).code == ErrorCode::BadFile);

    Json tile = goodResult("a-1");
    tile["output"]["tile_size"]["width"] = 0.0;
    REQUIRE(status(tile).code == ErrorCode::BadFile);
    tile["output"]["tile_size"]["width"] = 500.0;
    REQUIRE(status(tile).code == ErrorCode::BadFile);

    Json size = goodResult("a-1");
    size["output"]["texture_size"]["width"] = 0;
    REQUIRE(status(size).code == ErrorCode::BadFile);

    Json missing = goodResult("a-1");
    missing["output"].erase("thumbnail");
    REQUIRE(status(missing).code == ErrorCode::BadFile);

    Json info = goodResult("a-1");
    info["output"]["source_info"]["groups"] = "floor";
    REQUIRE(status(info).code == ErrorCode::BadFile);

    REQUIRE(readMaterialCleanupResult(goodResult("other"), "a-1").header.status.code == ErrorCode::BadFile);
}

TEST_CASE("a failed cleanup_material result reports the worker's message", "[core][materialjobs]") {
    Json json = goodResult("a-1");
    json["ok"] = false;
    json["error"] = Json{{"code", "several_materials"}, {"message", "The file has several materials: Brick, Wood."}};
    json["output"] = nullptr;
    const MaterialJobOutput out = readMaterialCleanupResult(json, "a-1");
    REQUIRE(out.header.status.code == ErrorCode::BlenderFailed);
    REQUIRE(out.header.errorCode == "several_materials");
    REQUIRE(out.header.status.message.find("Brick, Wood") != std::string::npos);
}
