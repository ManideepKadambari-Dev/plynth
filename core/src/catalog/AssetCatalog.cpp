#include "plynth/core/catalog/AssetCatalog.h"

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

// Upgrade steps of assets.db, one per version. Step n moves a database from version n + 1 to n + 2. Empty now:
// version 1 is the first.
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
        return fail(ErrorCode::DatabaseError, std::string("The assets database failed: ") + e.what());
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

// Rebuilds the search row of one asset from the tables.
void refreshSearch(Database& db, const std::string& id)
{
    {
        Statement del(db, "DELETE FROM asset_search WHERE id = ?");
        del.bind(1, id);
        del.run();
    }
    std::string name;
    std::string sourceRef;
    {
        Statement s(db, "SELECT name, source_ref FROM assets WHERE id = ?");
        s.bind(1, id);
        if (!s.step()) return;
        name = s.text(0);
        sourceRef = s.text(1);
    }
    std::vector<std::string> tags;
    {
        Statement s(db, "SELECT tag FROM asset_tags WHERE asset = ? ORDER BY tag");
        s.bind(1, id);
        while (s.step()) tags.push_back(s.text(0));
    }
    std::vector<std::string> categoryNames;
    {
        Statement s(db,
                    "SELECT c.name FROM asset_categories ac JOIN categories c ON c.id = ac.category WHERE ac.asset = ? "
                    "ORDER BY c.sort");
        s.bind(1, id);
        while (s.step()) categoryNames.push_back(s.text(0));
    }
    Statement ins(db, "INSERT INTO asset_search (id, name, tags, categories, source) VALUES (?, ?, ?, ?, ?)");
    ins.bind(1, id).bind(2, name).bind(3, joinWords(tags)).bind(4, joinWords(categoryNames)).bind(5, id + " " + sourceRef);
    ins.run();
}

// What the files say about an asset that the index also keeps.
struct Measured {
    AssetState state = AssetState::Ready;
    std::string problem;
    ThumbnailState thumbnail = ThumbnailState::Missing;
};

// Compares asset.blend and thumb.png on disk with what info.json recorded.
Measured measureFolder(const fs::path& folder, const AssetInfo& info)
{
    Measured m;
    std::error_code ec;
    const fs::path blend = folder / pathFromUtf8(kAssetBlendFileName);
    if (!fs::is_regular_file(blend, ec)) {
        m.state = AssetState::Broken;
        m.problem = "asset.blend is missing.";
    } else {
        const auto size = fs::file_size(blend, ec);
        if (ec || static_cast<long long>(size) != info.blendBytes || fileTime(blend) != info.blendModified) {
            m.state = AssetState::NeedsRefresh;
        }
    }
    if (info.thumbnail.rendered && fs::is_regular_file(folder / pathFromUtf8(kAssetThumbFileName), ec)) {
        m.thumbnail = ThumbnailState::Ok;
    } else if (!info.thumbnail.error.empty()) {
        m.thumbnail = ThumbnailState::Failed;
    }
    return m;
}

