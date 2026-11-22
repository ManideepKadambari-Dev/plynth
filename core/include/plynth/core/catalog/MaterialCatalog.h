#pragma once

#include "../Library.h"
#include "../Status.h"
#include "AssetCatalog.h"
#include "MaterialInfo.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// The materials catalog (DESIGN.md section 7.11): groups, then materials, all user editable, indexed in
// Library/materials/materials.db (SQLite, schema/materials-db.sql, version 1 unchanged). It shares nothing
// with the assets catalog: its own groups, its own license table (seeded with the same built-in list), its
// own search. Edits are plain calls, not commands, as for assets: info.json first, the index second.
// One MaterialCatalog is one database connection used from one thread, in WAL mode with a 5 second busy
// timeout, like AssetCatalog. The maps are not in the index; info() reads them from info.json.

struct MaterialGroupEntry {
    std::string id;   // "floor"
    std::string name; // "Floor"
    bool builtIn = false;
    int materialCount = 0;

    bool operator==(const MaterialGroupEntry&) const = default;
};

// The groups a new library starts with, in order: floor, wall, ceiling, exterior, trim, door, glass, metal.
// The licenses are builtInLicenses() of AssetCatalog.h.
std::span<const MaterialGroupEntry> builtInMaterialGroups();

enum class MaterialState {
    Ready,        // "ready"
    NeedsRefresh, // "needs_refresh": a map changed, the tile size was edited, or the folder was never cleaned
    Broken,       // "broken": info.json or a map file is missing or bad; problem says which
};

std::string_view materialStateName(MaterialState state);

// One indexed material, as search returns it.
struct MaterialRecord {
    std::string id;
    std::string name;
    std::vector<std::string> groups;
    std::vector<std::string> tags;
    std::string license;
    std::string sourceKind;
    std::string sourceReference;
    SessionTime importedAt{};
    TileSize tileSize;
    MaterialState state = MaterialState::Ready;
    std::string problem;
    ThumbnailState thumbnail = ThumbnailState::Missing;
    std::string thumbnailError;
    std::filesystem::path folder;    // items/<id>
    std::filesystem::path thumbFile; // items/<id>/thumb.png, set when thumbnail is Ok

    bool uncategorized() const { return groups.empty(); }
    bool operator==(const MaterialRecord&) const = default;
};

// A search, with the rules of AssetQuery: every given filter must match; text words match the start of a
// word in the name, id, tags, group names or source reference.
struct MaterialQuery {
    std::string text;
    std::optional<std::string> group;
    std::vector<std::string> tags;
    std::optional<std::string> license;
    bool uncategorizedOnly = false;
    int limit = 0;
};

// Changes to one material. Unset fields stay as they are. A new tile size makes the material NeedsRefresh,
// because its thumbnail shows the tile at real world size.
struct MaterialEdit {
    std::optional<std::string> name;
    std::optional<std::vector<std::string>> groups;
    std::optional<std::vector<std::string>> tags;
    std::optional<std::string> license;
    std::optional<TileSize> tileSize;
};

class MaterialCatalog {
public:
    // Opens materials.db of a library made with initLibrary, with the refusals of AssetCatalog::open. An
    // empty groups table (a library made before milestone 9) is seeded with builtInMaterialGroups first.
    static std::unique_ptr<MaterialCatalog> open(const LibraryPaths& library, Status& status);

    ~MaterialCatalog();
    MaterialCatalog(const MaterialCatalog&) = delete;
    MaterialCatalog& operator=(const MaterialCatalog&) = delete;

    const LibraryPaths& paths() const;

    // Groups in order, with how many materials each holds.
    std::vector<MaterialGroupEntry> groups() const;
    // InvalidValue for a bad id or name, AlreadyExists for a taken id. New ones go last.
    Status addGroup(std::string_view id, std::string_view name);
    // The eight built-in groups are protected (owner, milestone 10): renaming or removing one is InvalidValue
    // "The built-in group <name> cannot be renamed." (or "removed."), and nothing changes. Moving them is
    // allowed.
    Status renameGroup(std::string_view id, std::string_view name);
    // Takes the group off every material that had it (rewriting their info.json). NotFound when unknown,
    // InvalidValue for a built-in group.
    Status removeGroup(std::string_view id);
    // Moves a group to a place in the order, 0 based, clamped to the list.
    Status moveGroup(std::string_view id, int position);

    std::vector<LicenseEntry> licenses() const;

    // One material. Empty when it is not indexed.
    std::optional<MaterialRecord> material(std::string_view id) const;
    // Reads the material's info.json, with its maps. NotFound when it is not indexed.
    MaterialInfoRead info(std::string_view id) const;
    // Materials matching a query, sorted by name and then id.
    std::vector<MaterialRecord> search(const MaterialQuery& query) const;
    int materialCount() const;
    // Says whether an id is taken by an indexed material or a folder in items/ or .staging/.
    bool idTaken(std::string_view id) const;

    // Changes a material, with the refusals of AssetCatalog::editAsset, and InvalidValue from checkTileSize.
    Status editMaterial(std::string_view id, const MaterialEdit& edit);
    // Indexes the folder items/<info.id>, which must already hold info.json. Replaces an existing row. Groups
    // named in info that the catalog lacks are added, named with catalogNameFromStem.
    Status indexMaterial(const MaterialInfo& info);
    // Deletes items/<id> and its row. NotFound when it is not there. Cannot be undone; the CLI asks for --yes
    // and the app asks first. Plans that use the id keep it, and the build reports it as missing_material.
    Status removeMaterial(std::string_view id);

    // Rebuilds the rows from items/ (DESIGN.md section 7.11 "Rescanning"): folders starting with "." are
    // skipped; a folder with a valid info.json whose map files differ from info.files is NeedsRefresh and
    // listed in needsRefresh; a missing map file makes it Broken; a folder without info.json that holds image
    // files is indexed as NeedsRefresh (name from the folder, uncategorized, problem "Not cleaned yet.") and
    // listed in needsRefresh, so refreshMaterial cleans the images in it; any other folder is Broken; rows
    // without a folder are removed. inboxWaiting counts materialInboxEntries. Runs no Blender, deletes nothing.
    RescanReport rescan();

private:
    struct Impl;
    explicit MaterialCatalog(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// A material that can be used on a surface, or why not, for the app's picker and "plynth material set"
// (DESIGN.md section 7.18: commands validate before changing anything). NotFound "There is no material
// oak_planks in the library.", InvalidValue when it is Broken (the message gives its problem). A material that
// NeedsRefresh can be used.
struct MaterialForUse {
    Status status;
    std::string name;
};

MaterialForUse materialForUse(const MaterialCatalog& catalog, std::string_view materialId);

}
