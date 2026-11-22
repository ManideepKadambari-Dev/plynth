#include "plynth/core/catalog/MaterialCatalog.h"

#include "BuiltIn.h"
#include "Database.h"
#include "LibrarySetup.h"
#include "Staging.h"

#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <set>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;
using catalog::Database;
using catalog::DbError;
using catalog::Statement;
using catalog::Transaction;

namespace {

// Upgrade steps of materials.db, one per version. Empty now: version 1 is the first.
using MigrationStep = void (*)(Database&);
const std::vector<MigrationStep>& migrationSteps()
{
    static const std::vector<MigrationStep> steps;
    return steps;
}

// The write time of a file in whole seconds, or the epoch when it cannot be read.
SessionTime fileTime(const fs::path& file)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) return {};
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::time_point_cast<std::chrono::seconds>(system);
}

// Runs a function that uses the database and turns a database error into a status.
template <class F>
Status guarded(F&& f)
{
    try {
        return f();
    } catch (const DbError& e) {
        return fail(ErrorCode::DatabaseError, std::string("The materials database failed: ") + e.what());
    }
}

// Says whether a row exists for a query that takes one text parameter.
bool exists(Database& db, const char* sql, std::string_view value)
{
    Statement s(db, sql);
    s.bind(1, value);
    return s.step();
}

// The next sort number in a table with a sort column.
int nextSort(Database& db, const char* table)
{
    Statement s(db, std::string("SELECT COALESCE(MAX(sort) + 1, 0) FROM ") + table);
    return s.step() ? static_cast<int>(s.integer(0)) : 0;
}

// Joins words with spaces.
std::string joinWords(const std::vector<std::string>& words)
{
    std::string out;
    for (const std::string& w : words) {
        if (!out.empty()) out += ' ';
        out += w;
    }
    return out;
}

// Rebuilds the search row of one material from the tables.
void refreshSearch(Database& db, const std::string& id)
{
    {
        Statement del(db, "DELETE FROM material_search WHERE id = ?");
        del.bind(1, id);
        del.run();
    }
    std::string name;
    std::string sourceRef;
    {
        Statement s(db, "SELECT name, source_ref FROM materials WHERE id = ?");
        s.bind(1, id);
        if (!s.step()) return;
        name = s.text(0);
        sourceRef = s.text(1);
    }
    std::vector<std::string> tags;
    {
        Statement s(db, "SELECT tag FROM material_tags WHERE material = ? ORDER BY tag");
        s.bind(1, id);
        while (s.step()) tags.push_back(s.text(0));
    }
    std::vector<std::string> groupNames;
    {
        Statement s(db,
                    "SELECT g.name FROM material_group_members m JOIN material_groups g ON g.id = m.grp WHERE m.material = ? "
                    "ORDER BY g.sort");
        s.bind(1, id);
        while (s.step()) groupNames.push_back(s.text(0));
    }
    Statement ins(db, "INSERT INTO material_search (id, name, tags, groups, source) VALUES (?, ?, ?, ?, ?)");
    ins.bind(1, id).bind(2, name).bind(3, joinWords(tags)).bind(4, joinWords(groupNames)).bind(5, id + " " + sourceRef);
    ins.run();
}

// What the files say about a material that the index also keeps.
struct Measured {
    MaterialState state = MaterialState::Ready;
    std::string problem;
    ThumbnailState thumbnail = ThumbnailState::Missing;
};

// Compares the map files and thumb.png on disk with what info.json recorded.
Measured measureFolder(const fs::path& folder, const MaterialInfo& info)
{
    Measured m;
    std::error_code ec;
    for (const MapChannel channel : allMapChannels()) {
        const MaterialMap& map = materialMap(info.maps, channel);
        if (map.file.empty()) continue;
        if (!fs::is_regular_file(folder / pathFromUtf8(map.file), ec)) {
            m.state = MaterialState::Broken;
            m.problem = map.file + " is missing.";
            break;
        }
    }
    if (m.state == MaterialState::Ready) {
        const MaterialFiles now = measureMaterialFiles(folder, info.maps);
        if (now != info.files) {
            m.state = MaterialState::NeedsRefresh;
            m.problem = "The maps or the tile size changed since the last render.";
        }
    }
    if (info.thumbnail.rendered && fs::is_regular_file(folder / pathFromUtf8(kMaterialThumbFileName), ec)) {
        m.thumbnail = ThumbnailState::Ok;
    } else if (!info.thumbnail.error.empty()) {
        m.thumbnail = ThumbnailState::Failed;
    }
    return m;
}