// Adds a category the catalog does not have yet, named from its id.
void ensureCategory(Database& db, const std::string& id)
{
    if (exists(db, "SELECT 1 FROM categories WHERE id = ?", id)) return;
    Statement s(db, "INSERT INTO categories (id, name, sort, built_in) VALUES (?, ?, ?, 0)");
    s.bind(1, id).bind(2, catalogNameFromStem(id)).bind(3, nextSort(db, "categories"));
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

// Writes or replaces the index rows of one asset.
void indexRows(Database& db, const AssetInfo& info, const Measured& m, const std::string& infoModified)
{
    Transaction tx(db);
    for (const std::string& c : info.categories) ensureCategory(db, c);
    ensureLicense(db, info.license);
    {
        Statement s(db,
                    "INSERT INTO assets (id, name, license, source_kind, source_ref, imported_at, width, depth, height, "
                    "triangles, state, problem, thumbnail, thumbnail_error, blend_bytes, blend_modified, info_modified) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(id) DO UPDATE SET name = excluded.name, license = excluded.license, "
                    "source_kind = excluded.source_kind, source_ref = excluded.source_ref, "
                    "imported_at = excluded.imported_at, width = excluded.width, depth = excluded.depth, "
                    "height = excluded.height, triangles = excluded.triangles, state = excluded.state, "
                    "problem = excluded.problem, thumbnail = excluded.thumbnail, "
                    "thumbnail_error = excluded.thumbnail_error, blend_bytes = excluded.blend_bytes, "
                    "blend_modified = excluded.blend_modified, info_modified = excluded.info_modified");
        s.bind(1, info.id).bind(2, info.name).bind(3, info.license).bind(4, info.source.kind)
            .bind(5, info.source.reference).bind(6, formatSessionTime(info.source.importedAt))
            .bind(7, info.size.width).bind(8, info.size.depth).bind(9, info.size.height).bind(10, info.triangles)
            .bind(11, assetStateName(m.state)).bind(12, m.problem).bind(13, thumbnailStateName(m.thumbnail))
            .bind(14, info.thumbnail.error).bind(15, info.blendBytes).bind(16, formatSessionTime(info.blendModified))
            .bind(17, infoModified);
        s.run();
    }
    {
        Statement del(db, "DELETE FROM asset_categories WHERE asset = ?");
        del.bind(1, info.id);
        del.run();
        Statement ins(db, "INSERT OR IGNORE INTO asset_categories (asset, category) VALUES (?, ?)");
        for (const std::string& c : info.categories) {
            ins.reset();
            ins.bind(1, info.id).bind(2, c);
            ins.run();
        }
    }
    {
        Statement del(db, "DELETE FROM asset_tags WHERE asset = ?");
        del.bind(1, info.id);
        del.run();
        Statement ins(db, "INSERT OR IGNORE INTO asset_tags (asset, tag) VALUES (?, ?)");
        for (const std::string& t : info.tags) {
            ins.reset();
            ins.bind(1, info.id).bind(2, t);
            ins.run();
        }
    }
    refreshSearch(db, info.id);
    tx.commit();
}

// Writes a row for an asset folder that cannot be read, so it still shows as broken.
void indexBroken(Database& db, const std::string& id, const std::string& problem)
{
    Transaction tx(db);
    ensureLicense(db, std::string(kUnknownLicenseId));
    {
        Statement s(db,
                    "INSERT INTO assets (id, name, state, problem, thumbnail) VALUES (?, ?, 'broken', ?, 'missing') "
                    "ON CONFLICT(id) DO UPDATE SET name = excluded.name, state = 'broken', problem = excluded.problem, "
                    "thumbnail = 'missing', thumbnail_error = '', info_modified = ''");
        s.bind(1, id).bind(2, id).bind(3, problem);
        s.run();
    }
    for (const char* sql : {"DELETE FROM asset_categories WHERE asset = ?", "DELETE FROM asset_tags WHERE asset = ?"}) {
        Statement s(db, sql);
        s.bind(1, id);
        s.run();
    }
    refreshSearch(db, id);
    tx.commit();
}

// Deletes the rows of one asset.
void deleteRows(Database& db, const std::string& id)
{
    Transaction tx(db);
    {
        Statement s(db, "DELETE FROM asset_search WHERE id = ?");
        s.bind(1, id);
        s.run();
    }
    {
        Statement s(db, "DELETE FROM assets WHERE id = ?");
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

// Reads the user's categories of a room type, in category order.
std::vector<std::string> roomCategories(Database& db, const std::string& room)
{
    std::vector<std::string> out;
    Statement s(db,
                "SELECT rc.category FROM room_type_categories rc JOIN categories c ON c.id = rc.category "
                "WHERE rc.room_type = ? ORDER BY c.sort, c.id");
    s.bind(1, room);
    while (s.step()) out.push_back(s.text(0));
    return out;
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

}

// The name used in files and answers.
std::string_view assetStateName(AssetState state)
{
    switch (state) {
    case AssetState::Ready: return "ready";
    case AssetState::NeedsRefresh: return "needs_refresh";
    case AssetState::Broken: return "broken";
    }
    return "ready";
}

// The name used in files and answers.
std::string_view thumbnailStateName(ThumbnailState state)
{
    switch (state) {
    case ThumbnailState::Ok: return "ok";
    case ThumbnailState::Failed: return "failed";
    case ThumbnailState::Missing: return "missing";
    }
    return "missing";
}

struct AssetCatalog::Impl {
    LibraryPaths paths;
    std::unique_ptr<Database> db;

    // One asset from the index. Empty when there is no row.
    std::optional<AssetRecord> record(const std::string& id) const
    {
        AssetRecord r;
        {
            Statement s(*db,
                        "SELECT id, name, license, source_kind, source_ref, imported_at, width, depth, height, triangles, "
                        "state, problem, thumbnail, thumbnail_error FROM assets WHERE id = ?");
            s.bind(1, id);
            if (!s.step()) return std::nullopt;
            r.id = s.text(0);
            r.name = s.text(1);
            r.license = s.text(2);
            r.sourceKind = s.text(3);
            r.sourceReference = s.text(4);
            r.importedAt = parseSessionTime(s.text(5)).value_or(SessionTime{});
            r.size = {s.real(6), s.real(7), s.real(8)};
            r.triangles = s.integer(9);
            const std::string state = s.text(10);
            r.state = state == "broken" ? AssetState::Broken : state == "needs_refresh" ? AssetState::NeedsRefresh : AssetState::Ready;
            r.problem = s.text(11);
            const std::string thumb = s.text(12);
            r.thumbnail = thumb == "ok" ? ThumbnailState::Ok : thumb == "failed" ? ThumbnailState::Failed : ThumbnailState::Missing;
            r.thumbnailError = s.text(13);
        }
        {
            Statement s(*db,
                        "SELECT ac.category FROM asset_categories ac JOIN categories c ON c.id = ac.category "
                        "WHERE ac.asset = ? ORDER BY c.sort, c.id");
            s.bind(1, id);
            while (s.step()) r.categories.push_back(s.text(0));
        }
        {
            Statement s(*db, "SELECT tag FROM asset_tags WHERE asset = ? ORDER BY tag");
            s.bind(1, id);
            while (s.step()) r.tags.push_back(s.text(0));
        }
        r.folder = paths.assetItems / pathFromUtf8(id);
        if (r.thumbnail == ThumbnailState::Ok) r.thumbFile = r.folder / pathFromUtf8(kAssetThumbFileName);
        std::error_code ec;
        if (const fs::path fbx = r.folder / pathFromUtf8(kAssetFbxFileName); fs::is_regular_file(fbx, ec)) r.fbxFile = fbx;
        return r;
    }

    // Indexes info and the current files of its folder.
    void index(const AssetInfo& info) const
    {
        const fs::path folder = paths.assetItems / pathFromUtf8(info.id);
        const std::string modified = formatSessionTime(fileTime(folder / pathFromUtf8(kAssetInfoFileName)));
        indexRows(*db, info, measureFolder(folder, info), modified);
    }
};

AssetCatalog::AssetCatalog(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

AssetCatalog::~AssetCatalog() = default;

// Opens the catalog database of a library.
std::unique_ptr<AssetCatalog> AssetCatalog::open(const LibraryPaths& library, Status& status)
{
    status = {};
    if (!isLibraryFolder(library.root)) {
        status = fail(ErrorCode::NoLibrary, "The folder " + pathToUtf8(library.root) + " is not a library.");
        return nullptr;
    }
    std::error_code ec;
    if (!fs::is_regular_file(library.assetsDb, ec)) {
        if (status = makeLibraryFolders(library); !status.ok()) return nullptr;
        if (status = ensureLibraryDatabases(library); !status.ok()) return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->paths = library;
    try {
        impl->db = Database::open(library.assetsDb, false);
        int version = impl->db->userVersion();
        if (version > kAssetsDbVersion) {
            status = fail(ErrorCode::UnsupportedVersion, "The assets database was written by a newer Plynth.");
            return nullptr;
        }
        if (version < 1) {
            status = fail(ErrorCode::DatabaseError, "The file " + pathToUtf8(library.assetsDb) + " is not an assets database.");
            return nullptr;
        }
        while (version < kAssetsDbVersion) {
            const auto& steps = migrationSteps();
            if (static_cast<std::size_t>(version - 1) >= steps.size()) break;
            steps[static_cast<std::size_t>(version - 1)](*impl->db);
            ++version;
        }
    } catch (const DbError& e) {
        status = fail(ErrorCode::DatabaseError, std::string("The assets database could not be opened: ") + e.what());
        return nullptr;
    }
    staging::cleanOld(library.assetStaging, std::chrono::hours(24));
    return std::unique_ptr<AssetCatalog>(new AssetCatalog(std::move(impl)));
}

// The library paths this catalog was opened with.
const LibraryPaths& AssetCatalog::paths() const
{
    return impl_->paths;
}

// The room types in picker order, each with its categories.
std::vector<RoomTypeEntry> AssetCatalog::roomTypes() const
{
    std::vector<RoomTypeEntry> out;
    try {
        Statement s(*impl_->db, "SELECT id, name, built_in FROM room_types ORDER BY sort, id");
        while (s.step()) out.push_back({s.text(0), s.text(1), s.integer(2) != 0, {}});
        for (RoomTypeEntry& entry : out) entry.categories = roomCategories(*impl_->db, entry.id);
    } catch (const DbError&) {
    }
    return out;
}

// The room types in the form useRoomTypes takes.
std::vector<RoomType> AssetCatalog::roomTypeList() const
{
    std::vector<RoomType> out;
    try {
        Statement s(*impl_->db, "SELECT id, name FROM room_types ORDER BY sort, id");
        while (s.step()) out.push_back({s.text(0), s.text(1)});
    } catch (const DbError&) {
    }
    return out;
}

// Adds a room type at the end.
Status AssetCatalog::addRoomType(std::string_view id, std::string_view name)
{
    if (Status s = checkCatalogId(id); !s.ok()) return s;
    if (Status s = checkCatalogName(name); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (exists(db, "SELECT 1 FROM room_types WHERE id = ?", id)) {
            return fail(ErrorCode::AlreadyExists, "The room type \"" + std::string(id) + "\" already exists.");
        }
        Statement s(db, "INSERT INTO room_types (id, name, sort, built_in) VALUES (?, ?, ?, 0)");
        s.bind(1, id).bind(2, name).bind(3, nextSort(db, "room_types"));
        s.run();
        tx.commit();
        return Status{};
    });
}

// Changes the name of a room type.
Status AssetCatalog::renameRoomType(std::string_view id, std::string_view name)
{
    if (Status s = checkCatalogName(name); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM room_types WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no room type \"" + std::string(id) + "\".");
        }
        Statement s(db, "UPDATE room_types SET name = ? WHERE id = ?");
        s.bind(1, name).bind(2, id);
        s.run();
        tx.commit();
        return Status{};
    });
}

// Removes a room type.
Status AssetCatalog::removeRoomType(std::string_view id)
{
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM room_types WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no room type \"" + std::string(id) + "\".");
        }
        {
            Statement s(db, "DELETE FROM room_types WHERE id = ?");
            s.bind(1, id);
            s.run();
        }
        renumber(db, "room_types", idsInOrder(db, "room_types"));
        tx.commit();
        return Status{};
    });
}

