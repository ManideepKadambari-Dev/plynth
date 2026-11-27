#include "plynth/core/Storeys.h"

#include "ClipperInclude.h"
#include "PlanRingClean.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/Stairs.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

// A list of points as a Clipper path.
Clipper2Lib::PathD toPath(const std::vector<Vec2>& points)
{
    Clipper2Lib::PathD path;
    path.reserve(points.size());
    for (const Vec2& p : points) path.emplace_back(p.x, p.y);
    return path;
}

// The area as Clipper paths: the outline, then each hole.
void addPaths(const PlanArea& area, Clipper2Lib::PathsD& out)
{
    out.push_back(toPath(area.outline));
    for (const std::vector<Vec2>& hole : area.holes) out.push_back(toPath(hole));
}

// The ring turned to start at its lowest point, then leftmost, so equal shapes give equal lists.
std::vector<Vec2> startedAtLowest(std::vector<Vec2> ring)
{
    if (ring.empty()) return ring;
    std::size_t best = 0;
    for (std::size_t i = 1; i < ring.size(); ++i) {
        if (ring[i].y < ring[best].y || (ring[i].y == ring[best].y && ring[i].x < ring[best].x)) best = i;
    }
    std::rotate(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(best), ring.end());
    return ring;
}

// The ring with the wanted turning direction (positive signedArea for an outline, negative for a hole).
std::vector<Vec2> turned(std::vector<Vec2> ring, bool outline)
{
    if ((signedArea(ring) > 0.0) != outline) std::reverse(ring.begin(), ring.end());
    return ring;
}

// The pieces of plain paths as plan areas, each read by the turning of its rings and then tidied
// (PlanRingClean.h).
std::vector<PlanArea> collect(const Clipper2Lib::PathsD& paths)
{
    std::vector<PlanArea> out;
    for (const RingPiece& read : piecesByTurning(paths)) {
        for (RingPiece& piece : tidyPiece(read)) {
            PlanArea area;
            area.outline = startedAtLowest(std::move(piece.outline));
            for (std::vector<Vec2>& hole : piece.holes) area.holes.push_back(startedAtLowest(std::move(hole)));
            std::sort(area.holes.begin(), area.holes.end(), [](const auto& a, const auto& b) {
                return a.front().y != b.front().y ? a.front().y < b.front().y : a.front().x < b.front().x;
            });
            out.push_back(std::move(area));
        }
    }
    return out;
}

// Runs one clip operation and returns the pieces in a stable order.
std::vector<PlanArea> run(Clipper2Lib::ClipType type, const Clipper2Lib::PathsD& subject, const Clipper2Lib::PathsD& clip)
{
    std::vector<PlanArea> out = collect(clipAndTidy(type, subject, clip));
    std::sort(out.begin(), out.end(), [](const PlanArea& a, const PlanArea& b) {
        const Vec2 p = a.outline.front();
        const Vec2 q = b.outline.front();
        return p.y != q.y ? p.y < q.y : p.x < q.x;
    });
    return out;
}

Clipper2Lib::PathsD pathsOf(const std::vector<PlanArea>& areas)
{
    Clipper2Lib::PathsD paths;
    for (const PlanArea& a : areas) addPaths(a, paths);
    return paths;
}

// Every segment of every ring of the uncut slab area: the boundary the edge faces stand on.
struct Boundary {
    std::vector<std::pair<Vec2, Vec2>> segments;
};

Boundary boundaryOf(const std::vector<PlanArea>& areas)
{
    Boundary b;
    const auto add = [&](const std::vector<Vec2>& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) b.segments.emplace_back(ring[i], ring[(i + 1) % ring.size()]);
    };
    for (const PlanArea& a : areas) {
        add(a.outline);
        for (const std::vector<Vec2>& h : a.holes) add(h);
    }
    return b;
}

// The distance from a point to the nearest segment of the boundary.
double distanceToBoundary(const Boundary& boundary, const Vec2& p)
{
    double best = 1e18;
    for (const auto& [a, b] : boundary.segments) best = std::min(best, distanceToSegmentOf(p, a, b));
    return best;
}

// One ring with its edge kinds. A vertex is added where an edge changes from edge face to well face, so every
// edge is one kind.
struct SidedRing {
    std::vector<Vec2> ring;
    std::vector<SlabSide> sides;
};

