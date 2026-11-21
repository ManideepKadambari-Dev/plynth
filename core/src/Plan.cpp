#include "plynth/core/Plan.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <type_traits>
#include <utility>

namespace plynth {

namespace {

using IdSet = std::set<std::string, std::less<>>;

// Finds an object by id in a list. Null when there is none.
template <typename List>
auto* findById(List& list, std::string_view id)
{
    using Item = std::remove_reference_t<decltype(list.front())>;
    for (auto& item : list) {
        if (item.id == id) return &item;
    }
    return static_cast<Item*>(nullptr);
}

// Gives the number after a prefix when the id is exactly the prefix plus digits, otherwise a negative value.
long long numberAfterPrefix(std::string_view id, std::string_view prefix)
{
    if (id.size() <= prefix.size() || id.substr(0, prefix.size()) != prefix) return -1;
    const std::string_view digits = id.substr(prefix.size());
    if (digits.size() > 9) return -1;
    long long value = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

// Says whether a text is a catalog or registry name: letters, digits, "_" and "-", at most 128 characters.
bool isValidName(std::string_view name)
{
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Checks one id: well formed and not seen before.
Status checkId(const Id& id, IdSet& seen, const std::string& where)
{
    if (!isValidId(id)) {
        return fail(ErrorCode::InvalidValue,
                    "The id \"" + id + "\" is not valid. It starts with a letter and holds letters, digits, \"_\" or \"-\".", where);
    }
    if (!seen.insert(id).second) {
        return fail(ErrorCode::AlreadyExists, "The id " + id + " is used more than once.", where);
    }
    return {};
}

// Says whether a number is finite and above zero.
bool positive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

// Says whether a number is finite and zero or more.
bool nonNegative(double v)
{
    return std::isfinite(v) && v >= 0.0;
}

// Checks an optional catalog name.
Status checkOptionalName(const std::optional<std::string>& value, const std::string& what, const std::string& where)
{
    if (value && !isValidName(*value)) {
        return fail(ErrorCode::InvalidValue, "The " + what + " \"" + *value + "\" is not a valid name.", where);
    }
    return {};
}

// Checks the points of one floor.
Status checkPoints(const Floor& floor, const std::string& floorPath, IdSet& seen)
{
    for (std::size_t i = 0; i < floor.points.size(); ++i) {
        const Point& p = floor.points[i];
        const std::string base = floorPath + "/points/" + std::to_string(i);
        if (auto s = checkId(p.id, seen, base + "/id"); !s.ok()) return s;
        if (!std::isfinite(p.x)) return fail(ErrorCode::InvalidValue, "The x coordinate must be a finite number.", base + "/x");
        if (!std::isfinite(p.y)) return fail(ErrorCode::InvalidValue, "The y coordinate must be a finite number.", base + "/y");
        if (!nonNegative(p.cornerRadius)) {
            return fail(ErrorCode::InvalidValue, "The corner radius must be zero or more.", base + "/corner_radius");
        }
    }
    return {};
}

// Checks the walls of one floor.
Status checkWalls(const Floor& floor, const std::string& floorPath, IdSet& seen)
{
    for (std::size_t i = 0; i < floor.walls.size(); ++i) {
        const Wall& w = floor.walls[i];
        const std::string base = floorPath + "/walls/" + std::to_string(i);
        if (auto s = checkId(w.id, seen, base + "/id"); !s.ok()) return s;
        if (!findPoint(floor, w.from)) {
            return fail(ErrorCode::NotFound, "The wall " + w.id + " starts at " + w.from + ", which is not a point of this floor.",
                        base + "/from");
        }
        if (!findPoint(floor, w.to)) {
            return fail(ErrorCode::NotFound, "The wall " + w.id + " ends at " + w.to + ", which is not a point of this floor.",
                        base + "/to");
        }
        if (w.from == w.to) {
            return fail(ErrorCode::InvalidValue, "The wall " + w.id + " starts and ends at the same point.", base + "/to");
        }
        if (!positive(w.thickness)) {
            return fail(ErrorCode::InvalidValue, "The wall thickness must be a number above zero.", base + "/thickness");
        }
        if (w.height && !positive(*w.height)) {
            return fail(ErrorCode::InvalidValue, "The wall height must be a number above zero.", base + "/height");
        }
        if (!std::isfinite(w.bulge)) {
            return fail(ErrorCode::InvalidValue, "The wall bulge must be a finite number.", base + "/bulge");
        }
        if (auto s = checkOptionalName(w.materialInside, "material", base + "/material_inside"); !s.ok()) return s;
        if (auto s = checkOptionalName(w.materialOutside, "material", base + "/material_outside"); !s.ok()) return s;
    }
    return {};
}

// Checks the rooms of one floor.
Status checkRooms(const Floor& floor, const std::string& floorPath, IdSet& seen)
{
    for (std::size_t i = 0; i < floor.rooms.size(); ++i) {
        const Room& r = floor.rooms[i];
        const std::string base = floorPath + "/rooms/" + std::to_string(i);
        if (auto s = checkId(r.id, seen, base + "/id"); !s.ok()) return s;
        if (auto s = checkOptionalName(r.type, "room type", base + "/type"); !s.ok()) return s;
        for (std::size_t k = 0; k < r.walls.size(); ++k) {
            if (!findWall(floor, r.walls[k])) {
                return fail(ErrorCode::NotFound, "The room " + r.id + " lists " + r.walls[k] + ", which is not a wall of this floor.",
                            base + "/walls/" + std::to_string(k));
            }
        }
        if (auto s = checkOptionalName(r.floorMaterial, "material", base + "/floor_material"); !s.ok()) return s;
        if (auto s = checkOptionalName(r.ceilingMaterial, "material", base + "/ceiling_material"); !s.ok()) return s;
    }
    return {};
}

// Checks the openings of one floor.
Status checkOpenings(const Floor& floor, const std::string& floorPath, IdSet& seen)
{
    for (std::size_t i = 0; i < floor.openings.size(); ++i) {
        const Opening& o = floor.openings[i];
        const std::string base = floorPath + "/openings/" + std::to_string(i);
        if (auto s = checkId(o.id, seen, base + "/id"); !s.ok()) return s;
        if (!findWall(floor, o.wall)) {
            return fail(ErrorCode::NotFound, "The opening " + o.id + " sits on " + o.wall + ", which is not a wall of this floor.",
                        base + "/wall");
        }
        if (!nonNegative(o.offset)) return fail(ErrorCode::InvalidValue, "The opening offset must be zero or more.", base + "/offset");
        if (!isValidName(o.shape)) return fail(ErrorCode::InvalidValue, "The opening shape is not a valid name.", base + "/shape");
        if (!positive(o.width)) return fail(ErrorCode::InvalidValue, "The opening width must be a number above zero.", base + "/width");
        if (!positive(o.height)) return fail(ErrorCode::InvalidValue, "The opening height must be a number above zero.", base + "/height");
        if (!nonNegative(o.sill)) return fail(ErrorCode::InvalidValue, "The opening sill must be zero or more.", base + "/sill");
        if (auto s = checkOptionalName(o.frame, "frame", base + "/frame"); !s.ok()) return s;
    }
    return {};
}

// Checks one floor and everything on it.
Status checkFloor(const Floor& floor, std::size_t index, IdSet& seen)
{
    const std::string base = "/floors/" + std::to_string(index);
    if (auto s = checkId(floor.id, seen, base + "/id"); !s.ok()) return s;
    if (!positive(floor.ceilingHeight)) {
        return fail(ErrorCode::InvalidValue, "The ceiling height must be a number above zero.", base + "/ceiling_height");
    }
    if (floor.image && floor.image->empty()) {
        return fail(ErrorCode::InvalidValue, "The plan image path is empty.", base + "/image");
    }
    if (auto s = checkPoints(floor, base, seen); !s.ok()) return s;
    if (auto s = checkWalls(floor, base, seen); !s.ok()) return s;
    if (auto s = checkRooms(floor, base, seen); !s.ok()) return s;
    return checkOpenings(floor, base, seen);
}

}

// Makes a plan with one empty floor "f1" named "Floor 1".
Plan makeEmptyPlan()
{
    Plan plan;
    Floor floor;
    floor.id = "f1";
    floor.name = "Floor 1";
    plan.floors.push_back(std::move(floor));
    return plan;
}

// Finds a floor by its id.
Floor* findFloor(Plan& plan, std::string_view floorId) { return findById(plan.floors, floorId); }
const Floor* findFloor(const Plan& plan, std::string_view floorId) { return findById(plan.floors, floorId); }

// Finds the floor that holds an object with this id.
Floor* findFloorContaining(Plan& plan, std::string_view objectId)
{
    for (auto& f : plan.floors) {
        if (findPoint(f, objectId) || findWall(f, objectId) || findRoom(f, objectId) || findOpening(f, objectId)) return &f;
    }
    return nullptr;
}

const Floor* findFloorContaining(const Plan& plan, std::string_view objectId)
{
    return findFloorContaining(const_cast<Plan&>(plan), objectId);
}

Point* findPoint(Floor& floor, std::string_view id) { return findById(floor.points, id); }
const Point* findPoint(const Floor& floor, std::string_view id) { return findById(floor.points, id); }
Wall* findWall(Floor& floor, std::string_view id) { return findById(floor.walls, id); }
const Wall* findWall(const Floor& floor, std::string_view id) { return findById(floor.walls, id); }
Room* findRoom(Floor& floor, std::string_view id) { return findById(floor.rooms, id); }
const Room* findRoom(const Floor& floor, std::string_view id) { return findById(floor.rooms, id); }
Opening* findOpening(Floor& floor, std::string_view id) { return findById(floor.openings, id); }
const Opening* findOpening(const Floor& floor, std::string_view id) { return findById(floor.openings, id); }

// Says whether any floor or object already uses this id.
bool idInUse(const Plan& plan, std::string_view id)
{
    return findFloor(plan, id) != nullptr || findFloorContaining(plan, id) != nullptr;
}

// Makes a new id: the prefix and one more than the highest number used with it.
Id newId(const Plan& plan, std::string_view prefix)
{
    long long highest = 0;
    auto consider = [&](const Id& id) { highest = std::max(highest, numberAfterPrefix(id, prefix)); };
    for (const auto& f : plan.floors) {
        consider(f.id);
        for (const auto& p : f.points) consider(p.id);
        for (const auto& w : f.walls) consider(w.id);
        for (const auto& r : f.rooms) consider(r.id);
        for (const auto& o : f.openings) consider(o.id);
    }
    return std::string(prefix) + std::to_string(highest + 1);
}

// Says whether text is a well formed id.
bool isValidId(std::string_view id)
{
    if (id.empty() || id.size() > 64) return false;
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (!letter(id.front())) return false;
    for (char c : id) {
        if (!(letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

// Checks every rule of the plan format and returns the first problem found.
Status checkPlan(const Plan& plan)
{
    if (plan.floors.empty()) return fail(ErrorCode::InvalidValue, "The plan needs at least one floor.", "/floors");
    if (plan.scale.pixelsPerMetre && !positive(*plan.scale.pixelsPerMetre)) {
        return fail(ErrorCode::InvalidValue, "The scale must be a number above zero.", "/scale/pixels_per_metre");
    }
    IdSet seen;
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        if (auto s = checkFloor(plan.floors[i], i, seen); !s.ok()) return s;
    }
    return {};
}

// The name used in files for an opening type.
std::string_view openingTypeName(OpeningType type)
{
    switch (type) {
    case OpeningType::Door: return "door";
    case OpeningType::Window: return "window";
    case OpeningType::Plain: return "plain";
    }
    return "door";
}

// The opening type for a name used in files.
std::optional<OpeningType> openingTypeFromName(std::string_view name)
{
    if (name == "door") return OpeningType::Door;
    if (name == "window") return OpeningType::Window;
    if (name == "plain") return OpeningType::Plain;
    return std::nullopt;
}

// The name used in files for a wall side.
std::string_view wallSideName(WallSide side)
{
    return side == WallSide::Left ? "left" : "right";
}

// The wall side for a name used in files.
std::optional<WallSide> wallSideFromName(std::string_view name)
{
    if (name == "left") return WallSide::Left;
    if (name == "right") return WallSide::Right;
    return std::nullopt;
}

// The other side of a wall.
WallSide otherSide(WallSide side)
{
    return side == WallSide::Left ? WallSide::Right : WallSide::Left;
}

// The name used in files for a hinge end.
std::string_view hingeEndName(HingeEnd end)
{
    return end == HingeEnd::End ? "end" : "start";
}

// The hinge end for a name used in files.
std::optional<HingeEnd> hingeEndFromName(std::string_view name)
{
    if (name == "start") return HingeEnd::Start;
    if (name == "end") return HingeEnd::End;
    return std::nullopt;
}

// The other end of an opening.
HingeEnd otherEnd(HingeEnd end)
{
    return end == HingeEnd::Start ? HingeEnd::End : HingeEnd::Start;
}

}