// Adds a group the catalog does not have yet, named from its id.
void ensureGroup(Database& db, const std::string& id)
{
    if (exists(db, "SELECT 1 FROM material_groups WHERE id = ?", id)) return;
    Statement s(db, "INSERT INTO material_groups (id, name, sort, built_in) VALUES (?, ?, ?, 0)");
    s.bind(1, id).bind(2, catalogNameFromStem(id)).bind(3, nextSort(db, "material_groups"));
    s.run();
}

// Adds a license the catalog does not have yet, named from its id, so a rebuilt index never loses one.
void ensureLicense(Database& db, const std::string& id)
{
    if (exists(db, "SELECT 1 FROM licenses WHERE id = ?", id)) return;
    Statement s(db, "INSERT INTO licenses (id, name, url, redistributable, sort) VALUES (?, ?, '', 0, ?)");
    s.bind(1, id).bind(2, catalogNameFromStem(id)).bind(3, nextSort(db, "licenses"));
    s.run();
}

// Writes or replaces the index rows of one material.
void indexRows(Database& db, const MaterialInfo& info, const Measured& m, const std::string& infoModified)
{
    Transaction tx(db);
    for (const std::string& g : info.groups) ensureGroup(db, g);
    ensureLicense(db, info.license);
    {
        Statement s(db,
                    "INSERT INTO materials (id, name, license, source_kind, source_ref, imported_at, tile_width, "
                    "tile_height, state, problem, thumbnail, thumbnail_error, info_modified) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(id) DO UPDATE SET name = excluded.name, license = excluded.license, "
                    "source_kind = excluded.source_kind, source_ref = excluded.source_ref, "
                    "imported_at = excluded.imported_at, tile_width = excluded.tile_width, "
                    "tile_height = excluded.tile_height, state = excluded.state, problem = excluded.problem, "
                    "thumbnail = excluded.thumbnail, thumbnail_error = excluded.thumbnail_error, "
                    "info_modified = excluded.info_modified");
        s.bind(1, info.id).bind(2, info.name).bind(3, info.license).bind(4, info.source.kind)
            .bind(5, info.source.reference).bind(6, formatSessionTime(info.source.importedAt))
            .bind(7, info.tileSize.width).bind(8, info.tileSize.height).bind(9, materialStateName(m.state))
            .bind(10, m.problem).bind(11, thumbnailStateName(m.thumbnail)).bind(12, info.thumbnail.error)
            .bind(13, infoModified);
        s.run();
    }
    {
        Statement del(db, "DELETE FROM material_group_members WHERE material = ?");
        del.bind(1, info.id);
        del.run();
        Statement ins(db, "INSERT OR IGNORE INTO material_group_members (material, grp) VALUES (?, ?)");
        for (const std::string& g : info.groups) {
            ins.reset();
            ins.bind(1, info.id).bind(2, g);
            ins.run();
        }
    }
    {
        Statement del(db, "DELETE FROM material_tags WHERE material = ?");
        del.bind(1, info.id);
        del.run();
        Statement ins(db, "INSERT OR IGNORE INTO material_tags (material, tag) VALUES (?, ?)");
        for (const std::string& t : info.tags) {
            ins.reset();
            ins.bind(1, info.id).bind(2, t);
            ins.run();
        }
    }
    refreshSearch(db, info.id);
    tx.commit();
}