// Judges every edge of the ring in pieces, split at the boundary's points that lie within kSlabSideTolerance of
// the edge. A piece is an edge face when both its ends and its middle are within kSlabSideTolerance of the
// boundary, whatever way that runs: the middle matters because a well's side can run from boundary to boundary.
SidedRing sidedRing(const std::vector<Vec2>& ring, const Boundary& boundary)
{
    SidedRing out;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % n];
        const Vec2 d{b.x - a.x, b.y - a.y};
        const double len = std::hypot(d.x, d.y);

        std::vector<double> cuts{0.0, 1.0};
        for (const auto& [sa, sb] : boundary.segments) {
            for (const Vec2& q : {sa, sb}) {
                if (distanceToSegmentOf(q, a, b) > kSlabSideTolerance) continue;
                const double t = len > 0.0 ? ((q.x - a.x) * d.x + (q.y - a.y) * d.y) / (len * len) : 0.0;
                if (t * len > kSlabSideTolerance && (1.0 - t) * len > kSlabSideTolerance) cuts.push_back(t);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        std::vector<double> stops;
        for (double t : cuts) {
            if (stops.empty() || (t - stops.back()) * len > kSlabSideTolerance) stops.push_back(t);
        }
        if (stops.size() < 2) stops = {0.0, 1.0};
        stops.back() = 1.0;

        const auto at = [&](double t) { return Vec2{a.x + d.x * t, a.y + d.y * t}; };
        std::vector<SlabSide> kinds;
        for (std::size_t k = 0; k + 1 < stops.size(); ++k) {
            const bool edge = distanceToBoundary(boundary, at(stops[k])) <= kSlabSideTolerance &&
                              distanceToBoundary(boundary, at(stops[k + 1])) <= kSlabSideTolerance &&
                              distanceToBoundary(boundary, at(0.5 * (stops[k] + stops[k + 1]))) <= kSlabSideTolerance;
            kinds.push_back(edge ? SlabSide::Edge : SlabSide::Well);
        }
        out.ring.push_back(a);
        out.sides.push_back(kinds.front());
        for (std::size_t k = 1; k < kinds.size(); ++k) {
            if (kinds[k] == kinds[k - 1]) continue;
            out.ring.push_back(at(stops[k]));
            out.sides.push_back(kinds[k]);
        }
    }
    return out;
}

}

std::string_view slabSideName(SlabSide side)
{
    return side == SlabSide::Well ? "well" : "edge";
}

double planAreaSize(const PlanArea& area)
{
    double total = signedArea(area.outline);
    for (const std::vector<Vec2>& hole : area.holes) total += signedArea(hole);
    return total;
}

std::vector<PlanArea> floorFootprint(const Floor& floor)
{
    Clipper2Lib::PathsD paths;
    for (const Wall& wall : floor.walls) {
        for (const std::vector<Vec2>& ring : wallOutline(floor, wall)) paths.push_back(toPath(ring));
    }
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (!shape.closed) continue;
        // The area inside the centre lines, worked out for this room alone: the outer ring less its islands.
        // An island that reaches outside the room must not cut into another room's area.
        if (shape.rings.empty()) continue;
        Clipper2Lib::PathsD islands;
        for (std::size_t i = 1; i < shape.rings.size(); ++i) islands.push_back(toPath(turned(shape.rings[i], true)));
        const Clipper2Lib::PathsD outer{toPath(turned(shape.rings[0], true))};
        const std::vector<PlanArea> own = islands.empty() ? run(Clipper2Lib::ClipType::Union, outer, {})
                                                          : run(Clipper2Lib::ClipType::Difference, outer, islands);
        for (const PlanArea& a : own) addPaths(a, paths);
    }
    if (paths.empty()) return {};
    return run(Clipper2Lib::ClipType::Union, paths, {});
}

double floorRise(const Floor& floor)
{
    return floor.ceilingHeight + floor.slabThickness;
}

std::vector<PlanArea> exposedArea(const Plan& plan, std::size_t floorIndex)
{
    if (floorIndex >= plan.floors.size()) return {};
    const std::vector<PlanArea> own = floorFootprint(plan.floors[floorIndex]);
    if (floorIndex + 1 >= plan.floors.size() || own.empty()) return own;
    const std::vector<PlanArea> above = floorFootprint(plan.floors[floorIndex + 1]);
    if (above.empty()) return own;
    return run(Clipper2Lib::ClipType::Difference, pathsOf(own), pathsOf(above));
}

std::vector<PlanArea> roofSectionAreas(const Plan& plan, std::size_t floorIndex)
{
    const std::vector<PlanArea> exposed = exposedArea(plan, floorIndex);
    if (exposed.empty()) return {};
    const Clipper2Lib::PathsD all = pathsOf(exposed);
    const double half = 0.5 * (kMinRoofSectionWidth - kRoofWidthTolerance);
    const Clipper2Lib::PathsD shrunk = offsetByTurning(all, -half);
    if (shrunk.empty()) return {};
    const Clipper2Lib::PathsD grown = offsetByTurning(shrunk, half);
    if (grown.empty()) return {};
    const std::vector<PlanArea> kept = run(Clipper2Lib::ClipType::Intersection, grown, all);
    // Nothing was dropped: keep exposedArea as it is, so the corners of a wide part are not touched. A hairline
    // strip has next to no area, so the pieces must also have the same points.
    double keptSize = 0.0;
    double exposedSize = 0.0;
    std::size_t keptPoints = 0;
    std::size_t exposedPoints = 0;
    for (const PlanArea& a : kept) {
        keptSize += planAreaSize(a);
        keptPoints += a.outline.size();
        for (const auto& h : a.holes) keptPoints += h.size();
    }
    for (const PlanArea& a : exposed) {
        exposedSize += planAreaSize(a);
        exposedPoints += a.outline.size();
        for (const auto& h : a.holes) exposedPoints += h.size();
    }
    if (std::abs(keptSize - exposedSize) < kMinPlanAreaSize && keptPoints == exposedPoints) return exposed;
    return kept;
}

