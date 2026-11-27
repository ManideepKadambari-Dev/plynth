#include "RoomFixtures.h"

#include "../../core/src/PlanRingClean.h"
#include "../../core/src/roofs/Skeleton.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Storeys.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;
namespace sk = plynth::roofskeleton;

namespace {

const double kTan30 = std::tan(30.0 * std::numbers::pi / 180.0);
constexpr double kJoin = 3e-5; // metres: points of two faces this near are one point when checking a roof is closed
const Vec2 kFar{80.3, 60.1};   // a place far from the origin, where rounding shows

// The area of a ring, positive when it turns left.
double ringSize(const std::vector<Vec2>& ring)
{
    double twice = 0.0;
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        twice += (ring[i].x - ring[0].x) * (ring[i + 1].y - ring[0].y) -
                 (ring[i].y - ring[0].y) * (ring[i + 1].x - ring[0].x);
    }
    return 0.5 * twice;
}

// The area of a polygon, holes taken off.
double polygonSize(const WorldPolygon& poly)
{
    double size = ringSize(poly.outline);
    for (const std::vector<Vec2>& hole : poly.holes) size += ringSize(hole);
    return size;
}

// The points turned about the origin by degrees, then moved.
std::vector<Vec2> turned(const std::vector<Vec2>& ring, double degrees, Vec2 shift)
{
    const double r = degrees * std::numbers::pi / 180.0;
    std::vector<Vec2> out;
    for (const Vec2& p : ring) {
        out.push_back({std::cos(r) * p.x - std::sin(r) * p.y + shift.x, std::sin(r) * p.x + std::cos(r) * p.y + shift.y});
    }
    return out;
}

// An area turned and moved, holes too.
WorldPolygon turnedArea(const WorldPolygon& area, double degrees, Vec2 shift)
{
    WorldPolygon out;
    out.outline = turned(area.outline, degrees, shift);
    for (const std::vector<Vec2>& hole : area.holes) out.holes.push_back(turned(hole, degrees, shift));
    return out;
}

// The distance from a point to the segment from a to b.
double segmentDistance(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double size2 = dx * dx + dy * dy;
    const double t = size2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / size2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

// The distance from a point to the nearest edge of an area.
double boundaryDistance(Vec2 p, const WorldPolygon& area)
{
    double best = 1e18;
    const auto ring = [&](const std::vector<Vec2>& points) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            best = std::min(best, segmentDistance(p, points[i], points[(i + 1) % points.size()]));
        }
    };
    ring(area.outline);
    for (const std::vector<Vec2>& hole : area.holes) ring(hole);
    return best;
}

// Which side of the line from a to b a point lies on: positive on the left.
double sideOf(Vec2 a, Vec2 b, Vec2 p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

// Says whether the segments from a to b and from c to d cross at a point inside both.
bool properlyCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const double d1 = sideOf(a, b, c);
    const double d2 = sideOf(a, b, d);
    const double d3 = sideOf(c, d, a);
    const double d4 = sideOf(c, d, b);
    const double e = 1e-12;
    return ((d1 > e && d2 < -e) || (d1 < -e && d2 > e)) && ((d3 > e && d4 < -e) || (d3 < -e && d4 > e));
}

// Rings as Clipper paths.
Clipper2Lib::PathsD pathsFrom(const std::vector<std::vector<Vec2>>& rings)
{
    Clipper2Lib::PathsD paths;
    for (const std::vector<Vec2>& ring : rings) {
        Clipper2Lib::PathD path;
        for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
        paths.push_back(std::move(path));
    }
    return paths;
}

// The rings of an area: its outline, then its holes.
std::vector<std::vector<Vec2>> ringsOf(const WorldPolygon& area)
{
    std::vector<std::vector<Vec2>> rings{area.outline};
    for (const std::vector<Vec2>& hole : area.holes) rings.push_back(hole);
    return rings;
}

// The area paths cover, counted once where they overlap.
double unitedSize(const Clipper2Lib::PathsD& paths)
{
    if (paths.empty()) return 0.0;
    return std::fabs(Clipper2Lib::Area(Clipper2Lib::Union(paths, Clipper2Lib::FillRule::NonZero, kRingDigits)));
}

// The area two sets of paths do not share.
double differenceSize(const Clipper2Lib::PathsD& one, const Clipper2Lib::PathsD& other)
{
    return unitedSize(Clipper2Lib::Xor(one, other, Clipper2Lib::FillRule::NonZero, kRingDigits));
}