// Writes a row for a folder that cannot be read or has lost a map, so it still shows, as broken or as a folder
// that was never cleaned.
void indexProblem(Database& db, const std::string& id, const std::string& name, MaterialState state, const std::string& problem)
{
    Transaction tx(db);
    ensureLicense(db, std::string(kUnknownLicenseId));
    {
        Statement s(db,
                    "INSERT INTO materials (id, name, state, problem, thumbnail) VALUES (?, ?, ?, ?, 'missing') "
                    "ON CONFLICT(id) DO UPDATE SET name = excluded.name, state = excluded.state, "
                    "problem = excluded.problem, thumbnail = 'missing', thumbnail_error = '', info_modified = ''");
        s.bind(1, id).bind(2, name).bind(3, materialStateName(state)).bind(4, problem);
        s.run();
    }
    for (const char* sql : {"DELETE FROM material_group_members WHERE material = ?", "DELETE FROM material_tags WHERE material = ?"}) {
        Statement s(db, sql);
        s.bind(1, id);
        s.run();
    }
    refreshSearch(db, id);
    tx.commit();
}

// Deletes the rows of one material.
void deleteRows(Database& db, const std::string& id)
{
    Transaction tx(db);
    {
        Statement s(db, "DELETE FROM material_search WHERE id = ?");
        s.bind(1, id);
        s.run();
    }
    {
        Statement s(db, "DELETE FROM materials WHERE id = ?");
        s.bind(1, id);
        s.run();
    }
    tx.commit();
}

// Words of a search text: runs of letters and digits, including non ASCII bytes. Anything else separates words.
std::vector<std::string> searchWords(std::string_view text)
{
    std::vector<std::string> words;
    std::string current;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || u >= 0x80) {
            current.push_back(c);
        } else if (!current.empty()) {
            words.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) words.push_back(std::move(current));
    return words;
}

// Writes sort numbers 0, 1, 2 and so on in the given order of ids.
void renumber(Database& db, const char* table, const std::vector<std::string>& order)
{
    Statement s(db, std::string("UPDATE ") + table + " SET sort = ? WHERE id = ?");
    int sort = 0;
    for (const std::string& id : order) {
        s.reset();
        s.bind(1, sort++).bind(2, id);
        s.run();
    }
}

// The ids of a table in sort order.
std::vector<std::string> idsInOrder(Database& db, const char* table)
{
    std::vector<std::string> out;
    Statement s(db, std::string("SELECT id FROM ") + table + " ORDER BY sort, id");
    while (s.step()) out.push_back(s.text(0));
    return out;
}

// Says whether a file name is a picture that a texture set can hold.
bool isImageName(const fs::path& file)
{
    std::string ext = pathToUtf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* known : {".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".exr", ".webp", ".hdr"}) {
        if (ext == known) return true;
    }
    return false;
}

// Says whether a folder holds picture files, in it or below it.
bool holdsImages(const fs::path& folder)
{
    std::error_code ec;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && isImageName(it->path())) return true;
    }
    return false;
}

// The state of a name stored in the index.
MaterialState stateFromName(const std::string& state)
{
    return state == "broken" ? MaterialState::Broken : state == "needs_refresh" ? MaterialState::NeedsRefresh : MaterialState::Ready;
}

}

// The name used in files and answers.
std::string_view materialStateName(MaterialState state)
{
    switch (state) {
    case MaterialState::Ready: return "ready";
    case MaterialState::NeedsRefresh: return "needs_refresh";
    case MaterialState::Broken: return "broken";
    }
    return "ready";
}

struct MaterialCatalog::Impl {
    LibraryPaths paths;
    std::unique_ptr<Database> db;