std::vector<PlanArea> overhangArea(const Plan& plan, std::size_t floorIndex)
{
    if (floorIndex + 1 >= plan.floors.size()) return {};
    const std::vector<PlanArea> above = floorFootprint(plan.floors[floorIndex + 1]);
    if (above.empty()) return {};
    return run(Clipper2Lib::ClipType::Difference, pathsOf(above), pathsOf(floorFootprint(plan.floors[floorIndex])));
}

// The slab above floor i: the footprint of the floor and of the floor above, without the stair wells.
SlabBuild slabBuild(const Plan& plan, std::size_t floorIndex, const std::vector<double>& levels)
{
    SlabBuild slab;
    if (floorIndex >= plan.floors.size()) return slab;
    const Floor& floor = plan.floors[floorIndex];
    slab.floor = floor.id;
    slab.bottom = (floorIndex < levels.size() ? levels[floorIndex] : 0.0) + floor.ceilingHeight;
    slab.top = slab.bottom + floor.slabThickness;

    Clipper2Lib::PathsD holes;
    for (const Stair& stair : floor.stairs) {
        if (stair.rise) continue;
        const StairLayout layout = stairLayout(plan, floor, stair);
        if (layout.status.ok() && layout.footprint.size() >= 3) holes.push_back(toPath(layout.footprint));
    }

    std::vector<PlanArea> own = floorFootprint(floor);
    Clipper2Lib::PathsD cover = pathsOf(own);
    if (floorIndex + 1 < plan.floors.size()) {
        for (const PlanArea& a : floorFootprint(plan.floors[floorIndex + 1])) addPaths(a, cover);
    }
    const std::vector<PlanArea> covered = cover.empty() ? std::vector<PlanArea>{}
                                                         : run(Clipper2Lib::ClipType::Union, cover, {});
    const std::vector<PlanArea> hanging = overhangArea(plan, floorIndex);

    // A plan ring as a world ring: y turned into -y, points reversed to keep the turning direction.
    const auto worldRing = [](const std::vector<Vec2>& r) {
        std::vector<Vec2> out;
        for (auto it = r.rbegin(); it != r.rend(); ++it) out.push_back({it->x, -it->y});
        return out;
    };
    // A plan area as a world polygon.
    auto toWorld = [&](const PlanArea& a) {
        WorldPolygon w;
        w.outline = worldRing(a.outline);
        for (const std::vector<Vec2>& h : a.holes) w.holes.push_back(worldRing(h));
        return w;
    };
    // The edge kinds of a world ring made from plan ring r: world edge j runs along plan edge n - 2 - j.
    const auto worldSides = [](const std::vector<SlabSide>& plan) {
        const std::size_t n = plan.size();
        std::vector<SlabSide> out(n);
        for (std::size_t j = 0; j < n; ++j) out[j] = plan[(2 * n - 2 - j) % n];
        return out;
    };
    const auto cut = [&](const std::vector<PlanArea>& areas) {
        return holes.empty() || areas.empty() ? areas : run(Clipper2Lib::ClipType::Difference, pathsOf(areas), holes);
    };
    const Boundary boundary = boundaryOf(covered);
    for (const PlanArea& a : cut(covered)) {
        const SidedRing outline = sidedRing(a.outline, boundary);
        PlanArea split;
        split.outline = outline.ring;
        SlabPolygonSides sides;
        sides.outline = worldSides(outline.sides);
        for (const std::vector<Vec2>& hole : a.holes) {
            SidedRing h = sidedRing(hole, boundary);
            split.holes.push_back(std::move(h.ring));
            sides.holes.push_back(worldSides(h.sides));
        }
        slab.polygons.push_back(toWorld(split));
        slab.sides.push_back(std::move(sides));
    }
    for (const PlanArea& a : cut(hanging)) slab.undersides.push_back(toWorld(a));
    const std::vector<PlanArea> exposed = exposedArea(plan, floorIndex);
    const std::vector<PlanArea> sections = roofSectionAreas(plan, floorIndex);
    if (!exposed.empty()) {
        const std::vector<PlanArea> ledges = sections.empty() ? exposed
                                                               : run(Clipper2Lib::ClipType::Difference, pathsOf(exposed), pathsOf(sections));
        for (const PlanArea& a : cut(ledges)) slab.tops.push_back(toWorld(a));
    }
    return slab;
}

}
