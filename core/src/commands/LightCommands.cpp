#include "plynth/core/commands/LightCommands.h"

#include "plynth/core/Rooms.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace plynth {

namespace {

// A light and the floor it is on.
struct Located {
    Floor* floor = nullptr;
    LightMarker* light = nullptr;
};

// Finds a light anywhere in the plan.
Located locate(Project& project, const Id& lightId)
{
    for (Floor& floor : project.plan.floors) {
        if (LightMarker* light = findLight(floor, lightId)) return {&floor, light};
    }
    return {};
}

// The refusal for a light that is not there.
Status noLight(const Id& lightId)
{
    return fail(ErrorCode::NotFound, "There is no light " + lightId + ".");
}

// Checks a height against a floor's ceiling.
Status checkHeight(const Floor& floor, double z)
{
    if (!std::isfinite(z) || z < 0.0 || z > floor.ceilingHeight + 1e-9) {
        return fail(ErrorCode::InvalidValue, "The light height must be from 0 to the ceiling height of the floor.");
    }
    return {};
}

// Gives a light the room its position is in, or none.
void setRoom(const Floor& floor, LightMarker& light)
{
    const Id room = roomAt(floor, {light.x, light.y});
    if (room.empty()) light.room.reset();
    else light.room = room;
}

}

AddLightCommand::AddLightCommand(Id floorId, LightMarker light) : floorId_(std::move(floorId)), light_(std::move(light))
{
    light_.id.clear();
}

// The id of the new light, empty until the first apply.
const Id& AddLightCommand::newLightId() const
{
    return light_.id;
}

std::string AddLightCommand::name() const
{
    return "Add light";
}

// Checks the light and adds it at the end of the floor's list.
Status AddLightCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!std::isfinite(light_.x) || !std::isfinite(light_.y)) {
        return fail(ErrorCode::InvalidValue, "The light position must be finite numbers.");
    }
    if (const Status s = checkHeight(*floor, light_.z); !s.ok()) return s;
    LightMarker light = light_;
    if (light.id.empty()) light.id = newId(project.plan, kLightIdPrefix);
    setRoom(*floor, light);
    light_.id = light.id;
    floor->lights.push_back(std::move(light));
    return {};
}

// Removes the light that apply added.
void AddLightCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->lights, [&](const LightMarker& l) { return l.id == light_.id; });
}

unsigned AddLightCommand::changes() const
{
    return Change::Lights;
}

Id AddLightCommand::floorId() const
{
    return floorId_;
}

MoveLightCommand::MoveLightCommand(Id lightId, double x, double y, int mergeKey)
    : lightId_(std::move(lightId)), x_(x), y_(y), mergeKey_(mergeKey)
{
}

std::string MoveLightCommand::name() const
{
    return "Move light";
}

// Moves the light and gives it the room it is now in.
Status MoveLightCommand::apply(Project& project)
{
    const Located found = locate(project, lightId_);
    if (!found.light) return noLight(lightId_);
    if (!std::isfinite(x_) || !std::isfinite(y_)) return fail(ErrorCode::InvalidValue, "The light position must be finite numbers.");
    floorId_ = found.floor->id;
    previous_ = *found.light;
    found.light->x = x_;
    found.light->y = y_;
    setRoom(*found.floor, *found.light);
    return {};
}

// Puts the light back as it was.
void MoveLightCommand::undo(Project& project)
{
    const Located found = locate(project, lightId_);
    if (found.light) *found.light = previous_;
}

unsigned MoveLightCommand::changes() const
{
    return Change::Lights;
}

Id MoveLightCommand::floorId() const
{
    return floorId_;
}

// Takes over a later move of the same light in the same drag.
bool MoveLightCommand::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const MoveLightCommand*>(&next);
    if (!other || mergeKey_ == 0 || other->mergeKey_ != mergeKey_ || other->lightId_ != lightId_) return false;
    x_ = other->x_;
    y_ = other->y_;
    return true;
}

SetLightCommand::SetLightCommand(Id lightId, LightKind kind, double z) : lightId_(std::move(lightId)), kind_(kind), z_(z) {}

std::string SetLightCommand::name() const
{
    return "Change light";
}

// Checks the height and sets the kind and height.
Status SetLightCommand::apply(Project& project)
{
    const Located found = locate(project, lightId_);
    if (!found.light) return noLight(lightId_);
    if (const Status s = checkHeight(*found.floor, z_); !s.ok()) return s;
    floorId_ = found.floor->id;
    previous_ = *found.light;
    found.light->kind = kind_;
    found.light->z = z_;
    return {};
}

// Puts the light back as it was.
void SetLightCommand::undo(Project& project)
{
    const Located found = locate(project, lightId_);
    if (found.light) *found.light = previous_;
}

unsigned SetLightCommand::changes() const
{
    return Change::Lights;
}

Id SetLightCommand::floorId() const
{
    return floorId_;
}

RemoveLightCommand::RemoveLightCommand(Id lightId) : lightId_(std::move(lightId)) {}

std::string RemoveLightCommand::name() const
{
    return "Remove light";
}

// Removes the light, remembering its place in the list.
Status RemoveLightCommand::apply(Project& project)
{
    const Located found = locate(project, lightId_);
    if (!found.light) return noLight(lightId_);
    floorId_ = found.floor->id;
    index_ = static_cast<std::size_t>(found.light - found.floor->lights.data());
    removed_ = *found.light;
    found.floor->lights.erase(found.floor->lights.begin() + static_cast<std::ptrdiff_t>(index_));
    return {};
}

// Puts the light back at its old place.
void RemoveLightCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->lights.size());
    floor->lights.insert(floor->lights.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

unsigned RemoveLightCommand::changes() const
{
    return Change::Lights;
}

Id RemoveLightCommand::floorId() const
{
    return floorId_;
}

}
