#include "plynth/core/Plan.h"

#include "plynth/core/Roofs.h"
#include "plynth/core/Stairs.h"

#include "IdSource.h"

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
        if (auto s = checkOptionalName(r.wallMaterial, "material", base + "/wall_material"); !s.ok()) return s;
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

// Says whether a text is a generator or parameter name: a lowercase letter first, then lowercase letters,
// digits and "_", at most 64 characters.
bool isGeneratorName(std::string_view name)
{
    if (name.empty() || name.size() > 64) return false;
    if (name.front() < 'a' || name.front() > 'z') return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Checks the generator of one item.
Status checkGeneratorUse(const GeneratorUse& g, const std::string& base)
{
    if (!isGeneratorName(g.name)) {
        return fail(ErrorCode::InvalidValue, "The generator name \"" + g.name + "\" is not valid.", base + "/name");
    }
    for (std::size_t i = 0; i < g.params.size(); ++i) {
        const GeneratorParamValue& p = g.params[i];
        const std::string at = base + "/params/" + p.id;
        if (!isGeneratorName(p.id)) {
            return fail(ErrorCode::InvalidValue, "The parameter id \"" + p.id + "\" is not valid.", at);
        }
        if (i > 0 && !(g.params[i - 1].id < p.id)) {
            return fail(ErrorCode::InvalidValue, "The parameters must be sorted by id, each id once.", at);
        }
        if (const double* d = std::get_if<double>(&p.value); d && !std::isfinite(*d)) {
            return fail(ErrorCode::InvalidValue, "The parameter value must be a finite number.", at);
        }
    }
    return {};
}

// Checks the items of one floor.
Status checkItems(const Floor& floor, const std::string& floorPath, IdSet& seen)
{
    for (std::size_t i = 0; i < floor.items.size(); ++i) {
        const Item& it = floor.items[i];
        const std::string base = floorPath + "/items/" + std::to_string(i);
        if (auto s = checkId(it.id, seen, base + "/id"); !s.ok()) return s;
        if (it.generator) {
            if (!it.asset.empty()) {
                return fail(ErrorCode::InvalidValue, "A generator item has no asset.", base + "/asset");
            }
            if (auto s = checkGeneratorUse(*it.generator, base + "/generator"); !s.ok()) return s;
        } else if (!isValidName(it.asset)) {
            return fail(ErrorCode::InvalidValue, "The item asset \"" + it.asset + "\" is not a valid name.", base + "/asset");
        }
        if (it.room && !findRoom(floor, *it.room)) {
            return fail(ErrorCode::NotFound,
                        "The item " + it.id + " stands in " + *it.room + ", which is not a room of this floor.", base + "/room");
        }
        if (!std::isfinite(it.x)) return fail(ErrorCode::InvalidValue, "The item x coordinate must be a finite number.", base + "/x");
        if (!std::isfinite(it.y)) return fail(ErrorCode::InvalidValue, "The item y coordinate must be a finite number.", base + "/y");
        if (!nonNegative(it.z)) {
            return fail(ErrorCode::InvalidValue, "The item height above the floor must be zero or more.", base + "/z");
        }
        if (!std::isfinite(it.rotation) || it.rotation < 0.0 || it.rotation >= 360.0) {
            return fail(ErrorCode::InvalidValue, "The item rotation must be from 0 up to 360 degrees.", base + "/rotation");
        }
        if (it.wall && !findWall(floor, *it.wall)) {
            return fail(ErrorCode::NotFound,
                        "The item " + it.id + " hangs on " + *it.wall + ", which is not a wall of this floor.", base + "/wall");
        }
        if (!positive(it.size.width)) {
            return fail(ErrorCode::InvalidValue, "The item width must be a number above zero.", base + "/size/width");
        }
        if (!positive(it.size.depth)) {
            return fail(ErrorCode::InvalidValue, "The item depth must be a number above zero.", base + "/size/depth");
        }
        if (!positive(it.size.height)) {
            return fail(ErrorCode::InvalidValue, "The item height must be a number above zero.", base + "/size/height");
        }
    }
    return {};
}

// Checks the stairs of one floor.
Status checkStairs(const Floor& floor, const std::string& floorPath, IdSet& seen)
{
    for (std::size_t i = 0; i < floor.stairs.size(); ++i) {
        const Stair& st = floor.stairs[i];
        const std::string base = floorPath + "/stairs/" + std::to_string(i);
        if (auto s = checkId(st.id, seen, base + "/id"); !s.ok()) return s;
        if (!isValidName(st.type)) return fail(ErrorCode::InvalidValue, "The stair type is not a valid name.", base + "/type");
        if (!std::isfinite(st.x)) return fail(ErrorCode::InvalidValue, "The stair x coordinate must be a finite number.", base + "/x");
        if (!std::isfinite(st.y)) return fail(ErrorCode::InvalidValue, "The stair y coordinate must be a finite number.", base + "/y");
        if (!std::isfinite(st.rotation) || st.rotation < 0.0 || st.rotation >= 360.0) {
            return fail(ErrorCode::InvalidValue, "The stair rotation must be from 0 up to 360 degrees.", base + "/rotation");
        }
        if (!std::isfinite(st.width) || st.width < kMinStairWidth || st.width > kMaxStairWidth) {
            return fail(ErrorCode::InvalidValue, "The stair width must be from 0.6 to 3 metres.", base + "/width");
        }
        if (auto s = checkOptionalName(st.material, "material", base + "/material"); !s.ok()) return s;
    }
    return {};
}

// Checks one set of roof settings.
Status checkRoofSettingsFields(const RoofSettings& r, const std::string& where)
{
    if (!isValidName(r.type)) return fail(ErrorCode::InvalidValue, "The roof type is not a valid name.", where + "/type");
    if (!std::isfinite(r.pitch) || r.pitch < 0.0 || r.pitch > kMaxRoofPitch) {
        return fail(ErrorCode::InvalidValue, "The roof pitch must be from 0 to 75 degrees.", where + "/pitch");
    }
    if (!std::isfinite(r.overhang) || r.overhang < 0.0 || r.overhang > kMaxRoofOverhang) {
        return fail(ErrorCode::InvalidValue, "The roof overhang must be from 0 to 2 metres.", where + "/overhang");
    }
    if (!std::isfinite(r.parapet) || r.parapet < 0.0 || r.parapet > kMaxParapet) {
        return fail(ErrorCode::InvalidValue, "The parapet height must be from 0 to 3 metres.", where + "/parapet");
    }
    if (r.direction && (!std::isfinite(*r.direction) || *r.direction < 0.0 || *r.direction >= 360.0)) {
        return fail(ErrorCode::InvalidValue, "The roof direction must be from 0 up to 360 degrees.", where + "/direction");
    }
    return {};
}

// Checks the roof default and the roof section entries.
Status checkRoofs(const Plan& plan, IdSet& seen)
{
    if (auto s = checkRoofSettingsFields(plan.roof, "/roof"); !s.ok()) return s;
    for (std::size_t i = 0; i < plan.roofSections.size(); ++i) {
        const RoofSection& r = plan.roofSections[i];
        const std::string base = "/roof_sections/" + std::to_string(i);
        if (auto s = checkId(r.id, seen, base + "/id"); !s.ok()) return s;
        if (!findFloor(plan, r.floor)) {
            return fail(ErrorCode::NotFound, "The roof section " + r.id + " names " + r.floor + ", which is not a floor of the plan.",
                        base + "/floor");
        }
        if (!std::isfinite(r.x)) return fail(ErrorCode::InvalidValue, "The anchor x coordinate must be a finite number.", base + "/x");
        if (!std::isfinite(r.y)) return fail(ErrorCode::InvalidValue, "The anchor y coordinate must be a finite number.", base + "/y");
        if (auto s = checkRoofSettingsFields(r.settings, base + "/settings"); !s.ok()) return s;
        if (auto s = checkOptionalName(r.material, "material", base + "/material"); !s.ok()) return s;
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
    if (!std::isfinite(floor.slabThickness) || floor.slabThickness < kMinSlabThickness || floor.slabThickness > kMaxSlabThickness) {
        return fail(ErrorCode::InvalidValue, "The slab thickness must be from 0.05 to 1 metre.", base + "/slab_thickness");
    }
    if (!std::isfinite(floor.imageOffset.x)) {
        return fail(ErrorCode::InvalidValue, "The image offset must be a finite number.", base + "/image_offset/x");
    }
    if (!std::isfinite(floor.imageOffset.y)) {
        return fail(ErrorCode::InvalidValue, "The image offset must be a finite number.", base + "/image_offset/y");
    }
    if (floor.image && floor.image->empty()) {
        return fail(ErrorCode::InvalidValue, "The plan image path is empty.", base + "/image");
    }
    if (auto s = checkPoints(floor, base, seen); !s.ok()) return s;
    if (auto s = checkWalls(floor, base, seen); !s.ok()) return s;
    if (auto s = checkRooms(floor, base, seen); !s.ok()) return s;
    if (auto s = checkOpenings(floor, base, seen); !s.ok()) return s;
    if (auto s = checkItems(floor, base, seen); !s.ok()) return s;
    return checkStairs(floor, base, seen);
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
        if (findPoint(f, objectId) || findWall(f, objectId) || findRoom(f, objectId) || findOpening(f, objectId) || findItem(f, objectId) || findStair(f, objectId)) return &f;
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

// Says whether an item runs a generator rather than placing an asset.
bool isGeneratorItem(const Item& item) { return item.generator.has_value(); }

Stair* findStair(Floor& floor, std::string_view id) { return findById(floor.stairs, id); }
const Stair* findStair(const Floor& floor, std::string_view id) { return findById(floor.stairs, id); }
RoofSection* findRoofSection(Plan& plan, std::string_view id) { return findById(plan.roofSections, id); }
const RoofSection* findRoofSection(const Plan& plan, std::string_view id) { return findById(plan.roofSections, id); }

// Where a floor is in plan order.
std::optional<std::size_t> floorIndex(const Plan& plan, std::string_view floorId)
{
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        if (plan.floors[i].id == floorId) return i;
    }
    return std::nullopt;
}

// The floor right above one.
const Floor* floorAbove(const Plan& plan, std::string_view floorId)
{
    const auto i = floorIndex(plan, floorId);
    return i && *i + 1 < plan.floors.size() ? &plan.floors[*i + 1] : nullptr;
}

// The floor right below one.
const Floor* floorBelow(const Plan& plan, std::string_view floorId)
{
    const auto i = floorIndex(plan, floorId);
    return i && *i > 0 ? &plan.floors[*i - 1] : nullptr;
}

Item* findItem(Floor& floor, std::string_view id) { return findById(floor.items, id); }
const Item* findItem(const Floor& floor, std::string_view id) { return findById(floor.items, id); }

// Says whether any floor or object already uses this id.
bool idInUse(const Plan& plan, std::string_view id)
{
    return findFloor(plan, id) != nullptr || findFloorContaining(plan, id) != nullptr || findRoofSection(plan, id) != nullptr;
}

// Makes an id the old way: the prefix and one more than the highest number used with it, counting on while that
// id is taken by one the number rule skips.
static Id countedId(const Plan& plan, std::string_view prefix)
{
    long long highest = 0;
    auto consider = [&](const Id& id) { highest = std::max(highest, numberAfterPrefix(id, prefix)); };
    for (const auto& f : plan.floors) {
        consider(f.id);
        for (const auto& p : f.points) consider(p.id);
        for (const auto& w : f.walls) consider(w.id);
        for (const auto& r : f.rooms) consider(r.id);
        for (const auto& o : f.openings) consider(o.id);
        for (const auto& it : f.items) consider(it.id);
        for (const auto& st : f.stairs) consider(st.id);
    }
    for (const auto& r : plan.roofSections) consider(r.id);
    Id id = std::string(prefix) + std::to_string(highest + 1);
    for (long long next = highest + 2; idInUse(plan, id); ++next) id = std::string(prefix) + std::to_string(next);
    return id;
}

// Makes a new random id that nothing in the plan has, or a counted one when tests ask or every try was taken.
Id newId(const Plan& plan, std::string_view prefix)
{
    if (detail::countedIds()) return countedId(plan, prefix);
    const std::string part = detail::newIdPart(
        [&](std::string_view candidate) { return idInUse(plan, std::string(prefix) + std::string(candidate)); });
    if (part.empty()) return countedId(plan, prefix);
    return std::string(prefix) + part;
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
    return checkRoofs(plan, seen);
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

// The name used in files for a stair turn.
std::string_view stairTurnName(StairTurn turn)
{
    return turn == StairTurn::Left ? "left" : "right";
}

// The stair turn for a name used in files.
std::optional<StairTurn> stairTurnFromName(std::string_view name)
{
    if (name == "left") return StairTurn::Left;
    if (name == "right") return StairTurn::Right;
    return std::nullopt;
}

}