// Replaces the categories of a room type.
Status AssetCatalog::setRoomTypeCategories(std::string_view id, const std::vector<std::string>& categories)
{
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM room_types WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no room type \"" + std::string(id) + "\".");
        }
        for (const std::string& c : categories) {
            if (!exists(db, "SELECT 1 FROM categories WHERE id = ?", c)) {
                return fail(ErrorCode::NotFound, "There is no category \"" + c + "\".");
            }
        }
        {
            Statement s(db, "DELETE FROM room_type_categories WHERE room_type = ?");
            s.bind(1, id);
            s.run();
        }
        Statement ins(db, "INSERT OR IGNORE INTO room_type_categories (room_type, category) VALUES (?, ?)");
        for (const std::string& c : categories) {
            ins.reset();
            ins.bind(1, id).bind(2, c);
            ins.run();
        }
        tx.commit();
        return Status{};
    });
}

// Moves a room type to a place in the order.
Status AssetCatalog::moveRoomType(std::string_view id, int position)
{
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        std::vector<std::string> order = idsInOrder(db, "room_types");
        const auto it = std::find(order.begin(), order.end(), id);
        if (it == order.end()) return fail(ErrorCode::NotFound, "There is no room type \"" + std::string(id) + "\".");
        const std::string moved = *it;
        order.erase(it);
        const int place = std::clamp(position, 0, static_cast<int>(order.size()));
        order.insert(order.begin() + place, moved);
        renumber(db, "room_types", order);
        tx.commit();
        return Status{};
    });
}

