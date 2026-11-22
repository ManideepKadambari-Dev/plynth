#include "plynth/core/BuildGeometry.h"

#include "BandOutline.h"
#include "RingClean.h"

#include "plynth/core/OpeningShapes.h"
#include "plynth/core/Openings.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"

#include "ClipperInclude.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <string>

namespace plynth {

namespace {

constexpr int kPrecision = 8;

// Points closer than this are the same point.
constexpr double kClose = 1e-6;

// How far from half the thickness an edge may lie and still be a face of a wall's band.
constexpr double kFaceTolerance = 1e-3;

// Distances that differ by less than this are equal when choosing between walls.
constexpr double kTie = 1e-9;

// A room's floor area rings, used to tell which room an edge looks into.
struct RoomArea {
    Id id;
    std::vector<std::vector<Vec2>> rings; // plan coordinates
};

// What the wall solids need to know about one wall.
struct WallInfo {
    const Wall* wall = nullptr;
    double height = 0.0;
    double half = 0.0;
    std::vector<Vec2> line; // the drawn centre line, plan coordinates
    std::vector<std::vector<Vec2>> bands; // the band outlines, world coordinates
};

// Turns a world point into a plan point and back; the two are the same flip.
Vec2 flipped(Vec2 p)
{
    return {p.x, -p.y};
}

// A list of points as a Clipper path.
Clipper2Lib::PathD toPath(const std::vector<Vec2>& points)
{
    Clipper2Lib::PathD path;
    path.reserve(points.size());
    for (const Vec2& p : points) path.emplace_back(p.x, p.y);
    return path;
}

// A Clipper path as a list of points.
std::vector<Vec2> fromPath(const Clipper2Lib::PathD& path)
{
    std::vector<Vec2> points;
    points.reserve(path.size());
    for (const Clipper2Lib::PointD& p : path) points.push_back({p.x, p.y});
    return points;
}

using detail::withoutRepeats;

// The ring turned to run anticlockwise (or clockwise) as seen from above, by the world shoelace sum.
std::vector<Vec2> oriented(std::vector<Vec2> ring, bool anticlockwise)
{
    if ((signedArea(ring) > 0.0) != anticlockwise) std::reverse(ring.begin(), ring.end());
    return ring;
}

// The ring rotated to start at its lowest x, then lowest y, so equal shapes give equal lists.
std::vector<Vec2> startedAtLowest(std::vector<Vec2> ring)
{
    if (ring.empty()) return ring;
    std::size_t best = 0;
    for (std::size_t i = 1; i < ring.size(); ++i) {
        if (ring[i].x < ring[best].x || (ring[i].x == ring[best].x && ring[i].y < ring[best].y)) best = i;
    }
    std::rotate(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(best), ring.end());
    return ring;
}

// Whether a position is inside the rings by the even odd rule.
bool insideRings(Vec2 position, const std::vector<std::vector<Vec2>>& rings)
{
    int count = 0;
    for (const std::vector<Vec2>& ring : rings) {
        if (ring.size() >= 3 && insidePolygon(position, ring)) ++count;
    }
    return count % 2 == 1;
}

// The ring with a point added wherever one of the given points lies on one of its edges, so an edge that
// the union merged across two walls is cut again where the walls' bands met.
std::vector<Vec2> splitAtVertices(const std::vector<Vec2>& ring, const std::vector<Vec2>& vertices)
{
    std::vector<Vec2> out;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        out.push_back(a);
        const double length = distance(a, b);
        std::vector<std::pair<double, Vec2>> found;
        for (const Vec2& v : vertices) {
            const SegmentProjection hit = projectOntoSegment(v, a, b);
            if (hit.distance < kClose && hit.offset > kClose && hit.offset < length - kClose) found.emplace_back(hit.offset, v);
        }
        std::sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        double last = 0.0;
        for (const auto& [offset, v] : found) {
            if (offset - last < kClose) continue;
            out.push_back(v);
            last = offset;
        }
    }
    return out;
}

// What one edge of a ring shows. a and b are world points.
WallEdge tagEdge(Vec2 a, Vec2 b, const std::vector<const WallInfo*>& group, const std::vector<RoomArea>& rooms)
{
    const Vec2 pa = flipped(a);
    const Vec2 pb = flipped(b);
    const Vec2 mid{(pa.x + pb.x) / 2.0, (pa.y + pb.y) / 2.0};
    const Vec2 along = detail::unit({pb.x - pa.x, pb.y - pa.y});

    WallEdge edge;
    double bestFace = 1e300;
    double bestNear = 1e300;
    const WallInfo* nearest = nullptr;
    for (const WallInfo* info : group) {
        double dist = 1e300;
        Vec2 direction{1.0, 0.0};
        Vec2 foot = info->line.empty() ? mid : info->line.front();
        for (std::size_t i = 0; i + 1 < info->line.size(); ++i) {
            const SegmentProjection hit = projectOntoSegment(mid, info->line[i], info->line[i + 1]);
            if (hit.distance < dist) {
                dist = hit.distance;
                direction = detail::unit({info->line[i + 1].x - info->line[i].x, info->line[i + 1].y - info->line[i].y});
                foot = hit.position;
            }
        }
        if (dist < bestNear - kTie) {
            bestNear = dist;
            nearest = info;
        }
        const double diff = std::fabs(dist - info->half);
        const double cross = along.x * direction.y - along.y * direction.x;
        if (diff <= kFaceTolerance && std::fabs(cross) <= std::sin(kInLineAngle) && diff < bestFace - kTie) {
            bestFace = diff;
            edge.wall = info->wall->id;
            const Vec2 offset{mid.x - foot.x, mid.y - foot.y};
            edge.side = detail::dot(offset, rightOf(direction)) >= 0.0 ? WallSide::Right : WallSide::Left;
        }
    }
    if (edge.wall.empty() && nearest) {
        edge.wall = nearest->wall->id;
        edge.side.reset();
    }

    const Vec2 dw = detail::unit({b.x - a.x, b.y - a.y});
    const Vec2 probeWorld{(a.x + b.x) / 2.0 + dw.y * kBuildFaceProbe, (a.y + b.y) / 2.0 - dw.x * kBuildFaceProbe};
    const Vec2 probe = flipped(probeWorld);
    for (const RoomArea& room : rooms) {
        if (insideRings(probe, room.rings)) {
            edge.room = room.id;
            break;
        }
    }
    return edge;
}

// Makes a tagged ring from points that already have the right direction.
WallRing makeRing(const std::vector<Vec2>& points, const std::vector<const WallInfo*>& group,
                  const std::vector<Vec2>& vertices, const std::vector<RoomArea>& rooms)
{
    WallRing ring;
    ring.points = splitAtVertices(points, vertices);
    for (std::size_t i = 0; i < ring.points.size(); ++i) {
        ring.edges.push_back(tagEdge(ring.points[i], ring.points[(i + 1) % ring.points.size()], group, rooms));
    }
    return ring;
}

// The smallest wall id among the edges of a solid, empty when it has none.
Id smallestWall(const WallSolid& solid)
{
    Id best;
    const auto look = [&](const WallRing& ring) {
        for (const WallEdge& e : ring.edges) {
            if (!e.wall.empty() && (best.empty() || e.wall < best)) best = e.wall;
        }
    };
    look(solid.outline);
    for (const WallRing& h : solid.holes) look(h);
    return best;
}

// Whether most of a ring's points are inside another ring.
bool mostlyInside(const std::vector<Vec2>& inner, const std::vector<Vec2>& outer)
{
    std::size_t in = 0;
    for (const Vec2& p : inner) {
        if (insidePolygon(p, outer)) ++in;
    }
    return in * 2 >= inner.size();
}

// A ring from a Clipper result with its area.
struct AreaRing {
    std::vector<Vec2> points;
    double area = 0.0;
};

// Rotates a ring to start at the lowest point and runs it in the wanted direction.
std::vector<Vec2> canonical(const std::vector<Vec2>& ring, bool anticlockwise)
{
    return startedAtLowest(oriented(ring, anticlockwise));
}

// The outline of a polygon pushed in by a distance, the largest piece, anticlockwise. Empty when nothing is
// left.
std::vector<Vec2> pushedIn(const std::vector<Vec2>& outline, double delta)
{
    const Clipper2Lib::PathsD result = Clipper2Lib::InflatePaths(Clipper2Lib::PathsD{toPath(outline)}, -delta,
                                                                 Clipper2Lib::JoinType::Miter,
                                                                 Clipper2Lib::EndType::Polygon, 10.0, kPrecision);
    std::vector<Vec2> best;
    double bestArea = 0.0;
    for (const Clipper2Lib::PathD& path : result) {
        std::vector<Vec2> points = withoutRepeats(fromPath(path));
        if (points.size() < 3) continue;
        const double area = std::fabs(signedArea(points));
        if (area > bestArea) {
            bestArea = area;
            best = std::move(points);
        }
    }
    if (bestArea < 1e-9) return {};
    return oriented(std::move(best), true);
}

// Whether the outline has a straight bottom edge along the whole width of its box.
bool hasFlatBottom(const std::vector<Vec2>& outline, double width)
{
    bool left = false;
    bool right = false;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const Vec2 a = outline[i];
        const Vec2 b = outline[(i + 1) % outline.size()];
        if (std::fabs(a.y) > 1e-9 || std::fabs(b.y) > 1e-9) continue;
        left = left || std::min(a.x, b.x) <= -width / 2.0 + 1e-9;
        right = right || std::max(a.x, b.x) >= width / 2.0 - 1e-9;
    }
    return left && right;
}

