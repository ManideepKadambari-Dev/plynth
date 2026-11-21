#include "plynth/core/RoomTypes.h"

#include <utility>

namespace plynth {

namespace {

// The built-in list, made once.
const std::vector<RoomType>& builtIn()
{
    static const std::vector<RoomType> types{
        {"living", "Living room"},
        {"bedroom", "Bedroom"},
        {"kitchen", "Kitchen"},
        {"bathroom", "Bathroom"},
        {"hall", "Hall"},
        {"dining", "Dining room"},
        {"office", "Office"},
        {"storage", "Storage"},
        {"other", "Other"},
    };
    return types;
}

// The list given to useRoomTypes. Empty means the built-in list is in use.
std::vector<RoomType>& inUse()
{
    static std::vector<RoomType> types;
    return types;
}

}

// The list every library starts with.
std::span<const RoomType> builtInRoomTypes()
{
    return builtIn();
}

// The room types in use.
std::span<const RoomType> roomTypes()
{
    return inUse().empty() ? std::span<const RoomType>(builtIn()) : std::span<const RoomType>(inUse());
}

// Replaces the room types in use.
void useRoomTypes(std::vector<RoomType> types)
{
    inUse() = std::move(types);
}

// Looks a type up by id.
const RoomType* findRoomType(std::string_view id)
{
    for (const RoomType& t : roomTypes()) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

// The name to show for a room's type.
std::string roomTypeName(const std::optional<std::string>& type)
{
    if (!type || type->empty()) return "Room";
    if (const RoomType* t = findRoomType(*type)) return t->name;
    return *type;
}

// Refuses an id that is not in the list, naming the ones that are.
Status checkRoomType(std::string_view id)
{
    if (findRoomType(id)) return {};
    std::string known;
    for (const RoomType& t : roomTypes()) {
        if (!known.empty()) known += ", ";
        known += t.id;
    }
    return fail(ErrorCode::InvalidValue,
                "The room type \"" + std::string(id) + "\" is not known. Known types: " + known + ".");
}

}
