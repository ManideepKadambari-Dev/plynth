#include "plynth/core/commands/DeleteCommands.h"

#include "plynth/core/commands/PointCommands.h"
#include "plynth/core/commands/RoomCommands.h"
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
    std::set<Id> points;
    std::set<Id> walls;
    std::set<Id> rooms;
    for (const Id& id : ids) {
        const Floor* found = findFloorContaining(plan, id);
        if (!found) return refuse(ErrorCode::NotFound, "There is no point, wall or room " + id + ".");
        if (floor && floor != found) return refuse(ErrorCode::InvalidValue, "The items are on different floors.");
        floor = found;
        if (findPoint(*found, id)) {
            points.insert(id);
        } else if (findWall(*found, id)) {
            walls.insert(id);
        } else if (findRoom(*found, id)) {
            rooms.insert(id);
        } else {
            return refuse(ErrorCode::NotFound, "The id " + id + " is not a point, wall or room.");
        }
    }

    for (const Wall& w : floor->walls) {
        if (points.count(w.from) || points.count(w.to)) walls.insert(w.id);
    }
    for (const Opening& o : floor->openings) {
        if (walls.count(o.wall)) {
            return refuse(ErrorCode::InUse, "The wall " + o.wall + " has the opening " + o.id + " on it.");
        }
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
