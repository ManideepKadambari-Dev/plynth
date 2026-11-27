#include "plynth/core/commands/StairCommands.h"

#include "plynth/core/Items.h"
#include "plynth/core/Stairs.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace plynth {

namespace {

// A stair and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    Stair* stair = nullptr;
};

// Finds a stair anywhere in the plan.
Located locate(Project& project, const Id& stairId)
{
    Located found;
    for (Floor& floor : project.plan.floors) {
        if (Stair* stair = findStair(floor, stairId)) {
            found.floor = &floor;
            found.stair = stair;
            break;
        }
    }
    return found;
}

// The refusal for a stair that is not there.
Status noStair(const Id& stairId)
{
    return fail(ErrorCode::NotFound, "There is no stair " + stairId + ".");
}

// Puts the rotation in range when it is a finite number; other values are left for checkStair to refuse.
void normalize(Stair& stair)
{
    if (std::isfinite(stair.rotation)) stair.rotation = normalizeRotation(stair.rotation);
}

}

AddStairCommand::AddStairCommand(Id floorId, Stair stair) : floorId_(std::move(floorId)), stair_(std::move(stair))
{
    stair_.id.clear();
}

// The id of the new stair, empty until the first apply.
const Id& AddStairCommand::newStairId() const
{
    return stair_.id;
}

std::string AddStairCommand::name() const
{
    return "Add stair";
}

// Checks the stair and adds it at the end of the floor's list.
Status AddStairCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!floorAbove(project.plan, floorId_)) {
        return fail(ErrorCode::InvalidValue, "Add a floor above before placing a stair.");
    }
    Stair stair = stair_;
    normalize(stair);
    if (const Status s = checkStair(stair); !s.ok()) return s;
    if (stair.id.empty()) {
        stair.id = newId(project.plan, kStairIdPrefix);
    } else if (idInUse(project.plan, stair.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + stair.id + " is already used.");
    }
    stair_.id = stair.id;
    floor->stairs.push_back(std::move(stair));
    return {};
}

// Removes the stair that apply added.
void AddStairCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->stairs, [&](const Stair& s) { return s.id == stair_.id; });
}

unsigned AddStairCommand::changes() const
{
    return Change::Stairs;
}

Id AddStairCommand::floorId() const
{
    return floorId_;
}

MoveStairCommand::MoveStairCommand(Id stairId, double x, double y, double rotation, int mergeKey)
    : stairId_(std::move(stairId)), x_(x), y_(y), rotation_(rotation), mergeKey_(mergeKey)
{
}

std::string MoveStairCommand::name() const
{
    return "Move stair";
}

// Checks the numbers and moves the stair, keeping its old state for undo.
Status MoveStairCommand::apply(Project& project)
{
    const Located found = locate(project, stairId_);
    if (!found.stair) return noStair(stairId_);
    if (!(std::isfinite(x_) && std::isfinite(y_) && std::isfinite(rotation_))) {
        return fail(ErrorCode::InvalidValue, "The stair position and rotation must be finite numbers.");
    }
    floorId_ = found.floor->id;
    previous_ = *found.stair;
    found.stair->x = x_;
    found.stair->y = y_;
    found.stair->rotation = normalizeRotation(rotation_);
    return {};
}

// Puts the stair back as it was.
void MoveStairCommand::undo(Project& project)
{
    const Located found = locate(project, stairId_);
    if (found.stair) *found.stair = previous_;
}

unsigned MoveStairCommand::changes() const
{
    return Change::Stairs;
}

Id MoveStairCommand::floorId() const
{
    return floorId_;
}

// Folds the next move of the same stair and key into this one.
bool MoveStairCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MoveStairCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->stairId_ != stairId_) return false;
    x_ = other->x_;
    y_ = other->y_;
    rotation_ = other->rotation_;
    return true;
}

SetStairCommand::SetStairCommand(Id stairId, Stair values) : stairId_(std::move(stairId)), values_(std::move(values))
{
    values_.id = stairId_;
}

std::string SetStairCommand::name() const
{
    return "Change stair";
}

// Checks the values and replaces everything but the id.
Status SetStairCommand::apply(Project& project)
{
    const Located found = locate(project, stairId_);
    if (!found.stair) return noStair(stairId_);
    Stair values = values_;
    normalize(values);
    if (const Status s = checkStair(values); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.stair;
    *found.stair = std::move(values);
    return {};
}

// Puts the stair back as it was.
void SetStairCommand::undo(Project& project)
{
    const Located found = locate(project, stairId_);
    if (found.stair) *found.stair = previous_;
}

unsigned SetStairCommand::changes() const
{
    return Change::Stairs;
}

Id SetStairCommand::floorId() const
{
    return floorId_;
}

RemoveStairCommand::RemoveStairCommand(Id stairId) : stairId_(std::move(stairId)) {}

std::string RemoveStairCommand::name() const
{
    return "Remove stair";
}

// Takes the stair out of its floor with the step light strips that name it, remembering where it was.
Status RemoveStairCommand::apply(Project& project)
{
    const Located found = locate(project, stairId_);
    if (!found.stair) return noStair(stairId_);
    floorId_ = found.floor->id;
    removed_ = *found.stair;
    index_ = static_cast<std::size_t>(found.stair - found.floor->stairs.data());
    found.floor->stairs.erase(found.floor->stairs.begin() + static_cast<std::ptrdiff_t>(index_));
    std::vector<LightStrip>& strips = found.floor->lightStrips;
    const std::optional<Id> named(stairId_);
    stripsChanged_ = std::any_of(strips.begin(), strips.end(), [&](const LightStrip& l) { return l.stair == named; });
    previousStrips_.clear();
    if (stripsChanged_) {
        previousStrips_ = strips;
        std::erase_if(strips, [&](const LightStrip& l) { return l.kind == LightStripKind::Step && l.stair == named; });
        for (LightStrip& l : strips) {
            if (l.stair == named) l.stair.reset();
        }
    }
    return {};
}

// Puts the stair back in its old place in the list, and the light strips as they were.
void RemoveStairCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->stairs.size());
    floor->stairs.insert(floor->stairs.begin() + static_cast<std::ptrdiff_t>(at), removed_);
    if (stripsChanged_) floor->lightStrips = previousStrips_;
}

unsigned RemoveStairCommand::changes() const
{
    return stripsChanged_ ? (Change::Stairs | Change::Lights) : Change::Stairs;
}

Id RemoveStairCommand::floorId() const
{
    return floorId_;
}

}
