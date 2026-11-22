#include "BuildFixtures.h"
#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Gives an asset on disk one material, Fabric, with full size maps and a smaller base color preview, and an
// occlusion map but no opacity.
void giveFabricMaps(TestLibrary& lib, const std::string& id)
{
    const fs::path file = lib.paths.assetItems / pathFromUtf8(id) / "info.json";
    AssetInfoRead read = readAssetInfo(file, id);
    requireOk(read.status);
    AssetInfo info = read.info;
    AssetMaterial material;
    material.name = "Fabric";
    for (const MapChannel channel : {MapChannel::BaseColor, MapChannel::Normal, MapChannel::Roughness, MapChannel::Metallic, MapChannel::Occlusion}) {
        const std::string name(mapChannelName(channel));
        const MaterialMap map{"textures/maps/fabric/" + name + ".png", MapOrigin::File, "fabric.png"};
        materialMap(material.maps, channel) = map;
        materialMap(material.previewMaps, channel) = map;
    }
    material.previewMaps.baseColor.file = "textures/preview/fabric_base_color.png";
    info.materials = {"Fabric"};
    info.materialMaps = {material};
    requireOk(writeAssetInfo(file, info));
    requireOk(lib.catalog->indexAsset(info));
}

}

TEST_CASE("buildAssetSourceFor gives absolute full size map paths", "[core][build][assetmaps]") {
    TestLibrary lib("build-asset-maps");
    lib.add("sofa", "Sofa");
    lib.add("lamp", "Lamp");
    giveFabricMaps(lib, "sofa");
    const BuildAssetSource source = buildAssetSourceFor(*lib.catalog);

    const auto sofa = source("sofa");
    REQUIRE(sofa);
    REQUIRE(sofa->materials.size() == 1);
    const BuildAssetMaterial& fabric = sofa->materials[0];
    const fs::path folder = lib.paths.assetItems / "sofa";
    REQUIRE(fabric.name == "Fabric");
    REQUIRE(fabric.baseColor.is_absolute());
    REQUIRE(fabric.baseColor == folder / "textures/maps/fabric/base_color.png");
    REQUIRE(fabric.normal == folder / "textures/maps/fabric/normal.png");
    REQUIRE(fabric.roughness == folder / "textures/maps/fabric/roughness.png");
    REQUIRE(fabric.metallic == folder / "textures/maps/fabric/metallic.png");
    REQUIRE(fabric.occlusion == folder / "textures/maps/fabric/occlusion.png");
    REQUIRE(fabric.opacity.empty());

    const auto lamp = source("lamp");
    REQUIRE(lamp);
    REQUIRE(lamp->materials.empty());
}

TEST_CASE("runBuild puts the maps of each asset material in the job", "[core][build][assetmaps]") {
    TestLibrary lib("build-asset-maps-run");
    const fs::path projectFolder = lib.tmp.path() / "Project";
    fs::create_directories(projectFolder);
    const fs::path exe = makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", "");
    lib.add("sofa", "Sofa");
    giveFabricMaps(lib, "sofa");

    BuildRequest request;
    request.projectFolder = projectFolder;
    request.project = buildProject();
    clearMaterialDefaults(request.project);
    request.library = lib.paths;
    request.blender = inspectBlender(exe);
    request.run.scriptsFolder = makeFakeScripts(lib.tmp.path() / "scripts");
    runBuild(request);

    const nlohmann::json job = nlohmann::json::parse(readTextFile(projectFolder / "build" / "job.json"));
    REQUIRE(schemaProblem("blender-job.schema.json", job).empty());
    const nlohmann::json* sofa = nullptr;
    for (const auto& asset : job["params"]["assets"]) {
        if (asset["id"] == "sofa") sofa = &asset;
    }
    REQUIRE(sofa);
    REQUIRE((*sofa)["materials"].size() == 1);
    const auto& material = (*sofa)["materials"][0];
    REQUIRE(material["name"] == "Fabric");
    const std::string folder = pathToUtf8(lib.paths.assetItems / "sofa");
    REQUIRE(material["maps"]["base_color"] == folder + "/textures/maps/fabric/base_color.png");
    REQUIRE(material["maps"]["occlusion"] == folder + "/textures/maps/fabric/occlusion.png");
    REQUIRE(material["maps"]["opacity"].is_null());
    for (const auto& [channel, path] : material["maps"].items()) {
        if (path.is_null()) continue;
        REQUIRE(path.get<std::string>().find("textures/preview") == std::string::npos);
    }
}
