#include "plynth/core/StairRailings.h"

#include "plynth/core/Stairs.h"
#include "plynth/core/TrimProfiles.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace plynth {

namespace {

// The middle of an outline.
Vec2 centroidOf(const std::vector<Vec2>& points)
{
    Vec2 c;
    for (const Vec2& p : points) {
        c.x += p.x;
        c.y += p.y;
    }
    if (!points.empty()) {
        c.x /= static_cast<double>(points.size());
        c.y /= static_cast<double>(points.size());
    }
    return c;
}

// The place on a polyline nearest a position, with the unit direction of the polyline there.
struct OnLine {
    Vec2 position;
    Vec2 direction{1.0, 0.0};
};

// Finds the place on a polyline nearest a position.
OnLine nearestOn(const std::vector<Vec2>& line, Vec2 p)
{
    OnLine best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const SegmentProjection s = projectOntoSegment(p, line[i], line[i + 1]);
        const double len = distance(line[i], line[i + 1]);
        if (s.distance < bestDistance && len > 1e-9) {
            bestDistance = s.distance;
            best.position = s.position;
            best.direction = {(line[i + 1].x - line[i].x) / len, (line[i + 1].y - line[i].y) / len};
        }
    }
    return best;
}

// Says whether a plan position is within the gap of a wall's face on a floor.
bool nearWall(const Floor& floor, Vec2 p)
{
    for (const Wall& wall : floor.walls) {
        const WallCurve curve = wallCurve(floor, wall);
        if (!curve.valid) continue;
        if (projectOntoCurve(p, curve).distance < wall.thickness / 2.0 + kRailingWallGap) return true;
    }
    return false;
}

// The railing profile centred on its path, and whether it was missing.
std::vector<Vec2> railProfile(const TrimStyle& style, bool& missing)
{
    const TrimProfile* profile = findTrimProfile(style.profile);
    missing = profile == nullptr;
    if (!profile) profile = findTrimProfile(kSquareProfileId);
    if (!profile) return {};
    std::vector<Vec2> out = profile->outline(style.height, style.depth);
    for (Vec2& p : out) {
        p.x -= style.depth / 2.0;
        p.y -= style.height / 2.0;
    }
    return out;
}

// Adds a rail along a world path, posts at most kMaxPostSpacing apart standing on the surface railHeight below
// it, and one collision piece per segment from that surface to the rail.
void addRail(StairRailing& out, const std::vector<Vec3>& path, const std::vector<Vec2>& profile, const TrimStyle& style,
             double railHeight)
{
    if (path.size() < 2) return;
    Sweep rail = sweepPath(path, false, profile, {0.0, 0.0, 1.0});
    if (rail.sections.empty()) return;
    out.sweeps.push_back(std::move(rail));

    std::vector<double> along = {0.0};
    for (std::size_t i = 1; i < path.size(); ++i) {
        along.push_back(along.back() + std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y));
    }
    const double total = along.back();
    const int posts = std::max(1, static_cast<int>(std::ceil(total / kMaxPostSpacing - 1e-9)));
    for (int k = 0; k <= posts; ++k) {
        const double at = total * k / posts;
        std::size_t i = 1;
        while (i + 1 < path.size() && along[i] < at) ++i;
        const double span = along[i] - along[i - 1];
        const double t = span > 1e-12 ? (at - along[i - 1]) / span : 0.0;
        const Vec3 top{path[i - 1].x + (path[i].x - path[i - 1].x) * t, path[i - 1].y + (path[i].y - path[i - 1].y) * t,
                       path[i - 1].z + (path[i].z - path[i - 1].z) * t};
        const Vec3 bottom{top.x, top.y, top.z - railHeight};
        const Vec3 below{top.x, top.y, top.z - style.height / 2.0};
        Sweep post = sweepPath({bottom, below}, false, profile, {1.0, 0.0, 0.0});
        if (!post.sections.empty()) out.sweeps.push_back(std::move(post));
    }

    const double half = std::max(style.depth, 0.02) / 2.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const Vec3 a = path[i];
        const Vec3 b = path[i + 1];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 1e-3) continue;
        const Vec2 n{-(b.y - a.y) / len * half, (b.x - a.x) / len * half};
        CollisionPiece piece;
        piece.outline = {{a.x - n.x, a.y - n.y}, {b.x - n.x, b.y - n.y}, {b.x + n.x, b.y + n.y}, {a.x + n.x, a.y + n.y}};
        if (signedArea(piece.outline) < 0.0) std::reverse(piece.outline.begin(), piece.outline.end());
        piece.bottom = std::min(a.z, b.z) - railHeight;
        piece.top = std::max(a.z, b.z);
        out.collision.push_back(std::move(piece));
    }
}