    // One material from the index. Empty when there is no row.
    std::optional<MaterialRecord> record(const std::string& id) const
    {
        MaterialRecord r;
        {
            Statement s(*db,
                        "SELECT id, name, license, source_kind, source_ref, imported_at, tile_width, tile_height, state, "
                        "problem, thumbnail, thumbnail_error FROM materials WHERE id = ?");
            s.bind(1, id);
            if (!s.step()) return std::nullopt;
            r.id = s.text(0);
            r.name = s.text(1);
            r.license = s.text(2);
            r.sourceKind = s.text(3);
            r.sourceReference = s.text(4);
            r.importedAt = parseSessionTime(s.text(5)).value_or(SessionTime{});
            r.tileSize = {s.real(6), s.real(7)};
            r.state = stateFromName(s.text(8));
            r.problem = s.text(9);
            const std::string thumb = s.text(10);
            r.thumbnail = thumb == "ok" ? ThumbnailState::Ok : thumb == "failed" ? ThumbnailState::Failed : ThumbnailState::Missing;
            r.thumbnailError = s.text(11);
        }
        {
            Statement s(*db,
                        "SELECT m.grp FROM material_group_members m JOIN material_groups g ON g.id = m.grp "
                        "WHERE m.material = ? ORDER BY g.sort, g.id");
            s.bind(1, id);
            while (s.step()) r.groups.push_back(s.text(0));
        }
        {
            Statement s(*db, "SELECT tag FROM material_tags WHERE material = ? ORDER BY tag");
            s.bind(1, id);
            while (s.step()) r.tags.push_back(s.text(0));
        }
        r.folder = paths.materialItems / pathFromUtf8(id);
        if (r.thumbnail == ThumbnailState::Ok) r.thumbFile = r.folder / pathFromUtf8(kMaterialThumbFileName);
        return r;
    }

    // Indexes info and the current files of its folder.
    void index(const MaterialInfo& info) const
    {
        const fs::path folder = paths.materialItems / pathFromUtf8(info.id);
        const std::string modified = formatSessionTime(fileTime(folder / pathFromUtf8(kMaterialInfoFileName)));
        indexRows(*db, info, measureFolder(folder, info), modified);
    }
};

MaterialCatalog::MaterialCatalog(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

MaterialCatalog::~MaterialCatalog() = default;

// Opens the materials database of a library.
std::unique_ptr<MaterialCatalog> MaterialCatalog::open(const LibraryPaths& library, Status& status)
{
    status = {};
    if (!isLibraryFolder(library.root)) {
        status = fail(ErrorCode::NoLibrary, "The folder " + pathToUtf8(library.root) + " is not a library.");
        return nullptr;
    }
    std::error_code ec;
    if (!fs::is_regular_file(library.materialsDb, ec)) {
        if (status = makeLibraryFolders(library); !status.ok()) return nullptr;
        if (status = ensureLibraryDatabases(library); !status.ok()) return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->paths = library;
    try {
        impl->db = Database::open(library.materialsDb, false);
        int version = impl->db->userVersion();
        if (version > kMaterialsDbVersion) {
            status = fail(ErrorCode::UnsupportedVersion, "The materials database was written by a newer Plynth.");
            return nullptr;
        }
        if (version < 1) {
            status = fail(ErrorCode::DatabaseError, "The file " + pathToUtf8(library.materialsDb) + " is not a materials database.");
            return nullptr;
        }
        while (version < kMaterialsDbVersion) {
            const auto& steps = migrationSteps();
            if (static_cast<std::size_t>(version - 1) >= steps.size()) break;
            steps[static_cast<std::size_t>(version - 1)](*impl->db);
            ++version;
        }
        catalog::seedMaterialBuiltIns(*impl->db);
    } catch (const DbError& e) {
        status = fail(ErrorCode::DatabaseError, std::string("The materials database could not be opened: ") + e.what());
        return nullptr;
    }
    staging::cleanOld(library.materialStaging, std::chrono::hours(24));
    return std::unique_ptr<MaterialCatalog>(new MaterialCatalog(std::move(impl)));
}

// The library paths this catalog was opened with.
const LibraryPaths& MaterialCatalog::paths() const
{
    return impl_->paths;
}

// The groups with how many materials each holds.
std::vector<MaterialGroupEntry> MaterialCatalog::groups() const
{
    std::vector<MaterialGroupEntry> out;
    try {
        Statement s(*impl_->db,
                    "SELECT g.id, g.name, g.built_in, (SELECT COUNT(*) FROM material_group_members m WHERE m.grp = g.id) "
                    "FROM material_groups g ORDER BY g.sort, g.id");
        while (s.step()) out.push_back({s.text(0), s.text(1), s.integer(2) != 0, static_cast<int>(s.integer(3))});
    } catch (const DbError&) {
    }
    return out;
}

// Adds a group at the end.
Status MaterialCatalog::addGroup(std::string_view id, std::string_view name)
{
    if (Status s = checkCatalogId(id); !s.ok()) return s;
    if (Status s = checkCatalogName(name); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (exists(db, "SELECT 1 FROM material_groups WHERE id = ?", id)) {
            return fail(ErrorCode::AlreadyExists, "The group \"" + std::string(id) + "\" already exists.");
        }
        Statement s(db, "INSERT INTO material_groups (id, name, sort, built_in) VALUES (?, ?, ?, 0)");
        s.bind(1, id).bind(2, name).bind(3, nextSort(db, "material_groups"));
        s.run();
        tx.commit();
        return Status{};
    });
}

// Changes the name of a group and what search finds for the materials that have it.
Status MaterialCatalog::renameGroup(std::string_view id, std::string_view name)
{
    if (Status s = checkCatalogName(name); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM material_groups WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no group \"" + std::string(id) + "\".");
        }
        {
            Statement s(db, "UPDATE material_groups SET name = ? WHERE id = ?");
            s.bind(1, name).bind(2, id);
            s.run();
        }
        std::vector<std::string> members;
        {
            Statement s(db, "SELECT material FROM material_group_members WHERE grp = ?");
            s.bind(1, id);
            while (s.step()) members.push_back(s.text(0));
        }
        for (const std::string& m : members) refreshSearch(db, m);
        tx.commit();
        return Status{};
    });
}

