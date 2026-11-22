#pragma once

#include "CatalogSupport.h"

#include <plynth/core/catalog/MaterialCatalog.h>
#include <plynth/core/catalog/MaterialInfo.h>

#include <filesystem>
#include <string>
#include <vector>

namespace plynth::test {

// Writes a hand made material into a library: the four required maps, an occlusion map when asked for, an
// opacity map when asked for, thumb.png and info.json. The info is not indexed.
inline MaterialInfo makeMaterialOnDisk(const LibraryPaths& paths, const std::string& id, const std::string& name,
                                       std::vector<std::string> groups = {}, std::vector<std::string> tags = {},
                                       bool withThumbnail = true, bool withOpacity = false)
{
    const std::filesystem::path folder = paths.materialItems / pathFromUtf8(id);
    MaterialInfo info;
    info.id = id;
    info.name = name;
    info.groups = std::move(groups);
    info.tags = std::move(tags);
    info.source = {"texture_set", id, sessionNow()};
    info.tileSize = {1.0, 0.5};
    info.textureSize = {512, 512};
    info.blenderVersion = "5.2.0";
    const auto put = [&](MapChannel channel, const char* ext) {
        MaterialMap& map = materialMap(info.maps, channel);
        map.file = "textures/" + std::string(mapChannelName(channel)) + "." + ext;
        map.origin = MapOrigin::File;
        map.detail = std::string(mapChannelName(channel)) + "." + ext;
        writeBytes(folder / map.file, "IMG-" + map.file);
    };
    put(MapChannel::BaseColor, "png");
    put(MapChannel::Normal, "png");
    put(MapChannel::Roughness, "jpg");
    put(MapChannel::Metallic, "png");
    if (withOpacity) put(MapChannel::Opacity, "png");
    info.files = measureMaterialFiles(folder, info.maps);
    if (withThumbnail) {
        writeBytes(folder / "thumb.png", "PNG");
        info.thumbnail.rendered = true;
        info.thumbnail.engine = "eevee";
        info.thumbnail.renderedAt = sessionNow();
    }
    requireOk(writeMaterialInfo(folder / "info.json", info));
    return info;
}

// A new library in a temporary folder, with the materials catalog open on it.
struct MaterialTestLibrary {
    explicit MaterialTestLibrary(const std::string& tag) : tmp(tag)
    {
        paths = libraryPaths(tmp.path() / "Library");
        requireOk(initLibrary(tmp.path() / "Library"));
        reopen();
    }

    // Closes the catalog and opens it again, as a new run of a program would.
    void reopen()
    {
        catalog.reset();
        Status status;
        catalog = MaterialCatalog::open(paths, status);
        requireOk(status);
    }

    // Writes a material to disk and indexes it.
    MaterialInfo add(const std::string& id, const std::string& name, std::vector<std::string> groups = {},
                     std::vector<std::string> tags = {}, bool withThumbnail = true, bool withOpacity = false)
    {
        MaterialInfo info = makeMaterialOnDisk(paths, id, name, std::move(groups), std::move(tags), withThumbnail, withOpacity);
        requireOk(catalog->indexMaterial(info));
        return info;
    }

    std::filesystem::path folder(const std::string& id) const { return paths.materialItems / pathFromUtf8(id); }

    TempDir tmp;
    LibraryPaths paths;
    std::unique_ptr<MaterialCatalog> catalog;
};

// The ids of search results.
inline std::vector<std::string> ids(const std::vector<MaterialRecord>& records)
{
    std::vector<std::string> out;
    for (const MaterialRecord& r : records) out.push_back(r.id);
    return out;
}

}
