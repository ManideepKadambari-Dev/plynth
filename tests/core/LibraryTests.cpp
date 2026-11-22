#include "CatalogSupport.h"

#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
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
                                   &p.assetStaging, &p.materials, &p.materialItems, &p.materialInbox, &p.materialInboxDone,
                                   &p.materialInboxFailed, &p.materialStaging, &p.generators, &p.shapes}) {
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

TEST_CASE("initLibrary leaves the work folder of a running import alone", "[core][library]") {
    plynth::test::TempDir tmp("lib-init-live");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    plynth::test::writeBytes(p.assetStaging / "busy-00000001" / "job.json", "{}");
    plynth::test::markFolderLive(p.assetStaging / "busy-00000001");
    plynth::test::writeBytes(p.assetStaging / "left-00000002" / "job.json", "{}");
    plynth::test::requireOk(initLibrary(root));
    REQUIRE(fs::exists(p.assetStaging / "busy-00000001" / "job.json"));
    REQUIRE_FALSE(fs::exists(p.assetStaging / "left-00000002"));
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
    REQUIRE(p.materialInboxDone == p.materialInbox / "done");
    REQUIRE(p.materialInboxFailed == p.materialInbox / "failed");
    REQUIRE(p.materialStaging == p.materials / ".staging");
    REQUIRE(p.shapes == tmp.path() / "Nothing" / "shapes");
}

namespace {

// Runs one statement on a database file and returns the first number it gives, or 0.
long long scalar(const fs::path& db, const char* sql)
{
    sqlite3* handle = nullptr;
    REQUIRE(sqlite3_open(pathToUtf8(db).c_str(), &handle) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) == SQLITE_OK);
    long long value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(handle);
    return value;
}

// Runs statements that return nothing.
void runSql(const fs::path& db, const char* sql)
{
    sqlite3* handle = nullptr;
    REQUIRE(sqlite3_open(pathToUtf8(db).c_str(), &handle) == SQLITE_OK);
    REQUIRE(sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(handle);
}

}

TEST_CASE("a new library seeds the material groups and licenses", "[core][library][materials]") {
    plynth::test::TempDir tmp("lib-material-seed");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM material_groups") == 8);
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM material_groups WHERE built_in = 1") == 8);
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM licenses") == 6);
    REQUIRE(scalar(p.materialsDb, "SELECT sort FROM material_groups WHERE id = 'floor'") == 0);
    REQUIRE(scalar(p.materialsDb, "SELECT sort FROM material_groups WHERE id = 'metal'") == 7);
    for (const char* id : {"floor", "wall", "ceiling", "exterior", "trim", "door", "glass", "metal"}) {
        INFO(id);
        const std::string sql = std::string("SELECT COUNT(*) FROM material_groups WHERE id = '") + id + "'";
        REQUIRE(scalar(p.materialsDb, sql.c_str()) == 1);
    }
}

TEST_CASE("initLibrary on a milestone 6 library adds the new folders and seeds the groups once", "[core][library][materials]") {
    plynth::test::TempDir tmp("lib-material-upgrade");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);

    fs::remove_all(p.materialInboxDone);
    fs::remove_all(p.materialInboxFailed);
    fs::remove_all(p.materialStaging);
    runSql(p.materialsDb, "DELETE FROM material_groups; DELETE FROM licenses;");
    plynth::test::writeBytes(p.materialItems / "keep" / "info.json", "{}");
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM material_groups") == 0);

    plynth::test::requireOk(initLibrary(root));
    REQUIRE(fs::is_directory(p.materialInboxDone));
    REQUIRE(fs::is_directory(p.materialInboxFailed));
    REQUIRE(fs::is_directory(p.materialStaging));
    REQUIRE(fs::exists(p.materialItems / "keep" / "info.json"));
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM material_groups") == 8);
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM licenses") == 6);

    plynth::test::requireOk(initLibrary(root));
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM material_groups") == 8);
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM licenses") == 6);
}

TEST_CASE("initLibrary keeps the user's material groups when the table is not empty", "[core][library][materials]") {
    plynth::test::TempDir tmp("lib-material-keep");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    runSql(p.materialsDb, "INSERT INTO material_groups (id, name, sort, built_in) VALUES ('mine', 'Mine', 8, 0)");
    plynth::test::requireOk(initLibrary(root));
    REQUIRE(scalar(p.materialsDb, "SELECT COUNT(*) FROM material_groups") == 9);
}