// Checks a skeleton by its rules and gives the height of its highest point: one face per edge that starts with
// the edge, faces that tile the area without overlaps or crossing outlines, the same height for a point in every
// face it belongs to, and no height below 0 or above the point's distance to the area's boundary.
double checkSkeleton(const WorldPolygon& area)
{
    const std::optional<sk::Skeleton> skeleton = sk::straightSkeleton(area);
    REQUIRE(skeleton.has_value());
    std::size_t edges = area.outline.size();
    for (const std::vector<Vec2>& hole : area.holes) edges += hole.size();
    REQUIRE(skeleton->faces.size() == edges);

    struct Mark {
        Vec2 p;
        double h = 0.0;
    };
    std::vector<Mark> marks;
    std::vector<std::vector<Vec2>> parts;
    double total = 0.0;
    int below = 0;
    int above = 0;
    int crossings = 0;
    for (const sk::SkeletonFace& face : skeleton->faces) {
        REQUIRE_FALSE(face.parts.empty());
        REQUIRE(face.parts[0].size() >= 3);
        CHECK(face.parts[0][0] == face.from);
        CHECK(face.parts[0][1] == face.to);
        const double len = std::hypot(face.to.x - face.from.x, face.to.y - face.from.y);
        const Vec2 n{-(face.to.y - face.from.y) / len, (face.to.x - face.from.x) / len};
        for (const std::vector<Vec2>& part : face.parts) {
            CHECK(ringSize(part) > 0.0);
            total += ringSize(part);
            for (const Vec2& p : part) {
                const double h = n.x * (p.x - face.from.x) + n.y * (p.y - face.from.y);
                if (h < -1e-9) ++below;
                if (h > boundaryDistance(p, area) + 1e-7) ++above;
                marks.push_back({p, h});
            }
            const std::size_t k = part.size();
            for (std::size_t i = 0; i < k; ++i) {
                for (std::size_t j = i + 2; j < k; ++j) {
                    if (i == 0 && j == k - 1) continue;
                    if (properlyCross(part[i], part[i + 1], part[j], part[(j + 1) % k])) ++crossings;
                }
            }
            parts.push_back(part);
        }
    }
    CHECK(below == 0);
    CHECK(above == 0);
    CHECK(crossings == 0);
    const double size = polygonSize(area);
    CHECK(total == Approx(size).margin(1e-7 * std::max(1.0, size)));
    CHECK(unitedSize(pathsFrom(parts)) == Approx(size).margin(1e-6 * std::max(1.0, size)));

    int differ = 0;
    double top = 0.0;
    for (std::size_t i = 0; i < marks.size(); ++i) {
        top = std::max(top, marks[i].h);
        for (std::size_t j = i + 1; j < marks.size(); ++j) {
            const bool same = std::hypot(marks[i].p.x - marks[j].p.x, marks[i].p.y - marks[j].p.y) < 1e-9;
            if (same && std::fabs(marks[i].h - marks[j].h) > 1e-7) ++differ;
        }
    }
    CHECK(differ == 0);
    return top;
}

// One floor whose walls, 0.2 m thick, run along the given centre lines, turned and moved, round one room.
Plan outlinePlan(const std::vector<Vec2>& centre, const std::string& type, double overhang, double degrees = 0.0,
                 Vec2 shift = {})
{
    Plan plan = makeNewProject().plan;
    Floor& f = plan.floors[0];
    const std::vector<Vec2> points = turned(centre, degrees, shift);
    Room room;
    room.id = "r1";
    for (std::size_t k = 0; k < points.size(); ++k) addPoint(f, "p" + std::to_string(k), points[k].x, points[k].y);
    for (std::size_t k = 0; k < points.size(); ++k) {
        const std::string id = "w" + std::to_string(k);
        addWall(f, id, "p" + std::to_string(k), "p" + std::to_string((k + 1) % points.size()), 0.2);
        room.walls.push_back(id);
    }
    f.rooms.push_back(room);
    plan.roof.type = type;
    plan.roof.pitch = 30.0;
    plan.roof.overhang = overhang;
    return plan;
}

