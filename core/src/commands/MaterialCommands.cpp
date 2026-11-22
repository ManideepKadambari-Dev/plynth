#include "plynth/core/commands/MaterialCommands.h"

#include "CommandSupport.h"

#include <utility>

namespace plynth {

namespace {

// Checks an optional material name is well formed.
Status checkMaterialName(const std::optional<std::string>& material)
{
    if (material && !detail::isMaterialName(*material)) {
        return fail(ErrorCode::InvalidValue,
                    "The material \"" + *material + "\" is not a valid name. Use letters, digits, \"_\" and \"-\".");
    }
    return {};
}

}

SetWallMaterialCommand::SetWallMaterialCommand(Id wallId, WallFace face, std::optional<std::string> material)
    : wallId_(std::move(wallId)), face_(face), material_(std::move(material))
{
}

std::string SetWallMaterialCommand::name() const
{
    return face_ == WallFace::Inside ? "Set inside material" : "Set outside material";
}

// Sets the inside or outside material of the wall, remembering the old one.
Status SetWallMaterialCommand::apply(Project& project)
{
    if (const Status s = checkMaterialName(material_); !s.ok()) return s;
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    Wall* wall = findWall(*floor, wallId_);
    std::optional<std::string>& field = face_ == WallFace::Inside ? wall->materialInside : wall->materialOutside;
    previous_ = field;
    field = material_;
    floorId_ = floor->id;
    return {};
}

// Puts the old material back.
void SetWallMaterialCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    Wall* wall = floor ? findWall(*floor, wallId_) : nullptr;
    if (!wall) return;
    (face_ == WallFace::Inside ? wall->materialInside : wall->materialOutside) = previous_;
}

unsigned SetWallMaterialCommand::changes() const
{
    return Change::Walls;
}

Id SetWallMaterialCommand::floorId() const
{
    return floorId_;
}

SetRoomMaterialCommand::SetRoomMaterialCommand(Id roomId, RoomSurface surface, std::optional<std::string> material)
    : roomId_(std::move(roomId)), surface_(surface), material_(std::move(material))
{
}

std::string SetRoomMaterialCommand::name() const
{
    switch (surface_) {
    case RoomSurface::Floor: return "Set floor material";
    case RoomSurface::Walls: return "Set wall material";
    case RoomSurface::Ceiling: return "Set ceiling material";
    }
    return "Set room material";
}

namespace {

// The field of a room that holds one surface.
std::optional<std::string>& roomField(Room& room, RoomSurface surface)
{
    switch (surface) {
    case RoomSurface::Floor: return room.floorMaterial;
    case RoomSurface::Walls: return room.wallMaterial;
    case RoomSurface::Ceiling: return room.ceilingMaterial;
    }
    return room.floorMaterial;
}

}

// Sets the floor, wall or ceiling material of the room, remembering the old one.
Status SetRoomMaterialCommand::apply(Project& project)
{
    if (const Status s = checkMaterialName(material_); !s.ok()) return s;
    Floor* floor = findFloorContaining(project.plan, roomId_);
    Room* room = floor ? findRoom(*floor, roomId_) : nullptr;
    if (!room) return fail(ErrorCode::NotFound, "There is no room " + roomId_ + ".");
    std::optional<std::string>& field = roomField(*room, surface_);
    previous_ = field;
    field = material_;
    floorId_ = floor->id;
    return {};
}

// Puts the old material back.
void SetRoomMaterialCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    Room* room = floor ? findRoom(*floor, roomId_) : nullptr;
    if (!room) return;
    roomField(*room, surface_) = previous_;
}

unsigned SetRoomMaterialCommand::changes() const
{
    return Change::Rooms;
}

Id SetRoomMaterialCommand::floorId() const
{
    return floorId_;
}

SetDefaultMaterialCommand::SetDefaultMaterialCommand(Surface surface, std::optional<std::string> material)
    : surface_(surface), material_(std::move(material))
{
}

std::string SetDefaultMaterialCommand::name() const
{
    return "Set default material";
}

// Sets the project default for the surface, remembering the old one.
Status SetDefaultMaterialCommand::apply(Project& project)
{
    if (const Status s = checkMaterialName(material_); !s.ok()) return s;
    previous_ = materialDefault(project.settings.materials, surface_);
    setMaterialDefault(project.settings.materials, surface_, material_);
    return {};
}

// Puts the old default back.
void SetDefaultMaterialCommand::undo(Project& project)
{
    setMaterialDefault(project.settings.materials, surface_, previous_);
}

unsigned SetDefaultMaterialCommand::changes() const
{
    return Change::Settings;
}

}