TEST_CASE("initLibrary empties the idle material staging folders", "[core][library][materials]") {
    plynth::test::TempDir tmp("lib-material-staging");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    plynth::test::writeBytes(p.materialStaging / "left-00000001" / "job.json", "{}");
    plynth::test::writeBytes(p.materialStaging / "busy-00000002" / "job.json", "{}");
    plynth::test::markFolderLive(p.materialStaging / "busy-00000002");
    plynth::test::requireOk(initLibrary(root));
    REQUIRE_FALSE(fs::exists(p.materialStaging / "left-00000001"));
    REQUIRE(fs::exists(p.materialStaging / "busy-00000002" / "job.json"));
}

namespace {

// Makes a folder of sample shape files.
fs::path makeSamples(const fs::path& root, std::initializer_list<const char*> names)
{
    const fs::path shapes = root / "shapes";
    for (const char* name : names) plynth::test::writeBytes(shapes / name, std::string("{\"id\":\"") + name + "\"}");
    return shapes;
}

// Sets an environment variable for one test and puts the old value back.
struct EnvVar {
    std::wstring name;
    std::wstring old;
    bool had = false;
    EnvVar(const std::string& n, const fs::path& value) : name(fs::path(n).wstring())
    {
        wchar_t* v = nullptr;
        size_t size = 0;
        if (_wdupenv_s(&v, &size, name.c_str()) == 0 && v) {
            had = true;
            old = v;
            free(v);
        }
        _wputenv_s(name.c_str(), value.c_str());
    }
    ~EnvVar() { _wputenv_s(name.c_str(), had ? old.c_str() : L""); }
};

}

TEST_CASE("addSampleShapes copies the shape files and skips the ones already there", "[core][library][shapes]") {
    plynth::test::TempDir tmp("lib-samples");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    const fs::path from = makeSamples(tmp.path() / "samples", {"b.shape.json", "a.shape.json", "notes.txt"});

    SampleShapesCopy first = addSampleShapes(p, from);
    plynth::test::requireOk(first.status);
    REQUIRE(first.copied == std::vector<std::string>{"a.shape.json", "b.shape.json"});
    REQUIRE(first.skipped.empty());
    REQUIRE(fs::exists(p.shapes / "a.shape.json"));
    REQUIRE_FALSE(fs::exists(p.shapes / "notes.txt"));

    plynth::test::writeBytes(p.shapes / "a.shape.json", "mine");
    SampleShapesCopy second = addSampleShapes(p, from);
    plynth::test::requireOk(second.status);
    REQUIRE(second.copied.empty());
    REQUIRE(second.skipped == std::vector<std::string>{"a.shape.json", "b.shape.json"});
    std::ifstream in(p.shapes / "a.shape.json");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(text == "mine");
}

TEST_CASE("addSampleShapes on a missing folder is not_found", "[core][library][shapes]") {
    plynth::test::TempDir tmp("lib-samples-missing");
    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const SampleShapesCopy r = addSampleShapes(libraryPaths(root), tmp.path() / "nothing");
    REQUIRE(r.status.code == ErrorCode::NotFound);
    REQUIRE(r.copied.empty());
}

TEST_CASE("initLibrary copies the sample shapes of PLYNTH_SAMPLES and keeps edits", "[core][library][shapes]") {
    plynth::test::TempDir tmp("lib-samples-init");
    makeSamples(tmp.path() / "samples", {"keyhole.shape.json"});
    EnvVar env(std::string(kSamplesVariable), tmp.path() / "samples");
    REQUIRE(sampleShapesFolder() == tmp.path() / "samples" / "shapes");

    const fs::path root = tmp.path() / "Lib";
    plynth::test::requireOk(initLibrary(root));
    const LibraryPaths p = libraryPaths(root);
    REQUIRE(fs::exists(p.shapes / "keyhole.shape.json"));

    plynth::test::writeBytes(p.shapes / "keyhole.shape.json", "edited");
    plynth::test::requireOk(initLibrary(root));
    std::ifstream in(p.shapes / "keyhole.shape.json");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(text == "edited");
}

TEST_CASE("sampleShapesFolder uses the source tree only in a developer build", "[core][library][shapes]") {
    const fs::path none;
    EnvVar env(std::string(kSamplesVariable), none);
    const fs::path folder = sampleShapesFolder();
#if PLYNTH_DEV_SAMPLES
    REQUIRE_FALSE(folder.empty());
    REQUIRE(fs::exists(folder / "keyhole.shape.json"));
#else
    REQUIRE(folder.empty());
#endif
}
