#include "plynth/core/commands/CanopyCommands.h"

#include "plynth/core/Canopies.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace plynth {

namespace {

// A canopy and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    Canopy* canopy = nullptr;
};

// Finds a canopy anywhere in the plan.
Located locate(Project& project, const Id& canopyId)
{
    for (Floor& floor : project.plan.floors) {
        if (Canopy* canopy = findCanopy(floor, canopyId)) return {&floor, canopy};
    }
    return {};
}

// The refusal for a canopy that is not there.
Status noCanopy(const Id& canopyId)
{
    return fail(ErrorCode::NotFound, "There is no canopy " + canopyId + ".");
}

}

AddCanopyCommand::AddCanopyCommand(Id floorId, Canopy canopy) : floorId_(std::move(floorId)), canopy_(std::move(canopy))
{
    canopy_.id.clear();
}

// The id of the new canopy, empty until the first apply.
const Id& AddCanopyCommand::newCanopyId() const
{
    return canopy_.id;
}

std::string AddCanopyCommand::name() const
{
    return "Add canopy";
}

// Checks the canopy and adds it at the end of the floor's list, with the same id on redo.
Status AddCanopyCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (const Status s = checkCanopy(canopy_); !s.ok()) return s;
    Canopy canopy = canopy_;
    if (canopy.id.empty()) canopy.id = newId(project.plan, kCanopyIdPrefix);
    canopy_.id = canopy.id;
    floor->canopies.push_back(std::move(canopy));
    return {};
}

// Removes the canopy that apply added.
void AddCanopyCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->canopies, [&](const Canopy& c) { return c.id == canopy_.id; });
}

unsigned AddCanopyCommand::changes() const
{
    return Change::Outdoor;
}

Id AddCanopyCommand::floorId() const
{
    return floorId_;
}

SetCanopyCommand::SetCanopyCommand(Id canopyId, Canopy values, int mergeKey)
    : canopyId_(std::move(canopyId)), values_(std::move(values)), mergeKey_(mergeKey)
{
    values_.id = canopyId_;
}

std::string SetCanopyCommand::name() const
{
    return "Change canopy";
}

// Checks the new values and puts them on the canopy, keeping its id.
Status SetCanopyCommand::apply(Project& project)
{
    const Located found = locate(project, canopyId_);
    if (!found.canopy) return noCanopy(canopyId_);
    if (const Status s = checkCanopy(values_); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.canopy;
    *found.canopy = values_;
    found.canopy->id = canopyId_;
    return {};
}

// Puts the canopy back as it was.
void SetCanopyCommand::undo(Project& project)
{
    const Located found = locate(project, canopyId_);
    if (found.canopy) *found.canopy = previous_;
}

unsigned SetCanopyCommand::changes() const
{
    return Change::Outdoor;
}

Id SetCanopyCommand::floorId() const
{
    return floorId_;
}

// Takes over a later change of the same canopy in the same drag.
bool SetCanopyCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetCanopyCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->canopyId_ != canopyId_) return false;
    values_ = other->values_;
    return true;
}

RemoveCanopyCommand::RemoveCanopyCommand(Id canopyId) : canopyId_(std::move(canopyId)) {}

std::string RemoveCanopyCommand::name() const
{
    return "Remove canopy";
}

// Removes the canopy, remembering its place in the list.
Status RemoveCanopyCommand::apply(Project& project)
{
    const Located found = locate(project, canopyId_);
    if (!found.canopy) return noCanopy(canopyId_);
    floorId_ = found.floor->id;
    index_ = static_cast<std::size_t>(found.canopy - found.floor->canopies.data());
    removed_ = *found.canopy;
    found.floor->canopies.erase(found.floor->canopies.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the canopy back at its old place.
void RemoveCanopyCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->canopies.size());
    floor->canopies.insert(floor->canopies.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveCanopyCommand::changes() const
{
    return Change::Outdoor;
}

Id RemoveCanopyCommand::floorId() const
{
    return floorId_;
}

}
