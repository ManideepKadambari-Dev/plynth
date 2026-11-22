#include "CatalogSupport.h"
#include "SchemaSupport.h"

#include <plynth/core/catalog/AssetInfo.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <set>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A data file of the asset info tests.
fs::path dataFile(const std::string& name)
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / name;
}

// The maps of a material: files for the four required channels, a constant roughness, no opacity or occlusion.
// The base color preview is a scaled copy.
AssetMaterial materialWithMaps(const std::string& name, const std::string& key)
{
    AssetMaterial material;
    material.name = name;
    for (const MapChannel channel : {MapChannel::BaseColor, MapChannel::Normal, MapChannel::Roughness, MapChannel::Metallic}) {
        const std::string file = "textures/maps/" + key + "/" + std::string(mapChannelName(channel)) + ".png";
        materialMap(material.maps, channel) = {file, MapOrigin::File, key + ".png"};
        materialMap(material.previewMaps, channel) = {file, MapOrigin::File, key + ".png"};
    }
    materialMap(material.maps, MapChannel::Roughness) = {"textures/maps/" + key + "/roughness.png", MapOrigin::Value, "0.3"};
    materialMap(material.previewMaps, MapChannel::Roughness) = materialMap(material.maps, MapChannel::Roughness);
    materialMap(material.previewMaps, MapChannel::BaseColor) = {"textures/preview/" + key + "_base_color.png", MapOrigin::File, key + ".png"};
    return material;
}

// An info with every field set.
AssetInfo fullInfo()
{
    AssetInfo info;
    info.id = "red_chair_2";
    info.name = "Red chair ü";
    info.categories = {"chair", "armchair"};
    info.tags = {"oak", "wood-dark"};
    info.license = "cc0";
    info.source = {"local_file", "Red Chair (2).obj", *parseSessionTime("2026-11-21T10:00:00Z")};
    info.size = {0.45, 0.5, 0.9};
    info.triangles = 72;
    info.objects = 6;
    info.materials = {"Fabric", "Wood"};
    info.textures = {"textures/maps/fabric/base_color.png", "textures/maps/wood/base_color.png"};
    info.materialMaps = {materialWithMaps("Fabric", "fabric"), materialWithMaps("Wood", "wood")};
    info.blendBytes = 4096;
    info.blendModified = *parseSessionTime("2026-11-21T10:00:05Z");
    info.thumbnail.rendered = true;
    info.thumbnail.engine = "cycles";
    info.thumbnail.renderedAt = *parseSessionTime("2026-11-21T10:00:07Z");
    info.blenderVersion = "5.2.0";
    info.warnings = {{"fbx_failed", "asset.fbx could not be written."}};
    return info;
}

// Checks a stored info.json against the schema.
std::string schemaProblemFor(const fs::path& file)
{
    return plynth::test::schemaProblem("asset-info.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file)));
}

// Writes an info, changes its JSON, and reads it back.
AssetInfoRead readChanged(const fs::path& folder, const std::function<void(nlohmann::json&)>& change)
{
    const fs::path file = folder / "info.json";
    plynth::test::requireOk(writeAssetInfo(file, fullInfo()));
    nlohmann::json json = nlohmann::json::parse(plynth::test::readTextFile(file));
    change(json);
    plynth::test::writeBytes(file, json.dump(2));
    return readAssetInfo(file);
}

}

TEST_CASE("info.json round trip", "[core][assetinfo]") {
    plynth::test::TempDir tmp("info-roundtrip");
    const fs::path file = tmp.path() / "red_chair_2" / "info.json";
    const AssetInfo info = fullInfo();
    plynth::test::requireOk(writeAssetInfo(file, info));
    REQUIRE_FALSE(fs::exists(fs::path(file) += ".tmp"));
    const AssetInfoRead read = readAssetInfo(file, "red_chair_2");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info == info);
    REQUIRE(schemaProblemFor(file).empty());
}

