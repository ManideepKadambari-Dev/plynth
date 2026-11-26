#include "plynth/core/commands/RoomCommands.h"

#include "plynth/core/Rooms.h"

#include "CommandSupport.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace plynth {

namespace {

// Checks an optional room type is a well formed name.
Status checkTypeName(const std::optional<std::string>& type)
{
    if (type && !detail::isWellFormedName(*type)) {
        return fail(ErrorCode::InvalidValue, "The room type must be a name of letters, digits, \"_\" and \"-\".");
    }
    return {};
}

}

AddRoomCommand::AddRoomCommand(Id floorId, Vec2 position, std::optional<std::string> type)
    : floorId_(std::move(floorId)), position_(position)
{
    room_.type = std::move(type);
}

// The id of the new room, empty until the first apply.
const Id& AddRoomCommand::roomId() const
{
    return room_.id;
}

std::string AddRoomCommand::name() const
{
    return "Add room";
}

// Finds the closed area at the position and adds a room that lists its walls.
Status AddRoomCommand::apply(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no floor " + floorId_ + ".");
    if (!std::isfinite(position_.x) || !std::isfinite(position_.y)) {
        return fail(ErrorCode::InvalidValue, "The position must be finite numbers.");
    }
    if (const Status s = checkTypeName(room_.type); !s.ok()) return s;
    const std::optional<ClosedArea> area = findClosedAreaAt(*floor, position_);
    if (!area) return fail(ErrorCode::NotFound, "There is no closed area at that position. Close the walls around it first.");
    if (const Id existing = roomAt(*floor, position_); !existing.empty()) {
        return fail(ErrorCode::AlreadyExists, "The room " + existing + " is already there.");
    }
    Room r = room_;
    r.walls = area->walls;
    if (r.id.empty()) {
        r.id = newId(project.plan, kRoomIdPrefix);
    } else if (idInUse(project.plan, r.id)) {
        return fail(ErrorCode::AlreadyExists, "The id " + r.id + " is already used.");
    }
    room_.id = r.id;
    floor->rooms.push_back(std::move(r));
    itemsGiven_.clear();
    for (Item& item : floor->items) {
        if (item.room) continue;
        if (roomAt(*floor, {item.x, item.y}) != room_.id) continue;
        item.room = room_.id;
        itemsGiven_.push_back(item.id);
    }
    lightsGiven_.clear();
    for (LightMarker& light : floor->lights) {
        if (light.room) continue;
        if (roomAt(*floor, {light.x, light.y}) != room_.id) continue;
        light.room = room_.id;
        lightsGiven_.push_back(light.id);
    }
    return {};
}

// Removes the room that apply added.
void AddRoomCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    std::erase_if(floor->rooms, [&](const Room& r) { return r.id == room_.id; });
    for (const Id& id : itemsGiven_) {
        if (Item* item = findItem(*floor, id)) item->room.reset();
    }
    for (const Id& id : lightsGiven_) {
        if (LightMarker* light = findLight(*floor, id)) light->room.reset();
    }
}

unsigned AddRoomCommand::changes() const
{
    return Change::Rooms | Change::Items | Change::Lights;
}

Id AddRoomCommand::floorId() const
{
    return floorId_;
}

RemoveRoomCommand::RemoveRoomCommand(Id roomId) : roomId_(std::move(roomId)) {}

std::string RemoveRoomCommand::name() const
{
    return "Remove room";
}

// Removes the room, remembering its place in the list.
Status RemoveRoomCommand::apply(Project& project)
{
    Floor* floor = findFloorContaining(project.plan, roomId_);
    Room* room = floor ? findRoom(*floor, roomId_) : nullptr;
    if (!room) return fail(ErrorCode::NotFound, "There is no room " + roomId_ + ".");
    floorId_ = floor->id;
    index_ = static_cast<std::size_t>(room - floor->rooms.data());
    removed_ = *room;
    floor->rooms.erase(floor->rooms.begin() + static_cast<std::ptrdiff_t>(index_));
    itemsCleared_.clear();
    for (Item& item : floor->items) {
        if (item.room != roomId_) continue;
        item.room.reset();
        itemsCleared_.push_back(item.id);
    }
    lightsCleared_.clear();
    for (LightMarker& light : floor->lights) {
        if (light.room != roomId_) continue;
        light.room.reset();
        lightsCleared_.push_back(light.id);
    }
    return {};
}

