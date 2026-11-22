#pragma once

#include "../Library.h"
#include "../RoomTypes.h"
#include "../Status.h"
#include "AssetInfo.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

// The assets catalog (DESIGN.md section 7.11): room types, then categories, then assets, all user editable,
// indexed in Library/assets/assets.db (SQLite, schema/assets-db.sql). Catalog edits are not plan changes,
// so they are plain calls, not commands, and have no undo. Each edit that touches an asset writes its
// info.json first and the index second, so the files on disk stay the truth.
//
// One AssetCatalog is one database connection and is used from one thread. The app opens one on the main
// thread for browsing and a separate one on each import thread. The database runs in WAL mode with a busy
// timeout of 5 seconds, so the app and the CLI can use the same library at the same time.

struct CategoryEntry {
    std::string id;   // "sofa"
    std::string name; // "Sofa"
    bool builtIn = false;
    int assetCount = 0;

    bool operator==(const CategoryEntry&) const = default;
};

struct RoomTypeEntry {
    std::string id;
    std::string name;
    bool builtIn = false;
    std::vector<std::string> categories; // the category ids shown when placing items in this room type

    bool operator==(const RoomTypeEntry&) const = default;
};

struct LicenseEntry {
    std::string id;   // "cc0"
    std::string name; // "CC0 (public domain)"
    std::string url;
    bool redistributable = false; // may be copied into shared packs

    bool operator==(const LicenseEntry&) const = default;
};

// The lists a new library starts with. Categories, in order: sofa, armchair, chair, table, desk, bed,
// wardrobe, cabinet, shelf, tv, lamp, carpet, curtain, plant, decor, mirror, kitchen_unit, appliance, bath,
// shower, toilet, sink, door, window. Licenses: unknown, cc0, cc_by_4, royalty_free, personal_use, own_work.
// Room type to category links:
//   living: sofa, armchair, table, tv, shelf, lamp, carpet, curtain, plant, decor
//   bedroom: bed, wardrobe, desk, chair, lamp, carpet, curtain, mirror, decor
//   kitchen: kitchen_unit, appliance, table, chair, lamp
//   bathroom: bath, shower, toilet, sink, mirror, cabinet
//   hall: cabinet, shelf, mirror, lamp, carpet
//   dining: table, chair, cabinet, lamp, carpet, decor
//   office: desk, chair, shelf, cabinet, lamp
//   storage: shelf, cabinet
//   other: none
// door and window are in no room type: they are for opening frames.
std::span<const CategoryEntry> builtInCategories();
std::span<const LicenseEntry> builtInLicenses();
std::vector<std::pair<std::string, std::string>> builtInRoomTypeCategories(); // (room type, category)

enum class AssetState {
    Ready,        // "ready"
    NeedsRefresh, // "needs_refresh": asset.blend changed since it was measured
    Broken,       // "broken": info.json or asset.blend is missing or bad; problem says which
};

enum class ThumbnailState {
    Ok,      // "ok": thumb.png is there
    Failed,  // "failed": the last render failed; show "Render failed" and a retry button, never a placeholder
    Missing, // "missing": not rendered yet
};

std::string_view assetStateName(AssetState state);
std::string_view thumbnailStateName(ThumbnailState state);

// One indexed asset, as search returns it.
struct AssetRecord {
    std::string id;
    std::string name;
    std::vector<std::string> categories;
    std::vector<std::string> tags;
    std::string license;
    std::string sourceKind;
    std::string sourceReference;
    SessionTime importedAt{};
    AssetSize size;
    long long triangles = 0;
    AssetState state = AssetState::Ready;
    std::string problem;
    ThumbnailState thumbnail = ThumbnailState::Missing;
    std::string thumbnailError;
    std::filesystem::path folder;    // items/<id>
    std::filesystem::path thumbFile; // items/<id>/thumb.png, set when thumbnail is Ok
    std::filesystem::path fbxFile;   // items/<id>/asset.fbx, set when that file exists

    bool uncategorized() const { return categories.empty(); }
    bool operator==(const AssetRecord&) const = default;
};

// A search. Every given filter must match. text is split into words; each word must be the start of a word
// in the name, id, tags, category names or source reference, ignoring case and accents (FTS5 prefix query).
struct AssetQuery {
    std::string text;
    std::optional<std::string> roomType;  // assets in any category linked to this room type
    std::optional<std::string> category;
    std::vector<std::string> tags;        // all of them
    std::optional<std::string> license;
    bool uncategorizedOnly = false;
    int limit = 0;                        // 0: no limit
};

