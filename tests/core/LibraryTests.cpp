#include "CatalogSupport.h"

#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Every file under a folder with its size and write time, to see whether anything changed.
std::map<std::string, std::pair<std::uintmax_t, fs::file_time_type>> snapshot(const fs::path& root)
{
    std::map<std::string, std::pair<std::uintmax_t, fs::file_time_type>> out;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (e.is_regular_file()) out[pathToUtf8(e.path())] = {e.file_size(), e.last_write_time()};
    }
    return out;
}

}

TEST_CASE("initLibrary makes every folder and both databases", "[core][library]") {
    plynth::test::TempDir tmp("lib-init");
    const fs::path root = tmp.path() / "Lib";
    REQUIRE_FALSE(isLibraryFolder(root));
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    for (const fs::path* folder : {&p.assets, &p.assetItems, &p.assetInbox, &p.assetInboxDone, &p.assetInboxFailed,
                                   &p.assetStaging, &p.materials, &p.materialItems, &p.materialInbox, &p.generators, &p.shapes}) {
        INFO(pathToUtf8(*folder));
        REQUIRE(fs::is_directory(*folder));
    }
    REQUIRE(fs::is_regular_file(p.assetsDb));
    REQUIRE(fs::is_regular_file(p.materialsDb));
    REQUIRE(isLibraryFolder(root));
    REQUIRE(p.assetsDb.filename() == "assets.db");
    REQUIRE(p.assetStaging.filename() == ".staging");
}

TEST_CASE("a second initLibrary changes nothing and empties .staging", "[core][library]") {
    plynth::test::TempDir tmp("lib-init-twice");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    plynth::test::writeBytes(p.assetItems / "keep" / "asset.blend", "x");
    plynth::test::writeBytes(p.assetInbox / "waiting.obj", "x");
    const auto before = snapshot(root);

    plynth::test::writeBytes(p.assetStaging / "job-1" / "job.json", "{}");
    plynth::test::writeBytes(p.assetStaging / "leftover.txt", "x");
    plynth::test::requireOk(initLibrary(root));

    REQUIRE(snapshot(root) == before);
    REQUIRE(fs::is_directory(p.assetStaging));
    REQUIRE(fs::is_empty(p.assetStaging));
}

namespace {

// Reads or sets PRAGMA user_version, which SQLite keeps at byte 60 of the file header.
int userVersionOf(const fs::path& db)
{
    std::ifstream in(db, std::ios::binary);
    in.seekg(60);
    unsigned char b[4] = {};
    in.read(reinterpret_cast<char*>(b), 4);
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

void setUserVersion(const fs::path& db, int version)
{
    std::fstream io(db, std::ios::binary | std::ios::in | std::ios::out);
    io.seekp(60);
    const unsigned char b[4] = {static_cast<unsigned char>(version >> 24), static_cast<unsigned char>(version >> 16),
                                static_cast<unsigned char>(version >> 8), static_cast<unsigned char>(version)};
    io.write(reinterpret_cast<const char*>(b), 4);
}

}

TEST_CASE("the databases carry the schema version", "[core][library]") {
    plynth::test::TempDir tmp("lib-version");
    plynth::test::requireOk(initLibrary(tmp.path() / "Lib"));
    const LibraryPaths p = libraryPaths(tmp.path() / "Lib");
    REQUIRE(userVersionOf(p.assetsDb) == kAssetsDbVersion);
    REQUIRE(userVersionOf(p.materialsDb) == kMaterialsDbVersion);
}

TEST_CASE("a database at a newer version is unsupported_version", "[core][library]") {
    plynth::test::TempDir tmp("lib-newer");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    setUserVersion(p.assetsDb, 2);
    REQUIRE(initLibrary(root).code == ErrorCode::UnsupportedVersion);
    Status status;
    REQUIRE(AssetCatalog::open(p, status) == nullptr);
    REQUIRE(status.code == ErrorCode::UnsupportedVersion);
}

TEST_CASE("chooseLibrary", "[core][library]") {
    plynth::test::TempDir tmp("lib-choose");
    plynth::test::requireOk(initLibrary(tmp.path() / "Lib"));
    fs::create_directories(tmp.path() / "empty");

    SECTION("nothing set") {
        const LibraryChoice c = chooseLibrary(std::nullopt, MachineSettings{});
        REQUIRE(c.status.code == ErrorCode::NoLibrary);
        REQUIRE(c.status.message.find("library init") != std::string::npos);
    }
    SECTION("an option that is not a library") {
        const LibraryChoice c = chooseLibrary(tmp.path() / "empty", MachineSettings{});
        REQUIRE(c.status.code == ErrorCode::NoLibrary);
        REQUIRE(c.status.message.find("library init") != std::string::npos);
    }
    SECTION("a setting that is not a library") {
        MachineSettings s;
        s.library = tmp.path() / "missing";
        REQUIRE(chooseLibrary(std::nullopt, s).status.code == ErrorCode::NoLibrary);
    }
    SECTION("the setting is used, and the option wins") {
        MachineSettings s;
        s.library = tmp.path() / "Lib";
        const LibraryChoice fromSettings = chooseLibrary(std::nullopt, s);
        plynth::test::requireOk(fromSettings.status);
        REQUIRE(fromSettings.paths.root == tmp.path() / "Lib");
        s.library = tmp.path() / "empty";
        REQUIRE(chooseLibrary(tmp.path() / "Lib", s).status.ok());
    }
}

TEST_CASE("libraryPaths touches nothing", "[core][library]") {
    plynth::test::TempDir tmp("lib-paths");
    const LibraryPaths p = libraryPaths(tmp.path() / "Nothing");
    REQUIRE_FALSE(fs::exists(tmp.path() / "Nothing"));
    REQUIRE(p.assetInbox == tmp.path() / "Nothing" / "assets" / "inbox");
    REQUIRE(p.assetInboxDone == p.assetInbox / "done");
    REQUIRE(p.materialsDb == tmp.path() / "Nothing" / "materials" / "materials.db");
    REQUIRE(p.shapes == tmp.path() / "Nothing" / "shapes");
}
