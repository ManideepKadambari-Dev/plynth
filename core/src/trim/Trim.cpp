#include "plynth/core/Trim.h"

#include "plynth/core/TrimProfiles.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

namespace plynth {

namespace {

constexpr double kSampleStep = 0.05;      // edges are tested for gaps at this spacing, then the edge is found exactly
constexpr double kFaceReach = 0.01;       // how far past half the thickness a point still counts as on the wall face
constexpr double kMinPieceLength = 0.01;  // shorter pieces of trim are dropped
constexpr double kMaxMitreStretch = 4.0;  // a very sharp corner does not stretch a section further than this

// Small sums and products of world vectors.
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }

// A vector made one long, or zero when it has no length.
Vec3 unit(Vec3 a)
{
    const double l = length(a);
    return l > 1e-12 ? scale(a, 1.0 / l) : Vec3{};
}

// Sweeps a profile along a path with an up direction at each point.
Sweep sweepWithUps(std::vector<Vec3> path, std::vector<Vec3> ups, bool closed, const std::vector<Vec2>& profile)
{
    std::vector<Vec3> points;
    std::vector<Vec3> pointUps;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!points.empty() && length(sub(path[i], points.back())) < 1e-6) continue;
        points.push_back(path[i]);
        pointUps.push_back(ups[i]);
    }
    if (closed) {
        while (points.size() > 1 && length(sub(points.front(), points.back())) < 1e-6) {
            points.pop_back();
            pointUps.pop_back();
        }
    }
    Sweep sweep;
    sweep.closed = closed && points.size() >= 3;
    if (points.size() < 2 || profile.size() < 3) return {};
    const std::size_t n = points.size();
    for (std::size_t k = 0; k < n; ++k) {
        const Vec3 up = unit(pointUps[k]);
        const bool hasBefore = sweep.closed || k > 0;
        const bool hasAfter = sweep.closed || k + 1 < n;
        const Vec3 before = hasBefore ? unit(sub(points[k], points[(k + n - 1) % n])) : Vec3{};
        const Vec3 after = hasAfter ? unit(sub(points[(k + 1) % n], points[k])) : Vec3{};
        const Vec3 sideBefore = unit(cross(hasBefore ? before : after, up));
        const Vec3 sideAfter = unit(cross(hasAfter ? after : before, up));
        Vec3 side = unit(add(sideBefore, sideAfter));
        double stretch = 1.0;
        const double c = dot(side, sideAfter);
        if (length(side) < 1e-9) {
            side = sideAfter;
        } else if (c > 1e-9) {
            stretch = std::min(kMaxMitreStretch, 1.0 / c);
        }
        std::vector<Vec3> section;
        section.reserve(profile.size());
        for (const Vec2& p : profile) section.push_back(add(points[k], add(scale(side, p.x * stretch), scale(up, p.y))));
        sweep.sections.push_back(std::move(section));
    }
    return sweep;
}

// The profile outline a style uses, and whether its profile was missing.
std::vector<Vec2> styleOutline(const TrimStyle& style, bool& missing)
{
    const TrimProfile* profile = findTrimProfile(style.profile);
    missing = profile == nullptr;
    if (!profile) profile = findTrimProfile(kSquareProfileId);
    if (!profile) return {};
    return profile->outline(style.height, style.depth);
}

// Says whether a world point lies in any of the polygons.
bool inPolygons(Vec2 p, const std::vector<WorldPolygon>& polygons)
{
    for (const WorldPolygon& poly : polygons) {
        if (!insidePolygon(p, poly.outline)) continue;
        bool inHole = false;
        for (const auto& hole : poly.holes) inHole = inHole || insidePolygon(p, hole);
        if (!inHole) return true;
    }
    return false;
}

// A gap of a wall: a stretch along its centre line where trim stops.
struct WallGap {
    double from = 0.0;
    double to = 0.0;
};

// Says whether a world point on a face of a wall lies in one of the wall's gaps.
bool inWallGap(Vec2 world, const Wall& wall, const WallCurve& curve, const std::vector<WallGap>& gaps)
{
    if (gaps.empty() || !curve.valid) return false;
    const CurveProjection p = projectOntoCurve({world.x, -world.y}, curve);
    if (p.distance > wall.thickness / 2.0 + kFaceReach) return false;
    return std::any_of(gaps.begin(), gaps.end(), [&](const WallGap& g) { return p.offset > g.from && p.offset < g.to; });
}

