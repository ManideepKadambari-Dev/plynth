#include "plynth/core/MaterialSlots.h"

#include <string>

namespace plynth {

namespace {

bool isLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

}

// Turns an id into PascalCase.
std::string slotPascalCase(std::string_view id, std::string_view fallback)
{
    std::string out;
    bool startOfPart = true;
    for (const char c : id) {
        if (c == '_' || c == '-' || c == ' ') {
            startOfPart = true;
            continue;
        }
        if (!isLetter(c) && !isDigit(c)) continue;
        if (startOfPart && c >= 'a' && c <= 'z') {
            out += static_cast<char>(c - 'a' + 'A');
        } else {
            out += c;
        }
        startOfPart = false;
    }
    if (out.empty()) return std::string(fallback);
    if (isDigit(out.front())) return std::string(fallback) + out;
    return out;
}

// The label of a room's slots.
std::string roomSlotLabel(const Room& room)
{
    if (!room.type) return std::string(kNoRoomTypeLabel);
    return slotPascalCase(*room.type, kNoRoomTypeLabel);
}

// The name of a slot whose plain name shows more than one material.
std::string slotNameWithMaterial(std::string_view base, std::string_view material)
{
    std::string suffix = slotPascalCase(material, "");
    if (suffix.empty()) suffix = std::string(kNeutralSlotSuffix);
    return std::string(base) + "_" + suffix;
}

}