// The categories with how many assets each holds.
std::vector<CategoryEntry> AssetCatalog::categories() const
{
    std::vector<CategoryEntry> out;
    try {
        Statement s(*impl_->db,
                    "SELECT c.id, c.name, c.built_in, (SELECT COUNT(*) FROM asset_categories ac WHERE ac.category = c.id) "
                    "FROM categories c ORDER BY c.sort, c.id");
        while (s.step()) out.push_back({s.text(0), s.text(1), s.integer(2) != 0, static_cast<int>(s.integer(3))});
    } catch (const DbError&) {
    }
    return out;
}

// The categories linked to a room type.
std::vector<CategoryEntry> AssetCatalog::categoriesFor(std::string_view roomType) const
{
    std::vector<CategoryEntry> out;
    try {
        Statement s(*impl_->db,
                    "SELECT c.id, c.name, c.built_in, (SELECT COUNT(*) FROM asset_categories ac WHERE ac.category = c.id) "
                    "FROM categories c JOIN room_type_categories rc ON rc.category = c.id WHERE rc.room_type = ? "
                    "ORDER BY c.sort, c.id");
        s.bind(1, roomType);
        while (s.step()) out.push_back({s.text(0), s.text(1), s.integer(2) != 0, static_cast<int>(s.integer(3))});
    } catch (const DbError&) {
    }
    return out;
}