// Removes a group from the catalog and from every material that has it.
Status MaterialCatalog::removeGroup(std::string_view id)
{
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM material_groups WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no group \"" + std::string(id) + "\".");
        }
        std::vector<std::string> members;
        {
            Statement s(db, "SELECT material FROM material_group_members WHERE grp = ?");
            s.bind(1, id);
            while (s.step()) members.push_back(s.text(0));
        }
        // Every info.json is rewritten before the index changes, so the files stay the truth.
        for (const std::string& m : members) {
            const fs::path file = impl_->paths.materialItems / pathFromUtf8(m) / pathFromUtf8(kMaterialInfoFileName);
            MaterialInfoRead read = readMaterialInfo(file, m);
            if (!read.status.ok()) continue;
            std::erase(read.info.groups, std::string(id));
            if (Status s = writeMaterialInfo(file, read.info); !s.ok()) return s;
        }
        {
            Statement s(db, "DELETE FROM material_groups WHERE id = ?");
            s.bind(1, id);
            s.run();
        }
        renumber(db, "material_groups", idsInOrder(db, "material_groups"));
        for (const std::string& m : members) {
            const fs::path file = impl_->paths.materialItems / pathFromUtf8(m) / pathFromUtf8(kMaterialInfoFileName);
            const MaterialInfoRead read = readMaterialInfo(file, m);
            if (read.status.ok()) impl_->index(read.info);
            else refreshSearch(db, m);
        }
        tx.commit();
        return Status{};
    });
}

// Moves a group to a place in the order.
Status MaterialCatalog::moveGroup(std::string_view id, int position)
{
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        std::vector<std::string> order = idsInOrder(db, "material_groups");
        const auto it = std::find(order.begin(), order.end(), id);
        if (it == order.end()) return fail(ErrorCode::NotFound, "There is no group \"" + std::string(id) + "\".");
        const std::string moved = *it;
        order.erase(it);
        const int place = std::clamp(position, 0, static_cast<int>(order.size()));
        order.insert(order.begin() + place, moved);
        renumber(db, "material_groups", order);
        tx.commit();
        return Status{};
    });
}

// The licenses in order.
std::vector<LicenseEntry> MaterialCatalog::licenses() const
{
    std::vector<LicenseEntry> out;
    try {
        Statement s(*impl_->db, "SELECT id, name, url, redistributable FROM licenses ORDER BY sort, id");
        while (s.step()) out.push_back({s.text(0), s.text(1), s.text(2), s.integer(3) != 0});
    } catch (const DbError&) {
    }
    return out;
}