// The outline made smaller by the frame width on every side. When keepBottom is set the bottom stays put:
// the shape is first carried down below the floor, pushed in, and cut off at v = 0.
std::vector<Vec2> frameOutline(const std::vector<Vec2>& outline, double width, bool keepBottom)
{
    if (!keepBottom) return pushedIn(outline, kOpeningFrameWidth);
    const double reach = 1.0;
    const Clipper2Lib::PathsD below{
        toPath({{-width / 2.0, -reach}, {width / 2.0, -reach}, {width / 2.0, 0.0}, {-width / 2.0, 0.0}})};
    const Clipper2Lib::PathsD joined =
        Clipper2Lib::Union(Clipper2Lib::PathsD{toPath(outline)}, below, Clipper2Lib::FillRule::NonZero, kPrecision);
    std::vector<Vec2> best;
    double bestArea = 0.0;
    for (const Clipper2Lib::PathD& path : joined) {
        std::vector<Vec2> points = withoutRepeats(fromPath(path));
        if (points.size() < 3) continue;
        const double area = std::fabs(signedArea(points));
        if (area > bestArea) {
            bestArea = area;
            best = std::move(points);
        }
    }
    if (best.empty()) return {};
    const std::vector<Vec2> pushed = pushedIn(best, kOpeningFrameWidth);
    if (pushed.empty()) return {};
    double top = 0.0;
    for (const Vec2& p : outline) top = std::max(top, p.y);
    const Clipper2Lib::PathsD cut = Clipper2Lib::Intersect(
        Clipper2Lib::PathsD{toPath(pushed)},
        Clipper2Lib::PathsD{toPath({{-width, 0.0}, {width, 0.0}, {width, top + 1.0}, {-width, top + 1.0}})},
        Clipper2Lib::FillRule::NonZero, kPrecision);
    std::vector<Vec2> result;
    double resultArea = 0.0;
    for (const Clipper2Lib::PathD& path : cut) {
        std::vector<Vec2> points = withoutRepeats(fromPath(path));
        if (points.size() < 3) continue;
        const double area = std::fabs(signedArea(points));
        if (area > resultArea) {
            resultArea = area;
            result = std::move(points);
        }
    }
    if (resultArea < 1e-9) return {};
    return oriented(std::move(result), true);
}

