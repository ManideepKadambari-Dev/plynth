#include "plynth/core/commands/RailingCommands.h"

#include "plynth/core/Railings.h"

#include <algorithm>
#include <utility>

namespace plynth {

namespace {

// A railing and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    Railing* railing = nullptr;
};

// Finds a railing anywhere in the plan.
Located locate(Project& project, const Id& railingId)
{
    Located found;
    for (Floor& floor : project.plan.floors) {
        if (Railing* railing = findRailing(floor, railingId)) {
            found.floor = &floor;
            found.railing = railing;
            break;
        }
    }
    return found;
}

// The refusal for a railing that is not there.
Status noRailing(const Id& railingId)
{
    return fail(ErrorCode::NotFound, "There is no railing " + railingId + ".");
}

// Checks that a railing's host, when it has one, is a deck of the floor.
Status checkHost(const Floor& floor, const Railing& railing)
{
    if (railing.host && !findDeck(floor, *railing.host)) {
        return fail(ErrorCode::NotFound, "There is no deck " + *railing.host + " on " + floor.name + ".");
    }
    return {};
}

}

AddRailingCommand::AddRailingCommand(Id floorId, Railing railing) : floorId_(std::move(floorId)), railing_(std::move(railing))
{
    railing_.id.clear();
}

// The id of the new railing, empty until the first apply.
const Id& AddRailingCommand::newRailingId() const
{
    return railing_.id;
}

std::string AddRailingCommand::name() const
{
    return "Add railing";
}

// Checks the railing and adds it at the end of the floor's list.
Status AddRailingCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    Railing railing = railing_;
    if (const Status s = checkHost(*floor, railing); !s.ok()) return s;
    if (const Status s = checkRailing(railing); !s.ok()) return s;
    if (railing.id.empty()) {
        railing.id = newId(project.plan, kRailingIdPrefix);
    } else if (idInUse(project.plan, railing.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + railing.id + " is already used.");
    }
    railing_.id = railing.id;
    floor->railings.push_back(std::move(railing));
    return {};
}

// Removes the railing that apply added.
void AddRailingCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->railings, [&](const Railing& r) { return r.id == railing_.id; });
}

unsigned AddRailingCommand::changes() const
{
    return Change::Outdoor;
}

Id AddRailingCommand::floorId() const
{
    return floorId_;
}

SetRailingCommand::SetRailingCommand(Id railingId, Railing values, int mergeKey)
    : railingId_(std::move(railingId)), values_(std::move(values)), mergeKey_(mergeKey)
{
    values_.id = railingId_;
}

std::string SetRailingCommand::name() const
{
    return "Change railing";
}

// Checks the values and replaces everything but the id, keeping the old railing for undo.
Status SetRailingCommand::apply(Project& project)
{
    const Located found = locate(project, railingId_);
    if (!found.railing) return noRailing(railingId_);
    if (const Status s = checkHost(*found.floor, values_); !s.ok()) return s;
    if (const Status s = checkRailing(values_); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.railing;
    *found.railing = values_;
    return {};
}

// Puts the railing back as it was.
void SetRailingCommand::undo(Project& project)
{
    const Located found = locate(project, railingId_);
    if (found.railing) *found.railing = previous_;
}

unsigned SetRailingCommand::changes() const
{
    return Change::Outdoor;
}

Id SetRailingCommand::floorId() const
{
    return floorId_;
}

// Folds the next change of the same railing and key into this one, so a drag undoes in one step.
bool SetRailingCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetRailingCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->railingId_ != railingId_) return false;
    values_ = other->values_;
    return true;
}

RemoveRailingCommand::RemoveRailingCommand(Id railingId) : railingId_(std::move(railingId)) {}

std::string RemoveRailingCommand::name() const
{
    return "Remove railing";
}

// Takes the railing out of its floor, remembering where it was.
Status RemoveRailingCommand::apply(Project& project)
{
    const Located found = locate(project, railingId_);
    if (!found.railing) return noRailing(railingId_);
    floorId_ = found.floor->id;
    removed_ = *found.railing;
    index_ = static_cast<std::size_t>(found.railing - found.floor->railings.data());
    found.floor->railings.erase(found.floor->railings.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the railing back in its old place.
void RemoveRailingCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->railings.size());
    floor->railings.insert(floor->railings.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveRailingCommand::changes() const
{
    return Change::Outdoor;
}

Id RemoveRailingCommand::floorId() const
{
    return floorId_;
}

}
