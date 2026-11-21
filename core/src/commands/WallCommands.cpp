#include "plynth/core/commands/WallCommands.h"

#include "CommandSupport.h"

#include <algorithm>
#include <utility>

namespace plynth {

AddWallCommand::AddWallCommand(Id fromPointId, Id toPointId, double thickness, std::optional<double> height)
{
    wall_.from = std::move(fromPointId);
    wall_.to = std::move(toPointId);
    wall_.thickness = thickness;
    wall_.height = height;
}

// The id of the new wall, empty until the first apply.
const Id& AddWallCommand::wallId() const
{
    return wall_.id;
}

std::string AddWallCommand::name() const
{
    return "Add wall";
}

// Adds the wall after checking its points, with a new id on the first call and the same id on redo.
Status AddWallCommand::apply(Project& project)
{
    Floor* fromFloor = detail::floorOfPoint(project.plan, wall_.from);
    if (!fromFloor) return fail(ErrorCode::NotFound, "There is no point " + wall_.from + ".");
    Floor* toFloor = detail::floorOfPoint(project.plan, wall_.to);
    if (!toFloor) return fail(ErrorCode::NotFound, "There is no point " + wall_.to + ".");
    if (wall_.from == wall_.to) return fail(ErrorCode::InvalidValue, "A wall needs two different points.");
    if (fromFloor != toFloor) return fail(ErrorCode::InvalidValue, "The two points are on different floors.");
    if (!detail::isPositive(wall_.thickness)) {
        return fail(ErrorCode::InvalidValue, "The wall thickness must be a number above zero.");
    }
    if (wall_.height && !detail::isPositive(*wall_.height)) {
        return fail(ErrorCode::InvalidValue, "The wall height must be a number above zero.");
    }
    for (const Wall& w : fromFloor->walls) {
        const bool same = (w.from == wall_.from && w.to == wall_.to) || (w.from == wall_.to && w.to == wall_.from);
        if (same) return fail(ErrorCode::AlreadyExists, "The wall " + w.id + " already joins these two points.");
    }
    Wall w = wall_;
    if (w.id.empty()) {
        w.id = newId(project.plan, kWallIdPrefix);
    } else if (idInUse(project.plan, w.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + w.id + " is already used.");
    }
    wall_.id = w.id;
    floorId_ = fromFloor->id;
    fromFloor->walls.push_back(std::move(w));
    return {};
}

// Removes the wall that apply added.
void AddWallCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->walls, [&](const Wall& w) { return w.id == wall_.id; });
}

unsigned AddWallCommand::changes() const
{
    return Change::Walls;
}

Id AddWallCommand::floorId() const
{
    return floorId_;
}

RemoveWallCommand::RemoveWallCommand(Id wallId) : wallId_(std::move(wallId)) {}

std::string RemoveWallCommand::name() const
{
    return "Remove wall";
}

// Removes the wall unless a room or an opening uses it, remembering its place in the list.
Status RemoveWallCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    for (const Room& r : floor->rooms) {
        if (std::find(r.walls.begin(), r.walls.end(), wallId_) != r.walls.end()) {
            return fail(ErrorCode::InUse, "The wall " + wallId_ + " is used by the room " + r.id + ".");
        }
    }
    for (const Opening& o : floor->openings) {
        if (o.wall == wallId_) return fail(ErrorCode::InUse, "The wall " + wallId_ + " has the opening " + o.id + " on it.");
    }
    const auto it = std::find_if(floor->walls.begin(), floor->walls.end(), [&](const Wall& w) { return w.id == wallId_; });
    floorId_ = floor->id;
    index_ = static_cast<std::size_t>(it - floor->walls.begin());
    removed_ = *it;
    floor->walls.erase(it);
    return {};
}

// Puts the wall back at its old place.
void RemoveWallCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->walls.size());
    floor->walls.insert(floor->walls.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveWallCommand::changes() const
{
    return Change::Walls;
}

Id RemoveWallCommand::floorId() const
{
    return floorId_;
}

}
