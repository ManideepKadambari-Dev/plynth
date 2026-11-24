#pragma once

#include "CatalogSupport.h"
#include "MaterialCatalogSupport.h"
#include "SquareFixtures.h"

#include <plynth/core/Pack.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/MaterialCatalog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace plynth::test {

// A saved project and two libraries: A, where items come from, and B, an empty one on "another machine".
struct PackFixture {
    explicit PackFixture(const std::string& tag) : tmp(tag)
    {
        a = libraryPaths(tmp.path() / "LibA");
        b = libraryPaths(tmp.path() / "LibB");
        requireOk(initLibrary(a.root));
        requireOk(initLibrary(b.root));
        project = tmp.path() / std::filesystem::path(u8"Wohnung ä");
        Project p = squareProjectWithWalls();
        p.settings.name = "Flat";
        requireOk(saveProject(p, project, {}));
        Status status;
        assets = AssetCatalog::open(a, status);
        requireOk(status);
        materials = MaterialCatalog::open(a, status);
        requireOk(status);
    }

    // Adds an asset to library A with a texture, from the given source kind and with the given license.
    AssetInfo asset(const std::string& id, const std::string& kind = "local_file", const std::string& license = "cc0",
                    const std::string& reference = "")
    {
        AssetInfo info = makeAssetOnDisk(a, id, "Name " + id, {}, {}, true, true);
        const std::filesystem::path folder = a.assetItems / pathFromUtf8(id);
        writeBytes(folder / "textures" / "wood.png", "WOOD-" + id);
        info.textures = {"textures/wood.png"};
        info.source.kind = kind;
        info.source.reference = reference.empty() ? id + ".obj" : reference;
        info.license = license;
        requireOk(writeAssetInfo(folder / "info.json", info));
        requireOk(assets->indexAsset(info));
        return info;
    }

    // Adds a material to library A.
    MaterialInfo material(const std::string& id, const std::string& license = "cc0", const std::string& kind = "texture_set")
    {
        MaterialInfo info = makeMaterialOnDisk(a, id, "Material " + id);
        info.license = license;
        info.source.kind = kind;
        requireOk(writeMaterialInfo(a.materialItems / pathFromUtf8(id) / "info.json", info));
        requireOk(materials->indexMaterial(info));
        return info;
    }

    // Writes a custom shape file into library A.
    void shape(const std::string& id) const
    {
        writeBytes(a.shapes / pathFromUtf8(id + ".shape.json"),
                   "{\"format\": \"plynth-shape\", \"version\": 1, \"id\": \"" + id + "\", \"name\": \"Shape " + id +
                       "\", \"outline\": [[0, 0], [1, 0], [1, 1], [0, 1]]}");
    }

    // Packs the project with these items from library A.
    PackReport pack(const std::vector<PackUse>& used, bool checkOnly = false, std::vector<RoomType> rooms = {},
                    std::stop_token stop = {}) const
    {
        PackRequest request;
        request.projectFolder = project;
        request.contents.used = used;
        request.contents.roomTypes = std::move(rooms);
        request.library = a;
        request.checkOnly = checkOnly;
        return packProject(request, stop);
    }

    std::filesystem::path packFolder() const { return project / "pack"; }
    std::filesystem::path manifestFile() const { return packFolder() / "pack.json"; }

    // The manifest as JSON, read from disk.
    nlohmann::json manifest() const { return nlohmann::json::parse(readTextFile(manifestFile())); }

    // Writes changed manifest JSON back.
    void writeManifest(const nlohmann::json& json) const { writeBytes(manifestFile(), json.dump(2)); }

    // Every file below the pack folder, relative, with forward slashes.
    std::vector<std::string> packFiles() const
    {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(packFolder())) {
            if (entry.is_regular_file()) files.push_back(pathToUtf8(entry.path().lexically_relative(packFolder())));
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    TempDir tmp;
    LibraryPaths a;
    LibraryPaths b;
    std::filesystem::path project;
    std::unique_ptr<AssetCatalog> assets;
    std::unique_ptr<MaterialCatalog> materials;
};

// The entry of an id in a manifest, or null.
inline const PackEntry* findEntry(const PackManifest& manifest, const std::string& id)
{
    for (const PackEntry& entry : manifest.entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

}