// The outline with extra points on every edge that runs along u, at the given u values, so a cut on a curved
// wall can follow the curve.
std::vector<Vec2> withBreaks(const std::vector<Vec2>& outline, const std::vector<double>& breaks)
{
    std::vector<Vec2> out;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const Vec2 a = outline[i];
        const Vec2 b = outline[(i + 1) % outline.size()];
        out.push_back(a);
        const double lo = std::min(a.x, b.x);
        const double hi = std::max(a.x, b.x);
        if (hi - lo < 1e-9) continue;
        std::vector<double> inside;
        for (const double u : breaks) {
            if (u > lo + 1e-9 && u < hi - 1e-9) inside.push_back(u);
        }
        if (a.x > b.x) std::reverse(inside.begin(), inside.end());
        for (const double u : inside) {
            const double k = (u - a.x) / (b.x - a.x);
            out.push_back({u, a.y + (b.y - a.y) * k});
        }
    }
    return out;
}

}

// A plan position in world X and Y.
Vec2 planToWorld(Vec2 plan)
{
    return {plan.x, -plan.y};
}

// The level of each floor.
std::vector<double> floorElevations(const Plan& plan)
{
    std::vector<double> levels;
    double level = 0.0;
    for (const Floor& floor : plan.floors) {
        levels.push_back(level);
        level += floor.ceilingHeight;
    }
    return levels;
}

