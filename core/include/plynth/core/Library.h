#pragma once

#include "Settings.h"
#include "Status.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace plynth {

// The library, one per machine (DESIGN.md section 5). Two catalogs that share nothing:
//   Library/
//     assets/
//       assets.db        index: room types, categories, tags, licenses, search (schema/assets-db.sql)
//       items/<id>/      asset.blend, textures/, thumb.png, info.json (schema/asset-info.schema.json)
//       inbox/           model files, zips or folders to import
//         done/          inbox entries that were imported and are kept (see AssetImport.h)
//         failed/        inbox entries that could not be imported, each with <name>.error.txt
//       .staging/        work folders of running imports; never indexed, emptied by initLibrary
//     materials/
//       materials.db     index: material groups, tags, licenses, search (schema/materials-db.sql)
//       items/<id>/      textures/, thumb.png, info.json (catalog/MaterialInfo.h)
//       inbox/           texture folders, zips or .blend files to import
//         done/, failed/ as for assets (see catalog/MaterialImport.h)
//       .staging/        work folders of running material imports and refreshes, emptied by initLibrary
//     generators/        milestone 14
//     shapes/            custom opening outlines, read with registerShapeFolder
// The files in items/ are the truth. The .db files are indexes that rescan rebuilds, except for the user's
// room type and category lists, which live only in assets.db and are recreated from the built-in lists and
// the categories named in info.json files when assets.db is lost.

inline constexpr std::string_view kLibraryAssetsFolder = "assets";
inline constexpr std::string_view kLibraryMaterialsFolder = "materials";
inline constexpr std::string_view kLibraryItemsFolder = "items";
inline constexpr std::string_view kLibraryInboxFolder = "inbox";
inline constexpr std::string_view kLibraryInboxDoneFolder = "done";
inline constexpr std::string_view kLibraryInboxFailedFolder = "failed";
inline constexpr std::string_view kLibraryStagingFolder = ".staging";
inline constexpr std::string_view kLibraryGeneratorsFolder = "generators";
inline constexpr std::string_view kLibraryShapesFolder = "shapes";
inline constexpr std::string_view kAssetsDbName = "assets.db";
inline constexpr std::string_view kMaterialsDbName = "materials.db";

// PRAGMA user_version of each database. A newer number is refused with UnsupportedVersion; an older one is
// upgraded in place by a migration step when the version changes.
inline constexpr int kAssetsDbVersion = 1;
inline constexpr int kMaterialsDbVersion = 1;

struct LibraryPaths {
    std::filesystem::path root;
    std::filesystem::path assets;
    std::filesystem::path assetsDb;
    std::filesystem::path assetItems;
    std::filesystem::path assetInbox;
    std::filesystem::path assetInboxDone;
    std::filesystem::path assetInboxFailed;
    std::filesystem::path assetStaging;
    std::filesystem::path materials;
    std::filesystem::path materialsDb;
    std::filesystem::path materialItems;
    std::filesystem::path materialInbox;
    std::filesystem::path materialInboxDone;
    std::filesystem::path materialInboxFailed;
    std::filesystem::path materialStaging;
    std::filesystem::path generators;
    std::filesystem::path shapes;
};

// The paths of a library folder. Touches nothing on disk.
LibraryPaths libraryPaths(const std::filesystem::path& root);

// Says whether a folder holds a library, which means it has assets/assets.db.
bool isLibraryFolder(const std::filesystem::path& root);

// Makes a folder into a library: creates every missing folder above and both databases with their tables,
// seeds assets.db with the built-in room types, categories and licenses and materials.db with the built-in
// material groups and licenses (also into an existing, empty materials.db), and empties both .staging
// folders. Never removes
// or changes anything else, so it is safe on an existing library. CannotWrite when a folder or file cannot
// be made; DatabaseError or UnsupportedVersion from the databases.
Status initLibrary(const std::filesystem::path& root);

struct LibraryChoice {
    Status status; // NoLibrary when none is set or the folder is not a library, with a sentence naming
                   // "plynth library init" and the setup check
    LibraryPaths paths;
};

// The library a command uses: the --library option when given, otherwise the settings. Does not create it.
LibraryChoice chooseLibrary(const std::optional<std::filesystem::path>& option, const MachineSettings& settings);

}