// Cuts the qualifying edges of a ring into paths, leaving out every point the gap test marks. Each path comes
// with whether it is closed. qualifies says which edges get trim; cut says, for an edge and a point on it,
// whether the point is in a gap.
std::vector<std::pair<std::vector<Vec2>, bool>> ringPaths(const WallRing& ring, const std::function<bool(std::size_t)>& qualifies,
                                                          const std::function<bool(std::size_t, Vec2)>& cut)
{
    std::vector<std::pair<std::vector<Vec2>, bool>> out;
    const std::size_t n = ring.points.size();
    if (n < 3 || ring.edges.size() != n) return out;

    // Runs of qualifying edges in ring order.
    bool all = true;
    std::size_t firstOff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!qualifies(i)) {
            if (all) firstOff = i;
            all = false;
        }
    }
    std::vector<std::vector<std::size_t>> runs;
    if (all) {
        runs.emplace_back();
        for (std::size_t i = 0; i < n; ++i) runs.back().push_back(i);
    } else {
        std::vector<std::size_t> run;
        for (std::size_t step = 1; step <= n; ++step) {
            const std::size_t i = (firstOff + step) % n;
            if (qualifies(i)) {
                run.push_back(i);
            } else if (!run.empty()) {
                runs.push_back(run);
                run.clear();
            }
        }
        if (!run.empty()) runs.push_back(run);
    }

    for (const auto& run : runs) {
        std::vector<std::vector<Vec2>> pieces;
        std::vector<Vec2> current;
        bool cutSeen = false;
        bool startedOutside = false;
        auto flush = [&]() {
            if (current.size() >= 2) pieces.push_back(current);
            current.clear();
        };
        for (std::size_t k = 0; k < run.size(); ++k) {
            const std::size_t i = run[k];
            const Vec2 a = ring.points[i];
            const Vec2 b = ring.points[(i + 1) % n];
            auto at = [&](double t) { return Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; };
            bool inside = cut(i, a);
            if (k == 0) startedOutside = !inside;
            if (inside) {
                if (!current.empty()) cutSeen = true;
                flush();
            } else if (current.empty()) {
                current.push_back(a);
            }
            const int samples = std::max(1, static_cast<int>(std::ceil(distance(a, b) / kSampleStep)));
            double t0 = 0.0;
            for (int s = 1; s <= samples; ++s) {
                const double t1 = static_cast<double>(s) / samples;
                const bool next = cut(i, at(t1));
                if (next != inside) {
                    double lo = t0, hi = t1;
                    for (int it = 0; it < 50; ++it) {
                        const double mid = (lo + hi) / 2.0;
                        if (cut(i, at(mid)) == inside) lo = mid;
                        else hi = mid;
                    }
                    const Vec2 edge = at((lo + hi) / 2.0);
                    if (inside) {
                        current = {edge};
                    } else {
                        current.push_back(edge);
                        flush();
                    }
                    inside = next;
                    cutSeen = true;
                }
                t0 = t1;
            }
            if (!inside) current.push_back(b);
        }
        const bool endedOutside = !current.empty();
        flush();
        if (all && !cutSeen && startedOutside && pieces.size() == 1) {
            std::vector<Vec2> loop = pieces.front();
            loop.pop_back();
            out.push_back({std::move(loop), true});
            continue;
        }
        if (all && startedOutside && endedOutside && pieces.size() >= 2) {
            // The last piece ends where the first begins, at the ring's first point: join them.
            std::vector<Vec2> joined = pieces.back();
            joined.insert(joined.end(), pieces.front().begin() + 1, pieces.front().end());
            pieces.front() = std::move(joined);
            pieces.pop_back();
        }
        for (auto& piece : pieces) out.push_back({std::move(piece), false});
    }
    return out;
}

// The length of a polyline.
double polylineLength(const std::vector<Vec2>& points)
{
    double total = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) total += distance(points[i - 1], points[i]);
    return total;
}