// The walls of a floor as solids that never overlap.
std::vector<WallSolid> wallSolids(const Floor& floor)
{
    std::vector<const Wall*> ordered;
    for (const Wall& w : floor.walls) ordered.push_back(&w);
    std::sort(ordered.begin(), ordered.end(), [](const Wall* a, const Wall* b) { return a->id < b->id; });

    std::vector<WallInfo> infos;
    for (const Wall* wall : ordered) {
        WallInfo info;
        info.wall = wall;
        info.height = wallHeight(floor, *wall);
        info.half = wall->thickness / 2.0;
        info.line = wallPath(floor, *wall).centreLine;
        for (const std::vector<Vec2>& band : detail::wallBandWith(floor, *wall, true)) {
            std::vector<Vec2> world;
            for (const Vec2& p : band) world.push_back(flipped(p));
            info.bands.push_back(std::move(world));
        }
        if (info.bands.empty() || info.line.size() < 2) continue;
        infos.push_back(std::move(info));
    }
    if (infos.empty()) return {};

    std::vector<RoomArea> rooms;
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (shape.closed) rooms.push_back({room.id, shape.floorArea});
    }

    std::map<double, std::vector<const WallInfo*>, std::greater<double>> byHeight;
    for (const WallInfo& info : infos) byHeight[info.height].push_back(&info);

    std::vector<WallSolid> solids;
    for (const auto& [height, group] : byHeight) {
        Clipper2Lib::PathsD subject;
        std::vector<Vec2> vertices;
        for (const WallInfo* info : group) {
            for (const std::vector<Vec2>& band : info->bands) {
                subject.push_back(toPath(band));
                vertices.insert(vertices.end(), band.begin(), band.end());
            }
        }
        Clipper2Lib::PathsD taller;
        for (const WallInfo& info : infos) {
            if (info.height <= height) continue;
            for (const std::vector<Vec2>& band : info.bands) taller.push_back(toPath(band));
        }
        const Clipper2Lib::PathsD shape =
            taller.empty() ? Clipper2Lib::Union(subject, Clipper2Lib::FillRule::NonZero, kPrecision)
                           : Clipper2Lib::Difference(subject, taller, Clipper2Lib::FillRule::NonZero, kPrecision);

        std::vector<AreaRing> outers;
        std::vector<AreaRing> holes;
        for (const Clipper2Lib::PathD& path : shape) {
            AreaRing ring;
            ring.points = detail::cleanedWallRing(fromPath(path));
            if (ring.points.size() < 3) continue;
            ring.area = signedArea(ring.points);
            if (std::fabs(ring.area) < 1e-9) continue;
            (ring.area > 0.0 ? outers : holes).push_back(std::move(ring));
        }

        std::vector<std::vector<AreaRing>> holesOf(outers.size());
        for (AreaRing& hole : holes) {
            std::size_t owner = outers.size();
            for (std::size_t i = 0; i < outers.size(); ++i) {
                if (!mostlyInside(hole.points, outers[i].points)) continue;
                if (owner == outers.size() || outers[i].area < outers[owner].area) owner = i;
            }
            if (owner < outers.size()) holesOf[owner].push_back(std::move(hole));
        }

        for (std::size_t i = 0; i < outers.size(); ++i) {
            WallSolid solid;
            solid.height = height;
            solid.outline = makeRing(canonical(outers[i].points, true), group, vertices, rooms);
            std::vector<std::vector<Vec2>> rings;
            for (const AreaRing& h : holesOf[i]) rings.push_back(canonical(h.points, false));
            std::sort(rings.begin(), rings.end(), [](const auto& a, const auto& b) {
                return a.front().x != b.front().x ? a.front().x < b.front().x : a.front().y < b.front().y;
            });
            for (const std::vector<Vec2>& ring : rings) solid.holes.push_back(makeRing(ring, group, vertices, rooms));
            solids.push_back(std::move(solid));
        }
    }

    std::stable_sort(solids.begin(), solids.end(), [](const WallSolid& a, const WallSolid& b) {
        if (a.height != b.height) return a.height > b.height;
        const Id x = smallestWall(a);
        const Id y = smallestWall(b);
        if (x != y) return x < y;
        const Vec2 p = a.outline.points.front();
        const Vec2 q = b.outline.points.front();
        return p.x != q.x ? p.x < q.x : p.y < q.y;
    });
    return solids;
}

