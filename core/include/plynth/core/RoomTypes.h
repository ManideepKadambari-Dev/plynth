#pragma once

#include "Status.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// A kind of room, such as a kitchen. The plan stores only the id, so the list can grow or be edited
// without changing plan files. The user's list lives in the assets catalog (AssetCatalog::roomTypes).
// Whoever opens the library hands that list to useRoomTypes, and every function below then reads it.
struct RoomType {
    std::string id;   // stable, stored in plan.json, for example "living"
    std::string name; // shown to the user, for example "Living room"

    bool operator==(const RoomType&) const = default;
};

// The room types every library starts with: living, bedroom, kitchen, bathroom, hall, dining, office,
// storage, other. In this order, which is the order the room type picker shows.
std::span<const RoomType> builtInRoomTypes();

// The room types in use, in picker order: the list given to useRoomTypes, or the built-in list before that.
std::span<const RoomType> roomTypes();

// Replaces the room types in use. The app calls it after the library opens and after each room type edit;
// the CLI calls it once before a command that reads or sets room types. Not thread safe: call it from the
// thread that reads room types. An empty list goes back to the built-in list.
void useRoomTypes(std::vector<RoomType> types);

// Finds a room type in roomTypes() by id. Null when there is none. The pointer stays valid until the next
// useRoomTypes call.
const RoomType* findRoomType(std::string_view id);

// The name to show for a room's type: the type's name, the id itself when the type is not in the list
// (a plan made on another machine), or "Room" when the room has no type yet.
std::string roomTypeName(const std::optional<std::string>& type);

// Checks that a room type id is in roomTypes(). InvalidValue naming the known ids when it is not.
// Commands accept any well formed name so plans with unknown types still load and edit; the app and the
// CLI call this before they set a type the user typed or picked.
Status checkRoomType(std::string_view id);

}