// The railings of one stair along its open sides.
StairRailing stairSides(const Floor& floor, const Stair& stair, const StairLayout& layout, double elevation,
                        const TrimSettings& settings, const std::vector<Vec2>& profile)
{
    StairRailing out;
    out.stair = stair.id;
    out.floor = floor.id;
    out.kind = "railing";
    if (layout.walkLine.size() < 2 || layout.steps.empty()) return out;

    std::vector<const StairStep*> steps;
    Vec2 column;
    bool hasColumn = false;
    for (const StairStep& s : layout.steps) {
        if (s.part == StairPart::Column) {
            column = centroidOf(s.outline);
            hasColumn = true;
        } else {
            steps.push_back(&s);
        }
    }
    const double reach = stair.width / 2.0 - kStairSideOffset;
    std::vector<std::vector<Vec3>> sides;
    std::vector<double> fromColumn;
    for (double side : {1.0, -1.0}) {
        std::vector<Vec3> path;
        std::vector<Vec2> planPoints;
        double far = 0.0;
        auto add = [&](Vec2 at, Vec2 direction, double top) {
            const Vec2 r = rightOf(direction);
            const Vec2 p{at.x + r.x * side * reach, at.y + r.y * side * reach};
            planPoints.push_back(p);
            const Vec2 w = planToWorld(p);
            path.push_back({w.x, w.y, elevation + top + settings.railingHeight});
            if (hasColumn) far += distance(p, column);
        };
        add(layout.walkLine.front(), nearestOn(layout.walkLine, layout.walkLine.front()).direction, steps.front()->top);
        for (const StairStep* s : steps) {
            const OnLine on = nearestOn(layout.walkLine, centroidOf(s->outline));
            add(on.position, on.direction, s->top);
        }
        add(layout.walkLine.back(), nearestOn(layout.walkLine, layout.walkLine.back()).direction, layout.rise);
        const auto nearCount = std::count_if(planPoints.begin(), planPoints.end(), [&](Vec2 p) { return nearWall(floor, p); });
        if (static_cast<std::size_t>(nearCount) * 2 > planPoints.size()) path.clear();
        sides.push_back(std::move(path));
        fromColumn.push_back(far);
    }
    for (std::size_t i = 0; i < sides.size(); ++i) {
        if (hasColumn && fromColumn[i] < fromColumn[1 - i]) continue;
        addRail(out, sides[i], profile, settings.railing, settings.railingHeight);
    }
    return out;
}

// The guard round a stair's hole on the floor above, open at the edge nearest the arrival point.
StairRailing stairGuard(const Floor& above, const Stair& stair, const StairLayout& layout, double elevation,
                        const TrimSettings& settings, const std::vector<Vec2>& profile)
{
    StairRailing out;
    out.stair = stair.id;
    out.floor = above.id;
    out.kind = "guard";
    const std::vector<Vec2>& hole = layout.footprint;
    const std::size_t n = hole.size();
    if (n < 3) return out;
    std::size_t open = 0;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < n; ++i) {
        const double d = projectOntoSegment(layout.arrival, hole[i], hole[(i + 1) % n]).distance;
        if (d < best) {
            best = d;
            open = i;
        }
    }
    std::vector<Vec3> path;
    auto flush = [&]() {
        addRail(out, path, profile, settings.railing, settings.railingHeight);
        path.clear();
    };
    for (std::size_t step = 1; step < n; ++step) {
        const std::size_t i = (open + step) % n;
        const Vec2 a = hole[i];
        const Vec2 b = hole[(i + 1) % n];
        const Vec2 middle{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
        if (nearWall(above, middle)) {
            flush();
            continue;
        }
        const double z = elevation + settings.railingHeight;
        const Vec2 wa = planToWorld(a);
        const Vec2 wb = planToWorld(b);
        if (path.empty()) path.push_back({wa.x, wa.y, z});
        path.push_back({wb.x, wb.y, z});
    }
    flush();
    return out;
}

}

// The object name of a railing.
std::string railingObjectName(const StairRailing& railing)
{
    return "SM_" + railing.floor + "_" + railing.stair + (railing.kind == "guard" ? "_Guard" : "_Railing");
}

// The railings and guards of every stair of a plan.
std::vector<StairRailing> stairRailings(const Plan& plan, const TrimSettings& settings, const std::vector<double>& elevations)
{
    std::vector<StairRailing> out;
    if (!settings.railing.enabled) return out;
    bool missing = false;
    const std::vector<Vec2> profile = railProfile(settings.railing, missing);
    if (profile.size() < 3) return out;
    for (std::size_t i = 0; i < plan.floors.size() && i < elevations.size(); ++i) {
        const Floor& floor = plan.floors[i];
        for (const Stair& stair : floor.stairs) {
            const StairLayout layout = stairLayout(plan, floor, stair);
            if (!layout.status.ok()) continue;
            StairRailing sides = stairSides(floor, stair, layout, elevations[i], settings, profile);
            sides.profileMissing = missing;
            if (!sides.sweeps.empty()) out.push_back(std::move(sides));
            const Floor* above = findFloor(plan, layout.toFloor);
            const auto upper = floorIndex(plan, layout.toFloor);
            if (!above || !upper || *upper >= elevations.size()) continue;
            StairRailing guard = stairGuard(*above, stair, layout, elevations[*upper], settings, profile);
            guard.profileMissing = missing;
            if (!guard.sweeps.empty()) out.push_back(std::move(guard));
        }
    }
    return out;
}

}
