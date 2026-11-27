#pragma once

#include "RoomFixtures.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Storeys.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

// Checks shared by the tests of roof shapes: the faces, the covering against the eaves, and the collision hulls.
namespace plynth::test::roofshape {

namespace cl = Clipper2Lib;

inline constexpr int kDigits = 8;
inline const double kTan30 = std::tan(30.0 * std::numbers::pi / 180.0);

// Adds a room in a box of 0.2 m walls whose outer faces run from x0, y0 to x1, y1 on the plan.
inline void addBoxRoom(Floor& floor, int n, double x0, double y0, double x1, double y1)
{
    addRectangle(floor, n, x0 + 0.1, y0 + 0.1, x1 - 0.1, y1 - 0.1, 0.2);
    const std::string s = std::to_string(n);
    Room room;
    room.id = "r" + s;
    room.walls = {"w" + s + "1", "w" + s + "2", "w" + s + "3", "w" + s + "4"};
    floor.rooms.push_back(room);
}

// Adds a room of 0.2 m walls along the centre line points, closed back to the first.
inline void addOutlineRoom(Floor& floor, const std::vector<Vec2>& points)
{
    Room room;
    room.id = "r1";
    for (std::size_t i = 0; i < points.size(); ++i) {
        addPoint(floor, "q" + std::to_string(i), points[i].x, points[i].y);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const std::string id = "v" + std::to_string(i);
        addWall(floor, id, "q" + std::to_string(i), "q" + std::to_string((i + 1) % points.size()), 0.2);
        room.walls.push_back(id);
    }
    floor.rooms.push_back(room);
}

// One floor whose outer wall faces make an L of arms 4 m wide and 8 m long.
inline Plan lPlan()
{
    Plan plan = makeNewProject().plan;
    addOutlineRoom(plan.floors[0], {{0.1, 0.1}, {7.9, 0.1}, {7.9, 3.9}, {3.9, 3.9}, {3.9, 7.9}, {0.1, 7.9}});
    return plan;
}

// One floor whose outer wall faces make a T: a bar 12 by 4 m and a stem 4 m wide reaching 5 m from it.
inline Plan tPlan()
{
    Plan plan = makeNewProject().plan;
    addOutlineRoom(plan.floors[0], {{0.1, 0.1}, {11.9, 0.1}, {11.9, 3.9}, {7.9, 3.9}, {7.9, 8.9}, {4.1, 8.9},
                                    {4.1, 3.9}, {0.1, 3.9}});
    return plan;
}

// One floor whose outer wall faces make a U: a base 12 by 4 m with an arm 4 m wide at each end reaching 4 m on.
inline Plan uPlan()
{
    Plan plan = makeNewProject().plan;
    addOutlineRoom(plan.floors[0], {{0.1, 0.1}, {11.9, 0.1}, {11.9, 7.9}, {8.1, 7.9}, {8.1, 3.9}, {3.9, 3.9},
                                    {3.9, 7.9}, {0.1, 7.9}});
    return plan;
}

// One floor 6 by 4 m outside, so the roof base is 3.2.
inline Plan rectanglePlan()
{
    Plan plan = makeNewProject().plan;
    addBoxRoom(plan.floors[0], 1, 0.0, 0.0, 6.0, 4.0);
    return plan;
}

// The faces of the builds that are of one part.
inline std::vector<RoofFace> facesOf(const std::vector<RoofBuild>& builds, RoofPart part)
{
    std::vector<RoofFace> out;
    for (const RoofBuild& build : builds) {
        for (const RoofFace& face : build.faces) {
            if (face.part == part) out.push_back(face);
        }
    }
    return out;
}

// The normal of a ring by Newell's method, not scaled.
inline Vec3 newellOf(const std::vector<Vec3>& ring)
{
    Vec3 n;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

// Checks every face is closed (at least three points, none repeating the one before) and planar, and looks the
// way its part says: coverings up, undersides down, edges and gables sideways.
inline void checkFaces(const std::vector<RoofBuild>& builds)
{
    for (const RoofBuild& build : builds) {
        for (const RoofFace& f : build.faces) {
            REQUIRE(f.outline.size() >= 3);
            const Vec3 n = newellOf(f.outline);
            const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            REQUIRE(len > 1e-9);
            std::vector<std::vector<Vec3>> rings{f.outline};
            for (const auto& hole : f.holes) rings.push_back(hole);
            for (const auto& ring : rings) {
                REQUIRE(ring.size() >= 3);
                for (std::size_t i = 0; i < ring.size(); ++i) {
                    const Vec3& p = ring[i];
                    const Vec3& q = ring[(i + 1) % ring.size()];
                    CHECK(std::sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) + (q.z - p.z) * (q.z - p.z)) >= 1e-6);
                    const Vec3& o = f.outline[0];
                    CHECK(std::fabs(((p.x - o.x) * n.x + (p.y - o.y) * n.y + (p.z - o.z) * n.z) / len) < 1e-6);
                }
            }
            if (f.part == RoofPart::Covering) CHECK(n.z / len > 0.2);
            if (f.part == RoofPart::Underside) CHECK(n.z / len < -0.2);
            if (f.part == RoofPart::Gable || f.part == RoofPart::Edge) CHECK(std::fabs(n.z / len) < 1e-6);
        }
    }
}

