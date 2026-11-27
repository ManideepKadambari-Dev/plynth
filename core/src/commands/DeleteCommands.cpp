#include "plynth/core/commands/DeleteCommands.h"

#include "plynth/core/commands/CanopyCommands.h"
#include "plynth/core/commands/ItemCommands.h"
#include "plynth/core/commands/OpeningCommands.h"
#include "plynth/core/commands/PointCommands.h"
#include "plynth/core/commands/RoomCommands.h"
#include "plynth/core/commands/LightCommands.h"
#include "plynth/core/commands/StairCommands.h"
#include "plynth/core/commands/WallCommands.h"

#include "CommandSupport.h"

#include <algorithm>
#include <set>

namespace plynth {

namespace {

// Makes a failed build.
DeleteBuild refuse(ErrorCode code, std::string message)
{
    DeleteBuild build;
    build.status = fail(code, std::move(message));
    return build;
}

}

// Works out everything that has to go with the named items, then builds the group.
DeleteBuild makeDeleteCommand(const Plan& plan, std::span<const Id> ids)
{
    if (ids.empty()) return refuse(ErrorCode::InvalidValue, "Nothing was named to delete.");

    const Floor* floor = nullptr;
    std::set<Id> items;
    std::set<Id> openings;
    std::set<Id> points;
    std::set<Id> walls;
    std::set<Id> rooms;
    std::set<Id> stairs;
    std::set<Id> lights;
    std::set<Id> canopies;
    for (const Id& id : ids) {
        const Floor* found = findFloorContaining(plan, id);
        if (!found) return refuse(ErrorCode::NotFound, "There is no point, wall, room, opening, item, stair, light or canopy " + id + ".");
        if (floor && floor != found) return refuse(ErrorCode::InvalidValue, "The items are on different floors.");
        floor = found;
        if (findPoint(*found, id)) {
            points.insert(id);
        } else if (findWall(*found, id)) {
            walls.insert(id);
        } else if (findRoom(*found, id)) {
            rooms.insert(id);
        } else if (findOpening(*found, id)) {
            openings.insert(id);
        } else if (findItem(*found, id)) {
            items.insert(id);
        } else if (findStair(*found, id)) {
            stairs.insert(id);
        } else if (findLight(*found, id)) {
            lights.insert(id);
        } else if (findCanopy(*found, id)) {
            canopies.insert(id);
        } else {
            return refuse(ErrorCode::NotFound,
                          "The id " + id + " is not a point, wall, room, opening, item, stair, light or canopy.");
        }
    }

    for (const Wall& w : floor->walls) {
        if (points.count(w.from) || points.count(w.to)) walls.insert(w.id);
    }
    for (const Opening& o : floor->openings) {
        if (walls.count(o.wall)) openings.insert(o.id);
    }
    for (const Room& r : floor->rooms) {
        for (const Id& w : r.walls) {
            if (walls.count(w)) {
                rooms.insert(r.id);
                break;
            }
        }
    }
    for (const Wall& w : floor->walls) {
        if (!walls.count(w.id)) continue;
        for (const Id& end : {w.from, w.to}) {
            const bool stillUsed = std::any_of(floor->walls.begin(), floor->walls.end(), [&](const Wall& other) {
                return !walls.count(other.id) && (other.from == end || other.to == end);
            });
            if (!stillUsed) points.insert(end);
        }
    }

    DeleteBuild build;
    auto group = std::make_unique<CommandGroup>("Delete");
    for (const Stair& s : floor->stairs) {
        if (!stairs.count(s.id)) continue;
        build.removes.stairs.push_back(s.id);
        group->add(std::make_unique<RemoveStairCommand>(s.id));
    }
    for (const LightMarker& l : floor->lights) {
        if (!lights.count(l.id)) continue;
        build.removes.lights.push_back(l.id);
        group->add(std::make_unique<RemoveLightCommand>(l.id));
    }
    for (const Canopy& c : floor->canopies) {
        if (!canopies.count(c.id)) continue;
        build.removes.canopies.push_back(c.id);
        group->add(std::make_unique<RemoveCanopyCommand>(c.id));
    }
    for (const Item& i : floor->items) {
        if (!items.count(i.id)) continue;
        build.removes.items.push_back(i.id);
        group->add(std::make_unique<RemoveItemCommand>(i.id));
    }
    for (const Item& i : floor->items) {
        if (items.count(i.id) || !i.wall || !walls.count(*i.wall)) continue;
        build.removes.detached.push_back(i.id);
        group->add(std::make_unique<SetItemWallCommand>(i.id, std::nullopt));
    }
    for (const Opening& o : floor->openings) {
        if (!openings.count(o.id)) continue;
        build.removes.openings.push_back(o.id);
        group->add(std::make_unique<RemoveOpeningCommand>(o.id));
    }
    for (const Room& r : floor->rooms) {
        if (!rooms.count(r.id)) continue;
        build.removes.rooms.push_back(r.id);
        group->add(std::make_unique<RemoveRoomCommand>(r.id));
    }
    for (const Wall& w : floor->walls) {
        if (!walls.count(w.id)) continue;
        build.removes.walls.push_back(w.id);
        group->add(std::make_unique<RemoveWallCommand>(w.id));
    }
    for (const Point& p : floor->points) {
        if (!points.count(p.id)) continue;
        build.removes.points.push_back(p.id);
        group->add(std::make_unique<RemovePointCommand>(p.id));
    }
    build.command = std::move(group);
    return build;
}

}
