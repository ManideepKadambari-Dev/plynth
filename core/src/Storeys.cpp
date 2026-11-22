#include "plynth/core/Storeys.h"

#include "ClipperInclude.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/Stairs.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

constexpr int kPrecision = 8;
constexpr double kSliver = 1e-6; // pieces smaller than this many square metres are rounding noise

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

// Collects the pieces of a polygon tree: each outline with the holes directly inside it.
void collect(const Clipper2Lib::PolyPathD& node, std::vector<PlanArea>& out)
{
    for (std::size_t i = 0; i < node.Count(); ++i) {
        const Clipper2Lib::PolyPathD& child = *node.Child(i);
        if (!child.IsHole()) {
            PlanArea area;
            area.outline = startedAtLowest(turned(fromPath(child.Polygon()), true));
            for (std::size_t h = 0; h < child.Count(); ++h) {
                area.holes.push_back(startedAtLowest(turned(fromPath(child.Child(h)->Polygon()), false)));
            }
            std::sort(area.holes.begin(), area.holes.end(), [](const auto& a, const auto& b) {
                return a.front().y != b.front().y ? a.front().y < b.front().y : a.front().x < b.front().x;
            });
            if (area.outline.size() >= 3 && planAreaSize(area) >= kSliver) out.push_back(std::move(area));
        }
        collect(child, out);
    }
}

// Runs one clip operation and returns the pieces in a stable order.
std::vector<PlanArea> run(Clipper2Lib::ClipType type, const Clipper2Lib::PathsD& subject, const Clipper2Lib::PathsD& clip)
{
    Clipper2Lib::ClipperD clipper(kPrecision);
    clipper.AddSubject(subject);
    if (!clip.empty()) clipper.AddClip(clip);
    Clipper2Lib::PolyTreeD tree;
    clipper.Execute(type, Clipper2Lib::FillRule::NonZero, tree);
    std::vector<PlanArea> out;
    collect(tree, out);
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
        for (const std::vector<Vec2>& ring : wallBand(floor, wall)) paths.push_back(toPath(ring));
    }
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (!shape.closed) continue;
        for (const std::vector<Vec2>& ring : shape.floorArea) paths.push_back(toPath(ring));
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

    // A plan area as a world polygon: y turned into -y, points reversed to keep the turning direction.
    auto toWorld = [](const PlanArea& a) {
        auto ring = [](const std::vector<Vec2>& r) {
            std::vector<Vec2> out;
            for (auto it = r.rbegin(); it != r.rend(); ++it) out.push_back({it->x, -it->y});
            return out;
        };
        WorldPolygon w;
        w.outline = ring(a.outline);
        for (const std::vector<Vec2>& h : a.holes) w.holes.push_back(ring(h));
        return w;
    };
    const auto cut = [&](const std::vector<PlanArea>& areas) {
        return holes.empty() || areas.empty() ? areas : run(Clipper2Lib::ClipType::Difference, pathsOf(areas), holes);
    };
    for (const PlanArea& a : cut(covered)) slab.polygons.push_back(toWorld(a));
    for (const PlanArea& a : cut(hanging)) slab.undersides.push_back(toWorld(a));
    return slab;
}

}