// A ring seen from above as a path turning anticlockwise for an outline and clockwise for a hole.
inline cl::PathD pathOf(std::vector<Vec2> ring, bool outline)
{
    if ((signedArea(ring) > 0.0) != outline) std::reverse(ring.begin(), ring.end());
    cl::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// A face seen from above.
inline cl::PathsD seenFromAbove(const RoofFace& face)
{
    const auto flat = [](const std::vector<Vec3>& ring) {
        std::vector<Vec2> out;
        for (const Vec3& p : ring) out.push_back({p.x, p.y});
        return out;
    };
    cl::PathsD out{pathOf(flat(face.outline), true)};
    for (const auto& hole : face.holes) out.push_back(pathOf(flat(hole), false));
    return out;
}

// The area of paths that do not overlap.
inline double areaOf(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathD& path : paths) {
        std::vector<Vec2> ring;
        for (const cl::PointD& p : path) ring.push_back({p.x, p.y});
        sum += signedArea(ring);
    }
    return sum;
}

// The covering of the builds seen from above: the sum of the faces' areas, and the area they cover together.
struct Covering {
    double sum = 0.0;
    double united = 0.0;
};

// Measures the covering of the builds.
inline Covering coveringOf(const std::vector<RoofBuild>& builds)
{
    Covering out;
    for (const RoofFace& face : facesOf(builds, RoofPart::Covering)) out.sum += areaOf(seenFromAbove(face));
    // Each face is united on its own first, so a face's hole is not filled by the face's own outline.
    cl::PathsD merged;
    for (const RoofFace& face : facesOf(builds, RoofPart::Covering)) {
        const cl::PathsD one = cl::Union(seenFromAbove(face), cl::FillRule::NonZero, kDigits);
        merged = merged.empty() ? one : cl::Union(merged, one, cl::FillRule::NonZero, kDigits);
    }
    out.united = areaOf(merged);
    return out;
}

// The eaves of every roof section of a floor seen from above: each section grown by the overhang with mitred
// corners (limit 5), all united, in world coordinates.
inline cl::PathsD eavesOf(const Plan& plan, const std::string& floorId, double overhang)
{
    cl::PathsD all;
    for (const RoofArea& area : roofAreasOf(plan, floorId)) {
        cl::PathsD section;
        std::vector<Vec2> outline;
        for (const Vec2& p : area.area.outline) outline.push_back({p.x, -p.y});
        section.push_back(pathOf(outline, true));
        for (const auto& hole : area.area.holes) {
            std::vector<Vec2> ring;
            for (const Vec2& p : hole) ring.push_back({p.x, -p.y});
            section.push_back(pathOf(ring, false));
        }
        const cl::PathsD grown =
            overhang > 0.0 ? cl::InflatePaths(section, overhang, cl::JoinType::Miter, cl::EndType::Polygon, 5.0, kDigits)
                           : section;
        all = all.empty() ? cl::Union(grown, cl::FillRule::NonZero, kDigits)
                          : cl::Union(all, grown, cl::FillRule::NonZero, kDigits);
    }
    return all;
}

// The distance from a point to the nearest edge of the paths.
inline double distanceToOutline(const cl::PathsD& paths, Vec2 p)
{
    double best = 1e18;
    for (const cl::PathD& path : paths) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            const Vec2 a{path[i].x, path[i].y};
            const Vec2 b{path[(i + 1) % path.size()].x, path[(i + 1) % path.size()].y};
            best = std::min(best, projectOntoSegment(p, a, b).distance);
        }
    }
    return best;
}

