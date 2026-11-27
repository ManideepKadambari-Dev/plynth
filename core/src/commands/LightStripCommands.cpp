#include "plynth/core/commands/LightStripCommands.h"

#include "plynth/core/LightStrips.h"

#include <algorithm>
#include <utility>

namespace plynth {

namespace {

// A light strip and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    LightStrip* strip = nullptr;
};

// Finds a light strip anywhere in the plan.
Located locate(Project& project, const Id& stripId)
{
    for (Floor& floor : project.plan.floors) {
        if (LightStrip* strip = findLightStrip(floor, stripId)) return {&floor, strip};
    }
    return {};
}

// The refusal for a light strip that is not there.
Status noStrip(const Id& stripId)
{
    return fail(ErrorCode::NotFound, "There is no light strip " + stripId + ".");
}

}

AddLightStripCommand::AddLightStripCommand(Id floorId, LightStrip strip) : floorId_(std::move(floorId)), strip_(std::move(strip))
{
    strip_.id.clear();
}

// The id of the new strip, empty until the first apply.
const Id& AddLightStripCommand::newStripId() const
{
    return strip_.id;
}

std::string AddLightStripCommand::name() const
{
    return "Add light strip";
}

// Checks the strip and adds it at the end of the floor's list.
Status AddLightStripCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (const Status s = checkLightStrip(strip_, *floor); !s.ok()) return s;
    LightStrip strip = strip_;
    if (strip.id.empty()) strip.id = newId(project.plan, kLightStripIdPrefix);
    strip_.id = strip.id;
    floor->lightStrips.push_back(std::move(strip));
    return {};
}

// Removes the strip that apply added.
void AddLightStripCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->lightStrips, [&](const LightStrip& l) { return l.id == strip_.id; });
}

unsigned AddLightStripCommand::changes() const
{
    return Change::Lights;
}

Id AddLightStripCommand::floorId() const
{
    return floorId_;
}

SetLightStripCommand::SetLightStripCommand(Id stripId, LightStrip values, int mergeKey)
    : stripId_(std::move(stripId)), values_(std::move(values)), mergeKey_(mergeKey)
{
    values_.id = stripId_;
}

std::string SetLightStripCommand::name() const
{
    return "Change light strip";
}

// Checks the new values and puts them in place of the old ones, keeping the id.
Status SetLightStripCommand::apply(Project& project)
{
    const Located found = locate(project, stripId_);
    if (!found.strip) return noStrip(stripId_);
    if (const Status s = checkLightStrip(values_, *found.floor); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.strip;
    *found.strip = values_;
    return {};
}

// Puts the strip back as it was.
void SetLightStripCommand::undo(Project& project)
{
    const Located found = locate(project, stripId_);
    if (found.strip) *found.strip = previous_;
}

unsigned SetLightStripCommand::changes() const
{
    return Change::Lights;
}

Id SetLightStripCommand::floorId() const
{
    return floorId_;
}

// Takes over a later change of the same strip in the same drag or edit.
bool SetLightStripCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetLightStripCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->stripId_ != stripId_) return false;
    values_ = other->values_;
    return true;
}

RemoveLightStripCommand::RemoveLightStripCommand(Id stripId) : stripId_(std::move(stripId)) {}

std::string RemoveLightStripCommand::name() const
{
    return "Remove light strip";
}

// Removes the strip, remembering its place in the list.
Status RemoveLightStripCommand::apply(Project& project)
{
    const Located found = locate(project, stripId_);
    if (!found.strip) return noStrip(stripId_);
    floorId_ = found.floor->id;
    index_ = static_cast<std::size_t>(found.strip - found.floor->lightStrips.data());
    removed_ = *found.strip;
    found.floor->lightStrips.erase(found.floor->lightStrips.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the strip back at its old place.
void RemoveLightStripCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->lightStrips.size());
    floor->lightStrips.insert(floor->lightStrips.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveLightStripCommand::changes() const
{
    return Change::Lights;
}

Id RemoveLightStripCommand::floorId() const
{
    return floorId_;
}

}