// One indexed material.
std::optional<MaterialRecord> MaterialCatalog::material(std::string_view id) const
{
    try {
        return impl_->record(std::string(id));
    } catch (const DbError&) {
        return std::nullopt;
    }
}

// Reads info.json of an indexed material.
MaterialInfoRead MaterialCatalog::info(std::string_view id) const
{
    if (!material(id)) {
        MaterialInfoRead result;
        result.status = fail(ErrorCode::NotFound, "There is no material \"" + std::string(id) + "\".");
        return result;
    }
    return readMaterialInfo(impl_->paths.materialItems / pathFromUtf8(id) / pathFromUtf8(kMaterialInfoFileName), id);
}

// Materials that match a query.
std::vector<MaterialRecord> MaterialCatalog::search(const MaterialQuery& query) const
{
    std::vector<MaterialRecord> out;
    try {
        std::string sql = "SELECT m.id FROM materials m WHERE 1 = 1";
        std::vector<std::string> binds;
        const std::vector<std::string> words = searchWords(query.text);
        if (!words.empty()) {
            std::string match;
            for (const std::string& w : words) {
                if (!match.empty()) match += ' ';
                match += "\"" + w + "\"*";
            }
            sql += " AND m.id IN (SELECT id FROM material_search WHERE material_search MATCH ?)";
            binds.push_back(match);
        }
        if (query.group) {
            sql += " AND m.id IN (SELECT material FROM material_group_members WHERE grp = ?)";
            binds.push_back(*query.group);
        }
        for (const std::string& tag : query.tags) {
            sql += " AND m.id IN (SELECT material FROM material_tags WHERE tag = ?)";
            binds.push_back(tag);
        }
        if (query.license) {
            sql += " AND m.license = ?";
            binds.push_back(*query.license);
        }
        if (query.uncategorizedOnly) sql += " AND NOT EXISTS (SELECT 1 FROM material_group_members g WHERE g.material = m.id)";
        sql += " ORDER BY m.name COLLATE NOCASE, m.id";
        if (query.limit > 0) sql += " LIMIT " + std::to_string(query.limit);

        std::vector<std::string> ids;
        {
            Statement s(*impl_->db, sql);
            for (std::size_t i = 0; i < binds.size(); ++i) s.bind(static_cast<int>(i) + 1, binds[i]);
            while (s.step()) ids.push_back(s.text(0));
        }
        for (const std::string& id : ids) {
            if (auto r = impl_->record(id)) out.push_back(std::move(*r));
        }
    } catch (const DbError&) {
        out.clear();
    }
    return out;
}

// How many materials are indexed.
int MaterialCatalog::materialCount() const
{
    try {
        Statement s(*impl_->db, "SELECT COUNT(*) FROM materials");
        return s.step() ? static_cast<int>(s.integer(0)) : 0;
    } catch (const DbError&) {
        return 0;
    }
}

// Says whether an id is in use by a material, an item folder or a running import.
bool MaterialCatalog::idTaken(std::string_view id) const
{
    try {
        if (exists(*impl_->db, "SELECT 1 FROM materials WHERE id = ?", id)) return true;
    } catch (const DbError&) {
    }
    std::error_code ec;
    if (fs::exists(impl_->paths.materialItems / pathFromUtf8(id), ec)) return true;
    if (staging::isLive(impl_->paths.materialStaging / pathFromUtf8(id))) return true;
    const std::string prefix = std::string(id) + "-";
    for (const auto& entry : fs::directory_iterator(impl_->paths.materialStaging, ec)) {
        if (pathToUtf8(entry.path().filename()).rfind(prefix, 0) == 0 && staging::isLive(entry.path())) return true;
    }
    return false;
}

