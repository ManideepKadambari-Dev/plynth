#include "MaterialCatalogSupport.h"
#include "SchemaSupport.h"

#include <plynth/core/catalog/MaterialInfo.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A data file of the material info tests.
fs::path dataFile(const std::string& name)
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "materials" / name;
}

// An info with every field set.
MaterialInfo fullInfo()
{
    MaterialInfo info;
    info.id = "red_brick";
    info.name = "Red brick ü";
    info.groups = {"wall", "exterior"};
    info.tags = {"brick", "red"};
    info.license = "own_work";
    info.source = {"texture_set", "red_brick", *parseSessionTime("2026-11-21T10:00:00Z")};
    info.tileSize = {1.2, 0.6};
    info.maps.baseColor = {"textures/base_color.png", MapOrigin::File, "diff.png"};
    info.maps.normal = {"textures/normal.png", MapOrigin::File, "nor_gl.png"};
    info.maps.roughness = {"textures/roughness.jpg", MapOrigin::Baked, ""};
    info.maps.metallic = {"textures/metallic.png", MapOrigin::Value, "0"};
    info.maps.opacity = {"textures/opacity.png", MapOrigin::File, "alpha.png"};
    info.maps.occlusion = {"textures/occlusion.jpeg", MapOrigin::File, "ao.jpg"};
    info.textureSize = {1024, 512};
    info.files = {4096, *parseSessionTime("2026-11-21T10:00:05Z")};
    info.thumbnail.rendered = true;
    info.thumbnail.engine = "eevee";
    info.thumbnail.renderedAt = *parseSessionTime("2026-11-21T10:00:07Z");
    info.blenderVersion = "5.2.0";
    info.warnings = {{"unused_image", "notes.png was not used."}};
    return info;
}

// Checks a stored info.json against the schema.
std::string schemaProblemFor(const fs::path& file)
{
    return plynth::test::schemaProblem("material-info.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file)));
}

}

TEST_CASE("material info.json round trip", "[core][materialinfo]") {
    plynth::test::TempDir tmp("matinfo-roundtrip");
    const fs::path file = tmp.path() / "red_brick" / "info.json";
    const MaterialInfo info = fullInfo();
    plynth::test::requireOk(writeMaterialInfo(file, info));
    REQUIRE_FALSE(fs::exists(fs::path(file) += ".tmp"));
    const MaterialInfoRead read = readMaterialInfo(file, "red_brick");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info == info);
    REQUIRE(schemaProblemFor(file).empty());
}

TEST_CASE("every shape of written material info validates", "[core][materialinfo][schema]") {
    plynth::test::TempDir tmp("matinfo-schema");
    MaterialInfo opaque = fullInfo();
    opaque.maps.opacity = {};
    opaque.maps.occlusion = {};
    MaterialInfo failedThumb = fullInfo();
    failedThumb.thumbnail = {};
    failedThumb.thumbnail.error = "Fake render failure";
    MaterialInfo invalidated = fullInfo();
    invalidated.files = {};
    int n = 0;
    for (const MaterialInfo& info : {fullInfo(), opaque, failedThumb, invalidated}) {
        const fs::path file = tmp.path() / ("f" + std::to_string(n++)) / "info.json";
        plynth::test::requireOk(writeMaterialInfo(file, info));
        INFO(file.string());
        REQUIRE(schemaProblemFor(file).empty());
        const MaterialInfoRead read = readMaterialInfo(file);
        plynth::test::requireOk(read.status);
        REQUIRE(read.info == info);
    }
    REQUIRE(isOpaque(opaque));
    REQUIRE_FALSE(isOpaque(fullInfo()));
}

TEST_CASE("the valid fixture reads and validates", "[core][materialinfo][schema]") {
    const MaterialInfoRead read = readMaterialInfo(dataFile("info-valid.json"), "red_brick");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.name == "Red brick");
    REQUIRE(read.info.tileSize == TileSize{1.2, 0.6});
    REQUIRE(read.info.maps.metallic.origin == MapOrigin::Value);
    REQUIRE(read.info.maps.occlusion.file == "textures/occlusion.jpg");
    REQUIRE(isOpaque(read.info));
    REQUIRE(schemaProblemFor(dataFile("info-valid.json")).empty());
}

TEST_CASE("material info refusals", "[core][materialinfo]") {
    struct Case {
        const char* file;
        ErrorCode code;
        const char* where;
        bool schemaRefuses;
    };
    const Case cases[] = {
        {"info-invalid-map-outside.json", ErrorCode::BadFile, "/maps/normal/file", true},
        {"info-invalid-map-folder.json", ErrorCode::BadFile, "/maps/normal/file", true},
        {"info-invalid-required-none.json", ErrorCode::BadFile, "/maps/roughness/from", true},
        {"info-invalid-opacity-file.json", ErrorCode::BadFile, "/maps/opacity/file", true},
        {"info-invalid-tile.json", ErrorCode::BadFile, "/tile_size", true},
        {"info-invalid-unknown-field.json", ErrorCode::BadFile, "/extra", true},
        {"info-invalid-bad-id.json", ErrorCode::BadFile, "/id", true},
        {"info-invalid-engine.json", ErrorCode::BadFile, "/thumbnail/engine", true},
        {"info-invalid-version-2.json", ErrorCode::UnsupportedVersion, "/version", true},
    };
    for (const Case& c : cases) {
        INFO(c.file);
        const MaterialInfoRead read = readMaterialInfo(dataFile(c.file));
        REQUIRE(read.status.code == c.code);
        REQUIRE(read.status.where == c.where);
        REQUIRE(schemaProblemFor(dataFile(c.file)).empty() != c.schemaRefuses);
    }
}