// Adds a category at the end.
Status AssetCatalog::addCategory(std::string_view id, std::string_view name)
{
    if (Status s = checkCatalogId(id); !s.ok()) return s;
    if (Status s = checkCatalogName(name); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (exists(db, "SELECT 1 FROM categories WHERE id = ?", id)) {
            return fail(ErrorCode::AlreadyExists, "The category \"" + std::string(id) + "\" already exists.");
        }
        Statement s(db, "INSERT INTO categories (id, name, sort, built_in) VALUES (?, ?, ?, 0)");
        s.bind(1, id).bind(2, name).bind(3, nextSort(db, "categories"));
        s.run();
        tx.commit();
        return Status{};
    });
}

// Changes the name of a category and what search finds for the assets that have it.
Status AssetCatalog::renameCategory(std::string_view id, std::string_view name)
{
    if (Status s = checkCatalogName(name); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM categories WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no category \"" + std::string(id) + "\".");
        }
        {
            Statement s(db, "UPDATE categories SET name = ? WHERE id = ?");
            s.bind(1, name).bind(2, id);
            s.run();
        }
        std::vector<std::string> assets;
        {
            Statement s(db, "SELECT asset FROM asset_categories WHERE category = ?");
            s.bind(1, id);
            while (s.step()) assets.push_back(s.text(0));
        }
        for (const std::string& a : assets) refreshSearch(db, a);
        tx.commit();
        return Status{};
    });
}

// Removes a category from the catalog and from every asset that has it.
Status AssetCatalog::removeCategory(std::string_view id)
{
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM categories WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no category \"" + std::string(id) + "\".");
        }
        std::vector<std::string> assets;
        {
            Statement s(db, "SELECT asset FROM asset_categories WHERE category = ?");
            s.bind(1, id);
            while (s.step()) assets.push_back(s.text(0));
        }
        // Every info.json is rewritten before the index changes, so the files stay the truth.
        for (const std::string& a : assets) {
            const fs::path file = impl_->paths.assetItems / pathFromUtf8(a) / pathFromUtf8(kAssetInfoFileName);
            AssetInfoRead read = readAssetInfo(file, a);
            if (!read.status.ok()) continue;
            std::erase(read.info.categories, std::string(id));
            if (Status s = writeAssetInfo(file, read.info); !s.ok()) return s;
        }
        {
            Statement s(db, "DELETE FROM categories WHERE id = ?");
            s.bind(1, id);
            s.run();
        }
        renumber(db, "categories", idsInOrder(db, "categories"));
        for (const std::string& a : assets) {
            const fs::path file = impl_->paths.assetItems / pathFromUtf8(a) / pathFromUtf8(kAssetInfoFileName);
            const AssetInfoRead read = readAssetInfo(file, a);
            if (read.status.ok()) impl_->index(read.info);
            else refreshSearch(db, a);
        }
        tx.commit();
        return Status{};
    });
}