// Changes a material.
Status MaterialCatalog::editMaterial(std::string_view id, const MaterialEdit& edit)
{
    if (edit.name) {
        if (Status s = checkCatalogName(*edit.name); !s.ok()) return s;
    }
    if (edit.tags) {
        for (const std::string& t : *edit.tags) {
            if (Status s = checkTag(t); !s.ok()) return s;
        }
    }
    if (edit.tileSize) {
        if (Status s = checkTileSize(*edit.tileSize); !s.ok()) return s;
    }
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM materials WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no material \"" + std::string(id) + "\".");
        }
        if (edit.groups) {
            for (const std::string& g : *edit.groups) {
                if (!exists(db, "SELECT 1 FROM material_groups WHERE id = ?", g)) {
                    return fail(ErrorCode::NotFound, "There is no group \"" + g + "\".");
                }
            }
        }
        if (edit.license && !exists(db, "SELECT 1 FROM licenses WHERE id = ?", *edit.license)) {
            return fail(ErrorCode::NotFound, "There is no license \"" + *edit.license + "\".");
        }
        const fs::path file = impl_->paths.materialItems / pathFromUtf8(id) / pathFromUtf8(kMaterialInfoFileName);
        MaterialInfoRead read = readMaterialInfo(file, id);
        if (!read.status.ok()) return read.status;

        MaterialInfo& info = read.info;
        if (edit.name) info.name = *edit.name;
        if (edit.groups) {
            info.groups.clear();
            for (const std::string& g : *edit.groups) {
                if (std::find(info.groups.begin(), info.groups.end(), g) == info.groups.end()) info.groups.push_back(g);
            }
        }
        if (edit.tags) {
            info.tags = *edit.tags;
            std::sort(info.tags.begin(), info.tags.end());
            info.tags.erase(std::unique(info.tags.begin(), info.tags.end()), info.tags.end());
        }
        if (edit.license) info.license = *edit.license;
        if (edit.tileSize && !(*edit.tileSize == info.tileSize)) {
            info.tileSize = *edit.tileSize;
            // No map file can add up to zero bytes, so the record no longer matches the files: the material
            // stays NeedsRefresh, also after a rescan, until a refresh measures the maps again.
            info.files = {};
        }

        if (Status s = writeMaterialInfo(file, info); !s.ok()) return s;
        impl_->index(info);
        tx.commit();
        return Status{};
    });
}

// Indexes a material folder that holds info.json.
Status MaterialCatalog::indexMaterial(const MaterialInfo& info)
{
    if (Status s = checkCatalogId(info.id); !s.ok()) return s;
    std::error_code ec;
    const fs::path file = impl_->paths.materialItems / pathFromUtf8(info.id) / pathFromUtf8(kMaterialInfoFileName);
    if (!fs::is_regular_file(file, ec)) {
        return fail(ErrorCode::NotFound, "The folder for \"" + info.id + "\" has no info.json.");
    }
    return guarded([&] {
        impl_->index(info);
        return Status{};
    });
}

// Deletes a material folder and its rows.
Status MaterialCatalog::removeMaterial(std::string_view id)
{
    if (Status s = checkCatalogId(id); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        const fs::path folder = impl_->paths.materialItems / pathFromUtf8(id);
        std::error_code ec;
        if (folder.parent_path() != impl_->paths.materialItems) {
            return fail(ErrorCode::InvalidValue, "The id is not a material folder.");
        }
        const bool indexed = exists(db, "SELECT 1 FROM materials WHERE id = ?", id);
        const bool onDisk = fs::is_directory(folder, ec);
        if (!indexed && !onDisk) return fail(ErrorCode::NotFound, "There is no material \"" + std::string(id) + "\".");
        if (onDisk) {
            fs::remove_all(folder, ec);
            if (ec) return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(folder) + " could not be removed.");
        }
        deleteRows(db, std::string(id));
        return Status{};
    });
}