// Trim along the room faces of the walls: skirting at the floor or cornice at the ceiling.
std::vector<Sweep> faceTrim(const Floor& floor, const FloorBuild& build, const TrimSettings& settings, TrimKind kind,
                            const std::vector<Vec2>& profile, const std::vector<WorldPolygon>& skip)
{
    const TrimStyle& style = trimStyle(settings, kind);
    const bool skirting = kind == TrimKind::Skirting;
    const double z = skirting ? build.elevation : build.elevation + floor.ceilingHeight - style.height;

    // The gaps each wall's openings make.
    std::vector<std::pair<const Wall*, std::vector<WallGap>>> wallGaps;
    for (const Wall& wall : floor.walls) {
        std::vector<WallGap> gaps;
        for (const Opening& o : floor.openings) {
            if (o.wall != wall.id) continue;
            if (skirting) {
                if (o.sill >= style.height - 1e-9) continue;
                double half = o.width / 2.0;
                if (o.type == OpeningType::Door && settings.casing.enabled) half += settings.casing.height;
                gaps.push_back({o.offset - half, o.offset + half});
            } else {
                if (o.sill + o.height <= floor.ceilingHeight - style.height + 1e-9) continue;
                gaps.push_back({o.offset - o.width / 2.0, o.offset + o.width / 2.0});
            }
        }
        wallGaps.push_back({&wall, std::move(gaps)});
    }
    std::vector<WallCurve> curves;
    for (const Wall& wall : floor.walls) curves.push_back(wallCurve(floor, wall));

    std::vector<Sweep> sweeps;
    for (const WallSolid& solid : build.wallSolids) {
        if (!skirting && solid.height < floor.ceilingHeight - 1e-6) continue;
        std::vector<const WallRing*> rings = {&solid.outline};
        for (const WallRing& hole : solid.holes) rings.push_back(&hole);
        for (const WallRing* ring : rings) {
            auto qualifies = [&](std::size_t i) { return !ring->edges[i].room.empty(); };
            auto cut = [&](std::size_t i, Vec2 p) {
                if (inPolygons(p, skip)) return true;
                for (std::size_t w = 0; w < floor.walls.size(); ++w) {
                    if (floor.walls[w].id == ring->edges[i].wall) {
                        return inWallGap(p, floor.walls[w], curves[w], wallGaps[w].second);
                    }
                }
                return false;
            };
            for (const auto& [points, closed] : ringPaths(*ring, qualifies, cut)) {
                if (!closed && polylineLength(points) < kMinPieceLength) continue;
                std::vector<Vec3> path;
                for (const Vec2& p : points) path.push_back({p.x, p.y, z});
                Sweep s = sweepPath(path, closed, profile, {0.0, 0.0, 1.0});
                if (!s.sections.empty()) sweeps.push_back(std::move(s));
            }
        }
    }
    return sweeps;
}

// The world point of an opening's box (u along the wall, v up) on one face, following the wall's curve.
Vec3 faceAt(const WallCurve& curve, const Opening& opening, double thickness, double side, double elevation, Vec2 uv)
{
    const double offset = opening.offset + uv.x;
    const Vec2 centre = pointAlong(curve, offset);
    const Vec2 right = rightOf(tangentAlong(curve, offset));
    const Vec2 plan{centre.x + right.x * side * thickness / 2.0, centre.y + right.y * side * thickness / 2.0};
    const Vec2 world = planToWorld(plan);
    return {world.x, world.y, elevation + opening.sill + uv.y};
}

// The outward direction of one face of a wall at a distance along it, in world axes.
Vec3 faceNormal(const WallCurve& curve, double offset, double side)
{
    const Vec2 right = planToWorld(rightOf(tangentAlong(curve, offset)));
    return {right.x * side, right.y * side, 0.0};
}

// Casings round both faces of every door.
std::vector<Sweep> casingTrim(const Floor& floor, const FloorBuild& build, const std::vector<Vec2>& profile)
{
    std::vector<Vec2> swapped;
    for (const Vec2& p : profile) swapped.push_back({p.y, p.x});
    std::vector<Sweep> sweeps;
    for (const OpeningBuild& ob : build.openings) {
        if (ob.type != OpeningType::Door || ob.outline.size() < 3) continue;
        const Opening* opening = findOpening(floor, ob.id);
        const Wall* wall = opening ? findWall(floor, opening->wall) : nullptr;
        if (!wall) continue;
        const WallCurve curve = wallCurve(floor, *wall);
        if (!curve.valid) continue;

        const std::vector<Vec2>& outline = ob.outline;
        const std::size_t n = outline.size();
        double bottom = outline[0].y;
        for (const Vec2& p : outline) bottom = std::min(bottom, p.y);
        auto isBottom = [&](std::size_t i) { return outline[i % n].y <= bottom + 1e-9; };
        std::size_t bottomCount = 0;
        for (std::size_t i = 0; i < n; ++i) bottomCount += isBottom(i) ? 1 : 0;

        std::vector<Vec2> uvPath;
        bool closed = false;
        if (bottomCount < 2) {
            uvPath = outline;
            closed = true;
        } else {
            std::size_t start = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (isBottom(i) && !isBottom(i + 1)) start = i;
            }
            uvPath.push_back(outline[start]);
            for (std::size_t step = 1; step < n; ++step) {
                const std::size_t i = (start + step) % n;
                uvPath.push_back(outline[i]);
                if (isBottom(i)) break;
            }
        }

        for (double side : {1.0, -1.0}) {
            std::vector<Vec2> ordered = uvPath;
            // Seen from the right face the outline runs anticlockwise, which puts the profile's width away from
            // the hole; on the left face the path is walked the other way.
            if (side < 0.0) std::reverse(ordered.begin(), ordered.end());
            std::vector<Vec3> path;
            std::vector<Vec3> ups;
            for (const Vec2& uv : ordered) {
                path.push_back(faceAt(curve, *opening, wall->thickness, side, build.elevation, uv));
                ups.push_back(faceNormal(curve, opening->offset + uv.x, side));
            }
            Sweep s = sweepWithUps(path, ups, closed, swapped);
            if (!s.sections.empty()) sweeps.push_back(std::move(s));
        }
    }
    return sweeps;
}

