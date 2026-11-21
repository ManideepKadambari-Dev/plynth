#include "plynth/core/RoomTypes.h"

#include <array>

namespace plynth {

namespace {

constexpr std::array<RoomType, 9> kBuiltIn{{
    {"living", "Living room"},
    {"bedroom", "Bedroom"},
    {"kitchen", "Kitchen"},
    {"bathroom", "Bathroom"},
    {"hall", "Hall"},
    {"dining", "Dining room"},
    {"office", "Office"},
    {"storage", "Storage"},
    {"other", "Other"},
}};

}

// The list every library starts with.
std::span<const RoomType> builtInRoomTypes()
{
    return kBuiltIn;
}

// Looks a type up by id.
const RoomType* findRoomType(std::string_view id)
{
    for (const RoomType& t : kBuiltIn) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

// The name to show for a room's type.
std::string roomTypeName(const std::optional<std::string>& type)
{
    if (!type || type->empty()) return "Room";
    if (const RoomType* t = findRoomType(*type)) return std::string(t->name);
    return *type;
}

// Refuses an id that is not in the list, naming the ones that are.
Status checkRoomType(std::string_view id)
{
    if (findRoomType(id)) return {};
    std::string known;
    for (const RoomType& t : kBuiltIn) {
        if (!known.empty()) known += ", ";
        known += t.id;
    }
    return fail(ErrorCode::InvalidValue,
                "The room type \"" + std::string(id) + "\" is not known. Known types: " + known + ".");
}

}
