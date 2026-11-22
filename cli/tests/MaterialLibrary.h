#pragma once

#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/MaterialCatalog.h>
#include <plynth/core/catalog/MaterialInfo.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace plynth::cli::testing {

using MaterialSettingsReset = GuardedSettingsReset;

// Writes one material folder by hand (textures, thumbnail, info.json) and indexes it.
inline void makeMaterial(const std::filesystem::path& library, const std::string& id, const std::string& name,
                         const std::vector<std::string>& groups, const std::vector<std::string>& tags,
                         const std::string& license = "cc0", TileSize tile = {1.0, 1.0}) {
    Status status;
    auto catalog = MaterialCatalog::open(libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().materialItems / id;
    std::filesystem::create_directories(folder / "textures");
    MaterialInfo info;
    info.id = id;
    info.name = name;
    info.groups = groups;
    info.tags = tags;
    info.license = license;
    info.source.kind = std::string(kSourceTextureSet);
    info.source.reference = id + ".zip";
    info.source.importedAt = sessionNow();
    info.tileSize = tile;
    info.textureSize = {4, 4};
    for (const char* channel : {"base_color", "normal", "roughness", "metallic"}) {
        const std::string file = std::string("textures/") + channel + ".png";
        std::ofstream(folder / file, std::ios::binary) << channel;
        MaterialMap& map = materialMap(info.maps, *mapChannelFromName(channel));
        map.file = file;
        map.origin = MapOrigin::File;
        map.detail = std::string(channel) + ".png";
    }
    std::ofstream(folder / "thumb.png", std::ios::binary) << "x";
    info.files = measureMaterialFiles(folder, info.maps);
    info.thumbnail.rendered = true;
    info.thumbnail.engine = "eevee";
    info.thumbnail.renderedAt = sessionNow();
    info.blenderVersion = "5.2.0";
    REQUIRE(writeMaterialInfo(folder / std::string(kMaterialInfoFileName), info).ok());
    REQUIRE(catalog->indexMaterial(info).ok());
}

// A library made by the CLI and set in the settings, holding the materials the tests use.
struct MaterialLibrary {
    MaterialLibrary(const std::string& name, bool withMaterials = true) : temp(name) {
        root = temp.path() / "Library";
        REQUIRE(runChecked({"library", "init", utf8(root), "--use"}).exitCode == kExitOk);
        if (withMaterials) {
            makeMaterial(root, "oak_planks", "Oak planks", {"floor"}, {"wood", "oak"});
            makeMaterial(root, "grey_tile", "Grey tile", {"floor", "wall"}, {"ceramic"}, "cc0", {0.6, 0.6});
            makeMaterial(root, "red_brick", "Red brick", {"wall", "exterior"}, {"brick"});
            makeMaterial(root, "white_paint", "White paint", {"wall", "ceiling"}, {"paint"});
            makeMaterial(root, "sage_paint", "Sage paint", {"wall"}, {"paint"}, "cc_by_4");
            makeMaterial(root, "stone_wall", "Stone wall", {}, {"stone"});
            makeMaterial(root, "painted_wood", "Painted wood", {"trim", "door"}, {"wood"});
            makeMaterial(root, "brushed_steel", "Brushed steel", {"metal"}, {"steel"});
            makeMaterial(root, "clear_glass", "Clear glass", {"glass"}, {"glass"});
        }
    }

    // The item folder of a material.
    std::filesystem::path item(const std::string& id) const { return root / "materials" / "items" / id; }

    MaterialSettingsReset reset;
    TempFolder temp;
    std::filesystem::path root;
};

}