TEST_CASE("every shape of written info.json validates", "[core][assetinfo][schema]") {
    plynth::test::TempDir tmp("info-schema");
    AssetInfo minimal;
    minimal.id = "a";
    minimal.name = "A";
    minimal.source.kind = "local_file";
    AssetInfo failedThumb = fullInfo();
    failedThumb.thumbnail = {};
    failedThumb.thumbnail.error = "Fake render failure";
    int n = 0;
    for (const AssetInfo& info : {fullInfo(), minimal, failedThumb}) {
        const fs::path file = tmp.path() / ("f" + std::to_string(n++)) / "info.json";
        plynth::test::requireOk(writeAssetInfo(file, info));
        INFO(file.string());
        REQUIRE(schemaProblemFor(file).empty());
        const AssetInfoRead read = readAssetInfo(file);
        plynth::test::requireOk(read.status);
        REQUIRE(read.info == info);
    }
}

TEST_CASE("info.json data files", "[core][assetinfo]") {
    const AssetInfoRead valid = readAssetInfo(dataFile("asset-info-valid.json"), "chair");
    plynth::test::requireOk(valid.status);
    REQUIRE(valid.info.triangles == 72);
    REQUIRE(valid.info.thumbnail.rendered);
    REQUIRE(valid.info.warnings.size() == 1);
    REQUIRE(schemaProblemFor(dataFile("asset-info-valid.json")).empty());

    struct Case {
        const char* file;
        ErrorCode code;
        const char* where;
    };
    const Case cases[] = {
        {"asset-info-invalid-unknown-field.json", ErrorCode::BadFile, "/colour"},
        {"asset-info-invalid-version-2.json", ErrorCode::UnsupportedVersion, "/version"},
        {"asset-info-invalid-negative-size.json", ErrorCode::BadFile, "/size/width"},
        {"asset-info-invalid-bad-tag.json", ErrorCode::BadFile, "/tags/0"},
        {"asset-info-invalid-missing-license.json", ErrorCode::BadFile, ""},
        {"asset-info-invalid-bad-time.json", ErrorCode::BadFile, "/source/imported_at"},
        {"asset-info-invalid-bad-id.json", ErrorCode::BadFile, "/id"},
    };
    for (const Case& c : cases) {
        INFO(c.file);
        const AssetInfoRead read = readAssetInfo(dataFile(c.file));
        REQUIRE(read.status.code == c.code);
        REQUIRE(read.status.where == c.where);
        if (c.code == ErrorCode::BadFile) REQUIRE_FALSE(schemaProblemFor(dataFile(c.file)).empty());
    }
}

TEST_CASE("an id that differs from the folder is bad_file", "[core][assetinfo]") {
    const AssetInfoRead read = readAssetInfo(dataFile("asset-info-valid.json"), "table");
    REQUIRE(read.status.code == ErrorCode::BadFile);
    REQUIRE(read.status.where == "/id");
}

TEST_CASE("reading info.json that is missing or not JSON", "[core][assetinfo]") {
    plynth::test::TempDir tmp("info-bad");
    REQUIRE(readAssetInfo(tmp.path() / "none.json").status.code == ErrorCode::CannotRead);
    plynth::test::writeBytes(tmp.path() / "bad.json", "{oops");
    REQUIRE(readAssetInfo(tmp.path() / "bad.json").status.code == ErrorCode::BadFile);
}

TEST_CASE("catalogIdFromName", "[core][assetinfo]") {
    REQUIRE(catalogIdFromName("Red Chair (2)") == "red_chair_2");
    REQUIRE(catalogIdFromName("3D Lamp") == "a_3d_lamp");
    REQUIRE(catalogIdFromName("!!!") == "asset");
    REQUIRE(catalogIdFromName("") == "asset");
    REQUIRE(catalogIdFromName("__x__y__") == "x_y");
    REQUIRE(catalogIdFromName(std::string(100, 'a')) == std::string(64, 'a'));
    const std::string cut = catalogIdFromName(std::string(63, 'a') + " bbb");
    REQUIRE(cut.size() <= 64);
    REQUIRE(cut.back() != '_');
    REQUIRE(checkCatalogId(catalogIdFromName("Sofa Ünï 9")).ok());
}

TEST_CASE("uniqueCatalogId", "[core][assetinfo]") {
    std::set<std::string> taken{"chair"};
    const auto isTaken = [&](std::string_view id) { return taken.count(std::string(id)) > 0; };
    REQUIRE(uniqueCatalogId("lamp", isTaken) == "lamp");
    REQUIRE(uniqueCatalogId("chair", isTaken) == "chair_2");
    taken.insert("chair_2");
    REQUIRE(uniqueCatalogId("chair", isTaken) == "chair_3");
    const std::string longId(64, 'a');
    taken.insert(longId);
    const std::string next = uniqueCatalogId(longId, isTaken);
    REQUIRE(next.size() == 64);
    REQUIRE(next.substr(62) == "_2");
}