// The licenses in order.
std::vector<LicenseEntry> AssetCatalog::licenses() const
{
    std::vector<LicenseEntry> out;
    try {
        Statement s(*impl_->db, "SELECT id, name, url, redistributable FROM licenses ORDER BY sort, id");
        while (s.step()) out.push_back({s.text(0), s.text(1), s.text(2), s.integer(3) != 0});
    } catch (const DbError&) {
    }
    return out;
}

// One indexed asset.
std::optional<AssetRecord> AssetCatalog::asset(std::string_view id) const
{
    try {
        return impl_->record(std::string(id));
    } catch (const DbError&) {
        return std::nullopt;
    }
}

// Assets that match a query.
std::vector<AssetRecord> AssetCatalog::search(const AssetQuery& query) const
{
    std::vector<AssetRecord> out;
    try {
        std::string sql = "SELECT a.id FROM assets a WHERE 1 = 1";
        std::vector<std::string> binds;
        const std::vector<std::string> words = searchWords(query.text);
        if (!words.empty()) {
            std::string match;
            for (const std::string& w : words) {
                if (!match.empty()) match += ' ';
                match += "\"" + w + "\"*";
            }
            sql += " AND a.id IN (SELECT id FROM asset_search WHERE asset_search MATCH ?)";
            binds.push_back(match);
        }
        if (query.roomType) {
            sql += " AND a.id IN (SELECT ac.asset FROM asset_categories ac JOIN room_type_categories rc ON "
                   "rc.category = ac.category WHERE rc.room_type = ?)";
            binds.push_back(*query.roomType);
        }
        if (query.category) {
            sql += " AND a.id IN (SELECT asset FROM asset_categories WHERE category = ?)";
            binds.push_back(*query.category);
        }
        for (const std::string& tag : query.tags) {
            sql += " AND a.id IN (SELECT asset FROM asset_tags WHERE tag = ?)";
            binds.push_back(tag);
        }
        if (query.license) {
            sql += " AND a.license = ?";
            binds.push_back(*query.license);
        }
        if (query.uncategorizedOnly) sql += " AND NOT EXISTS (SELECT 1 FROM asset_categories ac WHERE ac.asset = a.id)";
        sql += " ORDER BY a.name COLLATE NOCASE, a.id";
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

// How many assets are indexed.
int AssetCatalog::assetCount() const
{
    try {
        Statement s(*impl_->db, "SELECT COUNT(*) FROM assets");
        return s.step() ? static_cast<int>(s.integer(0)) : 0;
    } catch (const DbError&) {
        return 0;
    }
}

// Says whether an id is in use by an asset, an item folder or a running import.
bool AssetCatalog::idTaken(std::string_view id) const
{
    try {
        if (exists(*impl_->db, "SELECT 1 FROM assets WHERE id = ?", id)) return true;
    } catch (const DbError&) {
    }
    std::error_code ec;
    if (fs::exists(impl_->paths.assetItems / pathFromUtf8(id), ec)) return true;
    if (staging::isLive(impl_->paths.assetStaging / pathFromUtf8(id))) return true;
    const std::string prefix = std::string(id) + "-";
    for (const auto& entry : fs::directory_iterator(impl_->paths.assetStaging, ec)) {
        if (pathToUtf8(entry.path().filename()).rfind(prefix, 0) == 0 && staging::isLive(entry.path())) return true;
    }
    return false;
}

// Changes an asset.
Status AssetCatalog::editAsset(std::string_view id, const AssetEdit& edit)
{
    if (edit.name) {
        if (Status s = checkCatalogName(*edit.name); !s.ok()) return s;
    }
    if (edit.tags) {
        for (const std::string& t : *edit.tags) {
            if (Status s = checkTag(t); !s.ok()) return s;
        }
    }
    return guarded([&] {
        Database& db = *impl_->db;
        Transaction tx(db);
        if (!exists(db, "SELECT 1 FROM assets WHERE id = ?", id)) {
            return fail(ErrorCode::NotFound, "There is no asset \"" + std::string(id) + "\".");
        }
        if (edit.categories) {
            for (const std::string& c : *edit.categories) {
                if (!exists(db, "SELECT 1 FROM categories WHERE id = ?", c)) {
                    return fail(ErrorCode::NotFound, "There is no category \"" + c + "\".");
                }
            }
        }
        if (edit.license && !exists(db, "SELECT 1 FROM licenses WHERE id = ?", *edit.license)) {
            return fail(ErrorCode::NotFound, "There is no license \"" + *edit.license + "\".");
        }
        const fs::path file = impl_->paths.assetItems / pathFromUtf8(id) / pathFromUtf8(kAssetInfoFileName);
        AssetInfoRead read = readAssetInfo(file, id);
        if (!read.status.ok()) return read.status;

        AssetInfo& info = read.info;
        if (edit.name) info.name = *edit.name;
        if (edit.categories) {
            info.categories.clear();
            for (const std::string& c : *edit.categories) {
                if (std::find(info.categories.begin(), info.categories.end(), c) == info.categories.end()) {
                    info.categories.push_back(c);
                }
            }
        }
        if (edit.tags) {
            info.tags = *edit.tags;
            std::sort(info.tags.begin(), info.tags.end());
            info.tags.erase(std::unique(info.tags.begin(), info.tags.end()), info.tags.end());
        }
        if (edit.license) info.license = *edit.license;

        if (Status s = writeAssetInfo(file, info); !s.ok()) return s;
        impl_->index(info);
        tx.commit();
        return Status{};
    });
}

// Indexes an asset folder that holds info.json.
Status AssetCatalog::indexAsset(const AssetInfo& info)
{
    if (Status s = checkCatalogId(info.id); !s.ok()) return s;
    std::error_code ec;
    const fs::path file = impl_->paths.assetItems / pathFromUtf8(info.id) / pathFromUtf8(kAssetInfoFileName);
    if (!fs::is_regular_file(file, ec)) {
        return fail(ErrorCode::NotFound, "The folder for \"" + info.id + "\" has no info.json.");
    }
    return guarded([&] {
        impl_->index(info);
        return Status{};
    });
}

// Deletes an asset folder and its rows.
Status AssetCatalog::removeAsset(std::string_view id)
{
    if (Status s = checkCatalogId(id); !s.ok()) return s;
    return guarded([&] {
        Database& db = *impl_->db;
        const fs::path folder = impl_->paths.assetItems / pathFromUtf8(id);
        std::error_code ec;
        // A valid id is one plain name, but the folder must also sit directly in items/.
        if (folder.parent_path() != impl_->paths.assetItems) {
            return fail(ErrorCode::InvalidValue, "The id is not an asset folder.");
        }
        const bool indexed = exists(db, "SELECT 1 FROM assets WHERE id = ?", id);
        const bool onDisk = fs::is_directory(folder, ec);
        if (!indexed && !onDisk) return fail(ErrorCode::NotFound, "There is no asset \"" + std::string(id) + "\".");
        if (onDisk) {
            fs::remove_all(folder, ec);
            if (ec) return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(folder) + " could not be removed.");
        }
        deleteRows(db, std::string(id));
        return Status{};
    });
}

// Rebuilds the asset rows from the item folders.
RescanReport AssetCatalog::rescan()
{
    RescanReport report;
    try {
        Database& db = *impl_->db;
        struct Row {
            std::string infoModified;
            AssetState state;
        };
        std::map<std::string, Row> rows;
        {
            Statement s(db, "SELECT id, info_modified, state FROM assets");
            while (s.step()) {
                const std::string state = s.text(2);
                rows[s.text(0)] = {s.text(1), state == "broken"         ? AssetState::Broken
                                              : state == "needs_refresh" ? AssetState::NeedsRefresh
                                                                         : AssetState::Ready};
            }
        }

        Transaction tx(db);
        std::set<std::string> seen;
        std::error_code ec;
        std::vector<fs::path> folders;
        for (const auto& entry : fs::directory_iterator(impl_->paths.assetItems, ec)) {
            if (entry.is_directory(ec)) folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end());

        for (const fs::path& folder : folders) {
            const std::string name = pathToUtf8(folder.filename());
            if (name.empty() || name.front() == '.') continue;
            seen.insert(name);
            const fs::path infoFile = folder / pathFromUtf8(kAssetInfoFileName);
            const fs::path blendFile = folder / pathFromUtf8(kAssetBlendFileName);
            const bool hasInfo = fs::is_regular_file(infoFile, ec);
            const bool hasBlend = fs::is_regular_file(blendFile, ec);
            const auto existing = rows.find(name);

            if (!hasInfo && !hasBlend) {
                report.broken.emplace_back(name, fail(ErrorCode::NotFound, "The folder has no info.json and no asset.blend."));
                if (checkCatalogId(name).ok()) indexBroken(db, name, "The folder has no info.json and no asset.blend.");
                continue;
            }
            if (Status idStatus = checkCatalogId(name); !idStatus.ok()) {
                report.broken.emplace_back(name, fail(ErrorCode::BadFile, "The folder name is not a valid asset id: " + idStatus.message));
                continue;
            }

            AssetInfo info;
            bool created = false;
            if (!hasInfo) {
                info.id = name;
                info.name = catalogNameFromStem(name);
                info.source.kind = std::string(kSourceLocalFile);
                info.source.reference = std::string(kAssetBlendFileName);
                info.source.importedAt = sessionNow();
                info.blenderVersion = "";
                if (Status s = writeAssetInfo(infoFile, info); !s.ok()) {
                    report.broken.emplace_back(name, s);
                    continue;
                }
                created = true;
            } else {
                AssetInfoRead read = readAssetInfo(infoFile, name);
                if (!read.status.ok()) {
                    report.broken.emplace_back(name, read.status);
                    indexBroken(db, name, read.status.message);
                    continue;
                }
                info = std::move(read.info);
            }

            const Measured measured = measureFolder(folder, info);
            const std::string modified = formatSessionTime(fileTime(infoFile));
            if (existing == rows.end() || created) {
                ++report.added;
                indexRows(db, info, measured, modified);
            } else if (existing->second.infoModified != modified || existing->second.state != measured.state) {
                ++report.updated;
                indexRows(db, info, measured, modified);
            } else {
                ++report.unchanged;
            }
            const bool lacksFbx = hasBlend && !fs::is_regular_file(folder / pathFromUtf8(kAssetFbxFileName), ec);
            const bool lacksMaps = hasBlend && !hasMaterialMaps(info);
            if (measured.state == AssetState::NeedsRefresh || lacksFbx || lacksMaps) report.needsRefresh.push_back(name);
        }

        for (const auto& [id, row] : rows) {
            if (seen.count(id)) continue;
            deleteRows(db, id);
            ++report.removed;
        }
        tx.commit();

        for (const auto& entry : fs::directory_iterator(impl_->paths.assetInbox, ec)) {
            const std::string name = pathToUtf8(entry.path().filename());
            if (name.empty() || name.front() == '.' || name.front() == '~') continue;
            if (name == kLibraryInboxDoneFolder || name == kLibraryInboxFailedFolder) continue;
            ++report.inboxWaiting;
        }
    } catch (const DbError& e) {
        report.broken.emplace_back("", fail(ErrorCode::DatabaseError, std::string("The assets database failed: ") + e.what()));
    }
    return report;
}

// Opens the catalog and gives its room types to useRoomTypes.
Status AssetCatalog::useLibraryRoomTypes(const LibraryPaths& library)
{
    Status status;
    const auto catalog = open(library, status);
    if (!catalog) return status;
    useRoomTypes(catalog->roomTypeList());
    return {};
}

}
