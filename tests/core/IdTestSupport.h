#pragma once

#include "SquareFixtures.h"
#include "TestSupport.h"
#include "../../core/src/IdSource.h"

#include <plynth/core/Document.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/ItemCommands.h>
#include <plynth/core/commands/OpeningCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/RoofCommands.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/StairCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <string_view>

namespace plynth::test {

// Says whether a part after a prefix has the shape of a new id: a digit, then seven of kIdAlphabet with a letter.
inline bool newIdPartShape(std::string_view part)
{
    static const std::regex shape("^[0-9][0-9a-hjkmnp-tv-z]{7}$");
    if (!std::regex_match(part.begin(), part.end(), shape)) return false;
    return part.substr(1).find_first_not_of("0123456789") != std::string_view::npos;
}

// Says whether an id is the prefix and a part of the new shape. "r" does not take "rf" ids.
inline bool newIdShape(std::string_view id, std::string_view prefix)
{
    return id.size() == prefix.size() + kNewIdLength && id.substr(0, prefix.size()) == prefix &&
           newIdPartShape(id.substr(prefix.size()));
}

// Says whether an id has this prefix followed by a digit, in the old or the new shape.
inline bool hasPrefix(std::string_view id, std::string_view prefix)
{
    return id.size() > prefix.size() && id.substr(0, prefix.size()) == prefix && id[prefix.size()] >= '0' &&
           id[prefix.size()] <= '9';
}

// Every id in a plan.
inline std::set<Id> idsIn(const Plan& plan)
{
    std::set<Id> out;
    const auto take = [&out](const auto& list) {
        for (const auto& one : list) out.insert(one.id);
    };
    take(plan.floors);
    for (const Floor& f : plan.floors) {
        take(f.points);
        take(f.walls);
        take(f.rooms);
        take(f.openings);
        take(f.items);
        take(f.stairs);
    }
    take(plan.roofSections);
    return out;
}

// The square with an empty floor above. The roof case gets a room to hold its section.
inline Project idBase(std::string_view prefix = {})
{
    Project p = squareProjectWithWalls();
    Floor above;
    above.id = "f2";
    above.name = "Above";
    p.plan.floors.push_back(above);
    if (prefix == kRoofSectionIdPrefix) {
        Room r;
        r.id = "r1";
        r.walls = {"w1", "w2", "w3", "w4"};
        p.plan.floors[0].rooms.push_back(r);
    }
    return p;
}

// A command that places one small item.
inline std::unique_ptr<Command> placeItem(const char* asset = "sofa", Vec2 at = {2.0, 2.0})
{
    Item item;
    item.asset = asset;
    item.x = at.x;
    item.y = at.y;
    item.size = {1.0, 0.5, 0.5};
    return std::make_unique<PlaceItemCommand>("f1", item);
}

// A command that adds one object of the kind that has this prefix.
inline std::unique_ptr<Command> addOne(std::string_view prefix)
{
    if (prefix == kFloorIdPrefix) return std::make_unique<AddFloorCommand>("More");
    if (prefix == kPointIdPrefix) return std::make_unique<AddPointCommand>("f1", 8.0, 8.0);
    if (prefix == kWallIdPrefix) return std::make_unique<AddWallCommand>("p1", "p3");
    if (prefix == kRoomIdPrefix) return std::make_unique<AddRoomCommand>("f1", Vec2{2.0, 2.0});
    if (prefix == kOpeningIdPrefix) return std::make_unique<AddOpeningCommand>(doorOn("w1", 2.0));
    if (prefix == kItemIdPrefix) return placeItem();
    if (prefix == kRoofSectionIdPrefix) {
        return std::make_unique<AddRoofSectionCommand>("f1", 2.0, 2.0, RoofSettings{});
    }
    REQUIRE(prefix == kStairIdPrefix);
    Stair stair;
    stair.x = 1.0;
    stair.y = 1.0;
    return std::make_unique<AddStairCommand>("f1", stair);
}

// A command that removes the object with this id.
inline std::unique_ptr<Command> removeOne(std::string_view prefix, const Id& id)
{
    if (prefix == kFloorIdPrefix) return std::make_unique<RemoveFloorCommand>(id);
    if (prefix == kPointIdPrefix) return std::make_unique<RemovePointCommand>(id);
    if (prefix == kWallIdPrefix) return std::make_unique<RemoveWallCommand>(id);
    if (prefix == kRoomIdPrefix) return std::make_unique<RemoveRoomCommand>(id);
    if (prefix == kOpeningIdPrefix) return std::make_unique<RemoveOpeningCommand>(id);
    if (prefix == kItemIdPrefix) return std::make_unique<RemoveItemCommand>(id);
    if (prefix == kRoofSectionIdPrefix) return std::make_unique<RemoveRoofSectionCommand>(id);
    REQUIRE(prefix == kStairIdPrefix);
    return std::make_unique<RemoveStairCommand>(id);
}

// Runs an adding command and gives back its new id with this prefix.
inline Id runAdd(Document& doc, std::unique_ptr<Command> command, std::string_view prefix)
{
    const std::set<Id> before = idsIn(doc.plan());
    requireOk(doc.execute(std::move(command)));
    for (const Id& id : idsIn(doc.plan())) {
        if (!before.contains(id) && hasPrefix(id, prefix)) return id;
    }
    FAIL("No new id with the prefix " << prefix);
    return {};
}

// Writes a text file, with its folder.
inline void putFile(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
    REQUIRE(out.good());
}

// Saves a project as a new folder and opens it.
inline void openNew(Document& doc, Project project, const std::filesystem::path& folder)
{
    requireOk(createProject(project, folder, {}));
    requireOk(doc.open(folder));
}

// The ids the product makes, for the life of the object.
struct SystemIds {
    detail::ScopedIdSource scope{detail::IdPartMaker(&detail::systemIdPart)};
};

}
