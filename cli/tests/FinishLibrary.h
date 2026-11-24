#pragma once

#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Library.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/AssetInfo.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace plynth::cli::testing {

// Writes one measured, ready asset folder by hand and indexes it.
inline void makeFinishAsset(const std::filesystem::path& library, const std::string& id,
                            const std::vector<std::string>& categories, const std::vector<std::string>& tags,
                            AssetSize size) {
    Status status;
    auto catalog = AssetCatalog::open(libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().assetItems / id;
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "asset.blend", std::ios::binary) << "x";
    AssetInfo info;
    info.id = id;
    info.name = id;
    info.categories = categories;
    info.tags = tags;
    info.source.kind = "local_file";
    info.source.reference = id + ".obj";
    info.source.importedAt = sessionNow();
    info.size = size;
    info.triangles = 12;
    info.blendBytes = 1;
    info.blenderVersion = "5.2.0";
    REQUIRE(writeAssetInfo(folder / "info.json", info).ok());
    REQUIRE(catalog->indexAsset(info).ok());
}

// A sample with the room square of drawRoomSquare (walls w1 to w4, living room r1), a door o1 on w2 and a
// window o2 on w3, and a library set in the settings holding a door that fits o1 exactly, a picture and two
// sofas of the same category. The settings are cleared before and after.
struct FinishSample {
    explicit FinishSample(const std::string& name) : sample(name) {
        sample.drawRoomSquare();
        REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--type", "door"}).exitCode == kExitOk);
        REQUIRE(sample.run({"opening", "add", "--wall", "w3", "--offset", "2", "--type", "window"}).exitCode ==
                kExitOk);
        library = sample.temp.path() / "Library";
        REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
        makeFinishAsset(library, "oak_door", {"door"}, {}, {0.9, 0.05, 2.1});
        makeFinishAsset(library, "small_window", {"window"}, {}, {0.6, 0.1, 0.6});
        makeFinishAsset(library, "sea_print", {"decor"}, {"wall_art"}, {0.6, 0.03, 0.5});
        makeFinishAsset(library, "sofa_a", {"sofa"}, {}, {2.0, 0.9, 0.85});
        makeFinishAsset(library, "sofa_b", {"sofa"}, {}, {1.9, 0.9, 0.8});
    }

    // Runs a command on the project.
    Outcome run(const std::vector<std::string>& args) const { return sample.run(args); }

    GuardedSettingsReset reset;
    Sample sample;
    std::filesystem::path library;
};

}