// Adds a second floor over a rectangle of the first, given by its wall centre lines, turned and moved.
void addUpperFloor(Plan& plan, double x0, double y0, double x1, double y1, double degrees, Vec2 shift)
{
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    const std::vector<Vec2> points = turned({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, degrees, shift);
    Room room;
    room.id = "u1";
    for (std::size_t k = 0; k < 4; ++k) addPoint(upper, "q" + std::to_string(k), points[k].x, points[k].y);
    for (std::size_t k = 0; k < 4; ++k) {
        const std::string id = "v" + std::to_string(k);
        addWall(upper, id, "q" + std::to_string(k), "q" + std::to_string((k + 1) % 4), 0.2);
        room.walls.push_back(id);
    }
    upper.rooms.push_back(room);
    plan.floors.push_back(upper);
}

// A plan ring in world coordinates: y turned into -y and the points reversed, so the turning is kept.
std::vector<Vec2> worldRing(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    for (auto it = ring.rbegin(); it != ring.rend(); ++it) out.push_back({it->x, -it->y});
    return out;
}

// The world rings of plan areas.
std::vector<std::vector<Vec2>> worldRings(const std::vector<PlanArea>& areas)
{
    std::vector<std::vector<Vec2>> rings;
    for (const PlanArea& area : areas) {
        rings.push_back(worldRing(area.outline));
        for (const std::vector<Vec2>& hole : area.holes) rings.push_back(worldRing(hole));
    }
    return rings;
}

// The eaves of a world area as the roofs make them: every ring grown by the overhang on its own, mitre limit 5.
std::vector<WorldPolygon> eavesOf(const WorldPolygon& area, double overhang)
{
    if (overhang <= 0.0) return {area};
    std::vector<WorldPolygon> out;
    for (const RingPiece& piece : piecesByTurning(offsetByTurning(pathsFrom(ringsOf(area)), overhang, 5.0))) {
        WorldPolygon poly;
        poly.outline = piece.outline;
        poly.holes = piece.holes;
        out.push_back(std::move(poly));
    }
    return out;
}

// The faces of a part.
std::vector<RoofFace> partFaces(const std::vector<RoofFace>& faces, RoofPart part)
{
    std::vector<RoofFace> out;
    for (const RoofFace& f : faces) {
        if (f.part == part) out.push_back(f);
    }
    return out;
}

// A face seen from above as rings: its outline, then its holes, with the turning they have from above.
std::vector<std::vector<Vec2>> seenRings(const RoofFace& face)
{
    std::vector<std::vector<Vec2>> rings;
    std::vector<Vec2> outline;
    for (const Vec3& p : face.outline) outline.push_back({p.x, p.y});
    rings.push_back(std::move(outline));
    for (const std::vector<Vec3>& hole : face.holes) {
        std::vector<Vec2> ring;
        for (const Vec3& p : hole) ring.push_back({p.x, p.y});
        rings.push_back(std::move(ring));
    }
    return rings;
}

// The area of faces seen from above, each counted on its own.
double summedSize(const std::vector<RoofFace>& faces)
{
    double total = 0.0;
    for (const RoofFace& face : faces) {
        for (const std::vector<Vec2>& ring : seenRings(face)) total += ringSize(ring);
    }
    return std::fabs(total);
}

// All rings of faces seen from above as Clipper paths.
Clipper2Lib::PathsD seenPaths(const std::vector<RoofFace>& faces)
{
    Clipper2Lib::PathsD paths;
    for (const RoofFace& face : faces) {
        for (const Clipper2Lib::PathD& path : pathsFrom(seenRings(face))) paths.push_back(path);
    }
    return paths;
}

// The highest and lowest point of faces.
std::pair<double, double> heightRange(const std::vector<RoofFace>& faces)
{
    double low = 1e18;
    double high = -1e18;
    for (const RoofFace& f : faces) {
        for (const Vec3& p : f.outline) {
            low = std::min(low, p.z);
            high = std::max(high, p.z);
        }
    }
    return {low, high};
}

// Checks that every face is planar and faces the way its part says: coverings up, undersides down, edges and
// gables upright.
void checkPlanar(const std::vector<RoofFace>& faces)
{
    int bent = 0;
    for (const RoofFace& f : faces) {
        REQUIRE(f.outline.size() >= 3);
        Vec3 n;
        for (std::size_t i = 0; i < f.outline.size(); ++i) {
            const Vec3& a = f.outline[i];
            const Vec3& b = f.outline[(i + 1) % f.outline.size()];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        REQUIRE(len > 1e-12);
        std::vector<std::vector<Vec3>> rings{f.outline};
        for (const std::vector<Vec3>& h : f.holes) rings.push_back(h);
        for (const std::vector<Vec3>& ring : rings) {
            for (const Vec3& p : ring) {
                const Vec3& o = f.outline[0];
                const double d = ((p.x - o.x) * n.x + (p.y - o.y) * n.y + (p.z - o.z) * n.z) / len;
                if (std::fabs(d) > 1e-6) ++bent;
            }
        }
        if (f.part == RoofPart::Covering) CHECK(n.z / len > 0.2);
        if (f.part == RoofPart::Underside) CHECK(n.z / len < -0.2);
        if (f.part == RoofPart::Gable || f.part == RoofPart::Edge) CHECK(std::fabs(n.z / len) < 1e-6);
    }
    CHECK(bent == 0);
}

// Says whether two points are within kJoin of each other.
bool closeTo(const Vec3& a, const Vec3& b)
{
    return std::fabs(a.x - b.x) < kJoin && std::fabs(a.y - b.y) < kJoin && std::fabs(a.z - b.z) < kJoin;
}

// Counts the edges of the covering, underside and edge faces that no other edge runs back along, once every edge
// is cut at the points of other faces that lie on it. A closed roof has none.
int openEdges(const std::vector<RoofFace>& faces)
{
    std::vector<std::vector<Vec3>> rings;
    for (const RoofFace& f : faces) {
        if (f.part != RoofPart::Covering && f.part != RoofPart::Underside && f.part != RoofPart::Edge) continue;
        rings.push_back(f.outline);
        for (const std::vector<Vec3>& h : f.holes) rings.push_back(h);
    }
    std::vector<Vec3> points;
    for (const std::vector<Vec3>& ring : rings) points.insert(points.end(), ring.begin(), ring.end());

    struct Segment {
        Vec3 a;
        Vec3 b;
    };
    std::vector<Segment> segments;
    for (const std::vector<Vec3>& ring : rings) {
        for (std::size_t k = 0; k < ring.size(); ++k) {
            const Vec3 a = ring[k];
            const Vec3 b = ring[(k + 1) % ring.size()];
            const Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
            const double size2 = d.x * d.x + d.y * d.y + d.z * d.z;
            if (size2 < kJoin * kJoin) continue;
            std::vector<std::pair<double, Vec3>> cuts{{0.0, a}, {1.0, b}};
            for (const Vec3& p : points) {
                const double t = ((p.x - a.x) * d.x + (p.y - a.y) * d.y + (p.z - a.z) * d.z) / size2;
                if (t <= 0.0 || t >= 1.0) continue;
                const Vec3 c{a.x + t * d.x, a.y + t * d.y, a.z + t * d.z};
                if (std::sqrt((p.x - c.x) * (p.x - c.x) + (p.y - c.y) * (p.y - c.y) + (p.z - c.z) * (p.z - c.z)) < kJoin) {
                    cuts.push_back({t, p});
                }
            }
            std::sort(cuts.begin(), cuts.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
            Vec3 from = cuts.front().second;
            for (std::size_t m = 1; m < cuts.size(); ++m) {
                if (closeTo(from, cuts[m].second)) continue;
                segments.push_back({from, cuts[m].second});
                from = cuts[m].second;
            }
        }
    }

    std::vector<bool> used(segments.size(), false);
    int open = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        bool found = false;
        for (std::size_t j = 0; j < segments.size() && !found; ++j) {
            if (used[j] || !closeTo(segments[j].a, segments[i].b) || !closeTo(segments[j].b, segments[i].a)) continue;
            used[j] = true;
            found = true;
        }
        if (!found) ++open;
    }
    return open;
}

// Checks the collision of a roof: every hull is a convex outline on the covering's plane with the same points
// kRoofThickness lower, and the hulls cover what the covering covers.
void checkHulls(const Floor& floor, const RoofBuild& roof, double coveringSize)
{
    Floor bare = floor;
    bare.walls.clear();
    FloorBuild build;
    build.id = bare.id;
    build.roofs = {roof};
    double total = 0.0;
    int hulls = 0;
    int bad = 0;
    for (const CollisionShape& shape : floorCollision(bare, build)) {
        if (shape.kind != CollisionTargetKind::Roof) continue;
        for (const std::vector<Vec3>& hull : shape.hulls) {
            ++hulls;
            REQUIRE(hull.size() >= 6);
            REQUIRE(hull.size() % 2 == 0);
            const std::size_t half = hull.size() / 2;
            std::vector<Vec2> top;
            for (std::size_t k = 0; k < half; ++k) {
                top.push_back({hull[k].x, hull[k].y});
                const Vec3& low = hull[k + half];
                if (low.x != hull[k].x || low.y != hull[k].y) ++bad;
                if (std::fabs(hull[k].z - low.z - kRoofThickness) > 1e-9) ++bad;
            }
            if (!isConvexRing(top)) ++bad;
            total += ringSize(top);
        }
    }
    CHECK(bad == 0);
    CHECK(hulls > 0);
    CHECK(total == Approx(coveringSize).margin(1e-5));
}

// Builds the roof of floor 0 and checks it: the type asked for with no problem, planar faces, a covering with no
// overlaps that adds up to the eaves (less the floor above), an underside below all of it, and convex hulls that
// cover it. With closed it also checks that the covering, underside and edges close up. Gives the roof.
RoofBuild checkRoof(const Plan& plan, const std::string& type, bool closed)
{
    const FloorRoofs r = floorRoofs(plan, 0, floorElevations(plan));
    REQUIRE(r.roofs.size() == 1);
    const RoofBuild roof = r.roofs[0];
    CHECK(roof.type == type);
    CHECK(r.problems.empty());
    checkPlanar(roof.faces);

    const std::vector<RoofArea> areas = roofAreasOf(plan, plan.floors[0].id);
    REQUIRE(areas.size() == 1);
    const Clipper2Lib::PathsD section = pathsFrom(worldRings({areas[0].area}));
    Clipper2Lib::PathsD eaves = plan.roof.overhang > 0.0 ? offsetByTurning(section, plan.roof.overhang, 5.0) : section;
    if (plan.floors.size() > 1) {
        const Clipper2Lib::PathsD above = pathsFrom(worldRings(floorFootprint(plan.floors[1])));
        eaves = Clipper2Lib::Difference(eaves, above, Clipper2Lib::FillRule::NonZero, kRingDigits);
    }

    const std::vector<RoofFace> covering = partFaces(roof.faces, RoofPart::Covering);
    const std::vector<RoofFace> underside = partFaces(roof.faces, RoofPart::Underside);
    REQUIRE_FALSE(covering.empty());
    const double united = unitedSize(seenPaths(covering));
    CHECK(summedSize(covering) == Approx(united).margin(1e-6 * static_cast<double>(covering.size() + 1)));
    CHECK(united == Approx(unitedSize(eaves)).margin(1e-5));
    CHECK(differenceSize(seenPaths(covering), eaves) < 1e-5);
    CHECK(unitedSize(seenPaths(underside)) == Approx(united).margin(1e-5));
    if (closed) CHECK(openEdges(roof.faces) == 0);
    checkHulls(plan.floors[0], roof, summedSize(covering));
    return roof;
}

// The roof base of floor 0: its level plus its ceiling and slab.
double baseOf(const Plan& plan)
{
    return floorElevations(plan)[0] + floorRise(plan.floors[0]);
}

// Wall centre lines of the test outlines; the outer faces lie 0.1 m further out.
const std::vector<Vec2> kRectangle{{0.1, 0.1}, {9.9, 0.1}, {9.9, 5.9}, {0.1, 5.9}}; // 10 by 6
const std::vector<Vec2> kL{{0.1, 0.1}, {8.9, 0.1}, {8.9, 2.9}, {2.9, 2.9}, {2.9, 7.9}, {0.1, 7.9}}; // arms 3 wide
const std::vector<Vec2> kT{{3.6, 0.1}, {6.4, 0.1}, {6.4, 5.1}, {9.9, 5.1},
                           {9.9, 7.9}, {0.1, 7.9}, {0.1, 5.1}, {3.6, 5.1}}; // stem and bar 3 wide
const std::vector<Vec2> kU{{0.1, 0.1}, {9.9, 0.1}, {9.9, 6.9}, {7.1, 6.9},
                           {7.1, 2.9}, {2.9, 2.9}, {2.9, 6.9}, {0.1, 6.9}}; // arms and base 3 wide
const std::vector<Vec2> kThin{{0.1, 0.1}, {19.9, 0.1}, {19.9, 0.4}, {0.1, 0.4}}; // 20 by 0.5
// A 10 by 4 rectangle with a notch 0.4 wide from the top down to 0.6 above the bottom edge.
const std::vector<Vec2> kNotch{{0.1, 0.1}, {9.9, 0.1}, {9.9, 3.9}, {5.3, 3.9},
                               {5.3, 0.5}, {4.7, 0.5}, {4.7, 3.9}, {0.1, 3.9}};
// An L whose long arm is 0.5 wide, so its reflex corner lies 0.5 from the far edge.
const std::vector<Vec2> kNarrowArm{{0.1, 0.1}, {9.9, 0.1}, {9.9, 0.4}, {2.9, 0.4}, {2.9, 4.9}, {0.1, 4.9}};
// A U whose arms stand 1 m apart, so eaves of 0.6 close the gap between them.
const std::vector<Vec2> kNarrowU{{0.1, 0.1}, {6.9, 0.1}, {6.9, 6.9}, {4.1, 6.9},
                                 {4.1, 2.9}, {2.9, 2.9}, {2.9, 6.9}, {0.1, 6.9}};

// A world area from the outer faces of the outlines above.
WorldPolygon outerArea(std::vector<Vec2> outline)
{
    WorldPolygon area;
    area.outline = std::move(outline);
    return area;
}

}

TEST_CASE("a skeleton of a rectangle has two trapezoids and two triangles", "[roofs][skeleton]")
{
    const WorldPolygon area = outerArea({{0, 0}, {10, 0}, {10, 4}, {0, 4}});
    CHECK(checkSkeleton(area) == Approx(2.0).margin(1e-9));
    const std::optional<sk::Skeleton> skeleton = sk::straightSkeleton(area);
    REQUIRE(skeleton.has_value());
    int triangles = 0;
    int trapezoids = 0;
    for (const sk::SkeletonFace& face : skeleton->faces) {
        REQUIRE(face.parts.size() == 1);
        if (face.parts[0].size() == 3) ++triangles;
        if (face.parts[0].size() == 4) ++trapezoids;
    }
    CHECK(triangles == 2);
    CHECK(trapezoids == 2);
}

TEST_CASE("a skeleton of a convex outline rises with the nearest edge everywhere", "[roofs][skeleton]")
{
    const WorldPolygon area = outerArea({{0, 0}, {6, -1}, {9, 2}, {8, 6}, {3, 7}, {-1, 4}});
    checkSkeleton(area);
    const std::optional<sk::Skeleton> skeleton = sk::straightSkeleton(area);
    REQUIRE(skeleton.has_value());
    int wrong = 0;
    for (const sk::SkeletonFace& face : skeleton->faces) {
        const double len = std::hypot(face.to.x - face.from.x, face.to.y - face.from.y);
        const Vec2 n{-(face.to.y - face.from.y) / len, (face.to.x - face.from.x) / len};
        for (const Vec2& p : face.parts[0]) {
            const double own = n.x * (p.x - face.from.x) + n.y * (p.y - face.from.y);
            double lowest = 1e18;
            for (std::size_t i = 0; i < area.outline.size(); ++i) {
                const Vec2 a = area.outline[i];
                const Vec2 b = area.outline[(i + 1) % area.outline.size()];
                const double l = std::hypot(b.x - a.x, b.y - a.y);
                lowest = std::min(lowest, (-(b.y - a.y) * (p.x - a.x) + (b.x - a.x) * (p.y - a.y)) / l);
            }
            if (std::fabs(own - lowest) > 1e-9) ++wrong;
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("a skeleton of L, T and U outlines rises to half the arm width", "[roofs][skeleton]")
{
    const std::vector<std::vector<Vec2>> outlines{
        {{0, 0}, {9, 0}, {9, 3}, {3, 3}, {3, 8}, {0, 8}},
        {{3.5, 0}, {6.5, 0}, {6.5, 5}, {10, 5}, {10, 8}, {0, 8}, {0, 5}, {3.5, 5}},
        {{0, 0}, {10, 0}, {10, 7}, {7, 7}, {7, 3}, {3, 3}, {3, 7}, {0, 7}},
    };
    for (const std::vector<Vec2>& outline : outlines) {
        for (const double degrees : {0.0, 20.0, 45.0}) {
            INFO("outline of " << outline.size() << " points turned " << degrees);
            CHECK(checkSkeleton(turnedArea(outerArea(outline), degrees, kFar)) == Approx(1.5).margin(1e-9));
        }
    }
}

TEST_CASE("a skeleton of a long thin outline is one ridge", "[roofs][skeleton]")
{
    CHECK(checkSkeleton(outerArea({{0, 0}, {20, 0}, {20, 0.5}, {0, 0.5}})) == Approx(0.25).margin(1e-9));
    CHECK(checkSkeleton(turnedArea(outerArea({{0, 0}, {20, 0}, {20, 0.5}, {0, 0.5}}), 7.0, kFar)) ==
          Approx(0.25).margin(1e-9));
}

TEST_CASE("a skeleton with reflex corners near an edge splits there", "[roofs][skeleton]")
{
    // A notch from the top down to 0.3 above the bottom edge.
    const WorldPolygon notch = outerArea({{0, 0}, {10, 0}, {10, 4}, {5.2, 4}, {5.2, 0.3}, {4.8, 0.3}, {4.8, 4}, {0, 4}});
    CHECK(checkSkeleton(notch) == Approx(2.0).margin(1e-9));
    CHECK(checkSkeleton(turnedArea(notch, 30.0, kFar)) == Approx(2.0).margin(1e-9));
    // An L whose reflex corner is 0.1 from the far edge of its thin arm.
    const WorldPolygon arm = outerArea({{0, 0}, {10, 0}, {10, 0.1}, {3, 0.1}, {3, 5}, {0, 5}});
    CHECK(checkSkeleton(arm) == Approx(1.5).margin(1e-9));
    CHECK(checkSkeleton(turnedArea(arm, 80.0, kFar)) == Approx(1.5).margin(1e-9));
}

TEST_CASE("a skeleton keeps points in line and very short edges", "[roofs][skeleton]")
{
    const WorldPolygon inLine = outerArea({{0, 0}, {5, 0}, {10, 0}, {10, 2}, {10, 4}, {4, 4}, {0, 4}, {0, 1}});
    CHECK(checkSkeleton(inLine) == Approx(2.0).margin(1e-9));
    const WorldPolygon chamfer = outerArea({{0, 0}, {10, 0}, {10, 3.999}, {9.999, 4}, {0, 4}});
    CHECK(checkSkeleton(chamfer) == Approx(2.0).margin(1e-6));
    const WorldPolygon jog = outerArea({{0, 0}, {6, 0}, {6, 0.002}, {10, 0.002}, {10, 4}, {0, 4}});
    CHECK(checkSkeleton(jog) == Approx(2.0).margin(2e-3));
    CHECK(checkSkeleton(turnedArea(jog, 20.0, kFar)) == Approx(2.0).margin(2e-3));
}

TEST_CASE("a skeleton of a courtyard rises from both rings", "[roofs][skeleton]")
{
    WorldPolygon even = outerArea({{0, 0}, {8, 0}, {8, 8}, {0, 8}});
    even.holes.push_back({{2, 2}, {2, 6}, {6, 6}, {6, 2}});
    CHECK(checkSkeleton(even) == Approx(1.0).margin(1e-9));
    CHECK(checkSkeleton(turnedArea(even, 30.0, kFar)) == Approx(1.0).margin(1e-9));
    WorldPolygon uneven = outerArea({{0, 0}, {10, 0}, {10, 8}, {0, 8}});
    uneven.holes.push_back({{2, 2}, {2, 5}, {5, 5}, {5, 2}});
    CHECK(checkSkeleton(uneven) == Approx(2.5).margin(1e-9));
}

TEST_CASE("a skeleton of a round room of radius 10 meets in one apex", "[roofs][skeleton]")
{
    const int sides = 4 * arcSegmentCount(10.0, std::numbers::pi / 2.0);
    std::vector<Vec2> ring;
    for (int k = 0; k < sides; ++k) {
        const double a = 2.0 * std::numbers::pi * k / sides;
        ring.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
    }
    const double inradius = 10.0 * std::cos(std::numbers::pi / sides);
    for (const double degrees : {0.0, 30.0}) {
        const WorldPolygon area = turnedArea(outerArea(ring), degrees, kFar);
        CHECK(checkSkeleton(area) == Approx(inradius).margin(1e-7));
        const std::optional<sk::Skeleton> skeleton = sk::straightSkeleton(area);
        REQUIRE(skeleton.has_value());
        int short_ = 0;
        for (const sk::SkeletonFace& face : skeleton->faces) {
            double best = 1e18;
            for (const Vec2& p : face.parts[0]) best = std::min(best, std::hypot(p.x - kFar.x, p.y - kFar.y));
            if (best > 1e-6) ++short_;
        }
        CHECK(short_ == 0);
    }
    // One corner pulled in makes it concave, and the faces still meet with no gap near the middle.
    ring[3] = {ring[3].x * 0.95, ring[3].y * 0.95};
    checkSkeleton(outerArea(ring));
    checkSkeleton(turnedArea(outerArea(ring), 30.0, kFar));
}

TEST_CASE("a skeleton refuses a broken area", "[roofs][skeleton]")
{
    CHECK_FALSE(sk::straightSkeleton(outerArea({{0, 0}, {0, 4}, {10, 4}, {10, 0}})).has_value());
    CHECK_FALSE(sk::straightSkeleton(outerArea({{0, 0}, {10, 0}})).has_value());
    WorldPolygon wrongHole = outerArea({{0, 0}, {8, 0}, {8, 8}, {0, 8}});
    wrongHole.holes.push_back({{2, 2}, {6, 2}, {6, 6}, {2, 6}});
    CHECK_FALSE(sk::straightSkeleton(wrongHole).has_value());
}

TEST_CASE("a hip roof on a rectangle, L, T and U is closed and adds up to the eaves", "[roofs][skeleton]")
{
    const std::vector<std::pair<std::vector<Vec2>, double>> shapes{
        {kRectangle, 3.0}, {kL, 1.5}, {kT, 1.5}, {kU, 1.5}, {kThin, 0.25}, {kNarrowArm, 1.5}};
    for (const auto& [centre, top] : shapes) {
        for (const double overhang : {0.0, 0.3}) {
            INFO("outline of " << centre.size() << " points, overhang " << overhang);
            const Plan plan = outlinePlan(centre, "hip", overhang);
            const RoofBuild roof = checkRoof(plan, "hip", true);
            const auto [low, high] = heightRange(partFaces(roof.faces, RoofPart::Covering));
            CHECK(high == Approx(baseOf(plan) + top * kTan30).margin(1e-6));
            CHECK(low == Approx(baseOf(plan) - overhang * kTan30).margin(1e-6));
            CHECK(partFaces(roof.faces, RoofPart::Gable).empty());
        }
    }
}

TEST_CASE("a hip roof with a reflex corner near an edge is closed", "[roofs][skeleton]")
{
    const Plan plan = outlinePlan(kNotch, "hip", 0.0);
    const RoofBuild roof = checkRoof(plan, "hip", true);
    CHECK(heightRange(partFaces(roof.faces, RoofPart::Covering)).second ==
          Approx(baseOf(plan) + 2.0 * kTan30).margin(1e-6));
    // Eaves of 0.3 close the notch, so the two overhangs meet over it.
    checkRoof(outlinePlan(kNotch, "hip", 0.3), "hip", false);
}

TEST_CASE("a hip roof whose eaves close a narrow gap does not overlap itself", "[roofs][skeleton]")
{
    for (const double degrees : {0.0, 20.0}) {
        INFO("turned " << degrees);
        checkRoof(outlinePlan(kNarrowU, "hip", 0.6, degrees, kFar), "hip", false);
    }
}

TEST_CASE("a hip roof on turned and moved outlines never overlaps", "[roofs][skeleton]")
{
    for (const std::vector<Vec2>* centre : {&kL, &kT, &kU}) {
        for (const double degrees : {5.0, 20.0, 30.0, 45.0, 80.0}) {
            for (const Vec2 shift : {Vec2{0.0, 0.0}, kFar}) {
                for (const double overhang : {0.3, 0.6}) {
                    INFO("outline of " << centre->size() << " points turned " << degrees << " at " << shift.x << ","
                                       << shift.y << ", overhang " << overhang);
                    const Plan plan = outlinePlan(*centre, "hip", overhang, degrees, shift);
                    const RoofBuild roof = checkRoof(plan, "hip", false);
                    CHECK(heightRange(partFaces(roof.faces, RoofPart::Covering)).second ==
                          Approx(baseOf(plan) + 1.5 * kTan30).margin(1e-6));
                }
            }
        }
    }
    checkRoof(outlinePlan(kL, "hip", 0.3, 30.0, kFar), "hip", true);
}

TEST_CASE("a hip roof on a round room with one corner pulled in has no hole at its apex", "[roofs][skeleton]")
{
    const int sides = 4 * arcSegmentCount(10.0, std::numbers::pi / 2.0);
    std::vector<Vec2> centre;
    for (int k = 0; k < sides; ++k) {
        const double a = 2.0 * std::numbers::pi * k / sides;
        const double r = k == 3 ? 9.5 : 10.0;
        centre.push_back({r * std::cos(a), r * std::sin(a)});
    }
    for (const double degrees : {0.0, 30.0}) {
        INFO("turned " << degrees);
        checkRoof(outlinePlan(centre, "hip", 0.3, degrees, kFar), "hip", false);
    }
}

TEST_CASE("a hip roof beside a floor above keeps off it and has no slivers", "[roofs][skeleton]")
{
    for (const std::string type : {"hip", "pyramid"}) {
        for (const double degrees : {7.0, 20.0}) {
            INFO(type << " turned " << degrees);
            Plan plan = outlinePlan(kRectangle, type, 0.3, degrees, kFar);
            // The hip stands on an L beside the upper floor; the pyramid on the rectangle the upper floor leaves.
            if (type == "hip") addUpperFloor(plan, 0.1, 0.1, 4.9, 2.9, degrees, kFar);
            else addUpperFloor(plan, 0.1, 0.1, 4.9, 5.9, degrees, kFar);
            const RoofBuild roof = checkRoof(plan, type, false);

            const Clipper2Lib::PathsD above =
                offsetByTurning(pathsFrom(worldRings(floorFootprint(plan.floors[1]))), -1e-4, 5.0);
            const Clipper2Lib::PathsD covering = seenPaths(partFaces(roof.faces, RoofPart::Covering));
            CHECK(unitedSize(Clipper2Lib::Intersect(covering, above, Clipper2Lib::FillRule::NonZero, kRingDigits)) <
                  1e-7);
            int slivers = 0;
            for (const RoofFace& face : partFaces(roof.faces, RoofPart::Covering)) {
                const std::vector<Vec2> ring = seenRings(face)[0];
                double perimeter = 0.0;
                for (std::size_t k = 0; k < ring.size(); ++k) {
                    const Vec2 a = ring[k];
                    const Vec2 b = ring[(k + 1) % ring.size()];
                    perimeter += std::hypot(b.x - a.x, b.y - a.y);
                }
                if (perimeter > 0.05 && 2.0 * std::fabs(ringSize(ring)) / perimeter < 1e-4) ++slivers;
            }
            CHECK(slivers == 0);
        }
    }
}

TEST_CASE("a hip roof on a courtyard is built from its skeleton", "[roofs][skeleton]")
{
    WorldPolygon section = outerArea({{0, 0}, {8, 0}, {8, 8}, {0, 8}});
    section.holes.push_back({{2, 2}, {2, 6}, {6, 6}, {6, 2}});
    const RoofType* hip = findRoofType(kHipRoofId);
    REQUIRE(hip != nullptr);
    for (const double degrees : {0.0, 30.0}) {
        for (const double overhang : {0.0, 0.3}) {
            INFO("turned " << degrees << ", overhang " << overhang);
            RoofInput input;
            input.section = turnedArea(section, degrees, kFar);
            input.eaves = eavesOf(input.section, overhang);
            input.base = 3.0;
            input.settings.type = std::string(kHipRoofId);
            input.settings.pitch = 30.0;
            input.settings.overhang = overhang;
            input.direction = {1.0, 0.0};
            REQUIRE(hip->fits(input).ok());
            const std::vector<RoofFace> faces = hip->build(input);
            checkPlanar(faces);
            const std::vector<RoofFace> covering = partFaces(faces, RoofPart::Covering);
            double eaves = 0.0;
            for (const WorldPolygon& poly : input.eaves) eaves += polygonSize(poly);
            CHECK(summedSize(covering) == Approx(eaves).margin(1e-5));
            CHECK(unitedSize(seenPaths(covering)) == Approx(eaves).margin(1e-5));
            const auto [low, high] = heightRange(covering);
            CHECK(high == Approx(3.0 + kTan30).margin(1e-6));
            CHECK(low == Approx(3.0 - overhang * kTan30).margin(1e-6));
            CHECK(openEdges(faces) == 0);
        }
    }
}

TEST_CASE("a pyramid roof rises from every edge to one apex", "[roofs][skeleton]")
{
    const RoofType* pyramid = findRoofType("pyramid");
    REQUIRE(pyramid != nullptr);
    CHECK(pyramid->name() == "Pyramid");
    CHECK(pyramid->usesPitch());
    CHECK_FALSE(pyramid->usesDirection());
    CHECK(pyramid->usesOverhang());
    CHECK_FALSE(pyramid->usesParapet());
    CHECK(pyramid->fallback() == kHipRoofId);
    CHECK(findRoofType(kHipRoofId)->sortKey() < pyramid->sortKey());
    CHECK(pyramid->sortKey() < findRoofType(kShedRoofId)->sortKey());

    for (const double degrees : {0.0, 20.0}) {
        for (const double overhang : {0.0, 0.3}) {
            INFO("turned " << degrees << ", overhang " << overhang);
            const Plan plan = outlinePlan(kRectangle, "pyramid", overhang, degrees, kFar);
            const RoofBuild roof = checkRoof(plan, "pyramid", true);
            const std::vector<RoofFace> covering = partFaces(roof.faces, RoofPart::Covering);
            CHECK(covering.size() == 4);
            // The apex stands 3 m in from the long sides, over the middle.
            const double apex = baseOf(plan) + 3.0 * kTan30;
            CHECK(heightRange(covering).second == Approx(apex).margin(1e-6));
            const Vec2 middle = turned({{5.0, 3.0}}, degrees, kFar)[0];
            int reach = 0;
            for (const RoofFace& face : covering) {
                for (const Vec3& p : face.outline) {
                    if (std::hypot(p.x - middle.x, p.y + middle.y) < 1e-6 && std::fabs(p.z - apex) < 1e-6) ++reach;
                }
            }
            CHECK(reach == 4);
        }
    }
}

TEST_CASE("a pyramid roof on an L shaped section is built as a hip", "[roofs][skeleton]")
{
    const Plan plan = outlinePlan(kL, "pyramid", 0.3);
    const FloorRoofs r = floorRoofs(plan, 0, floorElevations(plan));
    REQUIRE(r.roofs.size() == 1);
    CHECK(r.roofs[0].type == "hip");
    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].code == "roof_type_fallback");
    CHECK_FALSE(r.problems[0].message.empty());
    checkPlanar(r.roofs[0].faces);
}