// Puts the room back at its old place.
void RemoveRoomCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    const std::size_t at = std::min(index_, floor->rooms.size());
    floor->rooms.insert(floor->rooms.begin() + static_cast<std::ptrdiff_t>(at), removed_);
    for (const Id& id : itemsCleared_) {
        if (Item* item = findItem(*floor, id)) item->room = removed_.id;
    }
    for (const Id& id : lightsCleared_) {
        if (LightMarker* light = findLight(*floor, id)) light->room = removed_.id;
    }
}

unsigned RemoveRoomCommand::changes() const
{
    return Change::Rooms | Change::Items | Change::Lights;
}

Id RemoveRoomCommand::floorId() const
{
    return floorId_;
}

SetRoomTypeCommand::SetRoomTypeCommand(Id roomId, std::optional<std::string> type)
    : roomId_(std::move(roomId)), type_(std::move(type))
{
}

std::string SetRoomTypeCommand::name() const
{
    return type_ ? "Set room type" : "Clear room type";
}

// Sets or clears the type, remembering the old one.
Status SetRoomTypeCommand::apply(Project& project)
{
    Floor* floor = findFloorContaining(project.plan, roomId_);
    Room* room = floor ? findRoom(*floor, roomId_) : nullptr;
    if (!room) return fail(ErrorCode::NotFound, "There is no room " + roomId_ + ".");
    if (const Status s = checkTypeName(type_); !s.ok()) return s;
    floorId_ = floor->id;
    previous_ = room->type;
    room->type = type_;
    return {};
}

// Puts the old type back.
void SetRoomTypeCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    if (Room* room = findRoom(*floor, roomId_)) room->type = previous_;
}

unsigned SetRoomTypeCommand::changes() const
{
    return Change::Rooms;
}

Id SetRoomTypeCommand::floorId() const
{
    return floorId_;
}

}

namespace plynth {

SplitRoomsByWallCommand::SplitRoomsByWallCommand(Id wallId) : wallId_(std::move(wallId)) {}

// The rooms the last apply made.
const std::vector<Id>& SplitRoomsByWallCommand::newRoomIds() const
{
    return newRoomIds_;
}

std::string SplitRoomsByWallCommand::name() const
{
    return "Split room";
}

// Goes through the rooms in order and splits each one the wall cuts in two.
Status SplitRoomsByWallCommand::apply(Project& project)
{
    Floor* floor = detail::floorOfWall(project.plan, wallId_);
    if (!floor) return fail(ErrorCode::NotFound, "There is no wall " + wallId_ + ".");
    floorId_ = floor->id;
    previousRooms_ = floor->rooms;
    previousItems_ = floor->items;
    previousLights_ = floor->lights;
    newRoomIds_.clear();

    for (std::size_t i = 0; i < floor->rooms.size(); ++i) {
        const auto parts = roomSplitBy(*floor, floor->rooms[i], wallId_);
        if (!parts) continue;
        const bool firstKeeps = parts->first.centreLineArea >= parts->second.centreLineArea;
        const ClosedArea& kept = firstKeeps ? parts->first : parts->second;
        const ClosedArea& given = firstKeeps ? parts->second : parts->first;

        const std::size_t made = newRoomIds_.size();
        Id id = made < idsForRedo_.size() ? idsForRedo_[made] : newId(project.plan, kRoomIdPrefix);
        if (idInUse(project.plan, id)) id = newId(project.plan, kRoomIdPrefix);

        Room& room = floor->rooms[i];
        Room part = room;
        part.id = id;
        part.walls = given.walls;
        room.walls = kept.walls;
        for (Item& item : floor->items) {
            if (item.room == room.id && insideArea(given, {item.x, item.y})) item.room = id;
        }
        for (LightMarker& light : floor->lights) {
            if (light.room == room.id && insideArea(given, {light.x, light.y})) light.room = id;
        }
        floor->rooms.insert(floor->rooms.begin() + static_cast<std::ptrdiff_t>(i + 1), std::move(part));
        newRoomIds_.push_back(id);
        ++i;
    }
    if (idsForRedo_.size() < newRoomIds_.size()) idsForRedo_ = newRoomIds_;
    return {};
}

// Puts the rooms, items and light markers back as they were.
void SplitRoomsByWallCommand::undo(Project& project)
{
    Floor* floor = findFloor(project.plan, floorId_);
    if (!floor) return;
    floor->rooms = previousRooms_;
    floor->items = previousItems_;
    floor->lights = previousLights_;
}

unsigned SplitRoomsByWallCommand::changes() const
{
    return Change::Rooms | Change::Items | Change::Lights;
}

Id SplitRoomsByWallCommand::floorId() const
{
    return floorId_;
}

}
