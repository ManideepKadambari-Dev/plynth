#include "plynth/core/Library.h"

#include "catalog/BuiltIn.h"
#include "catalog/Database.h"
#include "catalog/LibrarySetup.h"
#include "catalog/Staging.h"

#include "PlynthSql.h"

#include "plynth/core/ProjectFiles.h"

#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Thrown when a database was written by a newer Plynth.
struct NewerDatabase {};

// Makes one folder and everything above it.
Status makeFolder(const fs::path& folder)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec || !fs::is_directory(folder, ec)) {
        return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(folder) + " could not be created.");
    }
    return {};
}

// Creates one database file when it is empty and refuses a newer one. script makes the tables. The material
// lists are seeded whenever the groups table is empty, so a library made before they existed gets them too.
void ensureDatabase(const fs::path& file, int currentVersion, const char* script, bool seedAssets, bool seedMaterials)
{
    std::error_code ec;
    const bool exists = fs::is_regular_file(file, ec);
    auto db = catalog::Database::open(file, !exists);
    const int version = db->userVersion();
    if (version > currentVersion) {
        throw NewerDatabase{};
    }
    if (version == 0) {
        {
            catalog::Transaction tx(*db);
            db->exec(script);
            tx.commit();
        }
        if (seedAssets) catalog::seedBuiltIns(*db);
    }
    if (seedMaterials) catalog::seedMaterialBuiltIns(*db);
}

}

// The paths inside a library folder.
LibraryPaths libraryPaths(const fs::path& root)
{
    LibraryPaths p;
    p.root = root;
    p.assets = root / pathFromUtf8(kLibraryAssetsFolder);
    p.assetsDb = p.assets / pathFromUtf8(kAssetsDbName);
    p.assetItems = p.assets / pathFromUtf8(kLibraryItemsFolder);
    p.assetInbox = p.assets / pathFromUtf8(kLibraryInboxFolder);
    p.assetInboxDone = p.assetInbox / pathFromUtf8(kLibraryInboxDoneFolder);
    p.assetInboxFailed = p.assetInbox / pathFromUtf8(kLibraryInboxFailedFolder);
    p.assetStaging = p.assets / pathFromUtf8(kLibraryStagingFolder);
    p.materials = root / pathFromUtf8(kLibraryMaterialsFolder);
    p.materialsDb = p.materials / pathFromUtf8(kMaterialsDbName);
    p.materialItems = p.materials / pathFromUtf8(kLibraryItemsFolder);
    p.materialInbox = p.materials / pathFromUtf8(kLibraryInboxFolder);
    p.materialInboxDone = p.materialInbox / pathFromUtf8(kLibraryInboxDoneFolder);
    p.materialInboxFailed = p.materialInbox / pathFromUtf8(kLibraryInboxFailedFolder);
    p.materialStaging = p.materials / pathFromUtf8(kLibraryStagingFolder);
    p.generators = root / pathFromUtf8(kLibraryGeneratorsFolder);
    p.shapes = root / pathFromUtf8(kLibraryShapesFolder);
    return p;
}

// A library has an assets.db. A folder whose index was deleted but still has assets/items counts too, so a
// rescan can rebuild it.
bool isLibraryFolder(const fs::path& root)
{
    const LibraryPaths p = libraryPaths(root);
    std::error_code ec;
    return fs::is_regular_file(p.assetsDb, ec) || fs::is_directory(p.assetItems, ec);
}

// Makes the folders and databases of a library.
Status initLibrary(const fs::path& root)
{
    const LibraryPaths p = libraryPaths(root);
    if (Status s = makeLibraryFolders(p); !s.ok()) return s;

    staging::clearIdle(p.assetStaging);
    staging::clearIdle(p.materialStaging);
    return ensureLibraryDatabases(p);
}

// Makes the folders that are missing.
Status makeLibraryFolders(const LibraryPaths& p)
{
    for (const fs::path* folder : {&p.root, &p.assets, &p.assetItems, &p.assetInbox, &p.assetInboxDone, &p.assetInboxFailed,
                                   &p.assetStaging, &p.materials, &p.materialItems, &p.materialInbox, &p.materialInboxDone,
                                   &p.materialInboxFailed, &p.materialStaging, &p.generators, &p.shapes}) {
        if (Status s = makeFolder(*folder); !s.ok()) return s;
    }
    return {};
}

// Creates the databases that are missing.
Status ensureLibraryDatabases(const LibraryPaths& p)
{
    try {
        ensureDatabase(p.assetsDb, kAssetsDbVersion, kAssetsDbSql, true, false);
    } catch (const NewerDatabase&) {
        return fail(ErrorCode::UnsupportedVersion, "The assets database was written by a newer Plynth.");
    } catch (const catalog::DbError& e) {
        return fail(ErrorCode::DatabaseError, "The assets database could not be set up: " + std::string(e.what()));
    }
    try {
        ensureDatabase(p.materialsDb, kMaterialsDbVersion, kMaterialsDbSql, false, true);
    } catch (const NewerDatabase&) {
        return fail(ErrorCode::UnsupportedVersion, "The materials database was written by a newer Plynth.");
    } catch (const catalog::DbError& e) {
        return fail(ErrorCode::DatabaseError, "The materials database could not be set up: " + std::string(e.what()));
    }
    return {};
}

// The library to use for a command.
LibraryChoice chooseLibrary(const std::optional<fs::path>& option, const MachineSettings& settings)
{
    LibraryChoice choice;
    const std::optional<fs::path>& root = option ? option : settings.library;
    if (!root) {
        choice.status = fail(ErrorCode::NoLibrary,
                             "No library folder is set. Run \"plynth library init <folder> --use\" or choose one in the setup check.");
        return choice;
    }
    if (!isLibraryFolder(*root)) {
        const std::string folder = pathToUtf8(*root);
        choice.status = fail(ErrorCode::NoLibrary,
                             "The folder " + folder + " is not a library. Run \"plynth library init " + folder +
                                 "\" to make it one, or choose another folder in the setup check.");
        return choice;
    }
    choice.paths = libraryPaths(*root);
    return choice;
}

}