// Rebuilds the material rows from the item folders.
RescanReport MaterialCatalog::rescan()
{
    RescanReport report;
    try {
        Database& db = *impl_->db;
        struct Row {
            std::string infoModified;
            MaterialState state;
        };
        std::map<std::string, Row> rows;
        {
            Statement s(db, "SELECT id, info_modified, state FROM materials");
            while (s.step()) rows[s.text(0)] = {s.text(1), stateFromName(s.text(2))};
        }

        Transaction tx(db);
        std::set<std::string> seen;
        std::error_code ec;
        std::vector<fs::path> folders;
        for (const auto& entry : fs::directory_iterator(impl_->paths.materialItems, ec)) {
            if (entry.is_directory(ec)) folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end());

        for (const fs::path& folder : folders) {
            const std::string name = pathToUtf8(folder.filename());
            if (name.empty() || name.front() == '.') continue;
            seen.insert(name);
            const fs::path infoFile = folder / pathFromUtf8(kMaterialInfoFileName);
            const bool hasInfo = fs::is_regular_file(infoFile, ec);
            const auto existing = rows.find(name);

            if (Status idStatus = checkCatalogId(name); !idStatus.ok()) {
                report.broken.emplace_back(name, fail(ErrorCode::BadFile, "The folder name is not a valid material id: " + idStatus.message));
                continue;
            }

            if (!hasInfo) {
                if (!holdsImages(folder)) {
                    const std::string problem = "The folder has no info.json and no images.";
                    report.broken.emplace_back(name, fail(ErrorCode::NotFound, problem));
                    indexProblem(db, name, name, MaterialState::Broken, problem);
                    continue;
                }
                const bool same = existing != rows.end() && existing->second.state == MaterialState::NeedsRefresh &&
                                  existing->second.infoModified.empty();
                if (existing == rows.end()) ++report.added;
                else if (!same) ++report.updated;
                else ++report.unchanged;
                if (!same) indexProblem(db, name, catalogNameFromStem(name), MaterialState::NeedsRefresh, "Not cleaned yet.");
                report.needsRefresh.push_back(name);
                continue;
            }

            MaterialInfoRead read = readMaterialInfo(infoFile, name);
            if (!read.status.ok()) {
                report.broken.emplace_back(name, read.status);
                indexProblem(db, name, name, MaterialState::Broken, read.status.message);
                continue;
            }
            const MaterialInfo& info = read.info;
            const Measured measured = measureFolder(folder, info);
            const std::string modified = formatSessionTime(fileTime(infoFile));
            if (existing == rows.end()) {
                ++report.added;
                indexRows(db, info, measured, modified);
            } else if (existing->second.infoModified != modified || existing->second.state != measured.state) {
                ++report.updated;
                indexRows(db, info, measured, modified);
            } else {
                ++report.unchanged;
            }
            if (measured.state == MaterialState::NeedsRefresh) report.needsRefresh.push_back(name);
            if (measured.state == MaterialState::Broken) report.broken.emplace_back(name, fail(ErrorCode::NotFound, measured.problem));
        }

        for (const auto& [id, row] : rows) {
            if (seen.count(id)) continue;
            deleteRows(db, id);
            ++report.removed;
        }
        tx.commit();

        for (const auto& entry : fs::directory_iterator(impl_->paths.materialInbox, ec)) {
            const std::string name = pathToUtf8(entry.path().filename());
            if (name.empty() || name.front() == '.' || name.front() == '~') continue;
            if (name == kLibraryInboxDoneFolder || name == kLibraryInboxFailedFolder) continue;
            ++report.inboxWaiting;
        }
    } catch (const DbError& e) {
        report.broken.emplace_back("", fail(ErrorCode::DatabaseError, std::string("The materials database failed: ") + e.what()));
    }
    return report;
}

// Says whether a material can be used on a surface.
MaterialForUse materialForUse(const MaterialCatalog& catalog, std::string_view materialId)
{
    MaterialForUse result;
    const std::optional<MaterialRecord> record = catalog.material(materialId);
    if (!record) {
        result.status = fail(ErrorCode::NotFound, "There is no material " + std::string(materialId) + " in the library.");
        return result;
    }
    result.name = record->name;
    if (record->state == MaterialState::Broken) {
        std::string message = "The material " + std::string(materialId) + " is broken";
        message += record->problem.empty() ? "." : ": " + record->problem;
        result.status = fail(ErrorCode::InvalidValue, message);
    }
    return result;
}

}
