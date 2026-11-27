#include "plynth/core/Stairs.h"

#include "plynth/core/Rooms.h"
#include "plynth/core/Storeys.h"
#include "plynth/core/WallGeometry.h"

#include "ClipperInclude.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>

namespace plynth {

namespace {

constexpr int kClipperPrecision = 8;
constexpr double kCollinearTolerance = 1e-9;
constexpr double kOverlapTolerance = 1e-4; // square metres

// The registry: every stair type ever registered. Built as a function local static so that registering from
// namespace scope objects works in any start up order.
struct Registry {
    std::mutex mutex;
    std::vector<std::unique_ptr<StairType>> types;
};

// The one registry.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether text is a well formed type id: lower case letters, digits and "_", starting with a letter.
bool wellFormedTypeId(std::string_view id)
{
    if (id.empty() || id.size() > 64) return false;
    if (id.front() < 'a' || id.front() > 'z') return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// The known type ids as one text, for messages.
std::string knownTypeIds()
{
    std::string known;
    for (const StairType* t : allStairTypes()) {
        if (!known.empty()) known += ", ";
        known += std::string(t->id());
    }
    return known;
}

// Says whether a text is a catalog name: letters, digits, "_" and "-", at most 128 characters.
bool wellFormedName(const std::string& name)
{
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Drops points that repeat or lie on the straight line between their neighbours.
std::vector<Vec2> withoutStraightPoints(std::vector<Vec2> points)
{
    bool changed = true;
    while (changed && points.size() > 3) {
        changed = false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Vec2 prev = points[(i + points.size() - 1) % points.size()];
            const Vec2 here = points[i];
            const Vec2 next = points[(i + 1) % points.size()];
            const double cross = (here.x - prev.x) * (next.y - here.y) - (here.y - prev.y) * (next.x - here.x);
            if (std::fabs(cross) <= kCollinearTolerance) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return points;
}

// The outer outline of the union of the step outlines, with a positive signedArea.
std::vector<Vec2> unionOutline(const std::vector<StairStep>& steps)
{
    Clipper2Lib::PathsD all;
    for (const StairStep& step : steps) {
        Clipper2Lib::PathD path;
        for (const Vec2& p : step.outline) path.emplace_back(p.x, p.y);
        all.push_back(std::move(path));
    }
    const Clipper2Lib::PathsD joined = Clipper2Lib::Union(all, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    std::vector<Vec2> best;
    double bestArea = 0.0;
    for (const Clipper2Lib::PathD& path : joined) {
        std::vector<Vec2> points;
        for (const Clipper2Lib::PointD& p : path) points.push_back({p.x, p.y});
        const double area = std::fabs(signedArea(points));
        if (points.size() >= 3 && area > bestArea) {
            bestArea = area;
            best = std::move(points);
        }
    }
    best = withoutStraightPoints(std::move(best));
    if (!best.empty() && signedArea(best) < 0.0) std::reverse(best.begin(), best.end());
    return best;
}

// A stair's name for messages.
std::string stairLabel(const Stair& stair)
{
    return stair.id.empty() ? std::string("This stair") : "Stair " + stair.id;
}

}

// The lower case name of a stair part.
std::string_view stairPartName(StairPart part)
{
    switch (part) {
    case StairPart::Step: return "step";
    case StairPart::Landing: return "landing";
    case StairPart::Column: return "column";
    }
    return "step";
}

// The stair part for a name.
std::optional<StairPart> stairPartFromName(std::string_view name)
{
    if (name == "step") return StairPart::Step;
    if (name == "landing") return StairPart::Landing;
    if (name == "column") return StairPart::Column;
    return std::nullopt;
}

// Adds a type unless its id is taken or malformed.
bool registerStairType(std::unique_ptr<StairType> type)
{
    if (!type || !wellFormedTypeId(type->id())) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.types) {
        if (existing->id() == type->id()) return false;
    }
    r.types.push_back(std::move(type));
    return true;
}

StairTypeRegistrar::StairTypeRegistrar(std::unique_ptr<StairType> type)
{
    registerStairType(std::move(type));
}

// Finds a type by id.
const StairType* findStairType(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const auto& type : r.types) {
        if (type->id() == id) return type.get();
    }
    return nullptr;
}

// Every type, sorted by sort key and then id.
std::vector<const StairType*> allStairTypes()
{
    Registry& r = registry();
    std::vector<const StairType*> out;
    {
        std::lock_guard lock(r.mutex);
        for (const auto& type : r.types) out.push_back(type.get());
    }
    std::sort(out.begin(), out.end(), [](const StairType* a, const StairType* b) {
        if (a->sortKey() != b->sortKey()) return a->sortKey() < b->sortKey();
        return a->id() < b->id();
    });
    return out;
}

// Checks the values a stair command would set.
Status checkStair(const Stair& stair)
{
    if (!findStairType(stair.type)) {
        return fail(ErrorCode::InvalidValue,
                    "The stair type \"" + stair.type + "\" is not known. Known types: " + knownTypeIds() + ".");
    }
    if (!(std::isfinite(stair.width) && stair.width >= kMinStairWidth && stair.width <= kMaxStairWidth)) {
        return fail(ErrorCode::InvalidValue, "The stair width must be from 0.6 to 6 metres.");
    }
    if (!(std::isfinite(stair.x) && std::isfinite(stair.y))) {
        return fail(ErrorCode::InvalidValue, "The stair position must be finite numbers.");
    }
    if (!(std::isfinite(stair.rotation) && stair.rotation >= 0.0 && stair.rotation < 360.0)) {
        return fail(ErrorCode::InvalidValue, "The stair rotation must be from 0 up to 360 degrees.");
    }
    if (stair.material && !wellFormedName(*stair.material)) {
        return fail(ErrorCode::InvalidValue, "The stair material \"" + *stair.material + "\" is not a valid name.");
    }
    return {};
}

// Works out a stair on its floor, whether or not the plan holds it yet.
StairLayout previewStairLayout(const Plan& plan, const Floor& floor, const Stair& stair)
{
    StairLayout layout;
    layout.stair = stair.id;
    layout.fromFloor = floor.id;

    const Floor* above = floorAbove(plan, floor.id);
    if (!above) {
        layout.status = fail(ErrorCode::InvalidValue, stairLabel(stair) + " has no floor above it.");
        return layout;
    }
    layout.toFloor = above->id;

    const StairType* type = findStairType(stair.type);
    if (!type) {
        layout.status = fail(ErrorCode::InvalidValue,
                             "The stair type \"" + stair.type + "\" is not known. Known types: " + knownTypeIds() + ".");
        return layout;
    }
    if (!(std::isfinite(stair.width) && stair.width >= kMinStairWidth && stair.width <= kMaxStairWidth)) {
        layout.status = fail(ErrorCode::InvalidValue, "The stair width must be from 0.6 to 6 metres.");
        return layout;
    }

    StairInput input;
    input.rise = floorRise(floor);
    input.width = stair.width;
    input.turn = stair.turn;
    input.riserCount = std::max(2, static_cast<int>(std::ceil(input.rise / kMaxRiser - 1e-9)));
    input.riser = input.rise / input.riserCount;
    const StairShape shape = type->shape(input);

    layout.rise = input.rise;
    layout.riserCount = input.riserCount;
    layout.riser = input.riser;

    // Frame to plan: climb is (0, 1) turned clockwise by the rotation, u runs to the right of it.
    const double r = stair.rotation * std::numbers::pi / 180.0;
    const Vec2 climb{-std::sin(r), std::cos(r)};
    const Vec2 right = rightOf(climb);
    const auto toPlan = [&](Vec2 p) {
        return Vec2{stair.x + p.y * climb.x + p.x * right.x, stair.y + p.y * climb.y + p.x * right.y};
    };

    for (const StairStep& source : shape.steps) {
        StairStep step = source;
        for (Vec2& p : step.outline) p = toPlan(p);
        // The change of frame is a mirror image, so the corners are turned round to stay positive.
        if (signedArea(step.outline) < 0.0) std::reverse(step.outline.begin(), step.outline.end());
        layout.steps.push_back(std::move(step));
    }
    for (const Vec2& p : shape.walkLine) layout.walkLine.push_back(toPlan(p));
    if (!layout.walkLine.empty()) layout.arrival = layout.walkLine.back();
    for (std::size_t i = 1; i < layout.walkLine.size(); ++i) {
        layout.length += distance(layout.walkLine[i - 1], layout.walkLine[i]);
    }
    layout.footprint = unionOutline(layout.steps);
    return layout;
}

// Works out a stair on its floor.
StairLayout stairLayout(const Plan& plan, const Floor& floor, const Stair& stair)
{
    return previewStairLayout(plan, floor, stair);
}

// The layouts of the stairs on the floor below that arrive at this floor.
std::vector<StairLayout> stairsArrivingAt(const Plan& plan, std::string_view floorId)
{
    std::vector<StairLayout> out;
    const Floor* below = floorBelow(plan, floorId);
    if (!below) return out;
    for (const Stair& stair : below->stairs) {
        StairLayout layout = stairLayout(plan, *below, stair);
        if (layout.status.ok()) out.push_back(std::move(layout));
    }
    return out;
}

namespace {

// A closed ring as a Clipper path with a positive orientation.
Clipper2Lib::PathD ringPath(const std::vector<Vec2>& ring, bool positive)
{
    Clipper2Lib::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    if (positive == (signedArea(ring) < 0.0)) std::reverse(path.begin(), path.end());
    return path;
}

// The plan area of the footprint as paths: outlines positive, holes negative.
Clipper2Lib::PathsD areaPaths(const std::vector<PlanArea>& areas)
{
    Clipper2Lib::PathsD out;
    for (const PlanArea& area : areas) {
        out.push_back(ringPath(area.outline, true));
        for (const auto& hole : area.holes) out.push_back(ringPath(hole, false));
    }
    return out;
}

// The size of the part of a polygon that lies inside the given paths, square metres.
double overlapSize(const std::vector<Vec2>& polygon, const Clipper2Lib::PathsD& clip)
{
    if (polygon.size() < 3 || clip.empty()) return 0.0;
    const Clipper2Lib::PathsD subject{ringPath(polygon, true)};
    const Clipper2Lib::PathsD common = Clipper2Lib::Intersect(subject, clip, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    return std::fabs(Clipper2Lib::Area(common));
}

// The size of the part of a polygon that lies outside the given paths, square metres.
double outsideSize(const std::vector<Vec2>& polygon, const Clipper2Lib::PathsD& clip)
{
    if (polygon.size() < 3) return 0.0;
    const Clipper2Lib::PathsD subject{ringPath(polygon, true)};
    const Clipper2Lib::PathsD rest = Clipper2Lib::Difference(subject, clip, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    return std::fabs(Clipper2Lib::Area(rest));
}

// Says whether a position is in a room of the floor.
bool inAnyRoom(const Floor& floor, Vec2 position)
{
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (!shape.closed) continue;
        int count = 0;
        for (const std::vector<Vec2>& ring : shape.floorArea) {
            if (ring.size() >= 3 && insidePolygon(position, ring)) ++count;
        }
        if (count % 2 == 1) return true;
    }
    return false;
}

// Adds a stair_overlaps_wall problem for every wall of a floor that the footprint overlaps.
void addWallOverlaps(const Stair& stair, const std::vector<Vec2>& footprint, const Floor& floor, std::vector<StairProblem>& out)
{
    for (const Wall& wall : floor.walls) {
        Clipper2Lib::PathsD band;
        for (const auto& ring : wallBand(floor, wall)) band.push_back(ringPath(ring, true));
        if (overlapSize(footprint, band) <= kOverlapTolerance) continue;
        out.push_back({stair.id, "stair_overlaps_wall", false,
                       stairLabel(stair) + " overlaps wall " + wall.id + " on " + floor.name + ".", wall.id});
    }
}

}

// The problems of the stairs on a floor, in stair order.
std::vector<StairProblem> stairProblems(const Plan& plan, const Floor& floor)
{
    std::vector<StairProblem> out;
    const Floor* above = floorAbove(plan, floor.id);
    for (const Stair& stair : floor.stairs) {
        if (!above) {
            out.push_back({stair.id, "stair_no_floor_above", true, stairLabel(stair) + " has no floor above it.", {}});
            continue;
        }
        if (!findStairType(stair.type)) {
            out.push_back({stair.id, "stair_type_unknown", true,
                           "The stair type \"" + stair.type + "\" is not known. Known types: " + knownTypeIds() + ".", {}});
            continue;
        }
        const StairLayout layout = stairLayout(plan, floor, stair);
        if (!layout.status.ok() || layout.footprint.empty()) continue;

        if (outsideSize(layout.footprint, areaPaths(floorFootprint(floor))) > kOverlapTolerance) {
            out.push_back({stair.id, "stair_outside_floor", false,
                           stairLabel(stair) + " reaches outside the walls and rooms of " + floor.name + ".", {}});
        }
        addWallOverlaps(stair, layout.footprint, floor, out);
        addWallOverlaps(stair, layout.footprint, *above, out);
        if (!inAnyRoom(*above, layout.arrival)) {
            out.push_back({stair.id, "stair_arrival_outside_room", false,
                           stairLabel(stair) + " arrives outside every room of " + above->name + ".", {}});
        }
    }
    return out;
}

}