// Sills on both faces of every window with a straight bottom of at least kMinSillWidth.
std::vector<Sweep> sillTrim(const Floor& floor, const FloorBuild& build, const TrimStyle& style,
                            const std::vector<Vec2>& profile)
{
    std::vector<Sweep> sweeps;
    for (const OpeningBuild& ob : build.openings) {
        if (ob.type != OpeningType::Window || ob.outline.size() < 3) continue;
        const Opening* opening = findOpening(floor, ob.id);
        const Wall* wall = opening ? findWall(floor, opening->wall) : nullptr;
        if (!wall) continue;
        const WallCurve curve = wallCurve(floor, *wall);
        if (!curve.valid) continue;
        double bottom = ob.outline[0].y;
        for (const Vec2& p : ob.outline) bottom = std::min(bottom, p.y);
        double low = 1e9, high = -1e9;
        int onBottom = 0;
        for (const Vec2& p : ob.outline) {
            if (p.y > bottom + 1e-9) continue;
            low = std::min(low, p.x);
            high = std::max(high, p.x);
            ++onBottom;
        }
        if (onBottom < 2 || high - low < kMinSillWidth - 1e-9) continue;
        const double from = opening->offset + low - kSillOverhang;
        const double to = opening->offset + high + kSillOverhang;
        const double z = build.elevation + opening->sill + bottom - style.height;
        for (double side : {1.0, -1.0}) {
            std::vector<Vec2> centre = curvePoints(curve, from, to);
            if (side < 0.0) std::reverse(centre.begin(), centre.end());
            std::vector<Vec3> path;
            for (std::size_t i = 0; i < centre.size(); ++i) {
                const double at = projectOntoCurve(centre[i], curve).offset;
                const Vec2 right = rightOf(tangentAlong(curve, at));
                const Vec2 world = planToWorld({centre[i].x + right.x * side * wall->thickness / 2.0,
                                                centre[i].y + right.y * side * wall->thickness / 2.0});
                path.push_back({world.x, world.y, z});
            }
            Sweep s = sweepPath(path, false, profile, {0.0, 0.0, 1.0});
            if (!s.sections.empty()) sweeps.push_back(std::move(s));
        }
    }
    return sweeps;
}

}

// Sweeps a profile along a path with one up direction.
Sweep sweepPath(const std::vector<Vec3>& path, bool closed, const std::vector<Vec2>& profile, Vec3 up)
{
    return sweepWithUps(path, std::vector<Vec3>(path.size(), up), closed, profile);
}

// Works out the trim of a floor.
std::vector<TrimBuild> trimBuild(const Floor& floor, const FloorBuild& build, const TrimSettings& settings,
                                 const std::vector<WorldPolygon>& skipAtFloor,
                                 const std::vector<WorldPolygon>& skipAtCeiling)
{
    std::vector<TrimBuild> out;
    for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill}) {
        const TrimStyle& style = trimStyle(settings, kind);
        if (!style.enabled) continue;
        TrimBuild made;
        made.kind = kind;
        const std::vector<Vec2> profile = styleOutline(style, made.profileMissing);
        if (profile.size() < 3) continue;
        switch (kind) {
        case TrimKind::Skirting: made.sweeps = faceTrim(floor, build, settings, kind, profile, skipAtFloor); break;
        case TrimKind::Cornice: made.sweeps = faceTrim(floor, build, settings, kind, profile, skipAtCeiling); break;
        case TrimKind::Casing: made.sweeps = casingTrim(floor, build, profile); break;
        case TrimKind::Sill: made.sweeps = sillTrim(floor, build, style, profile); break;
        case TrimKind::Railing: break;
        }
        if (!made.sweeps.empty()) out.push_back(std::move(made));
    }
    return out;
}

}
