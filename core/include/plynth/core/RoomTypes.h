#pragma once

#include "Status.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace plynth {

// A kind of room, such as a kitchen. The plan stores only the id, so the list can grow or be edited
// without changing plan files. Milestone 6 moves the list into the assets catalog, which starts from these
// entries with the same ids; these functions then read the catalog.
struct RoomType {
    std::string_view id;   // stable, stored in plan.json, for example "living"
    std::string_view name; // shown to the user, for example "Living room"
};

// The room types every library starts with: living, bedroom, kitchen, bathroom, hall, dining, office,
// storage, other. In this order, which is the order the room type picker shows.
std::span<const RoomType> builtInRoomTypes();

// Finds a room type by id. Null when there is none.
const RoomType* findRoomType(std::string_view id);

// The name to show for a room's type: the type's name, the id itself when the type is not in the list
// (a plan made on another machine), or "Room" when the room has no type yet.
std::string roomTypeName(const std::optional<std::string>& type);

// Checks that a room type id is in the list. InvalidValue naming the known ids when it is not.
// Commands accept any well formed name so plans with unknown types still load and edit; the app and the
// CLI call this before they set a type the user typed or picked.
Status checkRoomType(std::string_view id);

}
