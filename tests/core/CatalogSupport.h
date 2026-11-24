#pragma once

#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/AssetInfo.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace plynth::test {

// The write time of a file in whole seconds.
inline SessionTime fileWriteTime(const std::filesystem::path& file)
{
    const auto t = std::filesystem::last_write_time(file);
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(t);
    return std::chrono::time_point_cast<std::chrono::seconds>(system);
}

// Writes a small file, making its folder.
inline void writeBytes(const std::filesystem::path& file, const std::string& bytes)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// Writes a hand made asset into a library: asset.blend (a few bytes) and info.json, and returns its info.
// The info is not indexed.
inline AssetInfo makeAssetOnDisk(const LibraryPaths& paths, const std::string& id, const std::string& name,
                                 std::vector<std::string> categories = {}, std::vector<std::string> tags = {},
                                 bool withThumbnail = false, bool withFbx = true)
{
    const std::filesystem::path folder = paths.assetItems / pathFromUtf8(id);
    writeBytes(folder / "asset.blend", "FAKEBLEND");
    if (withFbx) writeBytes(folder / "asset.fbx", "FAKEFBX");
    AssetInfo info;
    info.id = id;
    info.name = name;
    info.categories = std::move(categories);
    info.tags = std::move(tags);
    info.source = {"local_file", id + ".obj", sessionNow()};
    info.size = {0.5, 0.5, 0.9};
    info.triangles = 12;
    info.objects = 1;
    info.blendBytes = 9;
    info.blendModified = fileWriteTime(folder / "asset.blend");
    info.blenderVersion = "5.2.0";
    info.supports = std::vector<AssetSupport>{}; // measured: no flat places
    if (withThumbnail) {
        writeBytes(folder / "thumb.png", "PNG");
        info.thumbnail.rendered = true;
        info.thumbnail.engine = "eevee";
        info.thumbnail.renderedAt = sessionNow();
    }
    requireOk(writeAssetInfo(folder / "info.json", info));
    return info;
}

// Marks a work folder as in use by this test process, the way a running import does.
inline void markFolderLive(const std::filesystem::path& folder)
{
    const ProcessIdentity me = currentProcess();
    writeBytes(folder / ".live", std::to_string(me.pid) + " " + std::to_string(me.startKey) + "\n");
}

// A new library in a temporary folder, with the catalog open on it.
struct TestLibrary {
    explicit TestLibrary(const std::string& tag) : tmp(tag)
    {
        paths = libraryPaths(tmp.path() / "Library");
        requireOk(initLibrary(tmp.path() / "Library"));
        Status status;
        catalog = AssetCatalog::open(paths, status);
        requireOk(status);
    }

    // Writes an asset to disk and indexes it.
    AssetInfo add(const std::string& id, const std::string& name, std::vector<std::string> categories = {},
                  std::vector<std::string> tags = {}, bool withThumbnail = false, bool withFbx = true)
    {
        AssetInfo info = makeAssetOnDisk(paths, id, name, std::move(categories), std::move(tags), withThumbnail, withFbx);
        requireOk(catalog->indexAsset(info));
        return info;
    }

    TempDir tmp;
    LibraryPaths paths;
    std::unique_ptr<AssetCatalog> catalog;
};

// The ids of search results.
inline std::vector<std::string> ids(const std::vector<AssetRecord>& records)
{
    std::vector<std::string> out;
    for (const AssetRecord& r : records) out.push_back(r.id);
    return out;
}

}