// Checks the covering adds up to the eaves: the faces seen from above cover the eaves once, with no gap and no
// overlap.
inline void checkCoversEaves(const Plan& plan, std::size_t floorIndex, const std::vector<RoofBuild>& builds,
                             double overhang)
{
    const double eaves = areaOf(eavesOf(plan, plan.floors[floorIndex].id, overhang));
    const Covering covering = coveringOf(builds);
    CHECK(covering.sum == Catch::Approx(eaves).margin(1e-4));
    CHECK(covering.united == Catch::Approx(eaves).margin(1e-4));
}

// Checks every edge face stands on the outline of the eaves, so none is left inside a joined roof.
inline void checkEdgesOnEaves(const Plan& plan, std::size_t floorIndex, const std::vector<RoofBuild>& builds,
                              double overhang)
{
    const cl::PathsD eaves = eavesOf(plan, plan.floors[floorIndex].id, overhang);
    for (const RoofFace& face : facesOf(builds, RoofPart::Edge)) {
        Vec2 mid{0.0, 0.0};
        for (const Vec3& p : face.outline) mid = {mid.x + p.x, mid.y + p.y};
        mid = {mid.x / static_cast<double>(face.outline.size()), mid.y / static_cast<double>(face.outline.size())};
        INFO("edge face at " << mid.x << ", " << mid.y);
        CHECK(distanceToOutline(eaves, mid) < 1e-4);
    }
}

// Checks the collision of the roofs of a floor: every hull is the points of a convex piece followed by the same
// points kRoofThickness lower, so it is closed and convex, and every roof has at least one.
inline void checkHulls(const Plan& plan, std::size_t floorIndex, const std::vector<RoofBuild>& builds)
{
    FloorBuild build;
    build.id = plan.floors[floorIndex].id;
    build.elevation = floorElevations(plan)[floorIndex];
    build.ceilingHeight = plan.floors[floorIndex].ceilingHeight;
    build.roofs = builds;
    int roofs = 0;
    for (const CollisionShape& shape : floorCollision(plan.floors[floorIndex], build)) {
        if (shape.kind != CollisionTargetKind::Roof) continue;
        ++roofs;
        CHECK_FALSE(shape.hulls.empty());
        for (const std::vector<Vec3>& hull : shape.hulls) {
            REQUIRE(hull.size() >= 6);
            REQUIRE(hull.size() % 2 == 0);
            const std::size_t half = hull.size() / 2;
            std::vector<Vec2> ring;
            for (std::size_t i = 0; i < half; ++i) {
                ring.push_back({hull[i].x, hull[i].y});
                CHECK(hull[i + half].x == hull[i].x);
                CHECK(hull[i + half].y == hull[i].y);
                CHECK(hull[i].z - hull[i + half].z == Catch::Approx(kRoofThickness).margin(1e-9));
            }
            CHECK(isConvexRing(ring));
            CHECK(std::fabs(signedArea(ring)) > 1e-9);
        }
    }
    CHECK(roofs == static_cast<int>(builds.size()));
}

// The highest point of the faces.
inline double topOf(const std::vector<RoofFace>& faces)
{
    double top = -1e18;
    for (const RoofFace& face : faces) {
        for (const Vec3& p : face.outline) top = std::max(top, p.z);
    }
    return top;
}

// The lowest point of the faces.
inline double bottomOf(const std::vector<RoofFace>& faces)
{
    double bottom = 1e18;
    for (const RoofFace& face : faces) {
        for (const Vec3& p : face.outline) bottom = std::min(bottom, p.z);
    }
    return bottom;
}

// Says whether one of the faces has a corner at x, y and height z.
inline bool hasCorner(const std::vector<RoofFace>& faces, double x, double y, double z)
{
    for (const RoofFace& face : faces) {
        for (const Vec3& p : face.outline) {
            if (std::fabs(p.x - x) < 1e-6 && std::fabs(p.y - y) < 1e-6 && std::fabs(p.z - z) < 1e-6) return true;
        }
    }
    return false;
}

// The length along x of the corners of the faces at height z.
inline double lengthAt(const std::vector<RoofFace>& faces, double z)
{
    double lo = 1e18;
    double hi = -1e18;
    for (const RoofFace& face : faces) {
        for (const Vec3& p : face.outline) {
            if (std::fabs(p.z - z) > 1e-6) continue;
            lo = std::min(lo, p.x);
            hi = std::max(hi, p.x);
        }
    }
    return hi - lo;
}

}