TEST_CASE("catalogNameFromStem", "[core][assetinfo]") {
    REQUIRE(catalogNameFromStem("red_chair-02") == "Red chair 02");
    REQUIRE(catalogNameFromStem("chair") == "Chair");
    REQUIRE(catalogNameFromStem("__a__b") == "A b");
    REQUIRE(catalogNameFromStem("") == "Asset");
}

TEST_CASE("id, tag and name rules each say what is wrong", "[core][assetinfo]") {
    const auto message = [](const Status& s) {
        REQUIRE(s.code == ErrorCode::InvalidValue);
        return s.message;
    };
    REQUIRE(checkCatalogId("chair_2").ok());
    std::set<std::string> sentences;
    sentences.insert(message(checkCatalogId("")));
    sentences.insert(message(checkCatalogId(std::string(65, 'a'))));
    sentences.insert(message(checkCatalogId("1abc")));
    sentences.insert(message(checkCatalogId("_abc")));
    sentences.insert(message(checkCatalogId("Abc")));
    sentences.insert(message(checkCatalogId("a-b")));
    REQUIRE(sentences.size() >= 4);
    REQUIRE(message(checkCatalogId("a b")).find("a b") != std::string::npos);

    REQUIRE(checkTag("wood-dark_2").ok());
    REQUIRE(checkTag("2x4").ok());
    std::set<std::string> tagSentences;
    tagSentences.insert(message(checkTag("")));
    tagSentences.insert(message(checkTag(std::string(41, 'a'))));
    tagSentences.insert(message(checkTag("-x")));
    tagSentences.insert(message(checkTag("Oak")));
    REQUIRE(tagSentences.size() == 4);

    REQUIRE(checkCatalogName("Chair").ok());
    REQUIRE(checkCatalogName("Stühle").ok());
    REQUIRE(checkCatalogName(std::string(120, 'x')).ok());
    REQUIRE(message(checkCatalogName("")) != message(checkCatalogName(std::string(121, 'x'))));
    REQUIRE(checkCatalogName("   ").code == ErrorCode::InvalidValue);
    std::string accents;
    for (int i = 0; i < 120; ++i) accents += "ü";
    REQUIRE(checkCatalogName(accents).ok());
}

TEST_CASE("material maps are written in order and validate", "[core][assetinfo][schema]") {
    plynth::test::TempDir tmp("info-maps");
    const fs::path file = tmp.path() / "info.json";
    plynth::test::requireOk(writeAssetInfo(file, fullInfo()));
    const nlohmann::json json = nlohmann::json::parse(plynth::test::readTextFile(file));
    REQUIRE(json["material_maps"].size() == 2);
    REQUIRE(json["material_maps"][1]["name"] == "Wood");
    REQUIRE(json["material_maps"][0]["maps"]["roughness"]["from"] == "value");
    REQUIRE(json["material_maps"][0]["maps"]["roughness"]["detail"] == "0.3");
    REQUIRE(json["material_maps"][0]["maps"]["opacity"]["file"].is_null());
    REQUIRE(json["material_maps"][0]["preview_maps"]["base_color"]["file"] == "textures/preview/fabric_base_color.png");
    REQUIRE(schemaProblemFor(file).empty());
}

TEST_CASE("an info.json from before milestone 10 reads with empty maps", "[core][assetinfo]") {
    const AssetInfoRead old = readAssetInfo(dataFile("asset-info-valid.json"), "chair");
    plynth::test::requireOk(old.status);
    REQUIRE(old.info.materials.size() == 2);
    REQUIRE(old.info.materialMaps.empty());
    REQUIRE_FALSE(hasMaterialMaps(old.info));
    REQUIRE(schemaProblemFor(dataFile("asset-info-valid.json")).empty());
}