// Changes to one asset. Unset fields stay as they are.
struct AssetEdit {
    std::optional<std::string> name;
    std::optional<std::vector<std::string>> categories;
    std::optional<std::vector<std::string>> tags;
    std::optional<std::string> license;
};

// What a rescan found and did.
struct RescanReport {
    int added = 0;     // folders in items/ that were not indexed
    int updated = 0;   // indexed assets whose info.json changed
    int removed = 0;   // index rows whose folder is gone
    int unchanged = 0;
    std::vector<std::string> needsRefresh; // ids whose asset.blend changed: run refreshAsset on each
    std::vector<std::pair<std::string, Status>> broken; // folder name and why
    int inboxWaiting = 0; // entries in inbox/ waiting for import
};

class AssetCatalog {
public:
    // Opens assets.db of a library made with initLibrary. NoLibrary when the folder is not a library,
    // UnsupportedVersion when the database is newer, DatabaseError when SQLite refuses. A database at an older
    // version is upgraded. Null with status filled on error.
    static std::unique_ptr<AssetCatalog> open(const LibraryPaths& library, Status& status);

    ~AssetCatalog();
    AssetCatalog(const AssetCatalog&) = delete;
    AssetCatalog& operator=(const AssetCatalog&) = delete;

    const LibraryPaths& paths() const;

    // Room types in picker order. roomTypeList is the same list in the form useRoomTypes takes.
    std::vector<RoomTypeEntry> roomTypes() const;
    std::vector<RoomType> roomTypeList() const;
    // InvalidValue for a bad id or name, AlreadyExists for a taken id. New ones go last.
    Status addRoomType(std::string_view id, std::string_view name);
    Status renameRoomType(std::string_view id, std::string_view name);
    // Plans that use the id keep it; roomTypeName then shows the id. NotFound when there is none.
    Status removeRoomType(std::string_view id);
    // Replaces the categories linked to a room type. NotFound for an unknown room type or category.
    Status setRoomTypeCategories(std::string_view id, const std::vector<std::string>& categories);
    // Moves a room type to a place in the picker order, 0 based, clamped to the list.
    Status moveRoomType(std::string_view id, int position);

    // Categories in order, with how many assets each holds.
    std::vector<CategoryEntry> categories() const;
    // The categories linked to a room type, in category order. Empty for an unknown room type.
    std::vector<CategoryEntry> categoriesFor(std::string_view roomType) const;
    Status addCategory(std::string_view id, std::string_view name);
    Status renameCategory(std::string_view id, std::string_view name);
    // Takes the category off every asset that had it (rewriting their info.json) and off every room type.
    Status removeCategory(std::string_view id);

    std::vector<LicenseEntry> licenses() const;

    // One asset. Empty when it is not indexed.
    std::optional<AssetRecord> asset(std::string_view id) const;
    // Assets matching a query, sorted by name and then id.
    std::vector<AssetRecord> search(const AssetQuery& query) const;
    int assetCount() const;
    // Says whether an id is taken by an indexed asset or a folder in items/ or .staging/.
    bool idTaken(std::string_view id) const;

    // Changes an asset. InvalidValue for a bad name or tag, NotFound for an unknown asset, category or
    // license. Tags are sorted and repeats dropped. Writes info.json, then the index.
    Status editAsset(std::string_view id, const AssetEdit& edit);
    // Indexes the folder items/<info.id>, which must already hold info.json. Replaces an existing row.
    // Categories named in info that the catalog lacks are added, named with catalogNameFromStem.
    Status indexAsset(const AssetInfo& info);
    // Deletes items/<id> and its index row. NotFound when it is not there. Cannot be undone; the CLI asks for
    // --yes and the app asks first.
    Status removeAsset(std::string_view id);

    // Rebuilds the asset rows from items/ (DESIGN.md section 7.11 "Rescanning"): folders starting with "."
    // are skipped; a folder with asset.blend but no info.json gets a new info.json (uncategorized, measured
    // later, so it is listed in needsRefresh); a folder whose asset.blend size or write time differs from
    // info.json is NeedsRefresh; a folder with asset.blend but no asset.fbx is listed in needsRefresh and keeps
    // its state, so refreshAsset writes the preview file; a folder whose info.json has no material maps
    // (hasMaterialMaps false) is listed in needsRefresh the same way, so every asset gets its maps once; rows
    // without a folder are removed. Runs no Blender and never deletes files.
    RescanReport rescan();

    // Opens the catalog and hands its room types to useRoomTypes. Used by the app when the library opens and
    // by CLI commands that read or set room types. On any error the built-in list stays in use and the
    // status says why.
    static Status useLibraryRoomTypes(const LibraryPaths& library);

private:
    struct Impl;
    explicit AssetCatalog(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}
