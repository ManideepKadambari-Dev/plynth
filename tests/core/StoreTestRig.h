#pragma once

#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"
#include "ZipTestWriter.h"

#include <plynth/core/Blender.h>
#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/MaterialImport.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

namespace plynth::test {

// The contents of a zip with these names, as the Fab rules see them.
inline FabZipContents fabContentsOf(const std::vector<std::string>& names)
{
    std::vector<ZipEntry> entries;
    for (const std::string& name : names) {
        ZipEntry entry;
        entry.name = name;
        entry.size = 1;
        entries.push_back(entry);
    }
    return classifyFabZip(entries);
}

// A made-up user, a library, a fake Blender and an import context that deletes zips for good.
template <class Library>
struct StoreRig {
    static constexpr bool isMaterial = std::is_same_v<Library, MaterialTestLibrary>;

    // Makes all of it in temporary folders. A store fetch runs the fake at low integrity, so the fake writes its
    // records into a folder labelled low (records). The fake's own folder and the library stay unlabelled.
    explicit StoreRig(const std::string& tag, const std::string& mode = "") : user(tag), lib(tag), records(lib.tmp.path() / "l")
    {
        context.library = lib.paths;
        context.blender = inspectBlender(makeFakeBlender(lib.tmp.path() / "b", "5.2.0", mode));
        context.run.scriptsFolder = makeFakeScripts(lib.tmp.path() / "s");
        context.removeOriginal = [](const std::filesystem::path& path) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return Status{};
        };
    }

    // Writes a zip of small files in the folder given, or beside the library.
    std::filesystem::path zip(const std::string& name, const std::vector<std::string>& names,
                              const std::filesystem::path& folder = {}) const
    {
        const std::filesystem::path file = (folder.empty() ? lib.tmp.path() / "in" : folder) / pathFromUtf8(name);
        std::filesystem::create_directories(file.parent_path());
        std::vector<ZipTestEntry> entries;
        for (const std::string& entry : names) entries.push_back({entry, "x"});
        writeZipFile(file, entries);
        return file;
    }

    // Writes a zip into this catalog's inbox.
    std::filesystem::path inboxZip(const std::string& name, const std::vector<std::string>& names) const
    {
        return zip(name, names, isMaterial ? lib.paths.materialInbox : lib.paths.assetInbox);
    }

    // The jobs the fake Blender was given.
    std::vector<nlohmann::json> jobs() const
    {
        std::vector<nlohmann::json> out;
        std::ifstream in(records.folder / "fake_jobs.log", std::ios::binary);
        for (std::string line; std::getline(in, line);) out.push_back(nlohmann::json::parse(line));
        return out;
    }

    // The info.json an import wrote for an id, with empty fields when there is none.
    nlohmann::json info(const std::string& id) const
    {
        const auto& items = isMaterial ? lib.paths.materialItems : lib.paths.assetItems;
        const auto file = items / pathFromUtf8(id) / "info.json";
        if (id.empty() || !std::filesystem::exists(file)) return nlohmann::json{{"license", ""}, {"source", {{"kind", ""}}}};
        return nlohmann::json::parse(readTextFile(file));
    }

    IsolatedUser user;
    Library lib;
    FakeRecords records;
    ImportContext context;
};

using AssetRig = StoreRig<TestLibrary>;
using MaterialRig = StoreRig<MaterialTestLibrary>;

}
