#include "BuiltIn.h"

#include "plynth/core/RoomTypes.h"
#include "plynth/core/catalog/AssetCatalog.h"
#include "plynth/core/catalog/MaterialCatalog.h"

#include <array>

namespace plynth {

namespace {

const std::vector<CategoryEntry>& categoryList()
{
    static const std::vector<CategoryEntry> list = {
        {"sofa", "Sofa", true, 0},
        {"armchair", "Armchair", true, 0},
        {"chair", "Chair", true, 0},
        {"table", "Table", true, 0},
        {"desk", "Desk", true, 0},
        {"bed", "Bed", true, 0},
        {"wardrobe", "Wardrobe", true, 0},
        {"cabinet", "Cabinet", true, 0},
        {"shelf", "Shelf", true, 0},
        {"tv", "TV", true, 0},
        {"lamp", "Lamp", true, 0},
        {"carpet", "Carpet", true, 0},
        {"curtain", "Curtain", true, 0},
        {"plant", "Plant", true, 0},
        {"decor", "Decor", true, 0},
        {"mirror", "Mirror", true, 0},
        {"kitchen_unit", "Kitchen unit", true, 0},
        {"appliance", "Appliance", true, 0},
        {"bath", "Bath", true, 0},
        {"shower", "Shower", true, 0},
        {"toilet", "Toilet", true, 0},
        {"sink", "Sink", true, 0},
        {"door", "Door", true, 0},
        {"window", "Window", true, 0},
    };
    return list;
}

const std::vector<LicenseEntry>& licenseList()
{
    static const std::vector<LicenseEntry> list = {
        {"unknown", "Unknown", "", false},
        {"cc0", "CC0 (public domain)", "https://creativecommons.org/publicdomain/zero/1.0/", true},
        {"cc_by_4", "CC BY 4.0", "https://creativecommons.org/licenses/by/4.0/", true},
        {"royalty_free", "Royalty free", "", false},
        {"personal_use", "Personal use only", "", false},
        {"own_work", "Own work", "", true},
    };
    return list;
}

}

// The groups a new materials catalog starts with.
std::span<const MaterialGroupEntry> builtInMaterialGroups()
{
    static const std::vector<MaterialGroupEntry> list = {
        {"floor", "Floor", true, 0},   {"wall", "Wall", true, 0},   {"ceiling", "Ceiling", true, 0}, {"exterior", "Exterior", true, 0},
        {"trim", "Trim", true, 0},     {"door", "Door", true, 0},   {"glass", "Glass", true, 0},     {"metal", "Metal", true, 0},
    };
    return list;
}

// The categories a new library starts with.
std::span<const CategoryEntry> builtInCategories()
{
    return categoryList();
}

// The licenses a new library starts with.
std::span<const LicenseEntry> builtInLicenses()
{
    return licenseList();
}

// Which categories each built-in room type shows.
std::vector<std::pair<std::string, std::string>> builtInRoomTypeCategories()
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> links = {
        {"living", {"sofa", "armchair", "table", "tv", "shelf", "lamp", "carpet", "curtain", "plant", "decor"}},
        {"bedroom", {"bed", "wardrobe", "desk", "chair", "lamp", "carpet", "curtain", "mirror", "decor"}},
        {"kitchen", {"kitchen_unit", "appliance", "table", "chair", "lamp"}},
        {"bathroom", {"bath", "shower", "toilet", "sink", "mirror", "cabinet"}},
        {"hall", {"cabinet", "shelf", "mirror", "lamp", "carpet"}},
        {"dining", {"table", "chair", "cabinet", "lamp", "carpet", "decor"}},
        {"office", {"desk", "chair", "shelf", "cabinet", "lamp"}},
        {"storage", {"shelf", "cabinet"}},
        {"other", {}},
    };
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [room, categories] : links) {
        for (const std::string& c : categories) out.emplace_back(room, c);
    }
    return out;
}

}

namespace plynth::catalog {

// Writes the built-in lists into a new database.
void seedBuiltIns(Database& db)
{
    Transaction tx(db);
    {
        Statement s(db, "INSERT INTO room_types (id, name, sort, built_in) VALUES (?, ?, ?, 1)");
        int sort = 0;
        for (const RoomType& t : builtInRoomTypes()) {
            s.reset();
            s.bind(1, t.id).bind(2, t.name).bind(3, sort++);
            s.run();
        }
    }
    {
        Statement s(db, "INSERT INTO categories (id, name, sort, built_in) VALUES (?, ?, ?, 1)");
        int sort = 0;
        for (const CategoryEntry& c : builtInCategories()) {
            s.reset();
            s.bind(1, c.id).bind(2, c.name).bind(3, sort++);
            s.run();
        }
    }
    {
        Statement s(db, "INSERT INTO room_type_categories (room_type, category) VALUES (?, ?)");
        for (const auto& [room, category] : builtInRoomTypeCategories()) {
            s.reset();
            s.bind(1, room).bind(2, category);
            s.run();
        }
    }
    {
        Statement s(db, "INSERT INTO licenses (id, name, url, redistributable, sort) VALUES (?, ?, ?, ?, ?)");
        int sort = 0;
        for (const LicenseEntry& l : builtInLicenses()) {
            s.reset();
            s.bind(1, l.id).bind(2, l.name).bind(3, l.url).bind(4, l.redistributable ? 1 : 0).bind(5, sort++);
            s.run();
        }
    }
    tx.commit();
}

}

namespace plynth::catalog {

// Fills a materials database with the built-in groups and licenses where those tables are empty.
void seedMaterialBuiltIns(Database& db)
{
    const auto isEmpty = [&db](const char* table) {
        Statement s(db, std::string("SELECT COUNT(*) FROM ") + table);
        return s.step() && s.integer(0) == 0;
    };
    Transaction tx(db);
    if (isEmpty("material_groups")) {
        Statement s(db, "INSERT INTO material_groups (id, name, sort, built_in) VALUES (?, ?, ?, 1)");
        int sort = 0;
        for (const MaterialGroupEntry& g : builtInMaterialGroups()) {
            s.reset();
            s.bind(1, g.id).bind(2, g.name).bind(3, sort++);
            s.run();
        }
    }
    if (isEmpty("licenses")) {
        Statement s(db, "INSERT INTO licenses (id, name, url, redistributable, sort) VALUES (?, ?, ?, ?, ?)");
        int sort = 0;
        for (const LicenseEntry& l : builtInLicenses()) {
            s.reset();
            s.bind(1, l.id).bind(2, l.name).bind(3, l.url).bind(4, l.redistributable ? 1 : 0).bind(5, sort++);
            s.run();
        }
    }
    tx.commit();
}

}
