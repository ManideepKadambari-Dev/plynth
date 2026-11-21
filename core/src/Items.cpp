#include "plynth/core/Items.h"

#include "plynth/core/Openings.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"
#include "plynth/core/catalog/AssetInfo.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace plynth {

namespace {

constexpr int kClipperPrecision = 8;

// A difference in size smaller than this is the same size (a micrometre).
constexpr double kSizeTolerance = 1e-6;

// How far inside a wall band a probe point is put to find which wall a face belongs to.
constexpr double kProbeDepth = 1e-4;

// Writes a length in metres with two decimals for a message.
std::string metres(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.2f m", value);
    return text;
}

// Says whether a text is a catalog name: letters, digits, "_" and "-", at most 128 characters.
bool isValidAssetName(std::string_view name)
{
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

Vec2 plus(Vec2 a, Vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}

Vec2 minus(Vec2 a, Vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}

Vec2 times(Vec2 a, double k)
{
    return {a.x * k, a.y * k};
}

Vec2 unit(Vec2 a)
{
    const double length = std::hypot(a.x, a.y);
    if (length < 1e-12) return {0.0, 0.0};
    return {a.x / length, a.y / length};
}

// The unit direction an item's front faces, from a rotation in degrees.
Vec2 frontOf(double rotationDegrees)
{
    const double r = rotationDegrees * std::numbers::pi / 180.0;
    return {-std::sin(r), std::cos(r)};
}

// A footprint as a Clipper path.
Clipper2Lib::PathsD footprintPaths(const ItemFootprint& footprint)
{
    Clipper2Lib::PathD path;
    for (const Vec2& c : footprint.corners) path.emplace_back(c.x, c.y);
    return Clipper2Lib::PathsD{path};
}

// Turns outlines into Clipper paths.
Clipper2Lib::PathsD toPaths(const std::vector<std::vector<Vec2>>& outlines)
{
    Clipper2Lib::PathsD paths;
    for (const auto& outline : outlines) {
        Clipper2Lib::PathD path;
        for (const Vec2& p : outline) path.emplace_back(p.x, p.y);
        paths.push_back(std::move(path));
    }
    return paths;
}

// The area two sets of shapes share, in square metres.
double sharedArea(const Clipper2Lib::PathsD& a, const Clipper2Lib::PathsD& b)
{
    if (a.empty() || b.empty()) return 0.0;
    const Clipper2Lib::PathsD both = Clipper2Lib::Intersect(a, b, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    return std::fabs(Clipper2Lib::Area(both));
}

// The area of the first set of shapes that the second does not cover.
double uncoveredArea(const Clipper2Lib::PathsD& a, const Clipper2Lib::PathsD& b)
{
    const Clipper2Lib::PathsD rest = Clipper2Lib::Difference(a, b, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    return std::fabs(Clipper2Lib::Area(rest));
}

// Says whether a position is inside the outer ring and outside every island ring.
bool insideRings(Vec2 position, const std::vector<std::vector<Vec2>>& rings)
{
    if (rings.empty() || !insidePolygon(position, rings[0])) return false;
    for (std::size_t i = 1; i < rings.size(); ++i) {
        if (insidePolygon(position, rings[i])) return false;
    }
    return true;
}

// What checkItem needs to know about a floor, worked out once so itemProblems can reuse it for every item.
struct FloorContext {
    struct RoomInfo {
        Id id;
        RoomShape shape;
        Clipper2Lib::PathsD area;
    };
    struct WallInfo {
        Id id;
        Clipper2Lib::PathsD band;
    };
    std::vector<RoomInfo> rooms;
    std::vector<WallInfo> walls;
};

FloorContext makeContext(const Floor& floor)
{
    FloorContext context;
    for (const Room& room : floor.rooms) {
        FloorContext::RoomInfo info;
        info.id = room.id;
        info.shape = roomShape(floor, room);
        info.area = toPaths(info.shape.floorArea);
        context.rooms.push_back(std::move(info));
    }
    for (const Wall& wall : floor.walls) {
        context.walls.push_back({wall.id, toPaths(wallBand(floor, wall))});
    }
    return context;
}

// The plain name of what an item is, with its id: "Sofa i1". A new item with no id yet is just "Sofa".
std::string labelOf(const Item& item)
{
    std::string label = catalogNameFromStem(item.asset);
    if (!item.id.empty()) label += " " + item.id;
    return label;
}

// One side of the exposed face of the walls, for snapping.
struct Face {
    Vec2 a;
    Vec2 dir;     // unit, from a to the next corner
    Vec2 outward; // unit, away from the walls
    double length = 0.0;
};

// Every edge of the merged wall bands. The merge drops the faces that lie inside another wall.
std::vector<Face> exposedFaces(const Floor& floor)
{
    Clipper2Lib::PathsD bands;
    for (const Wall& wall : floor.walls) {
        for (const auto& path : toPaths(wallBand(floor, wall))) bands.push_back(path);
    }
    std::vector<Face> faces;
    if (bands.empty()) return faces;
    const Clipper2Lib::PathsD merged = Clipper2Lib::Union(bands, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    for (const Clipper2Lib::PathD& path : merged) {
        const std::size_t n = path.size();
        for (std::size_t i = 0; i < n && n >= 3; ++i) {
            const Vec2 a{path[i].x, path[i].y};
            const Vec2 b{path[(i + 1) % n].x, path[(i + 1) % n].y};
            const double length = distance(a, b);
            if (length < 1e-6) continue;
            const Vec2 dir = unit(minus(b, a));
            faces.push_back({a, dir, Vec2{dir.y, -dir.x}, length});
        }
    }
    return faces;
}

// A footprint side that can be moved onto a wall face.
struct SideSnap {
    const Face* face = nullptr;
    double signedDistance = 0.0;
    Vec2 probe; // a place on the face where the side and the face overlap
};

// Looks for the closest face a footprint side runs along, skipping faces parallel to one already used.
SideSnap findSideSnap(const std::vector<Face>& faces, const ItemFootprint& footprint, double radius, const Face* notParallelTo)
{
    SideSnap best;
    double bestDistance = radius + 1.0;
    const double minCos = std::cos(kInLineAngle);
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 s0 = footprint.corners[i];
        const Vec2 s1 = footprint.corners[(i + 1) % 4];
        const Vec2 sideDir = unit(minus(s1, s0));
        const Vec2 mid = times(plus(s0, s1), 0.5);
        for (const Face& face : faces) {
            if (std::fabs(dot(sideDir, face.dir)) < minCos) continue;
            if (notParallelTo && std::fabs(dot(notParallelTo->dir, face.dir)) >= minCos) continue;
            const double d = dot(minus(mid, face.a), face.outward);
            if (std::fabs(d) > radius || std::fabs(d) >= bestDistance) continue;
            if (dot(minus(footprint.centre, mid), face.outward) <= 0.0) continue;
            const double t0 = dot(minus(s0, face.a), face.dir);
            const double t1 = dot(minus(s1, face.a), face.dir);
            const double low = std::max(std::min(t0, t1), 0.0);
            const double high = std::min(std::max(t0, t1), face.length);
            if (high - low <= kItemTouchTolerance) continue;
            bestDistance = std::fabs(d);
            best.face = &face;
            best.signedDistance = d;
            best.probe = plus(face.a, times(face.dir, (low + high) / 2.0));
        }
    }
    return best;
}

// The wall whose band holds a point just inside the face, or an empty id.
Id wallBehind(const FloorContext& context, const Face& face, Vec2 probe)
{
    const Vec2 inside = minus(probe, times(face.outward, kProbeDepth));
    for (const FloorContext::WallInfo& wall : context.walls) {
        for (const Clipper2Lib::PathD& path : wall.band) {
            std::vector<Vec2> ring;
            for (const Clipper2Lib::PointD& p : path) ring.push_back({p.x, p.y});
            if (ring.size() >= 3 && insidePolygon(inside, ring)) return wall.id;
        }
    }
    return {};
}

}

// A rotation in degrees brought into the range from 0 up to 360.
double normalizeRotation(double degrees)
{
    if (!std::isfinite(degrees)) return degrees;
    double r = std::fmod(degrees, 360.0);
    if (r < 0.0) r += 360.0;
    if (r >= 360.0 || r == 0.0) return 0.0;
    return r;
}

// The direction an item's front faces.
Vec2 itemFront(const Item& item)
{
    return frontOf(item.rotation);
}

// Works out an item's footprint.
ItemFootprint itemFootprint(const Item& item)
{
    ItemFootprint footprint;
    const double r = item.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    const double hw = item.size.width / 2.0;
    const double hd = item.size.depth / 2.0;
    const Vec2 local[4] = {{-hw, -hd}, {hw, -hd}, {hw, hd}, {-hw, hd}};
    for (std::size_t i = 0; i < 4; ++i) {
        footprint.corners[i] = {item.x + local[i].x * c - local[i].y * s, item.y + local[i].x * s + local[i].y * c};
    }
    footprint.centre = {item.x, item.y};
    footprint.front = frontOf(item.rotation);
    return footprint;
}

// The height of the middle of an item.
double itemCentreHeight(const Item& item)
{
    return item.z + item.size.height / 2.0;
}

// The z that puts the middle of an item at the default wall item centre.
double defaultWallItemZ(const DefaultSizes& defaults, double height)
{
    return std::max(0.0, defaults.wallItemCentre - height / 2.0);
}

// Checks the rules every item command enforces.
Status checkItemPlacement(const Floor& floor, const Item& item)
{
    if (!isValidAssetName(item.asset)) {
        return fail(ErrorCode::InvalidValue, "The asset name \"" + item.asset + "\" is not valid.");
    }
    if (!std::isfinite(item.x) || !std::isfinite(item.y)) {
        return fail(ErrorCode::InvalidValue, "The position of the item must be finite numbers.");
    }
    if (!std::isfinite(item.z)) return fail(ErrorCode::InvalidValue, "The height above the floor must be a finite number.");
    if (!std::isfinite(item.rotation)) return fail(ErrorCode::InvalidValue, "The rotation must be a finite number.");
    const ItemSize& size = item.size;
    if (!std::isfinite(size.width) || !std::isfinite(size.depth) || !std::isfinite(size.height)) {
        return fail(ErrorCode::InvalidValue, "The size of the item must be finite numbers.");
    }
    if (!(size.width > 0.0) || !(size.depth > 0.0) || !(size.height > 0.0)) {
        return fail(ErrorCode::InvalidValue, "The size of the item must be above zero in every direction.");
    }
    if (item.z < 0.0) return fail(ErrorCode::InvalidValue, "The height above the floor cannot be below zero.");
    if (item.room && !findRoom(floor, *item.room)) {
        return fail(ErrorCode::NotFound, "There is no room " + *item.room + " on this floor.");
    }
    if (item.wall) {
        const Wall* wall = findWall(floor, *item.wall);
        if (!wall) return fail(ErrorCode::NotFound, "There is no wall " + *item.wall + " on this floor.");
        const double height = wallHeight(floor, *wall);
        if (item.z + size.height > height + 1e-9) {
            return fail(ErrorCode::InvalidValue, "The item would reach " + metres(item.z + size.height)
                                                     + ", but the wall is " + metres(height) + " high.");
        }
        const double length = wallLength(floor, *wall);
        if (size.width > length + 1e-9) {
            return fail(ErrorCode::InvalidValue,
                        "The item is " + metres(size.width) + " wide, but the wall is only " + metres(length) + " long.");
        }
    }
    return {};
}

// Works out where a wall item hangs on its wall.
WallItemSpot wallItemSpot(const Floor& floor, const Item& item)
{
    WallItemSpot spot;
    if (!item.wall) return spot;
    const Wall* wall = findWall(floor, *item.wall);
    if (!wall) return spot;
    const WallCurve curve = wallCurve(floor, *wall);
    if (!curve.valid) return spot;
    const CurveProjection centre = projectOntoCurve({item.x, item.y}, curve);
    const ItemFootprint footprint = itemFootprint(item);
    const Vec2 back = minus(footprint.centre, times(footprint.front, item.size.depth / 2.0));
    const CurveProjection backProjection = projectOntoCurve(back, curve);
    spot.valid = true;
    spot.wall = wall->id;
    spot.side = centre.side;
    spot.offset = centre.offset;
    spot.gap = backProjection.distance - wall->thickness / 2.0;
    return spot;
}

// The pose that hangs an item flush on one face of a wall.
ItemPose wallItemPose(const Floor& floor, const Wall& wall, WallSide side, double offset, const ItemSize& size)
{
    ItemPose pose;
    if (!findWall(floor, wall.id)) {
        pose.status = fail(ErrorCode::NotFound, "There is no wall " + wall.id + " on this floor.");
        return pose;
    }
    if (!std::isfinite(offset) || !std::isfinite(size.width) || !std::isfinite(size.depth) || !std::isfinite(size.height)) {
        pose.status = fail(ErrorCode::InvalidValue, "The offset and the size of the item must be finite numbers.");
        return pose;
    }
    const WallCurve curve = wallCurve(floor, wall);
    if (!curve.valid) {
        pose.status = fail(ErrorCode::InvalidValue, "The wall " + wall.id + " has no valid shape.");
        return pose;
    }
    const double half = size.width / 2.0;
    if (size.width >= curve.length) {
        offset = curve.length / 2.0;
    } else {
        offset = std::clamp(offset, half, curve.length - half);
    }
    const Vec2 onLine = pointAlong(curve, offset);
    Vec2 normal = rightOf(tangentAlong(curve, offset));
    if (side == WallSide::Left) normal = times(normal, -1.0);
    const double reach = wall.thickness / 2.0 + size.depth / 2.0;
    pose.position = plus(onLine, times(normal, reach));
    pose.rotation = normalizeRotation(std::atan2(-normal.x, normal.y) * 180.0 / std::numbers::pi);
    pose.offset = offset;
    return pose;
}

// Snaps the centre of a floor item.
ItemSnap snapItemPosition(const Floor& floor, const Item& item, Vec2 wanted, const ItemSnapOptions& options)
{
    ItemSnap result;
    result.position = wanted;
    if (options.gridSize > 0.0) {
        result.position.x = std::round(wanted.x / options.gridSize) * options.gridSize;
        result.position.y = std::round(wanted.y / options.gridSize) * options.gridSize;
        result.toGrid = true;
    }
    if (!(options.radius > 0.0) || floor.walls.empty()) return result;

    const std::vector<Face> faces = exposedFaces(floor);
    if (faces.empty()) return result;
    const FloorContext context = [&] {
        FloorContext c;
        for (const Wall& wall : floor.walls) c.walls.push_back({wall.id, toPaths(wallBand(floor, wall))});
        return c;
    }();

    Item probe = item;
    probe.x = result.position.x;
    probe.y = result.position.y;
    const Face* first = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        const SideSnap snap = findSideSnap(faces, itemFootprint(probe), options.radius, first);
        if (!snap.face) break;
        probe.x -= snap.signedDistance * snap.face->outward.x;
        probe.y -= snap.signedDistance * snap.face->outward.y;
        const Id wall = wallBehind(context, *snap.face, snap.probe);
        if (!wall.empty() && std::find(result.walls.begin(), result.walls.end(), wall) == result.walls.end()) {
            result.walls.push_back(wall);
        }
        if (!first) first = snap.face;
    }
    result.position = {probe.x, probe.y};
    return result;
}

// The stable name of a problem kind.
std::string_view itemProblemName(ItemProblemKind kind)
{
    switch (kind) {
    case ItemProblemKind::NoRoom: return "no_room";
    case ItemProblemKind::OutsideRoom: return "outside_room";
    case ItemProblemKind::OverlapsItem: return "overlaps_item";
    case ItemProblemKind::OverlapsWall: return "overlaps_wall";
    case ItemProblemKind::OverlapsOpening: return "overlaps_opening";
    case ItemProblemKind::OffWall: return "off_wall";
    case ItemProblemKind::TooTall: return "too_tall";
    case ItemProblemKind::MissingAsset: return "missing_asset";
    case ItemProblemKind::SizeChanged: return "size_changed";
    }
    return "no_room";
}

namespace {

// The problems of one item, using the context of its floor.
std::vector<ItemProblem> problemsFor(const Floor& floor, const FloorContext& context, const Item& item, const AssetLookup& lookup)
{
    std::vector<ItemProblem> found;
    const auto add = [&](ItemProblemKind kind, Id other, std::string message) {
        found.push_back({item.id, kind, std::move(other), std::move(message)});
    };
    const std::string label = labelOf(item);
    const ItemFootprint footprint = itemFootprint(item);
    const Clipper2Lib::PathsD shape = footprintPaths(footprint);

    // The room it stands in: the first room whose rings hold its centre.
    const FloorContext::RoomInfo* room = nullptr;
    for (const FloorContext::RoomInfo& r : context.rooms) {
        if (r.shape.closed && insideRings(footprint.centre, r.shape.rings)) {
            room = &r;
            break;
        }
    }
    if (!room) {
        add(ItemProblemKind::NoRoom, {}, label + " is not in any room.");
    } else if (uncoveredArea(shape, room->area) > kItemOverlapArea) {
        add(ItemProblemKind::OutsideRoom, room->id, label + " sticks out of room " + room->id + ".");
    }

    const std::size_t first = found.size();
    for (const Item& other : floor.items) {
        if (other.id == item.id) continue;
        const double bottom = std::max(item.z, other.z);
        const double top = std::min(item.z + item.size.height, other.z + other.size.height);
        if (top - bottom <= kItemTouchTolerance) continue;
        const double reach = std::hypot(item.size.width, item.size.depth) / 2.0 + std::hypot(other.size.width, other.size.depth) / 2.0;
        if (distance(footprint.centre, {other.x, other.y}) > reach) continue;
        if (sharedArea(shape, footprintPaths(itemFootprint(other))) > kItemOverlapArea) {
            add(ItemProblemKind::OverlapsItem, other.id, label + " overlaps " + labelOf(other) + ".");
        }
    }
    std::sort(found.begin() + static_cast<std::ptrdiff_t>(first), found.end(),
              [](const ItemProblem& a, const ItemProblem& b) { return a.other < b.other; });

    const std::size_t wallsFirst = found.size();
    for (const FloorContext::WallInfo& wall : context.walls) {
        if (item.wall && *item.wall == wall.id) continue;
        if (sharedArea(shape, wall.band) > kItemOverlapArea) {
            add(ItemProblemKind::OverlapsWall, wall.id, label + " overlaps wall " + wall.id + ".");
        }
    }
    std::sort(found.begin() + static_cast<std::ptrdiff_t>(wallsFirst), found.end(),
              [](const ItemProblem& a, const ItemProblem& b) { return a.other < b.other; });

    if (item.wall) {
        const WallItemSpot spot = wallItemSpot(floor, item);
        if (spot.valid) {
            const std::size_t openingsFirst = found.size();
            const double uLow = spot.offset - item.size.width / 2.0;
            const double uHigh = spot.offset + item.size.width / 2.0;
            for (const Opening& o : floor.openings) {
                if (o.wall != *item.wall) continue;
                const double across = std::min(uHigh, o.offset + o.width / 2.0) - std::max(uLow, o.offset - o.width / 2.0);
                const double up = std::min(item.z + item.size.height, o.sill + o.height) - std::max(item.z, o.sill);
                if (across > kItemTouchTolerance && up > kItemTouchTolerance) {
                    add(ItemProblemKind::OverlapsOpening, o.id, label + " overlaps opening " + o.id + ".");
                }
            }
            std::sort(found.begin() + static_cast<std::ptrdiff_t>(openingsFirst), found.end(),
                      [](const ItemProblem& a, const ItemProblem& b) { return a.other < b.other; });
            if (std::fabs(spot.gap) > kItemOffWallTolerance) {
                add(ItemProblemKind::OffWall, spot.wall, label + " is not against wall " + spot.wall + ".");
            }
        }
    } else if (item.z + item.size.height > floor.ceilingHeight + 1e-9) {
        add(ItemProblemKind::TooTall, {}, label + " is taller than the ceiling.");
    }

    if (lookup) {
        const std::optional<ItemSize> known = lookup(item.asset);
        if (!known) {
            add(ItemProblemKind::MissingAsset, {}, label + " uses asset " + item.asset + ", which is not in the library.");
        } else if (std::fabs(known->width - item.size.width) > kSizeTolerance || std::fabs(known->depth - item.size.depth) > kSizeTolerance
                   || std::fabs(known->height - item.size.height) > kSizeTolerance) {
            add(ItemProblemKind::SizeChanged, {}, label + " has another size than asset " + item.asset + " has in the library.");
        }
    }
    return found;
}

}

// The problems of one item as it would be on the floor.
std::vector<ItemProblem> checkItem(const Floor& floor, const Item& item, const AssetLookup& lookup)
{
    return problemsFor(floor, makeContext(floor), item, lookup);
}

// The problems of every item on the floor.
std::vector<ItemProblem> itemProblems(const Floor& floor, const AssetLookup& lookup)
{
    std::vector<ItemProblem> all;
    if (floor.items.empty()) return all;
    const FloorContext context = makeContext(floor);
    for (const Item& item : floor.items) {
        for (ItemProblem& p : problemsFor(floor, context, item, lookup)) all.push_back(std::move(p));
    }
    return all;
}

}