// Works out how one opening is built on a floor at a level.
OpeningBuildResult openingBuild(const Floor& floor, const Opening& opening, double elevation)
{
    OpeningBuildResult result;
    OpeningBuild& out = result.opening;
    out.id = opening.id;
    out.wall = opening.wall;
    out.type = opening.type;
    out.shape = opening.shape;
    result.status = checkOpeningFit(floor, opening);
    if (!result.status.ok()) return result;

    const Wall* wall = findWall(floor, opening.wall);
    const OpeningShape* shape = findOpeningShape(opening.shape);
    const WallCurve curve = wallCurve(floor, *wall);
    const OpeningPlacement placement = openingPlacement(floor, opening);

    out.width = opening.width;
    out.height = opening.height;
    out.sill = opening.sill;
    out.hinge = opening.hinge;
    out.swing = opening.swing;
    out.frame = opening.frame;
    out.thickness = wall->thickness;
    out.curved = curve.curved;

    const Vec2 tangentPlan = tangentAlong(curve, opening.offset);
    const Vec2 rightPlan = rightOf(tangentPlan);
    out.centre = planToWorld(placement.centre);
    out.tangent = planToWorld(tangentPlan);
    out.right = planToWorld(rightPlan);

    out.outline = shape->outline(opening.width, opening.height);
    const bool flatBottom = hasFlatBottom(out.outline, opening.width);
    const bool keepBottom = opening.sill <= 1e-9 && opening.type != OpeningType::Window && flatBottom;
    out.innerOutline = frameOutline(out.outline, opening.width, keepBottom);

    std::vector<double> breaks;
    if (curve.curved) {
        const int count = arcSegmentCount(curve.radius, std::fabs(curve.sweep) * opening.width / curve.length);
        for (int k = 1; k < count; ++k) breaks.push_back(-opening.width / 2.0 + opening.width * k / count);
    }
    const std::vector<Vec2> cutOutline = curve.curved ? withBreaks(out.outline, breaks) : out.outline;

    const double wallTop = wallHeight(floor, *wall);
    const bool touchesTop = opening.sill + opening.height >= wallTop - 1e-9;
    const bool touchesFloor = opening.sill <= 1e-9;
    const double reach = wall->thickness / 2.0 + kBuildCutMargin;
    for (const Vec2& uv : cutOutline) {
        Vec2 centre = placement.centre;
        Vec2 tangent = tangentPlan;
        if (curve.curved) {
            const double at = std::clamp(opening.offset + uv.x, 0.0, curve.length);
            centre = pointAlong(curve, at);
            tangent = tangentAlong(curve, at);
        } else {
            centre = detail::plus(centre, tangentPlan, uv.x);
        }
        const Vec2 normal = rightOf(tangent);
        double z = elevation + opening.sill + uv.y;
        if (touchesFloor && uv.y <= 1e-9) z -= kBuildCutMargin;
        if (touchesTop && uv.y >= opening.height - 1e-9) z += kBuildCutMargin;
        const Vec2 r = planToWorld(detail::plus(centre, normal, reach));
        const Vec2 l = planToWorld(detail::plus(centre, normal, -reach));
        out.cutter.right.push_back({r.x, r.y, z});
        out.cutter.left.push_back({l.x, l.y, z});
    }

    if (opening.sill <= 1e-9) {
        std::vector<Vec2> ring;
        for (const Vec2& p : placement.footprint) ring.push_back(planToWorld(p));
        out.threshold = oriented(withoutRepeats(ring), true);
    }
    return result;
}