TEST_CASE("an id that differs from the folder is refused", "[core][materialinfo]") {
    const MaterialInfoRead read = readMaterialInfo(dataFile("info-valid.json"), "white_paint");
    REQUIRE(read.status.code == ErrorCode::BadFile);
    REQUIRE(read.status.where == "/id");
}

TEST_CASE("a missing or broken material info file", "[core][materialinfo]") {
    plynth::test::TempDir tmp("matinfo-missing");
    REQUIRE(readMaterialInfo(tmp.path() / "nope.json").status.code == ErrorCode::CannotRead);
    plynth::test::writeBytes(tmp.path() / "bad.json", "{not json");
    REQUIRE(readMaterialInfo(tmp.path() / "bad.json").status.code == ErrorCode::BadFile);
    plynth::test::writeBytes(tmp.path() / "list.json", "[]");
    REQUIRE(readMaterialInfo(tmp.path() / "list.json").status.code == ErrorCode::BadFile);
}

TEST_CASE("parseTileSize", "[core][materialinfo]") {
    TileSize size;
    plynth::test::requireOk(parseTileSize("1.2x0.6", size));
    REQUIRE(size == TileSize{1.2, 0.6});
    plynth::test::requireOk(parseTileSize("2", size));
    REQUIRE(size == TileSize{2.0, 2.0});
    plynth::test::requireOk(parseTileSize("0.5X3", size));
    REQUIRE(size == TileSize{0.5, 3.0});

    const TileSize before = size;
    for (const char* bad : {"0x1", "abc", "", "1x", "x1", "1x2x3", "101", "0.001", "-1", "1,2", "nan"}) {
        INFO(bad);
        REQUIRE(parseTileSize(bad, size).code == ErrorCode::InvalidValue);
        REQUIRE(size == before);
    }
}

TEST_CASE("checkTileSize limits", "[core][materialinfo]") {
    REQUIRE(checkTileSize({0.01, 100.0}).ok());
    REQUIRE(checkTileSize({1.0, 1.0}).ok());
    REQUIRE(checkTileSize({0.009, 1.0}).code == ErrorCode::InvalidValue);
    REQUIRE(checkTileSize({1.0, 100.1}).code == ErrorCode::InvalidValue);
    REQUIRE(checkTileSize({std::nan(""), 1.0}).code == ErrorCode::InvalidValue);
}

TEST_CASE("channel and origin names", "[core][materialinfo]") {
    REQUIRE(allMapChannels().size() == 6);
    for (const MapChannel channel : allMapChannels()) {
        REQUIRE(mapChannelFromName(mapChannelName(channel)) == channel);
    }
    REQUIRE(mapChannelName(MapChannel::BaseColor) == "base_color");
    REQUIRE_FALSE(mapChannelFromName("emission"));
    for (const MapOrigin origin : {MapOrigin::File, MapOrigin::Value, MapOrigin::Baked, MapOrigin::None}) {
        REQUIRE(mapOriginFromName(mapOriginName(origin)) == origin);
    }
    REQUIRE_FALSE(mapOriginFromName("magic"));
    MaterialMaps maps;
    materialMap(maps, MapChannel::Occlusion).file = "textures/occlusion.png";
    REQUIRE(maps.occlusion.file == "textures/occlusion.png");
    REQUIRE(materialMap(static_cast<const MaterialMaps&>(maps), MapChannel::Occlusion).file == maps.occlusion.file);
}

TEST_CASE("measureMaterialFiles adds sizes and takes the latest time", "[core][materialinfo]") {
    plynth::test::TempDir tmp("matinfo-measure");
    const MaterialInfo info = plynth::test::makeMaterialOnDisk(libraryPaths(tmp.path()), "stone", "Stone");
    const fs::path folder = libraryPaths(tmp.path()).materialItems / "stone";
    long long bytes = 0;
    for (const MapChannel channel : allMapChannels()) {
        const MaterialMap& map = materialMap(info.maps, channel);
        if (!map.file.empty()) bytes += static_cast<long long>(fs::file_size(folder / map.file));
    }
    const MaterialFiles files = measureMaterialFiles(folder, info.maps);
    REQUIRE(files.bytes == bytes);
    REQUIRE(files.bytes > 0);
    REQUIRE(files == info.files);

    fs::remove(folder / "textures" / "normal.png");
    const MaterialFiles fewer = measureMaterialFiles(folder, info.maps);
    REQUIRE(fewer.bytes < files.bytes);
}