TEST_CASE("hasMaterialMaps matches materials by name and order", "[core][assetinfo]") {
    AssetInfo info = fullInfo();
    REQUIRE(hasMaterialMaps(info));
    AssetInfo swapped = info;
    std::swap(swapped.materialMaps[0], swapped.materialMaps[1]);
    REQUIRE_FALSE(hasMaterialMaps(swapped));
    AssetInfo fewer = info;
    fewer.materialMaps.pop_back();
    REQUIRE_FALSE(hasMaterialMaps(fewer));
    AssetInfo extra = info;
    extra.materials.push_back("Glass");
    REQUIRE_FALSE(hasMaterialMaps(extra));
    REQUIRE(hasMaterialMaps(AssetInfo{}));
}

TEST_CASE("bad material maps are bad_file at the value", "[core][assetinfo]") {
    plynth::test::TempDir tmp("info-bad-maps");
    struct Case {
        const char* what;
        std::function<void(nlohmann::json&)> change;
        const char* where;
    };
    const Case cases[] = {
        {"outside textures", [](nlohmann::json& j) { j["material_maps"][0]["maps"]["normal"]["file"] = "elsewhere/normal.png"; },
         "/material_maps/0/maps/normal/file"},
        {"in textures but not in maps or preview", [](nlohmann::json& j) { j["material_maps"][0]["maps"]["normal"]["file"] = "textures/normal.png"; },
         "/material_maps/0/maps/normal/file"},
        {"going up", [](nlohmann::json& j) { j["material_maps"][1]["preview_maps"]["metallic"]["file"] = "textures/maps/../../x.png"; },
         "/material_maps/1/preview_maps/metallic/file"},
        {"not an image", [](nlohmann::json& j) { j["material_maps"][0]["maps"]["normal"]["file"] = "textures/maps/wood/normal.exr"; },
         "/material_maps/0/maps/normal/file"},
        {"required map without a file", [](nlohmann::json& j) {
             j["material_maps"][0]["maps"]["base_color"] = {{"file", nullptr}, {"from", "none"}, {"detail", ""}}; },
         "/material_maps/0/maps/base_color/from"},
        {"none with a file", [](nlohmann::json& j) { j["material_maps"][0]["maps"]["opacity"]["file"] = "textures/maps/a/opacity.png"; },
         "/material_maps/0/maps/opacity/file"},
        {"unknown origin", [](nlohmann::json& j) { j["material_maps"][0]["maps"]["normal"]["from"] = "guessed"; },
         "/material_maps/0/maps/normal/from"},
        {"missing channel", [](nlohmann::json& j) { j["material_maps"][0]["maps"].erase("occlusion"); }, "/material_maps/0/maps"},
        {"unknown field", [](nlohmann::json& j) { j["material_maps"][0]["extra"] = 1; }, "/material_maps/0/extra"},
        {"not a list", [](nlohmann::json& j) { j["material_maps"] = 3; }, "/material_maps"},
    };
    int n = 0;
    for (const Case& c : cases) {
        INFO(c.what);
        const AssetInfoRead read = readChanged(tmp.path() / std::to_string(n++), c.change);
        REQUIRE(read.status.code == ErrorCode::BadFile);
        REQUIRE(read.status.where == c.where);
    }
}

TEST_CASE("an info.json with material maps from a data file", "[core][assetinfo]") {
    const fs::path file = dataFile("assets/info-material-maps.json");
    const AssetInfoRead read = readAssetInfo(file, "armchair");
    plynth::test::requireOk(read.status);
    REQUIRE(schemaProblemFor(file).empty());
    REQUIRE(hasMaterialMaps(read.info));
    REQUIRE(read.info.warnings[0].code == "material_approximated");
    const AssetMaterial& paint = read.info.materialMaps[1];
    REQUIRE(paint.name == "Paint");
    REQUIRE(paint.maps.baseColor.origin == MapOrigin::Value);
    REQUIRE(paint.maps.baseColor.detail == "0.70, 0.38, 0.35");
    REQUIRE(paint.maps.normal.detail == "flat");
    REQUIRE(paint.maps.roughness.origin == MapOrigin::Baked);
    REQUIRE(paint.maps.opacity.file == "textures/maps/paint/opacity.png");
    REQUIRE(paint.maps.occlusion.origin == MapOrigin::None);
    const AssetMaterial& fabric = read.info.materialMaps[0];
    REQUIRE(fabric.previewMaps.baseColor.file == "textures/preview/fabric_base_color.png");
    REQUIRE(fabric.maps.baseColor.file == "textures/maps/fabric/base_color.png");
}