// Works out a room's floor and ceiling.
std::optional<RoomBuild> roomBuild(const Floor& floor, const Room& room, double elevation)
{
    const RoomShape shape = roomShape(floor, room);
    if (!shape.closed) return std::nullopt;

    RoomBuild out;
    out.id = room.id;
    out.type = room.type;
    out.area = shape.area;
    out.floorZ = elevation;
    out.ceilingZ = elevation + floor.ceilingHeight;

    std::vector<AreaRing> outers;
    std::vector<AreaRing> holes;
    for (const std::vector<Vec2>& ring : shape.floorArea) {
        AreaRing r;
        for (const Vec2& p : ring) r.points.push_back(flipped(p));
        r.points = withoutRepeats(r.points);
        if (r.points.size() < 3) continue;
        r.area = signedArea(ring);
        (r.area > 0.0 ? outers : holes).push_back(std::move(r));
    }
    std::vector<WorldPolygon> polygons(outers.size());
    std::vector<double> areas;
    for (std::size_t i = 0; i < outers.size(); ++i) {
        polygons[i].outline = canonical(outers[i].points, true);
        areas.push_back(outers[i].area);
    }
    for (const AreaRing& hole : holes) {
        std::size_t owner = outers.size();
        for (std::size_t i = 0; i < outers.size(); ++i) {
            if (!mostlyInside(hole.points, outers[i].points)) continue;
            if (owner == outers.size() || areas[i] < areas[owner]) owner = i;
        }
        if (owner < outers.size()) polygons[owner].holes.push_back(canonical(hole.points, false));
    }
    out.polygons = std::move(polygons);
    return out;
}

// Where an item's asset goes in the world.
ItemPlacement itemPlacement(const Item& item, double elevation)
{
    ItemPlacement placement;
    placement.location = {item.x, -item.y, elevation + item.z};
    placement.rotationZ = item.rotation == 0.0 ? 0.0 : -item.rotation * std::numbers::pi / 180.0;
    return placement;
}

}
