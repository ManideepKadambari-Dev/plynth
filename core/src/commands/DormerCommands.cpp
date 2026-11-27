#include "plynth/core/commands/DormerCommands.h"

#include "../parts/DormerShape.h"
#include "plynth/core/BuildGeometry.h"
#include "plynth/core/Dormers.h"
#include "plynth/core/Roofs.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace plynth {

namespace {

// A dormer and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    Dormer* dormer = nullptr;
};

// Finds a dormer anywhere in the plan.
Located locate(Project& project, const Id& dormerId)
{
    for (Floor& floor : project.plan.floors) {
        if (Dormer* dormer = findDormer(floor, dormerId)) return {&floor, dormer};
    }
    return {};
}

// The refusal for a dormer that is not there.
Status noDormer(const Id& dormerId)
{
    return fail(ErrorCode::NotFound, "There is no dormer " + dormerId + ".");
}

// Checks that a dormer fits a pitched roof of its floor and overlaps none of the floor's other dormers that fit,
// leaving out the one with the id skip.
Status checkFit(const Plan& plan, const Floor& floor, const Dormer& dormer, const Id& skip)
{
    const std::optional<std::size_t> index = floorIndex(plan, floor.id);
    if (!index) return fail(ErrorCode::NotFound, "There is no floor " + floor.id + ".");
    const std::vector<RoofBuild> roofs = floorRoofs(plan, *index, floorElevations(plan)).roofs;
    const std::vector<RoofArea> areas = roofAreasOf(plan, floor.id);
    const dormers::Shape shape = dormers::shapeOf(dormer, floor.id, roofs, areas);
    if (!shape.code.empty()) return shape.status;
    for (const Dormer& other : floor.dormers) {
        if (other.id == skip) continue;
        const dormers::Shape there = dormers::shapeOf(other, floor.id, roofs, areas);
        if (there.code.empty() && dormers::overlaps(shape, there)) {
            return fail(ErrorCode::InvalidValue, "The dormer would overlap dormer " + other.id + ".");
        }
    }
    return {};
}

}

AddDormerCommand::AddDormerCommand(Id floorId, Dormer dormer) : floorId_(std::move(floorId)), dormer_(std::move(dormer))
{
    dormer_.id.clear();
}

// The id of the new dormer, empty until the first apply.
const Id& AddDormerCommand::newDormerId() const
{
    return dormer_.id;
}

std::string AddDormerCommand::name() const
{
    return "Add dormer";
}

// Checks the values and the fit, then adds the dormer with a new id, or the same one on redo.
Status AddDormerCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (const Status s = checkDormer(dormer_); !s.ok()) return s;
    if (const Status s = checkFit(project.plan, *floor, dormer_, Id()); !s.ok()) return s;
    if (dormer_.id.empty()) dormer_.id = newId(project.plan, kDormerIdPrefix);
    floor->dormers.push_back(dormer_);
    return {};
}

// Removes the dormer that apply added.
void AddDormerCommand::undo(Project& project)
{
    if (Floor* floor = findFloor(project.plan, floorId_)) {
        std::erase_if(floor->dormers, [&](const Dormer& d) { return d.id == dormer_.id; });
    }
}

unsigned AddDormerCommand::changes() const
{
    return Change::Roofs;
}

Id AddDormerCommand::floorId() const
{
    return floorId_;
}

SetDormerCommand::SetDormerCommand(Id dormerId, Dormer values, int mergeKey)
    : dormerId_(std::move(dormerId)), values_(std::move(values)), mergeKey_(mergeKey)
{
    values_.id = dormerId_;
}

std::string SetDormerCommand::name() const
{
    return "Change dormer";
}

// Checks the new values and the fit, then replaces the dormer's values, keeping the old ones for undo.
Status SetDormerCommand::apply(Project& project)
{
    const Located found = locate(project, dormerId_);
    if (!found.dormer) return noDormer(dormerId_);
    if (const Status s = checkDormer(values_); !s.ok()) return s;
    if (const Status s = checkFit(project.plan, *found.floor, values_, dormerId_); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.dormer;
    *found.dormer = values_;
    return {};
}

// Puts the old values back.
void SetDormerCommand::undo(Project& project)
{
    if (Dormer* dormer = locate(project, dormerId_).dormer) *dormer = previous_;
}

unsigned SetDormerCommand::changes() const
{
    return Change::Roofs;
}

Id SetDormerCommand::floorId() const
{
    return floorId_;
}

// Folds a later change of the same dormer with the same non zero merge key into this one, for a drag.
bool SetDormerCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetDormerCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->dormerId_ != dormerId_) return false;
    values_ = other->values_;
    return true;
}

RemoveDormerCommand::RemoveDormerCommand(Id dormerId) : dormerId_(std::move(dormerId)) {}

std::string RemoveDormerCommand::name() const
{
    return "Remove dormer";
}

// Removes the dormer, keeping it and its place for undo.
Status RemoveDormerCommand::apply(Project& project)
{
    const Located found = locate(project, dormerId_);
    if (!found.dormer) return noDormer(dormerId_);
    std::vector<Dormer>& list = found.floor->dormers;
    index_ = static_cast<std::size_t>(found.dormer - list.data());
    removed_ = *found.dormer;
    floorId_ = found.floor->id;
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the dormer back where it was.
void RemoveDormerCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::vector<Dormer>& list = floor->dormers;
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index_, list.size())), removed_);
}

unsigned RemoveDormerCommand::changes() const
{
    return Change::Roofs;
}

Id RemoveDormerCommand::floorId() const
{
    return floorId_;
}

}
