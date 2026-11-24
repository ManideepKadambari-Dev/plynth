#include "RoomFixtures.h"

#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/Storeys.h>
#include <plynth/core/WallGeometry.h>

#include <catch2/catch_test_macros.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "../../core/src/PlanRingClean.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

namespace cl = Clipper2Lib;

// The numbers of Storeys.h, written out so a change of a constant cannot loosen a test.
constexpr double kMerge = 1.2e-5;
constexpr double kSideTolerance = 2e-5;
constexpr double kLeastArea = 1e-6;
// Two rounds of cleaning move an edge by at most two merge distances, so a wider mismatch is a fault.
constexpr double kDeep = 1.25e-5;
// A part this narrow for a millimetre or more is a needle or a slit, not the short stub a cut may leave.
constexpr double kNarrow = 0.75 * kMerge;
constexpr double kLongRun = 1e-3;
// Where corners touch at a point, rounding may put one ring this far across another ring.
constexpr double kTouchDepth = 1e-7;
constexpr int kDigits = 8;
constexpr cl::FillRule kFill = cl::FillRule::NonZero;

// Collects what went wrong, so a sweep names its first failing buildings and does not stop at one.
struct Report {
    int builds = 0;
    int failed = 0;
    std::string label;
    std::set<std::string> found;
    std::ostringstream text;

    void begin(const std::string& name)
    {
        ++builds;
        label = name;
        found.clear();
    }
    void fail(const std::string& what)
    {
        if (found.empty()) {
            ++failed;
            if (failed <= 15) text << "\n" << label << ":";
        }
        if (found.insert(what).second && failed <= 15) text << "\n    " << what;
    }
    void expect(bool ok, const std::string& what)
    {
        if (!ok) fail(what);
    }
};

// Fails the test with the buildings the report holds.
void requireNone(const Report& report)
{
    INFO(report.failed << " of " << report.builds << " buildings failed" << report.text.str());
    REQUIRE(report.builds > 0);
    REQUIRE(report.failed == 0);
}

// The distance from a point to a segment.
double toSegment(const Vec2& p, const Vec2& a, const Vec2& b)
{
    return projectOntoSegment(p, a, b).distance;
}

// The distance from a point to the nearest edge of some paths.
double toPaths(const Vec2& p, const cl::PathsD& paths)
{
    double best = 1e18;
    for (const cl::PathD& path : paths) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            const cl::PointD& a = path[i];
            const cl::PointD& b = path[(i + 1) % path.size()];
            best = std::min(best, toSegment(p, {a.x, a.y}, {b.x, b.y}));
        }
    }
    return best;
}

// A plan position in world X and Y.
Vec2 flipped(const Vec2& p)
{
    return {p.x, -p.y};
}

// The area of a ring measured from its own first point, so a place far from the origin loses no digits.
double ringArea(const std::vector<Vec2>& ring)
{
    if (ring.size() < 3) return 0.0;
    std::vector<Vec2> local;
    for (const Vec2& p : ring) local.push_back({p.x - ring[0].x, p.y - ring[0].y});
    return signedArea(local);
}

// A ring as a path that turns the way asked.
cl::PathD pathOf(std::vector<Vec2> ring, bool outline)
{
    if ((ringArea(ring) > 0.0) != outline) std::reverse(ring.begin(), ring.end());
    cl::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// Plan areas as world polygons.
std::vector<WorldPolygon> inWorld(const std::vector<PlanArea>& areas)
{
    const auto turn = [](const std::vector<Vec2>& ring) {
        std::vector<Vec2> out;
        for (auto it = ring.rbegin(); it != ring.rend(); ++it) out.push_back(flipped(*it));
        return out;
    };
    std::vector<WorldPolygon> out;
    for (const PlanArea& area : areas) {
        WorldPolygon polygon;
        polygon.outline = turn(area.outline);
        for (const auto& hole : area.holes) polygon.holes.push_back(turn(hole));
        out.push_back(std::move(polygon));
    }
    return out;
}

// World polygons as paths.
cl::PathsD pathsOf(const std::vector<WorldPolygon>& polygons)
{
    cl::PathsD out;
    for (const WorldPolygon& polygon : polygons) {
        out.push_back(pathOf(polygon.outline, true));
        for (const auto& hole : polygon.holes) out.push_back(pathOf(hole, false));
    }
    return out;
}

// The area two sets of paths cover together, with no cleaning at all.
cl::PathsD united(const cl::PathsD& a, const cl::PathsD& b = {})
{
    cl::PathsD all = a;
    all.insert(all.end(), b.begin(), b.end());
    return all.empty() ? all : cl::Union(all, kFill, kDigits);
}

// What is in the first set and not in the second.
cl::PathsD minus(const cl::PathsD& a, const cl::PathsD& b)
{
    return a.empty() || b.empty() ? united(a) : cl::Difference(a, b, kFill, kDigits);
}

// What is in both sets.
cl::PathsD common(const cl::PathsD& a, const cl::PathsD& b)
{
    return a.empty() || b.empty() ? cl::PathsD{} : cl::Intersect(a, b, kFill, kDigits);
}

// The paths moved outward by delta, or inward when it is negative, with mitred corners.
cl::PathsD offset(const cl::PathsD& paths, double delta, double limit = 10.0)
{
    if (paths.empty()) return {};
    return cl::InflatePaths(united(paths), delta, cl::JoinType::Miter, cl::EndType::Polygon, limit, kDigits);
}

// The area of paths that do not overlap.
double areaOf(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathD& path : paths) {
        std::vector<Vec2> ring;
        for (const cl::PointD& p : path) ring.push_back({p.x, p.y});
        sum += ringArea(ring);
    }
    return sum;
}

// The length of every edge of the paths.
double perimeterOf(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathD& path : paths) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            const cl::PointD& a = path[i];
            const cl::PointD& b = path[(i + 1) % path.size()];
            sum += std::hypot(b.x - a.x, b.y - a.y);
        }
    }
    return sum;
}

// The part of the paths further than depth from all of their edges.
cl::PathsD deepPart(const cl::PathsD& paths, double depth)
{
    if (paths.empty()) return {};
    const cl::PathsD all = united(paths);
    if (all.empty()) return {};
    return minus(all, cl::InflatePaths(all, depth, cl::JoinType::Round, cl::EndType::Joined, 2.0, kDigits));
}

// How much lies deeper than depth.
double deepArea(const cl::PathsD& paths, double depth)
{
    return areaOf(deepPart(paths, depth));
}

// The smallest outline round a point, or none.
cl::PathD outlineRound(const cl::PathsD& paths, const cl::PointD& at)
{
    cl::PathD found;
    double least = 1e18;
    for (const cl::PathD& path : paths) {
        const double size = areaOf({path});
        if (size <= 0.0 || size >= least) continue;
        std::vector<Vec2> ring;
        for (const cl::PointD& p : path) ring.push_back({p.x, p.y});
        if (!insidePolygon({at.x, at.y}, ring)) continue;
        found = path;
        least = size;
    }
    return found;
}

// Says whether a spot is part of a piece or hole that cleaning may drop and that is gone entirely.
bool droppedWhole(const cl::PathD& spot, const cl::PathsD& got, const cl::PathsD& wanted)
{
    const double size = areaOf({spot});
    const bool lost = areaOf(common({spot}, minus(wanted, got))) >= size - 1e-10;
    if (!lost && areaOf(common({spot}, minus(got, wanted))) < size - 1e-10) return false;

    cl::PathsD from = wanted;
    if (!lost) {
        cl::PathsD all = got;
        all.insert(all.end(), wanted.begin(), wanted.end());
        cl::RectD box = cl::GetBounds(all);
        box.left -= 1.0;
        box.top -= 1.0;
        box.right += 1.0;
        box.bottom += 1.0;
        from = minus({box.AsPath()}, wanted);
    }
    const cl::PathD piece = outlineRound(deepPart(from, kMerge / 2.0), spot.front());
    if (piece.empty() || areaOf({piece}) > 1.01 * kLeastArea) return false;
    const cl::PathsD left = lost ? common({piece}, got) : minus({piece}, got);
    return deepArea(left, kDeep) <= 1e-10;
}

// Says whether two areas are the same but for hairlines, an area within the bound and, where they are cleaned,
// whole pieces too small to keep.
void expectSame(Report& r, const cl::PathsD& got, const cl::PathsD& wanted, double bound, const std::string& what,
                bool cleaned = true)
{
    const cl::PathsD off = united(minus(got, wanted), minus(wanted, got));
    if (off.empty()) return;
    r.expect(areaOf(off) <= bound, what + " differs by more than the area bound");
    double deep = 0.0;
    for (const cl::PathD& spot : deepPart(off, kDeep)) {
        const double size = areaOf({spot});
        if (size <= 0.0) continue;
        if (cleaned && droppedWhole(spot, got, wanted)) continue;
        deep += size;
    }
    r.expect(deep <= 1e-10, what + " is moved by more than the side tolerance");
}

// A footprint as the header defines it, with no cleaning, in world coordinates.
cl::PathsD plainFootprint(const Floor& floor)
{
    cl::PathsD all;
    const auto add = [&all](const std::vector<Vec2>& ring) {
        std::vector<Vec2> world;
        for (const Vec2& p : ring) world.push_back(flipped(p));
        all.push_back(pathOf(world, true));
    };
    for (const Wall& wall : floor.walls) {
        for (const auto& ring : wallOutline(floor, wall)) add(ring);
    }
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (!shape.closed || shape.rings.empty()) continue;
        cl::PathsD outer;
        cl::PathsD islands;
        for (std::size_t i = 0; i < shape.rings.size(); ++i) {
            std::vector<Vec2> world;
            for (const Vec2& p : shape.rings[i]) world.push_back(flipped(p));
            (i == 0 ? outer : islands).push_back(pathOf(world, true));
        }
        const cl::PathsD inside = minus(outer, islands);
        all.insert(all.end(), inside.begin(), inside.end());
    }
    return united(all);
}

// A number as float32 holds it.
double as32(double v)
{
    return static_cast<double>(static_cast<float>(v));
}

// Which side of the line from a to b the point is on.
int sideOf(const Vec2& a, const Vec2& b, const Vec2& p)
{
    const double v = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
}

// Says whether two segments cross, or also whether they touch.
bool segmentsMeet(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, bool touching)
{
    if (std::max(a.x, b.x) < std::min(c.x, d.x) || std::max(c.x, d.x) < std::min(a.x, b.x) ||
        std::max(a.y, b.y) < std::min(c.y, d.y) || std::max(c.y, d.y) < std::min(a.y, b.y)) {
        return false;
    }
    const int c1 = sideOf(a, b, c);
    const int d1 = sideOf(a, b, d);
    const int a1 = sideOf(c, d, a);
    const int b1 = sideOf(c, d, b);
    if (c1 * d1 < 0 && a1 * b1 < 0) return true;
    if (!touching) return false;
    const auto within = [](const Vec2& p, const Vec2& q, const Vec2& v) {
        return std::min(p.x, q.x) <= v.x && v.x <= std::max(p.x, q.x) && std::min(p.y, q.y) <= v.y &&
               v.y <= std::max(p.y, q.y);
    };
    return (c1 == 0 && within(a, b, c)) || (d1 == 0 && within(a, b, d)) || (a1 == 0 && within(c, d, a)) ||
           (b1 == 0 && within(c, d, b));
}

// Says whether the rings of one polygon cross themselves or each other. A crossing inside one ring always counts.
// One between two rings counts only if all four end points are further than depth from the other segment.
bool ringsMeet(const std::vector<std::vector<Vec2>>& rings, bool touching, double depth = 0.0)
{
    for (std::size_t r = 0; r < rings.size(); ++r) {
        const std::size_t n = rings[r].size();
        for (std::size_t s = r; s < rings.size(); ++s) {
            const std::size_t m = rings[s].size();
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = (r == s ? i + 1 : 0); j < m; ++j) {
                    if (r == s && (j == i + 1 || (i == 0 && j == n - 1))) continue;
                    const Vec2& a = rings[r][i];
                    const Vec2& b = rings[r][(i + 1) % n];
                    const Vec2& c = rings[s][j];
                    const Vec2& d = rings[s][(j + 1) % m];
                    if (!segmentsMeet(a, b, c, d, touching)) continue;
                    if (r != s && depth > 0.0 && segmentsMeet(a, b, c, d, false) &&
                        std::min({toSegment(a, c, d), toSegment(b, c, d), toSegment(c, a, b), toSegment(d, a, b)}) <= depth) {
                        continue;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

// The longest stretch over which two edges that share no corner run closer than the narrow limit.
double longestNarrowRun(const std::vector<std::vector<Vec2>>& rings)
{
    struct Edge {
        Vec2 a;
        Vec2 b;
        std::size_t ring;
        std::size_t at;
    };
    std::vector<Edge> edges;
    for (std::size_t r = 0; r < rings.size(); ++r) {
        for (std::size_t i = 0; i < rings[r].size(); ++i) {
            const Vec2 a = rings[r][i];
            const Vec2 b = rings[r][(i + 1) % rings[r].size()];
            if (std::hypot(b.x - a.x, b.y - a.y) > kLongRun) edges.push_back({a, b, r, i});
        }
    }
    double longest = 0.0;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const Edge& e = edges[i];
        const double len = std::hypot(e.b.x - e.a.x, e.b.y - e.a.y);
        const double ux = (e.b.x - e.a.x) / len;
        const double uy = (e.b.y - e.a.y) / len;
        for (std::size_t j = i + 1; j < edges.size(); ++j) {
            const Edge& o = edges[j];
            if (o.ring == e.ring) {
                const std::size_t n = rings[e.ring].size();
                if ((e.at + 1) % n == o.at || (o.at + 1) % n == e.at) continue;
            }
            double t0 = (o.a.x - e.a.x) * ux + (o.a.y - e.a.y) * uy;
            double t1 = (o.b.x - e.a.x) * ux + (o.b.y - e.a.y) * uy;
            double d0 = (o.a.y - e.a.y) * ux - (o.a.x - e.a.x) * uy;
            double d1 = (o.b.y - e.a.y) * ux - (o.b.x - e.a.x) * uy;
            if (std::fabs(d1 - d0) > 0.05 * std::hypot(o.b.x - o.a.x, o.b.y - o.a.y)) continue;
            if (t0 > t1) {
                std::swap(t0, t1);
                std::swap(d0, d1);
            }
            double from = std::max(0.0, t0);
            double to = std::min(len, t1);
            if (to - from <= longest) continue;
            const double slope = (d1 - d0) / (t1 - t0);
            if (slope == 0.0) {
                if (std::fabs(d0) >= kNarrow) continue;
            } else {
                const double p = t0 + (-kNarrow - d0) / slope;
                const double q = t0 + (kNarrow - d0) / slope;
                from = std::max(from, std::min(p, q));
                to = std::min(to, std::max(p, q));
            }
            longest = std::max(longest, to - from);
        }
    }
    return longest;
}

// What every list of cleaned polygons must be. strictFloat is off only where corners touch at a point.
void checkPolygons(Report& r, const std::vector<WorldPolygon>& polygons, const std::string& what, bool strictFloat)
{
    std::vector<std::vector<Vec2>> everyRing;
    double sum = 0.0;
    for (const WorldPolygon& polygon : polygons) {
        std::vector<std::vector<Vec2>> rings{polygon.outline};
        rings.insert(rings.end(), polygon.holes.begin(), polygon.holes.end());
        std::vector<std::vector<Vec2>> rounded;
        double size = 0.0;
        for (std::size_t k = 0; k < rings.size(); ++k) {
            const std::vector<Vec2>& ring = rings[k];
            if (ring.size() < 3) {
                r.fail(what + " has a ring of under three points");
                continue;
            }
            const double area = ringArea(ring);
            size += area;
            r.expect(k == 0 ? area > 0.0 : area < 0.0, what + " has a ring that turns the wrong way");
            r.expect(std::fabs(area) >= 0.999 * kLeastArea, what + " has a ring under the least area");
            std::vector<Vec2> ring32;
            double widest = 1e18;
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Vec2& a = ring[i];
                const Vec2& b = ring[(i + 1) % ring.size()];
                const double len = std::hypot(b.x - a.x, b.y - a.y);
                r.expect(len >= 0.99999 * kMerge, what + " has a short edge");
                if (len > 0.0) {
                    double reach = 0.0;
                    for (const Vec2& p : ring) {
                        reach = std::max(reach, std::fabs((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / len);
                    }
                    widest = std::min(widest, reach);
                }
                ring32.push_back({as32(a.x), as32(a.y)});
            }
            r.expect(widest >= kNarrow, what + " has a needle");
            const double area32 = ringArea(ring32);
            r.expect(k == 0 ? area32 > 0.0 : area32 < 0.0, what + " loses its turning in float32");
            rounded.push_back(std::move(ring32));
            if (k > 0) {
                for (const Vec2& p : ring) {
                    r.expect(insidePolygon(p, polygon.outline) || toPaths(p, {pathOf(polygon.outline, true)}) <= kSideTolerance,
                             what + " has a hole outside its outline");
                }
            }
        }
        sum += size;
        r.expect(size >= 0.999 * kLeastArea, what + " has a piece under the least area");
        r.expect(!ringsMeet(rings, false, strictFloat ? 0.0 : kTouchDepth), what + " crosses itself");
        if (strictFloat) r.expect(!ringsMeet(rounded, true), what + " is not simple in float32");
        everyRing.insert(everyRing.end(), rings.begin(), rings.end());
    }
    r.expect(longestNarrowRun(everyRing) <= kLongRun, what + " has a narrow piece, hole or gap");
    const cl::PathsD paths = pathsOf(polygons);
    if (!paths.empty()) {
        r.expect(std::fabs(sum - areaOf(united(paths))) <= 2.0 * kMerge * perimeterOf(paths) + kLeastArea,
                 what + " has pieces that overlap");
    }
}

// The area vector of a ring in space: its length is the area, its direction is the side it faces.
Vec3 areaVector(const std::vector<Vec3>& ring)
{
    Vec3 v;
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        const double ax = ring[i].x - ring[0].x;
        const double ay = ring[i].y - ring[0].y;
        const double az = ring[i].z - ring[0].z;
        const double bx = ring[i + 1].x - ring[0].x;
        const double by = ring[i + 1].y - ring[0].y;
        const double bz = ring[i + 1].z - ring[0].z;
        v.x += 0.5 * (ay * bz - az * by);
        v.y += 0.5 * (az * bx - ax * bz);
        v.z += 0.5 * (ax * by - ay * bx);
    }
    return v;
}

// The length of a vector.
double lengthOf(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// The area of a face less its holes.
double faceArea(const RoofFace& face)
{
    double area = lengthOf(areaVector(face.outline));
    for (const auto& hole : face.holes) area -= lengthOf(areaVector(hole));
    return area;
}

// A face seen from above, whichever way it looks.
cl::PathsD seenFromAbove(const RoofFace& face)
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

// What every finished roof face must be.
void checkFace(Report& r, const RoofFace& face, bool strictFloat)
{
    const std::string what = "roof " + std::string(roofPartName(face.part));
    if (face.outline.size() < 3) {
        r.fail(what + " has under three points");
        return;
    }
    const Vec3 v = areaVector(face.outline);
    const double size = lengthOf(v);
    if (size < 0.99 * kLeastArea) {
        r.fail(what + " has a face under the least area");
        return;
    }
    if (face.part == RoofPart::Covering) r.expect(v.z > 0.0, what + " looks down");
    if (face.part == RoofPart::Underside) r.expect(v.z < 0.0, what + " looks up");
    if (face.part == RoofPart::Parapet && std::fabs(v.z) > 0.5 * size) r.expect(v.z > 0.0, what + " top looks down");

    std::vector<std::vector<Vec3>> rings{face.outline};
    rings.insert(rings.end(), face.holes.begin(), face.holes.end());
    std::vector<std::vector<Vec2>> flat;
    std::vector<std::vector<Vec2>> flat32;
    for (std::size_t k = 0; k < rings.size(); ++k) {
        const std::vector<Vec3>& ring = rings[k];
        if (ring.size() < 3) {
            r.fail(what + " has a hole of under three points");
            continue;
        }
        if (k > 0) {
            const Vec3 h = areaVector(ring);
            r.expect(h.x * v.x + h.y * v.y + h.z * v.z < 0.0, what + " has a hole that turns with its outline");
        }
        std::vector<Vec2> two;
        std::vector<Vec2> two32;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1) % ring.size()];
            r.expect(lengthOf({b.x - a.x, b.y - a.y, b.z - a.z}) >= 0.99999 * kMerge, what + " has a short edge");
            const Vec3& o = face.outline[0];
            r.expect(std::fabs((a.x - o.x) * v.x + (a.y - o.y) * v.y + (a.z - o.z) * v.z) / size <= kMerge,
                     what + " is not planar");
            const double ax = std::fabs(v.x);
            const double ay = std::fabs(v.y);
            const double az = std::fabs(v.z);
            if (az >= ax && az >= ay) {
                two.push_back({a.x, a.y});
                two32.push_back({as32(a.x), as32(a.y)});
            } else if (ax >= ay) {
                two.push_back({a.y, a.z});
                two32.push_back({as32(a.y), as32(a.z)});
            } else {
                two.push_back({a.z, a.x});
                two32.push_back({as32(a.z), as32(a.x)});
            }
        }
        r.expect(ringArea(two32) != 0.0, what + " has no area in float32");
        for (std::size_t i = 0; i < two32.size(); ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1) % ring.size()];
            r.expect(as32(a.x) != as32(b.x) || as32(a.y) != as32(b.y) || as32(a.z) != as32(b.z),
                     what + " has an edge that collapses in float32");
        }
        if (k > 0 && !flat.empty()) {
            for (const Vec2& p : two) {
                r.expect(insidePolygon(p, flat[0]) || toPaths(p, {pathOf(flat[0], true)}) <= kSideTolerance,
                         what + " has a hole outside its outline");
            }
        }
        flat.push_back(std::move(two));
        flat32.push_back(std::move(two32));
    }
    r.expect(!ringsMeet(flat, false, strictFloat ? 0.0 : kTouchDepth), what + " crosses itself");
    if (strictFloat) r.expect(!ringsMeet(flat32, false), what + " crosses itself in float32");
}

// The convex outline round points seen from above.
std::vector<Vec2> convexAround(const std::vector<Vec3>& points)
{
    std::vector<Vec2> sorted;
    for (const Vec3& p : points) sorted.push_back({p.x, p.y});
    std::sort(sorted.begin(), sorted.end(),
              [](const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (sorted.size() < 3) return {};
    const auto half = [](auto first, auto last) {
        std::vector<Vec2> out;
        for (auto it = first; it != last; ++it) {
            while (out.size() >= 2 && sideOf(out[out.size() - 2], out.back(), *it) <= 0) out.pop_back();
            out.push_back(*it);
        }
        out.pop_back();
        return out;
    };
    std::vector<Vec2> out = half(sorted.begin(), sorted.end());
    const std::vector<Vec2> back = half(sorted.rbegin(), sorted.rend());
    out.insert(out.end(), back.begin(), back.end());
    return out;
}

// The world direction of a plan's roof. The tests always set one.
Vec2 roofDirection(const RoofSettings& settings)
{
    const double a = settings.direction.value_or(0.0) * std::numbers::pi / 180.0;
    return {std::cos(a), -std::sin(a)};
}

// Where a section's covering must lie, worked out from the rule in Roofs.h with no cleaning.
cl::PathsD wantedCovering(const RoofBuild& roof, const WorldPolygon& section, const cl::PathsD& above,
                          const RoofSettings& settings)
{
    const bool flat = roof.type == kFlatRoofId;
    cl::PathsD out = united(pathsOf({section}));
    if (flat && settings.parapet > 0.0) return out;
    if (settings.overhang > 0.0) {
        // Each ring of the section grows by itself (Roofs.h). A hole is turned to an outline, shrunk and turned back.
        // Uniting the section first is not the same: rounding decides there whether touching corners give one ring.
        std::vector<std::vector<Vec2>> rings{section.outline};
        rings.insert(rings.end(), section.holes.begin(), section.holes.end());
        cl::PathsD grown;
        for (std::size_t k = 0; k < rings.size(); ++k) {
            const cl::PathsD ring{pathOf(rings[k], true)};
            const double by = k == 0 ? settings.overhang : -settings.overhang;
            for (cl::PathD path : cl::InflatePaths(ring, by, cl::JoinType::Miter, cl::EndType::Polygon, 5.0, kDigits)) {
                if (k > 0) std::reverse(path.begin(), path.end());
                grown.push_back(std::move(path));
            }
        }
        out = grown.empty() ? grown : cl::Union(grown, cl::FillRule::Positive, kDigits);
    }
    out = minus(out, above);
    if (roof.type == kSawtoothRoofId) {
        const Vec2 d = roofDirection(settings);
        const Vec2 n{-d.y, d.x};
        double high = -1e18;
        for (const Vec2& p : section.outline) high = std::max(high, n.x * p.x + n.y * p.y);
        const Vec2 on{n.x * high, n.y * high};
        const double along = d.x * section.outline[0].x + d.y * section.outline[0].y;
        const Vec2 mid{on.x + d.x * along, on.y + d.y * along};
        const double reach = 100.0;
        const std::vector<Vec2> half{{mid.x - d.x * reach, mid.y - d.y * reach},
                                     {mid.x + d.x * reach, mid.y + d.y * reach},
                                     {mid.x + (d.x - n.x) * reach, mid.y + (d.y - n.y) * reach},
                                     {mid.x - (d.x + n.x) * reach, mid.y - (d.y + n.y) * reach}};
        out = common(out, {pathOf(half, true)});
    }
    return out;
}

// A floor's roofs, slab and their collision, from the public functions.
struct Built {
    SlabBuild slab;
    std::vector<RoofBuild> roofs;
    std::vector<CollisionShape> collision;
};

// Builds one floor without the rest of a build plan.
Built buildFloor(const Plan& plan, std::size_t i)
{
    const std::vector<double> levels = floorElevations(plan);
    Built out;
    out.slab = slabBuild(plan, i, levels);
    out.roofs = floorRoofs(plan, i, levels).roofs;
    FloorBuild build;
    build.id = plan.floors[i].id;
    build.elevation = levels[i];
    build.ceilingHeight = plan.floors[i].ceilingHeight;
    build.slab = out.slab;
    build.roofs = out.roofs;
    out.collision = floorCollision(plan.floors[i], build);
    return out;
}

// Edge where a side lies on the uncut slab's boundary, Well where it is clearly off it.
void checkSides(Report& r, const SlabBuild& slab, const cl::PathsD& covered)
{
    if (slab.sides.size() != slab.polygons.size()) {
        r.fail("slab sides do not match the polygons");
        return;
    }
    const auto ringSides = [&](const std::vector<Vec2>& ring, const std::vector<SlabSide>& sides) {
        if (ring.size() != sides.size()) {
            r.fail("slab sides do not match the edges");
            return;
        }
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2& a = ring[i];
            const Vec2& b = ring[(i + 1) % ring.size()];
            const double off = std::max({toPaths(a, covered), toPaths(b, covered),
                                         toPaths({(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, covered)});
            if (off <= kSideTolerance - kMerge) r.expect(sides[i] == SlabSide::Edge, "an outside band is a well");
            if (off > 2.0 * kSideTolerance) r.expect(sides[i] == SlabSide::Well, "a well is an outside band");
        }
    };
    for (std::size_t p = 0; p < slab.polygons.size(); ++p) {
        ringSides(slab.polygons[p].outline, slab.sides[p].outline);
        if (slab.sides[p].holes.size() != slab.polygons[p].holes.size()) {
            r.fail("slab sides do not match the holes");
            continue;
        }
        for (std::size_t h = 0; h < slab.polygons[p].holes.size(); ++h) {
            ringSides(slab.polygons[p].holes[h], slab.sides[p].holes[h]);
        }
    }
}

// Everything the rules of Storeys.h and Roofs.h promise of a plan, without saved numbers.
void audit(Report& r, const Plan& plan, bool strictFloat = true)
{
    RingFaultScope ringFaults;
    std::vector<std::vector<WorldPolygon>> footprints;
    std::vector<cl::PathsD> foot;
    for (const Floor& floor : plan.floors) {
        for (const Room& room : floor.rooms) r.expect(roomShape(floor, room).closed, "the test's room is not closed");
        footprints.push_back(inWorld(floorFootprint(floor)));
        foot.push_back(pathsOf(footprints.back()));
        checkPolygons(r, footprints.back(), "footprint", strictFloat);
        const cl::PathsD plain = plainFootprint(floor);
        expectSame(r, foot.back(), plain, 2.0 * kMerge * perimeterOf(plain) + 8.0 * kLeastArea, "footprint");
    }

    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        const Floor& floor = plan.floors[i];
        const cl::PathsD& own = foot[i];
        const cl::PathsD above = i + 1 < foot.size() ? foot[i + 1] : cl::PathsD{};
        cl::PathsD holes;
        for (const Stair& stair : floor.stairs) {
            const StairLayout layout = stairLayout(plan, floor, stair);
            if (!layout.status.ok()) continue;
            std::vector<Vec2> world;
            for (const Vec2& p : layout.footprint) world.push_back(flipped(p));
            holes.push_back(pathOf(world, true));
        }
        const double perimeter = perimeterOf(own) + perimeterOf(above) + perimeterOf(holes);
        const double bound = 2.0 * kMerge * perimeter +
                             kLeastArea * static_cast<double>(4 + own.size() + above.size() + holes.size());

        const std::vector<WorldPolygon> exposed = inWorld(exposedArea(plan, i));
        const std::vector<WorldPolygon> overhang = inWorld(overhangArea(plan, i));
        const std::vector<WorldPolygon> sections = inWorld(roofSectionAreas(plan, i));
        const Built built = buildFloor(plan, i);
        checkPolygons(r, exposed, "exposed area", strictFloat);
        checkPolygons(r, overhang, "overhang", strictFloat);
        checkPolygons(r, sections, "roof section", strictFloat);
        checkPolygons(r, built.slab.polygons, "slab", strictFloat);
        checkPolygons(r, built.slab.undersides, "underside", strictFloat);
        checkPolygons(r, built.slab.tops, "ledge", strictFloat);

        const cl::PathsD covered = united(own, above);
        const cl::PathsD open = pathsOf(exposed);
        const cl::PathsD roofed = pathsOf(sections);
        const cl::PathsD slabArea = pathsOf(built.slab.polygons);
        expectSame(r, open, minus(own, above), bound, "exposed area");
        expectSame(r, pathsOf(overhang), minus(above, own), bound, "overhang");
        expectSame(r, slabArea, minus(covered, holes), bound, "slab");
        expectSame(r, pathsOf(built.slab.undersides), minus(pathsOf(overhang), holes), bound, "underside");
        expectSame(r, pathsOf(built.slab.tops), minus(minus(open, roofed), holes), bound, "ledge");
        r.expect(deepArea(minus(roofed, open), kDeep) <= 1e-10, "a roof section reaches out of the exposed area");
        r.expect(deepArea(minus(open, roofed), kMinRoofSectionWidth / 2.0 + 1e-3) <= 1e-10,
                 "a wide exposed part has no roof section");
        checkSides(r, built.slab, covered);

        // The roofs may reach the side tolerance into the storey beside them and no further.
        const cl::PathsD storey = offset(above, -(kSideTolerance + 1e-6));
        const auto notAbove = [&](const cl::PathsD& paths, const std::string& what) {
            if (!storey.empty()) r.expect(areaOf(common(paths, storey)) <= 1e-7, what + " lies over the floor above");
        };
        r.expect(built.roofs.size() == sections.size(), "the roofs do not match the sections");
        for (const RoofBuild& roof : built.roofs) {
            if (roof.index < 0 || static_cast<std::size_t>(roof.index) >= sections.size()) {
                r.fail("a roof has no section");
                continue;
            }
            const WorldPolygon& section = sections[static_cast<std::size_t>(roof.index)];
            cl::PathsD covering;
            cl::PathsD underside;
            double coveringSum = 0.0;
            for (const RoofFace& face : roof.faces) {
                checkFace(r, face, strictFloat);
                if (face.outline.size() < 3) continue;
                if (face.part != RoofPart::Covering && face.part != RoofPart::Underside) {
                    const Vec3 v = areaVector(face.outline);
                    if (face.part == RoofPart::Parapet && std::fabs(v.z) > 0.5 * lengthOf(v)) {
                        notAbove(seenFromAbove(face), "a parapet");
                    }
                    continue;
                }
                const cl::PathsD seen = seenFromAbove(face);
                notAbove(seen, "a roof " + std::string(roofPartName(face.part)));
                cl::PathsD& into = face.part == RoofPart::Covering ? covering : underside;
                into.insert(into.end(), seen.begin(), seen.end());
                if (face.part == RoofPart::Covering) coveringSum += areaOf(seen);
            }
            const cl::PathsD wanted = wantedCovering(roof, section, above, plan.roof);
            const cl::PathsD top = united(covering);
            const double roofBound = 2.0 * kMerge * (perimeterOf(covering) + perimeterOf(wanted)) +
                                     kLeastArea * static_cast<double>(4 + roof.faces.size());
            expectSame(r, top, wanted, roofBound, roof.type + " covering");
            expectSame(r, united(underside), top, roofBound, roof.type + " underside", false);
            r.expect(std::fabs(coveringSum - areaOf(top)) <= roofBound, roof.type + " covering faces overlap");

            cl::PathsD hulls;
            for (const CollisionShape& shape : built.collision) {
                if (shape.kind != CollisionTargetKind::Roof || shape.index != roof.index) continue;
                for (const auto& hull : shape.hulls) {
                    r.expect(hull.size() >= 4, "a roof hull has under four points");
                    const std::vector<Vec2> round = convexAround(hull);
                    if (round.size() < 3) {
                        r.fail("a roof hull is flat");
                        continue;
                    }
                    hulls.push_back(pathOf(round, true));
                }
            }
            notAbove(hulls, "a roof hull");
            expectSame(r, united(hulls), top, roofBound, roof.type + " collision", false);
        }

        cl::PathsD slabPieces;
        for (const CollisionShape& shape : built.collision) {
            if (shape.kind != CollisionTargetKind::Slab && shape.kind != CollisionTargetKind::Roof) continue;
            for (const CollisionPiece& piece : shape.pieces) {
                r.expect(piece.top > piece.bottom, "a collision piece has no height");
                r.expect(isConvexRing(piece.outline), "a collision piece is not convex");
                r.expect(ringArea(piece.outline) > 0.0, "a collision piece has no area");
                const cl::PathsD path{pathOf(piece.outline, true)};
                if (shape.kind == CollisionTargetKind::Roof) notAbove(path, "parapet collision");
                else slabPieces.push_back(path[0]);
            }
        }
        expectSame(r, united(slabPieces), slabArea, bound, "slab collision", false);
    }
    r.expect(!ringFaults.any(), "the ring reader found a hole without an outline");
}

}

namespace {

// How a plan is moved, after it is turned and before it is rounded to the grid (0 is no rounding).
struct Place {
    double x = 0.0;
    double y = 0.0;
    double grid = 0.0;
};

const std::vector<Place> kPlaces{{0.0, 0.0, 0.0}, {0.0, 0.0, 1e-6}, {97.3, 88.1, 1e-6}, {-99.1, 64.7, 1e-6}};

// A plan position after turning and moving.
Vec2 placed(const Vec2& p, double angle, const Place& place)
{
    const double c = std::cos(angle * std::numbers::pi / 180.0);
    const double s = std::sin(angle * std::numbers::pi / 180.0);
    const auto snap = [&place](double v) { return place.grid > 0.0 ? std::round(v / place.grid) * place.grid : v; };
    return {snap(p.x * c - p.y * s + place.x), snap(p.x * s + p.y * c + place.y)};
}

// An angle in degrees from 0 up to 360.
double wrapped(double degrees)
{
    const double v = std::fmod(degrees, 360.0);
    return v < 0.0 ? v + 360.0 : v;
}

// Turns a whole plan, with its stairs and roof direction, then moves and rounds it.
void turn(Plan& plan, double angle, const Place& place = {})
{
    for (Floor& floor : plan.floors) {
        for (Point& p : floor.points) {
            const Vec2 q = placed({p.x, p.y}, angle, place);
            p.x = q.x;
            p.y = q.y;
        }
        for (Stair& stair : floor.stairs) {
            const Vec2 q = placed({stair.x, stair.y}, angle, place);
            stair.x = q.x;
            stair.y = q.y;
            stair.rotation = wrapped(stair.rotation + angle);
        }
    }
    plan.roof.direction = wrapped(plan.roof.direction.value_or(0.0) + angle);
}

// Adds a closed ring of walls and returns their ids. bulges is empty or has one value for each wall.
std::vector<Id> addRing(Floor& floor, const std::string& key, const std::vector<Vec2>& points, double thickness = 0.2,
                        const std::vector<double>& bulges = {})
{
    for (std::size_t i = 0; i < points.size(); ++i) addPoint(floor, "p" + key + std::to_string(i), points[i].x, points[i].y);
    std::vector<Id> walls;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const std::string id = "w" + key + std::to_string(i);
        addWall(floor, id, "p" + key + std::to_string(i), "p" + key + std::to_string((i + 1) % points.size()), thickness);
        if (i < bulges.size()) floor.walls.back().bulge = bulges[i];
        walls.push_back(id);
    }
    return walls;
}

// Adds a room inside rings of walls.
void addRoom(Floor& floor, const std::string& key, const std::vector<Id>& walls)
{
    Room room;
    room.id = "r" + key;
    room.walls = walls;
    floor.rooms.push_back(std::move(room));
}

// The corners of a rectangle, clockwise on screen from the top left.
std::vector<Vec2> box(double x0, double y0, double x1, double y1)
{
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// Adds a rectangular room.
void addBox(Floor& floor, const std::string& key, double x0, double y0, double x1, double y1, double thickness = 0.2)
{
    addRoom(floor, key, addRing(floor, key, box(x0, y0, x1, y1), thickness));
}

// A plan of one empty floor with the roof's direction set, so it can turn with the plan.
Plan emptyPlan(const std::string& type = "flat")
{
    Plan plan = makeEmptyPlan();
    plan.roof.type = type;
    plan.roof.direction = 0.0;
    return plan;
}

// Adds a floor on top.
Floor& addFloor(Plan& plan)
{
    Floor floor;
    floor.id = "f" + std::to_string(plan.floors.size() + 1);
    plan.floors.push_back(std::move(floor));
    return plan.floors.back();
}

// A 4 m square under an upper floor from x 0 to 3.
Plan setback(const std::string& type = "flat", double slab = 0.2)
{
    Plan plan = emptyPlan(type);
    addBox(plan.floors[0], "a", 0, 0, 4, 4);
    plan.floors[0].slabThickness = slab;
    addBox(addFloor(plan), "a", 0, 0, 3, 4);
    return plan;
}

// The registered roof types' ids.
std::vector<std::string> roofTypeIds()
{
    std::vector<std::string> out;
    for (const RoofType* type : allRoofTypes()) out.emplace_back(type->id());
    return out;
}

// A name for a failing building that is enough to make it again.
std::string named(const std::string& what, double angle, const Place& place)
{
    std::ostringstream out;
    out.precision(17);
    out << what << ", angle " << angle << ", at " << place.x << "," << place.y << ", grid " << place.grid;
    return out.str();
}

// The number of holes in some areas.
std::size_t holeCount(const std::vector<PlanArea>& areas)
{
    std::size_t count = 0;
    for (const PlanArea& area : areas) count += area.holes.size();
    return count;
}

// The area of some world polygons.
double polygonsArea(const std::vector<WorldPolygon>& polygons)
{
    return areaOf(pathsOf(polygons));
}

}

TEST_CASE("edge cases needle undersides at 20 50 and 80 degrees stay out", "[core][edges]")
{
    Report r;
    for (const int angle : {20, 50, 80}) {
        for (const Place& place : kPlaces) {
            for (const double slab : {0.05, 0.2}) {
                for (const std::string& type : roofTypeIds()) {
                    Plan plan = setback(type, slab);
                    turn(plan, angle, place);
                    r.begin(named("setback " + type + " slab " + std::to_string(slab), angle, place));
                    r.expect(overhangArea(plan, 0).empty(), "an overhang is left");
                    r.expect(slabBuild(plan, 0, floorElevations(plan)).undersides.empty(), "an underside is left");
                    r.expect(roofSectionAreas(plan, 0).size() == 1, "there is not one roof section");
                    audit(r, plan);
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases roof points at 30 degrees stay apart in float32", "[core][edges]")
{
    Report r;
    for (const double angle : {30.0, 60.0, 120.0, 210.0}) {
        for (const Place& place : kPlaces) {
            for (const double overhang : {0.0, 0.3}) {
                for (const std::string& type : roofTypeIds()) {
                    for (const bool automatic : {false, true}) {
                        Plan plan = setback(type);
                        plan.roof.overhang = overhang;
                        turn(plan, angle, place);
                        r.begin(named("setback " + type + (automatic ? " automatic" : "") + " overhang " +
                                          std::to_string(overhang),
                                      angle, place));
                        if (automatic) {
                            // Without a set direction only the faces are checked: the audit needs a direction.
                            plan.roof.direction.reset();
                            for (std::size_t i = 0; i < plan.floors.size(); ++i) {
                                for (const RoofBuild& roof : floorRoofs(plan, i, floorElevations(plan)).roofs) {
                                    for (const RoofFace& face : roof.faces) checkFace(r, face, true);
                                }
                            }
                        } else {
                            audit(r, plan);
                        }
                    }
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases the outside band stays an edge beside a well at a corner", "[core][edges]")
{
    Report r;
    for (const double gap : {0.0, 1e-6, 5e-6, 9e-6, 2.5e-5, 5e-5, 1e-4}) {
        for (const double angle : {0.0, 30.0, 90.0, 135.0}) {
            for (const Place& place : kPlaces) {
                Plan plan = emptyPlan();
                addBox(plan.floors[0], "a", 0, 0, 4, 4);
                addFloor(plan);
                Stair stair;
                stair.id = "s1";
                stair.x = 3.0;
                stair.y = 2.0;
                plan.floors[0].stairs.push_back(stair);
                const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
                REQUIRE(layout.status.ok());
                double right = -1e18;
                double bottom = -1e18;
                for (const Vec2& p : layout.footprint) {
                    right = std::max(right, p.x);
                    bottom = std::max(bottom, p.y);
                }
                REQUIRE(bottom > 4.2);
                plan.floors[0].stairs[0].x += 4.1 - gap - right;
                turn(plan, angle, place);
                r.begin(named("well " + std::to_string(gap * 1e6) + " micrometres from the corner", angle, place));
                audit(r, plan);

                const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
                REQUIRE(slab.sides.size() == slab.polygons.size());
                const Vec2 from = flipped(placed({4.1, -0.1}, angle, place));
                const Vec2 to = flipped(placed({4.1, 4.1}, angle, place));
                double onBand = 0.0;
                for (std::size_t p = 0; p < slab.polygons.size(); ++p) {
                    const std::vector<Vec2>& ring = slab.polygons[p].outline;
                    REQUIRE(slab.sides[p].outline.size() == ring.size());
                    for (std::size_t i = 0; i < ring.size(); ++i) {
                        const Vec2& a = ring[i];
                        const Vec2& b = ring[(i + 1) % ring.size()];
                        if (toSegment(a, from, to) > kSideTolerance || toSegment(b, from, to) > kSideTolerance) continue;
                        if (slab.sides[p].outline[i] == SlabSide::Edge) onBand += std::hypot(b.x - a.x, b.y - a.y);
                    }
                }
                // Below the merge distance the strip beside the well is left out, so the band ends at y 2.
                r.expect(onBand >= (gap < kMerge ? 2.0 : 4.199), "the east band is not all edge");
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a well from boundary to boundary stays a well", "[core][edges]")
{
    Report r;
    for (const double angle : {0.0, 30.0, 45.0, 80.0}) {
        for (const Place& place : kPlaces) {
            Plan plan = emptyPlan();
            addBox(plan.floors[0], "a", 0, 0, 1, 6);
            addFloor(plan);
            Stair stair;
            stair.id = "s1";
            stair.x = 1.5;
            stair.y = 1.0;
            stair.width = 1.6;
            stair.rotation = 90.0;
            plan.floors[0].stairs.push_back(stair);
            turn(plan, angle, place);
            r.begin(named("well across", angle, place));
            audit(r, plan);
            const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
            r.expect(slab.polygons.size() == 2, "the slab is not in two parts");
            REQUIRE(slab.sides.size() == slab.polygons.size());
            double well = 0.0;
            for (std::size_t p = 0; p < slab.polygons.size(); ++p) {
                const std::vector<Vec2>& ring = slab.polygons[p].outline;
                REQUIRE(slab.sides[p].outline.size() == ring.size());
                for (std::size_t i = 0; i < ring.size(); ++i) {
                    if (slab.sides[p].outline[i] != SlabSide::Well) continue;
                    const Vec2& b = ring[(i + 1) % ring.size()];
                    well += std::hypot(b.x - ring[i].x, b.y - ring[i].y);
                }
            }
            r.expect(std::fabs(well - 2.4) <= 1e-3, "the two cuts are not both wells");
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases hairline rings go and wider offsets keep their whole ledge", "[core][edges]")
{
    Report r;
    for (const double angle : {0.0, 20.0, 30.0, 61.0}) {
        for (const Place& place : kPlaces) {
            for (const double grow : {-5e-6, -1e-6, 1e-6, 5e-6}) {
                Plan plan = emptyPlan();
                addBox(plan.floors[0], "a", 0, 0, 4, 4);
                addBox(addFloor(plan), "a", -grow, -grow, 4 + grow, 4 + grow);
                turn(plan, angle, place);
                r.begin(named("ring " + std::to_string(grow * 1e6) + " micrometres", angle, place));
                r.expect(exposedArea(plan, 0).empty(), "a hairline exposed ring is left");
                r.expect(overhangArea(plan, 0).empty(), "a hairline overhang ring is left");
                const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
                r.expect(slab.tops.empty() && slab.undersides.empty(), "a hairline ledge or underside is left");
                audit(r, plan);
            }
            for (const double shift : {2.5e-5, 5e-5, 1e-4}) {
                Plan plan = emptyPlan();
                addBox(plan.floors[0], "a", 0, 0, 4, 4);
                addBox(addFloor(plan), "a", shift, 0, 4 + shift, 4);
                turn(plan, angle, place);
                r.begin(named("offset " + std::to_string(shift * 1e6) + " micrometres", angle, place));
                const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
                const double whole = shift * 4.2;
                r.expect(std::fabs(polygonsArea(slab.tops) - whole) <= 0.2 * whole, "the ledge is not whole");
                r.expect(std::fabs(polygonsArea(slab.undersides) - whole) <= 0.2 * whole, "the underside is not whole");
                audit(r, plan);
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a hairline neck parts two wide overhangs", "[core][edges]")
{
    Report r;
    for (const double neck : {1e-7, 1e-6, 5e-6, 9e-6}) {
        for (const double angle : {0.0, 30.0, 50.0, 77.0}) {
            for (const Place& place : kPlaces) {
                Plan plan = emptyPlan();
                addBox(plan.floors[0], "a", 0, 0, 4, 2 - neck);
                addBox(addFloor(plan), "a", -2, 0, 6, 2);
                turn(plan, angle, place);
                r.begin(named("neck " + std::to_string(neck * 1e6) + " micrometres", angle, place));
                r.expect(overhangArea(plan, 0).size() == 2, "the neck joins the overhangs");
                r.expect(slabBuild(plan, 0, floorElevations(plan)).undersides.size() == 2, "the neck joins the undersides");
                audit(r, plan);
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases slit holes in double walls are filled", "[core][edges]")
{
    Report r;
    for (const double slit : {1e-7, 1e-6, 5e-6, 9e-6}) {
        for (const bool junction : {false, true}) {
            for (const std::string& type : roofTypeIds()) {
                for (const double angle : {0.0, 30.0, 50.0}) {
                    for (const Place& place : {Place{20.0, 20.0, 0.0}, Place{97.3, 88.1, 1e-6}}) {
                        Plan plan = emptyPlan(type);
                        Floor& f = plan.floors[0];
                        addPoint(f, "a", 0, 0);
                        addPoint(f, "b", 4, 0);
                        addPoint(f, "c", 4, 4);
                        addPoint(f, "d", 0, 4);
                        addPoint(f, "p", 0, 0.2 + slit);
                        addPoint(f, "q", 4, 0.2 + slit);
                        addWall(f, "w1", "a", "b", 0.2);
                        addWall(f, "w2", "b", "c", 0.2);
                        addWall(f, "w3", "c", "d", 0.2);
                        if (junction) {
                            addWall(f, "w4", "d", "p", 0.2);
                            addWall(f, "w5", "p", "a", 0.2);
                        } else {
                            addWall(f, "w4", "d", "a", 0.2);
                        }
                        addWall(f, "w9", "p", "q", 0.2);
                        turn(plan, angle, place);
                        r.begin(named("slit " + std::to_string(slit * 1e6) + (junction ? " junction " : " ") + type,
                                      angle, place));
                        const std::vector<PlanArea> footprint = floorFootprint(plan.floors[0]);
                        r.expect(footprint.size() == 1 && holeCount(footprint) == 1, "the yard is not the only hole");
                        audit(r, plan);
                    }
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a hole too small to keep is filled and a small real one stays", "[core][edges]")
{
    Report r;
    for (const double side : {9e-6, 5e-5, 5e-4, 2e-3}) {
        for (const double angle : {0.0, 30.0}) {
            for (const Place& place : kPlaces) {
                Plan plan = emptyPlan();
                addRing(plan.floors[0], "a", box(0, 0, 0.2 + side, 0.2 + side));
                turn(plan, angle, place);
                r.begin(named("pinhole " + std::to_string(side * 1e6) + " micrometres", angle, place));
                const std::vector<PlanArea> footprint = floorFootprint(plan.floors[0]);
                r.expect(holeCount(footprint) == (side > 1e-3 ? 1u : 0u), "the wrong holes are kept");
                for (const PlanArea& area : footprint) checkPolygons(r, inWorld({area}), "footprint", true);
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a footprint lies on its wall solids", "[core][edges]")
{
    Report r;
    for (int variant = 0; variant < 3; ++variant) {
        for (int angle = 0; angle < 90; angle += 5) {
            for (const Place& place : kPlaces) {
                Plan plan = setback();
                if (variant == 1) plan.floors[0].walls[1].bulge = -0.4;
                if (variant == 2) {
                    const std::vector<Id> inner = addRing(plan.floors[0], "i", box(0.6, 0.6, 1.6, 1.6));
                    std::vector<Id>& walls = plan.floors[0].rooms[0].walls;
                    walls.insert(walls.end(), inner.begin(), inner.end());
                }
                turn(plan, angle, place);
                r.begin(named("wall solids variant " + std::to_string(variant), angle, place));
                const std::vector<WorldPolygon> footprint = inWorld(floorFootprint(plan.floors[0]));
                if (footprint.size() != 1) {
                    r.fail("the footprint is not one piece");
                    continue;
                }
                cl::PathsD solidRings;
                cl::PathD outer;
                double biggest = 0.0;
                for (const WallSolid& solid : wallSolids(plan.floors[0])) {
                    solidRings.push_back(pathOf(solid.outline.points, true));
                    if (ringArea(solid.outline.points) > biggest) {
                        biggest = ringArea(solid.outline.points);
                        outer = solidRings.back();
                    }
                    for (const WallRing& hole : solid.holes) solidRings.push_back(pathOf(hole.points, false));
                }
                for (const Vec2& p : footprint[0].outline) {
                    r.expect(toPaths(p, solidRings) <= kSideTolerance, "the footprint leaves the wall solids");
                }
                const cl::PathsD outline{pathOf(footprint[0].outline, true)};
                for (const cl::PointD& p : outer) {
                    r.expect(toPaths({p.x, p.y}, outline) <= kSideTolerance, "a wall corner is off the footprint");
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a corner touching the edge below puts no roof over the upper floor", "[core][edges]")
{
    Report r;
    for (const double out : {-9e-6, -4e-6, -1e-7, 0.0, 1e-7, 4e-6, 9e-6}) {
        for (const std::string& type : roofTypeIds()) {
            for (const double angle : {0.0, 30.0}) {
                for (const Place& place : {Place{}, Place{97.3, 88.1, 1e-6}}) {
                    for (const double overhang : {0.0, 0.3}) {
                        for (const double parapet : {0.0, 0.6}) {
                            if (parapet > 0.0 && type != kFlatRoofId) continue;
                            Plan plan = emptyPlan(type);
                            plan.roof.overhang = overhang;
                            plan.roof.parapet = parapet;
                            addBox(plan.floors[0], "a", 0, 0, 4, 4);
                            Floor& upper = addFloor(plan);
                            // The lower corner's outer tip is out past the outer face at y 4.1.
                            const double y = 4.1 + out - 0.1 * std::numbers::sqrt2 - 1.0;
                            addRoom(upper, "d", addRing(upper, "d", {{2, y - 1}, {3, y}, {2, y + 1}, {1, y}}));
                            turn(plan, angle, place);
                            r.begin(named("diamond out " + std::to_string(out * 1e6) + " " + type + " overhang " +
                                              std::to_string(overhang) + " parapet " + std::to_string(parapet),
                                          angle, place));
                            audit(r, plan, false);
                        }
                    }
                }
            }
        }
    }
    requireNone(r);
}

namespace {

// Says how many covering faces of the top floor's hip reach the place and expects the count.
void expectApex(Report& r, const Plan& plan, const Vec2& planCentre, int wanted, bool every)
{
    const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, floorElevations(plan)).roofs;
    if (roofs.size() != 1 || roofs[0].type != kHipRoofId) {
        r.fail("there is not one hip roof");
        return;
    }
    const Vec2 centre = flipped(planCentre);
    int faces = 0;
    int reached = 0;
    for (const RoofFace& face : roofs[0].faces) {
        if (face.part != RoofPart::Covering) continue;
        ++faces;
        double best = 1e18;
        for (const Vec3& p : face.outline) best = std::min(best, std::hypot(p.x - centre.x, p.y - centre.y));
        if (best <= 2.0 * kMerge) ++reached;
        else if (every) r.fail("a hip face stops short of the apex");
    }
    r.expect(faces >= wanted, "hip faces are missing");
    r.expect(reached >= wanted, "too few hip faces reach the apex");
}

// Expects covering at and just round a place, where pieces that lose their tips would leave a hole.
void expectCovered(Report& r, const Plan& plan, const Vec2& planCentre)
{
    const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, floorElevations(plan)).roofs;
    const Vec2 centre = flipped(planCentre);
    for (const double out : {0.0, 2e-4, 1e-3, 3e-3, 5e-3}) {
        for (int k = 0; k < 24; ++k) {
            const double a = 0.0123 + k * std::numbers::pi / 12.0;
            const Vec2 at{centre.x + out * std::cos(a), centre.y + out * std::sin(a)};
            double best = 1e18;
            for (const RoofBuild& roof : roofs) {
                for (const RoofFace& face : roof.faces) {
                    if (face.part != RoofPart::Covering) continue;
                    const cl::PathsD seen = seenFromAbove(face);
                    std::vector<Vec2> outline;
                    for (const cl::PointD& p : seen[0]) outline.push_back({p.x, p.y});
                    best = std::min(best, insidePolygon(at, outline) ? 0.0 : toPaths(at, seen));
                }
            }
            r.expect(best <= 2.0 * kMerge, "the hip has a hole at its apex");
        }
    }
}

// Where the hip tests turn and move their rooms. No rounding: equal sides are what makes a common apex.
const std::vector<std::pair<double, Place>> kHipPlaces{{0.0, {}}, {30.0, {}}, {30.0, {80.3, 60.1, 0.0}}};

// A room round as a regular polygon, whose hip faces all meet in the middle, and the same room with curved walls.
// The polygon has straight walls because only those are mitred at joints this flat, so its sides stay equal.
void checkRoundHip(double radius)
{
    Report r;
    const int sides = 4 * arcSegmentCount(radius, std::numbers::pi / 2.0);
    std::vector<Vec2> polygon;
    for (int k = 0; k < sides; ++k) {
        const double a = 2.0 * std::numbers::pi * k / sides;
        polygon.push_back({radius * std::cos(a), radius * std::sin(a)});
    }
    const double bulge = -radius * (1.0 - std::sqrt(0.5));
    for (const auto& [angle, place] : kHipPlaces) {
        for (const bool curved : {false, true}) {
            Plan plan = emptyPlan("hip");
            Floor& f = plan.floors[0];
            if (curved) {
                const std::vector<Vec2> ring{{0, -radius}, {radius, 0}, {0, radius}, {-radius, 0}};
                addRoom(f, "o", addRing(f, "o", ring, 0.2, {bulge, bulge, bulge, bulge}));
            } else {
                addRoom(f, "o", addRing(f, "o", polygon));
            }
            turn(plan, angle, place);
            r.begin(named((curved ? "curved" : "polygon") + std::string(" hip radius ") + std::to_string(radius),
                          angle, place));
            if (!curved) expectApex(r, plan, placed({0, 0}, angle, place), sides, true);
            expectCovered(r, plan, placed({0, 0}, angle, place));
            audit(r, plan);
        }
    }
    requireNone(r);
}

}

TEST_CASE("edge cases a round hip of radius 2 keeps its apex", "[core][edges]")
{
    checkRoundHip(2.0);
}

TEST_CASE("edge cases a round hip of radius 4 keeps its apex", "[core][edges]")
{
    checkRoundHip(4.0);
}

TEST_CASE("edge cases a round hip of radius 10 keeps its apex", "[core][edges]")
{
    checkRoundHip(10.0);
}

TEST_CASE("edge cases a hip on a D bay keeps its apex", "[core][edges]")
{
    Report r;
    const int bay = arcSegmentCount(2.0, std::numbers::pi);
    std::vector<Vec2> chords{{-3, -2}};
    for (int k = 0; k <= bay; ++k) {
        const double a = std::numbers::pi * (static_cast<double>(k) / bay - 0.5);
        chords.push_back({2.0 * std::cos(a), 2.0 * std::sin(a)});
    }
    chords.push_back({-3, 2});
    for (const auto& [angle, place] : kHipPlaces) {
        for (const bool curved : {false, true}) {
            Plan plan = emptyPlan("hip");
            Floor& f = plan.floors[0];
            if (curved) {
                const std::vector<Vec2> ring{{-3, -2}, {0, -2}, {0, 2}, {-3, 2}};
                addRoom(f, "d", addRing(f, "d", ring, 0.2, {0.0, -2.0, 0.0, 0.0}));
            } else {
                addRoom(f, "d", addRing(f, "d", chords));
            }
            turn(plan, angle, place);
            r.begin(named(curved ? "curved D bay hip" : "polygon D bay hip", angle, place));
            // The straight sides' faces meet on the ridge, the bay's in the middle of its arc.
            if (!curved) expectApex(r, plan, placed({0, 0}, angle, place), bay, false);
            expectCovered(r, plan, placed({0, 0}, angle, place));
            audit(r, plan);
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases two pieces nearer than the merge distance join", "[core][edges]")
{
    Report r;
    for (const bool freeWall : {false, true}) {
        for (const double gap : {0.0, 1e-7, 1e-6, 5e-6, 9e-6, 2.5e-5, 5e-5, 1e-3}) {
            for (const std::string& type : roofTypeIds()) {
                for (const double angle : {0.0, 30.0}) {
                    for (const Place& place : {Place{}, Place{0.0, 0.0, 1e-6}, Place{97.3, 88.1, 1e-6}}) {
                        Plan plan = emptyPlan(type);
                        Floor& f = plan.floors[0];
                        addBox(f, "a", 0, 0, 4, 4);
                        if (freeWall) {
                            addPoint(f, "p", 4.2 + gap, 0);
                            addPoint(f, "q", 4.2 + gap, 4);
                            addWall(f, "w9", "p", "q", 0.2);
                        } else {
                            addBox(f, "b", 4.2 + gap, 0, 8.2 + gap, 4);
                        }
                        turn(plan, angle, place);
                        r.begin(named(std::string(freeWall ? "free wall" : "two squares") + " gap " +
                                          std::to_string(gap * 1e6) + " " + type,
                                      angle, place));
                        const std::size_t wanted = gap < kMerge ? 1u : 2u;
                        const std::vector<PlanArea> footprint = floorFootprint(f);
                        r.expect(footprint.size() == wanted && holeCount(footprint) == 0, "the footprint pieces are wrong");
                        r.expect(roofSectionAreas(plan, 0).size() == wanted, "the roof sections are wrong");
                        r.expect(slabBuild(plan, 0, floorElevations(plan)).polygons.size() == wanted, "the slab pieces are wrong");
                        audit(r, plan);
                    }
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a piece in a courtyard joins across a hairline", "[core][edges]")
{
    Report r;
    for (const double gap : {1e-7, 1e-6, 5e-6, 9e-6, 2.5e-5, 5e-5}) {
        for (const std::string& type : roofTypeIds()) {
            for (const double angle : {0.0, 30.0, 180.0}) {
                for (const Place& place : {Place{}, Place{97.3, 88.1, 1e-6}}) {
                    Plan plan = emptyPlan(type);
                    Floor& f = plan.floors[0];
                    std::vector<Id> walls = addRing(f, "a", box(0, 0, 10, 10));
                    const std::vector<Id> inner = addRing(f, "b", box(2, 2, 8, 8));
                    walls.insert(walls.end(), inner.begin(), inner.end());
                    addRoom(f, "a", walls);
                    addBox(f, "c", 2.2 + gap, 4, 4, 6);
                    turn(plan, angle, place);
                    r.begin(named("piece in a courtyard gap " + std::to_string(gap * 1e6) + " " + type, angle, place));
                    const std::vector<PlanArea> footprint = floorFootprint(f);
                    r.expect(footprint.size() == (gap < kMerge ? 1u : 2u), "the piece does not join as it should");
                    r.expect(holeCount(footprint) == 1, "the courtyard is lost");
                    audit(r, plan);
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases eaves tips keep the mitre limit of 5", "[core][edges]")
{
    Report r;
    for (const double corner : {14.0, 20.0, 22.0, 30.0}) {
        for (const double overhang : {0.3, 2.0}) {
            for (const std::string& type : roofTypeIds()) {
                for (const double angle : {0.0, 30.0}) {
                    Plan plan = emptyPlan(type);
                    plan.roof.overhang = overhang;
                    plan.roof.parapet = 0.0;
                    // Walls meeting under 25 degrees are not mitred, so the sharp corner is cut by the floor
                    // above: its outer face leaves the outer face below at x 0.5 at the angle to be tested.
                    addBox(plan.floors[0], "a", 0, 0, 6, 4);
                    const double a = corner * std::numbers::pi / 180.0;
                    const double bend = 0.5 + (0.1 - 0.1 / std::cos(a)) / std::tan(a);
                    const double east = -0.1 + 5.5 * std::tan(a) + 0.1 / std::cos(a);
                    Floor& upper = addFloor(plan);
                    addRoom(upper, "u", addRing(upper, "u", {{0, 0}, {bend, 0}, {6, east}, {6, 4}, {0, 4}}));
                    turn(plan, angle);
                    r.begin(named("corner " + std::to_string(corner) + " overhang " + std::to_string(overhang) + " " + type,
                                  angle, {}));
                    audit(r, plan);
                    const cl::PathsD section = pathsOf(inWorld(roofSectionAreas(plan, 0)));
                    REQUIRE_FALSE(section.empty());
                    double furthest = 0.0;
                    for (const RoofBuild& roof : floorRoofs(plan, 0, floorElevations(plan)).roofs) {
                        for (const RoofFace& face : roof.faces) {
                            for (const Vec3& p : face.outline) furthest = std::max(furthest, toPaths({p.x, p.y}, section));
                        }
                    }
                    r.expect(furthest <= 5.0 * overhang + kSideTolerance, "an eaves tip passes 5 times the overhang");
                    // The sawtooth cuts its eaves at the high side, so only the others must keep a 30 degree tip.
                    if (corner > 25.0 && type != kSawtoothRoofId) {
                        r.expect(furthest >= 0.99 * overhang / std::sin(a / 2.0), "a tip inside the limit is cut off");
                    }
                }
            }
        }
    }
    requireNone(r);
}

namespace {

// Says whether a covering face lies over the position.
bool coveredByRoofs(const std::vector<RoofBuild>& roofs, const Vec2& p)
{
    for (const RoofBuild& roof : roofs) {
        for (const RoofFace& face : roof.faces) {
            if (face.part != RoofPart::Covering) continue;
            int inside = 0;
            for (const cl::PathD& path : seenFromAbove(face)) {
                std::vector<Vec2> ring;
                for (const cl::PointD& q : path) ring.push_back({q.x, q.y});
                if (insidePolygon(p, ring)) ++inside;
            }
            if (inside % 2 == 1) return true;
        }
    }
    return false;
}

}

TEST_CASE("edge cases a slit between two eaves is filled", "[core][edges]")
{
    Report r;
    for (const double slit : {-5e-6, -1e-7, 0.0, 1e-7, 1e-6, 5e-6, 9e-6}) {
        for (const std::string& type : roofTypeIds()) {
            for (const double angle : {0.0, 30.0}) {
                for (const Place& place : {Place{}, Place{0.0, 0.0, 1e-6}, Place{97.3, 88.1, 1e-6}}) {
                    Plan plan = emptyPlan(type);
                    plan.roof.parapet = 0.0;
                    const double g = 0.8 + slit;
                    const std::vector<Vec2> ring{{0, 0}, {3, 0}, {3, 4}, {3 + g, 4}, {3 + g, 0}, {6 + g, 0}, {6 + g, 6}, {0, 6}};
                    addRoom(plan.floors[0], "u", addRing(plan.floors[0], "u", ring));
                    turn(plan, angle, place);
                    r.begin(named("eaves slit " + std::to_string(slit * 1e6) + " " + type, angle, place));
                    const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, floorElevations(plan)).roofs;
                    for (const double along : {0.5, 1.3, 2.3, 3.4}) {
                        const Vec2 middle = flipped(placed({3.0 + g / 2.0, along}, angle, place));
                        r.expect(coveredByRoofs(roofs, middle), "the eaves keep a slit");
                    }
                    audit(r, plan);
                }
            }
        }
    }
    requireNone(r);
}

namespace {

// The area of each roof part of a plan.
std::vector<double> areaByPart(const Plan& plan)
{
    std::vector<double> out(6, 0.0);
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        for (const RoofBuild& roof : floorRoofs(plan, i, floorElevations(plan)).roofs) {
            for (const RoofFace& face : roof.faces) out[static_cast<std::size_t>(face.part)] += faceArea(face);
        }
    }
    return out;
}

// One of the shapes whose roofs must not change when the plan turns.
Plan turningShape(int shape, const std::string& type)
{
    Plan plan = emptyPlan(type);
    Floor& f = plan.floors[0];
    if (shape == 0) return setback(type);
    if (shape == 1) {
        addRoom(f, "l", addRing(f, "l", {{0, 0}, {5, 0}, {5, 2.3}, {2.1, 2.3}, {2.1, 5}, {0, 5}}));
    } else if (shape == 2) {
        std::vector<Id> walls = addRing(f, "a", box(0, 0, 7.5, 7.5));
        const std::vector<Id> inner = addRing(f, "b", box(2.2, 2.2, 5.1, 5.1));
        walls.insert(walls.end(), inner.begin(), inner.end());
        addRoom(f, "a", walls);
    } else if (shape == 3) {
        addRoom(f, "c", addRing(f, "c", box(0, 0, 4, 5), 0.2, {0.0, -0.4, 0.0, 0.0}));
    } else {
        // An upper floor a hairline out of line.
        addBox(f, "a", 0, 0, 4, 4);
        addBox(addFloor(plan), "a", 0, -5e-6, 3, 4);
    }
    return plan;
}

}

namespace {

// Expects every roof part of the turning shapes to keep its area when the plan turns, for the sawtooth alone or
// for every other roof type.
void checkTurning(bool sawtoothOnly)
{
    Report r;
    for (int shape = 0; shape < 5; ++shape) {
        for (const std::string& type : roofTypeIds()) {
            if ((type == kSawtoothRoofId) != sawtoothOnly) continue;
            const Plan straight = turningShape(shape, type);
            const std::vector<double> wanted = areaByPart(straight);
            double perimeter = 0.0;
            for (const Floor& floor : straight.floors) perimeter += perimeterOf(pathsOf(inWorld(floorFootprint(floor))));
            const double bound = 4.0 * (2.0 * kMerge * perimeter + 16.0 * kLeastArea);
            REQUIRE(wanted[static_cast<std::size_t>(RoofPart::Covering)] > 0.1);
            for (const double angle : {7.0, 20.0, 30.0, 45.0, 50.0, 63.0, 80.0}) {
                for (const Place& place : {Place{}, Place{97.3, 88.1, 0.0}}) {
                    Plan plan = straight;
                    turn(plan, angle, place);
                    r.begin(named("shape " + std::to_string(shape) + " " + type, angle, place));
                    const std::vector<double> got = areaByPart(plan);
                    for (std::size_t part = 0; part < wanted.size(); ++part) {
                        r.expect(std::fabs(got[part] - wanted[part]) <= bound,
                                 std::string(roofPartName(static_cast<RoofPart>(part))) + " changes its area");
                    }
                    r.expect(roofSectionAreas(plan, 0).size() == roofSectionAreas(straight, 0).size(),
                             "the sections change");
                }
            }
        }
    }
    requireNone(r);
}

}

TEST_CASE("edge cases turning keeps the area of every roof part", "[core][edges]")
{
    checkTurning(false);
}

TEST_CASE("edge cases turning keeps the area of a sawtooth roof", "[core][edges]")
{
    checkTurning(true);
}

namespace {

// How near a fill or glass must stand to a side to close it.
constexpr double kOnSide = 2e-4;

// An upright face seen from above: its two furthest points and how high it reaches.
struct Upright {
    Vec2 a;
    Vec2 b;
    double bottom = 0.0;
    double top = 0.0;
};

// The upright faces of one part of a roof.
std::vector<Upright> uprightFaces(const RoofBuild& roof, RoofPart part)
{
    std::vector<Upright> out;
    for (const RoofFace& face : roof.faces) {
        if (face.part != part || face.outline.empty()) continue;
        Upright found;
        found.bottom = face.outline[0].z;
        found.top = face.outline[0].z;
        double longest = -1.0;
        for (const Vec3& p : face.outline) {
            found.bottom = std::min(found.bottom, p.z);
            found.top = std::max(found.top, p.z);
            for (const Vec3& q : face.outline) {
                const double len = std::hypot(q.x - p.x, q.y - p.y);
                if (len <= longest) continue;
                longest = len;
                found.a = {p.x, p.y};
                found.b = {q.x, q.y};
            }
        }
        out.push_back(found);
    }
    return out;
}

// Expects no fill over glass, and a fill or glass wherever the roof stands above the section's outline.
void checkSawtoothSides(Report& r, const Plan& plan)
{
    const std::vector<double> levels = floorElevations(plan);
    const std::vector<WorldPolygon> sections = inWorld(roofSectionAreas(plan, 0));
    const double base = slabBuild(plan, 0, levels).top;
    for (const RoofBuild& roof : floorRoofs(plan, 0, levels).roofs) {
        if (roof.index < 0 || static_cast<std::size_t>(roof.index) >= sections.size()) {
            r.fail("a roof has no section");
            continue;
        }
        const std::vector<Upright> fills = uprightFaces(roof, RoofPart::Gable);
        const std::vector<Upright> panes = uprightFaces(roof, RoofPart::Glazing);
        for (const Upright& fill : fills) {
            const double len = std::hypot(fill.b.x - fill.a.x, fill.b.y - fill.a.y);
            if (len <= kLongRun) continue;
            for (const Upright& pane : panes) {
                const double ux = pane.b.x - pane.a.x;
                const double uy = pane.b.y - pane.a.y;
                const double paneLength = std::hypot(ux, uy);
                if (paneLength <= 0.0) continue;
                const auto off = [&](const Vec2& p) {
                    return std::fabs((p.x - pane.a.x) * uy - (p.y - pane.a.y) * ux) / paneLength;
                };
                if (off(fill.a) > kOnSide || off(fill.b) > kOnSide) continue;
                for (const double t : {0.1, 0.3, 0.5, 0.7, 0.9}) {
                    const Vec2 on{fill.a.x + (fill.b.x - fill.a.x) * t, fill.a.y + (fill.b.y - fill.a.y) * t};
                    r.expect(toSegment(on, pane.a, pane.b) > kOnSide, "a gable fill lies over the glass");
                }
            }
        }

        const WorldPolygon& section = sections[static_cast<std::size_t>(roof.index)];
        std::vector<std::vector<Vec2>> rings{section.outline};
        rings.insert(rings.end(), section.holes.begin(), section.holes.end());
        for (const std::vector<Vec2>& ring : rings) {
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Vec2& a = ring[i];
                const Vec2& b = ring[(i + 1) % ring.size()];
                const double len = std::hypot(b.x - a.x, b.y - a.y);
                if (len <= kLongRun) continue;
                for (const double t : {0.27, 0.53, 0.77}) {
                    const Vec2 on{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
                    // The roof is on the left. Far enough in to be past rounding and the glass.
                    const double step = 1.5 * kOnSide / len;
                    const Vec2 inside{on.x - (b.y - a.y) * step, on.y + (b.x - a.x) * step};
                    double above = 0.0;
                    for (const RoofFace& face : roof.faces) {
                        if (face.part != RoofPart::Covering || face.outline.size() < 3) continue;
                        int within = 0;
                        for (const cl::PathD& path : seenFromAbove(face)) {
                            std::vector<Vec2> flat;
                            for (const cl::PointD& p : path) flat.push_back({p.x, p.y});
                            if (insidePolygon(inside, flat)) ++within;
                        }
                        if (within % 2 == 0) continue;
                        const Vec3 v = areaVector(face.outline);
                        const Vec3& o = face.outline[0];
                        if (v.z != 0.0) {
                            const double z = o.z - ((inside.x - o.x) * v.x + (inside.y - o.y) * v.y) / v.z;
                            above = std::max(above, z - kRoofThickness - base);
                        }
                    }
                    if (above <= 0.01) continue;
                    bool closed = false;
                    for (const std::vector<Upright>* faces : {&fills, &panes}) {
                        for (const Upright& face : *faces) {
                            if (toSegment(on, face.a, face.b) <= kOnSide && face.top >= base + above - 0.01) closed = true;
                        }
                    }
                    r.expect(closed, "a side under the roof has neither a fill nor glass");
                }
            }
        }
    }
}

// Expects no face of the part to stand over a plan position.
void expectNoneOver(Report& r, const Plan& plan, RoofPart part, const Vec2& planPosition, const std::string& what)
{
    const Vec2 p = flipped(planPosition);
    for (const RoofBuild& roof : floorRoofs(plan, 0, floorElevations(plan)).roofs) {
        for (const Upright& face : uprightFaces(roof, part)) r.expect(toSegment(p, face.a, face.b) > kOnSide, what);
    }
}

// An L whose notch has its outer face at y c + 0.1.
Plan notchedRoom(double c, double direction, double overhang)
{
    Plan plan = emptyPlan("sawtooth");
    plan.roof.direction = direction;
    plan.roof.overhang = overhang;
    addRoom(plan.floors[0], "l", addRing(plan.floors[0], "l", {{0, 0}, {5, 0}, {5, c}, {2.1, c}, {2.1, 5}, {0, 5}}));
    return plan;
}

}

TEST_CASE("edge cases a sawtooth side on a bay boundary keeps its glass", "[core][edges]")
{
    struct Side {
        Vec2 middle;
        bool low = false;
    };
    struct Shape {
        std::string name;
        Plan plan;
        std::vector<Side> onBoundary;
    };
    Report r;
    for (const double overhang : {0.0, 0.3}) {
        std::vector<Shape> shapes;
        shapes.push_back({"low L", notchedRoom(2.0, 0.0, overhang), {{{3.65, 2.1}, true}}});
        shapes.push_back({"high L", notchedRoom(2.8, 180.0, overhang), {{{3.65, 2.9}, false}}});
        shapes.push_back({"L off the boundary 0", notchedRoom(2.8, 0.0, overhang), {}});
        shapes.push_back({"L off the boundary 180", notchedRoom(2.0, 180.0, overhang), {}});
        // A courtyard has both sides on boundaries, on a ring that turns the other way.
        Plan yard = emptyPlan("sawtooth");
        yard.roof.overhang = overhang;
        std::vector<Id> walls = addRing(yard.floors[0], "a", box(0, 0, 7.5, 7.5));
        const std::vector<Id> inner = addRing(yard.floors[0], "b", box(2.2, 1.5, 5.1, 4.7));
        walls.insert(walls.end(), inner.begin(), inner.end());
        addRoom(yard.floors[0], "a", walls);
        shapes.push_back({"courtyard", std::move(yard), {{{3.65, 1.6}, true}, {{3.65, 4.6}, false}}});

        for (const Shape& shape : shapes) {
            const std::vector<double> wanted = areaByPart(shape.plan);
            const double perimeter = perimeterOf(pathsOf(inWorld(floorFootprint(shape.plan.floors[0]))));
            const double bound = 4.0 * (2.0 * kMerge * perimeter + 16.0 * kLeastArea);
            for (const double angle : {0.0, 30.0, 67.0, 70.0, 71.0}) {
                for (const Place& place : kPlaces) {
                    Plan plan = shape.plan;
                    turn(plan, angle, place);
                    r.begin(named("sawtooth " + shape.name + " overhang " + std::to_string(overhang), angle, place));
                    audit(r, plan);
                    checkSawtoothSides(r, plan);
                    const std::vector<double> got = areaByPart(plan);
                    for (std::size_t part = 0; part < wanted.size(); ++part) {
                        r.expect(std::fabs(got[part] - wanted[part]) <= bound,
                                 std::string(roofPartName(static_cast<RoofPart>(part))) + " changes its area");
                    }
                    for (const Side& side : shape.onBoundary) {
                        const Vec2 at = placed(side.middle, angle, place);
                        expectNoneOver(r, plan, RoofPart::Gable, at, "a gable fill stands on the bay boundary");
                        if (side.low && overhang == 0.0) {
                            expectNoneOver(r, plan, RoofPart::Glazing, at, "glass stands on a low side with no roof below it");
                        }
                    }
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a sawtooth low side a hairline off its boundary has no glass", "[core][edges]")
{
    Report r;
    for (const double off : {-9e-5, -1e-6, -1e-7, 0.0, 1e-7, 1e-6, 5e-6, 1.2e-5, 4e-5}) {
        for (const double angle : {0.0, 30.0, 67.0}) {
            for (const Place& place : {Place{}, Place{97.3, 88.1, 0.0}}) {
                Plan plan = notchedRoom(2.0 + off, 0.0, 0.0);
                turn(plan, angle, place);
                r.begin(named("low side off by " + std::to_string(off * 1e6) + " micrometres", angle, place));
                const Vec2 at = placed({3.65, 2.1 + off}, angle, place);
                expectNoneOver(r, plan, RoofPart::Glazing, at, "glass stands on a low side with no roof below it");
                expectNoneOver(r, plan, RoofPart::Gable, at, "a gable fill stands on a low side");
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a sawtooth high side in the band has glass or a fill", "[core][edges]")
{
    Report r;
    for (const bool innerBay : {false, true}) {
        for (const double off : {-5e-5, 0.0, 1e-6, 1.2e-5, 5e-5, 9e-5, 9.95e-5, 1.1e-4}) {
            for (const double angle : {0.0, 30.0, 67.0}) {
                for (const Place& place : {Place{}, Place{97.3, 88.1, 0.0}}) {
                    // Two arms, so the glass strip of the shorter one is a piece of its own.
                    Plan plan = emptyPlan("sawtooth");
                    plan.roof.direction = 180.0;
                    plan.roof.overhang = 0.0;
                    const double end = (innerBay ? 2.8 : 4.0) - off;
                    const std::vector<Vec2> ring{{0, 0}, {5, 0}, {5, end}, {3, end}, {3, 1.5}, {2, 1.5}, {2, 4}, {0, 4}};
                    addRoom(plan.floors[0], "u", addRing(plan.floors[0], "u", ring));
                    turn(plan, angle, place);
                    r.begin(named(std::string(innerBay ? "inner" : "last") + " bay high side off by " +
                                      std::to_string(off * 1e6) + " micrometres",
                                  angle, place));
                    checkSawtoothSides(r, plan);
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a short sawtooth high side has glass or a fill", "[core][edges]")
{
    struct Line {
        bool inner = false;
        double off = 0.0;
    };
    Report r;
    for (const Line& line : {Line{false, 0.0}, Line{true, 0.0}, Line{true, 1e-6}}) {
        for (const double wide : {0.005, 0.008, 0.02, 0.5}) {
            for (const double angle : {0.0, 30.0, 67.0}) {
                for (const Place& place : {Place{}, Place{97.3, 88.1, 0.0}}) {
                    // The floor above cuts the section to a short side on the high line.
                    Plan plan = emptyPlan("sawtooth");
                    if (line.inner) {
                        // The notch of an L is on the inner line at y 2.9. The other arm goes on to the last bay.
                        plan = notchedRoom(2.8 - line.off, 180.0, 0.0);
                        const double south = 2.8 - line.off;
                        // The centre line of the slanted wall is x - y = cut.
                        const double cut = 2.2 - wide - 0.1 * std::numbers::sqrt2;
                        Floor& upper = addFloor(plan);
                        addRoom(upper, "u", addRing(upper, "u", {{2.6, 1.6}, {cut + 1.6, 1.6}, {cut + south, south}, {2.6, south}}));
                    } else {
                        plan.roof.overhang = 0.0;
                        addBox(plan.floors[0], "a", 0, 0, 6, 4);
                        const double bend = 6.0 - wide - 0.1 * std::numbers::sqrt2;
                        Floor& upper = addFloor(plan);
                        addRoom(upper, "u", addRing(upper, "u", {{0, 0}, {bend, 0}, {bend - 4.0, 4}, {0, 4}}));
                    }
                    turn(plan, angle, place);
                    r.begin(named(std::string(line.inner ? "inner" : "last") + " line, " + std::to_string(line.off * 1e6) +
                                      " micrometres short of it, high side " + std::to_string(wide * 1e3) + " millimetres long",
                                  angle, place));
                    checkSawtoothSides(r, plan);
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a corner touching a sawtooth high side puts no fill over the glass", "[core][edges]")
{
    Report r;
    for (const double out : {-5e-5, -2e-5, -9e-6, -4e-6, -1e-6, 0.0, 4e-6}) {
        for (const double x : {2.0, 1.4}) {
            for (const double overhang : {0.0, 0.3}) {
                for (const double angle : {0.0, 30.0, 67.0}) {
                    for (const Place& place : {Place{}, Place{97.3, 88.1, 0.0}}) {
                        // The corner parts the glass, at x 2 right under the middle of the side.
                        Plan plan = emptyPlan("sawtooth");
                        plan.roof.direction = 180.0;
                        plan.roof.overhang = overhang;
                        addBox(plan.floors[0], "a", 0, 0, 4, 4);
                        const double y = 4.1 + out - 0.1 * std::numbers::sqrt2 - 1.0;
                        Floor& upper = addFloor(plan);
                        addRoom(upper, "d", addRing(upper, "d", {{x, y - 1}, {x + 1, y}, {x, y + 1}, {x - 1, y}}));
                        turn(plan, angle, place);
                        r.begin(named("corner at x " + std::to_string(x) + " out " + std::to_string(out * 1e6) + " overhang " +
                                          std::to_string(overhang),
                                      angle, place));
                        checkSawtoothSides(r, plan);
                    }
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a sawtooth step across a narrow tip has glass", "[core][edges]")
{
    Report r;
    for (const double past : {0.002, 0.004, 0.008, 0.02, 0.2}) {
        for (const double angle : {0.0, 30.0, 67.0}) {
            for (const Place& place : {Place{}, Place{97.3, 88.1, 0.0}}) {
                // The tip of a diamond reaches past a bay line. The step there is no side: only glass can close it.
                Plan plan = emptyPlan("sawtooth");
                plan.roof.overhang = 0.0;
                const double half = (3.0 + past) / 2.0 - 0.1 * std::numbers::sqrt2;
                addRoom(plan.floors[0], "d", addRing(plan.floors[0], "d", {{0, -half}, {half, 0}, {0, half}, {-half, 0}}));
                turn(plan, angle, place);
                r.begin(named("tip " + std::to_string(past * 1e3) + " millimetres past the line", angle, place));
                const std::vector<double> levels = floorElevations(plan);
                const double base = slabBuild(plan, 0, levels).top;
                const Vec2 on = flipped(placed({0.0, half + 0.1 * std::numbers::sqrt2 - 3.0}, angle, place));
                bool closed = false;
                for (const RoofBuild& roof : floorRoofs(plan, 0, levels).roofs) {
                    for (const Upright& pane : uprightFaces(roof, RoofPart::Glazing)) {
                        if (toSegment(on, pane.a, pane.b) <= kOnSide && pane.top >= base + 1.0) closed = true;
                    }
                }
                r.expect(closed, "the step between two bays is open");
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases a low last bay closes the end of its eaves", "[core][edges]")
{
    Report r;
    for (const double bay : {0.1, 0.2, 0.25}) {
        for (const double angle : {0.0, 30.0, 67.0}) {
            for (const Place& place : kPlaces) {
                // The last bay rises less than the roof is thick, so the glass from base up leaves fascia below it.
                Plan plan = emptyPlan("sawtooth");
                plan.roof.pitch = 30.0;
                plan.roof.overhang = 0.3;
                addBox(plan.floors[0], "a", 0, 0, 4, 2.8 + bay);
                turn(plan, angle, place);
                r.begin(named("last bay " + std::to_string(bay), angle, place));
                const std::vector<double> levels = floorElevations(plan);
                const double base = slabBuild(plan, 0, levels).top;
                const double rise = bay * std::tan(30.0 * std::numbers::pi / 180.0);
                REQUIRE(rise < kRoofThickness);
                // Halfway between the underside and the base.
                const double z = base + (rise - kRoofThickness) / 2.0;
                const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, levels).roofs;
                for (const double x : {-0.25, 4.25}) {
                    const Vec2 on = flipped(placed({x, -0.1}, angle, place));
                    bool closed = false;
                    for (const RoofBuild& roof : roofs) {
                        for (const RoofPart part : {RoofPart::Edge, RoofPart::Glazing}) {
                            for (const Upright& face : uprightFaces(roof, part)) {
                                if (toSegment(on, face.a, face.b) <= kOnSide && face.bottom <= z && face.top >= z) closed = true;
                            }
                        }
                    }
                    r.expect(closed, "the end of the eaves is open below the glass");
                }
            }
        }
    }
    requireNone(r);
}

namespace {

// The families of buildings the seeded sweep makes.
enum class Kind { Setback, Rings, LShape, UShape, Courtyard, Touching, Crossing, Curved, Stairs, Corners };

// A repeatable source of choices. The standard distributions differ between libraries, so it uses none.
class Dice {
public:
    explicit Dice(std::uint32_t seed) : engine_(seed) {}

    std::uint32_t below(std::uint32_t n) { return static_cast<std::uint32_t>(engine_()) % n; }
    bool chance(std::uint32_t percent) { return below(100) < percent; }
    double between(double low, double high)
    {
        return low + (high - low) * (static_cast<double>(engine_()) / 4294967296.0);
    }

    // An offset of 0 or 1e-7 to 0.01, clear of the merge distance so rounding cannot move it across.
    double offset()
    {
        static constexpr double kOffsets[] = {0.0, 1e-7, 1e-6, 3e-6, 5e-6, 9e-6, 2.5e-5, 5e-5, 1e-4, 1e-3, 1e-2};
        if (chance(70)) return kOffsets[below(static_cast<std::uint32_t>(std::size(kOffsets)))];
        if (chance(50)) return std::pow(10.0, between(-7.0, std::log10(5e-6)));
        return std::pow(10.0, between(std::log10(2.5e-5), -2.0));
    }

    // The offset either way or not at all.
    double maybe(double v)
    {
        const std::uint32_t pick = below(3);
        return pick == 0 ? 0.0 : (pick == 1 ? v : -v);
    }

private:
    std::mt19937 engine_;
};

// Adds a room that is everything between two rectangles.
void addYard(Floor& floor, double x0, double y0, double x1, double y1, double wide, double grow, double thickness)
{
    std::vector<Id> walls = addRing(floor, "a", box(x0 - grow, y0 - grow, x1 + grow, y1 + grow), thickness);
    const std::vector<Id> inner =
        addRing(floor, "b", box(x0 + wide + grow, y0 + wide + grow, x1 - wide - grow, y1 - wide - grow), thickness);
    walls.insert(walls.end(), inner.begin(), inner.end());
    addRoom(floor, "a", walls);
}

// One building of a family, before it is mirrored, turned and moved.
Plan randomShape(Kind kind, Dice& dice, double d)
{
    Plan plan = emptyPlan();
    static constexpr double kThicknesses[] = {0.1, 0.2, 0.2, 0.3};
    const double t = kThicknesses[dice.below(4)];
    const double w = dice.between(3.6, 5.6);
    const double h = dice.between(3.2, 5.4);

    switch (kind) {
    case Kind::Setback: {
        addBox(plan.floors[0], "a", 0, 0, w, h, t);
        const double set = dice.chance(30) ? dice.between(0.03, 0.19) : dice.between(0.5, 1.5);
        const double west = dice.maybe(d);
        const double north = dice.maybe(d);
        const double east = dice.maybe(d);
        const double south = dice.maybe(d);
        addBox(addFloor(plan), "a", west, north, w - set + east, h + south, t);
        break;
    }
    case Kind::Rings: {
        addBox(plan.floors[0], "a", 0, 0, w, h, t);
        const double grow = dice.chance(50) ? d : -d;
        if (dice.chance(50)) addBox(addFloor(plan), "a", -grow, -grow, w + grow, h + grow, t);
        else addBox(addFloor(plan), "a", grow, 0, w + grow, h, t);
        break;
    }
    case Kind::LShape: {
        const double cx = dice.between(1.6, w - 1.2);
        const double cy = dice.between(1.4, h - 1.2);
        addRoom(plan.floors[0], "l", addRing(plan.floors[0], "l", {{0, 0}, {w, 0}, {w, cy}, {cx, cy}, {cx, h}, {0, h}}, t));
        const std::uint32_t pick = dice.below(4);
        const double s = dice.maybe(d);
        if (pick == 1) addBox(addFloor(plan), "a", 0, s, cx + s, h, t);
        if (pick == 2) addBox(addFloor(plan), "a", s, 0, w, cy + s, t);
        if (pick == 3) addBox(addFloor(plan), "a", 0, 0, w + s, h + s, t);
        break;
    }
    case Kind::UShape: {
        const double arm = dice.between(1.5, 2.6);
        const std::uint32_t pick = dice.below(3);
        // The gap between the arms' faces: a slit, a slit between eaves, or open.
        const double g = t + (pick == 0 ? 0.6 + dice.maybe(d) : (pick == 1 ? 0.4 + d : dice.between(0.9, 1.5)));
        const double deep = dice.between(1.5, h - 1.2);
        const double r1 = 2 * arm + g;
        const std::vector<Vec2> ring{{0, 0}, {arm, 0}, {arm, deep}, {arm + g, deep}, {arm + g, 0}, {r1, 0}, {r1, h}, {0, h}};
        addRoom(plan.floors[0], "u", addRing(plan.floors[0], "u", ring, t));
        const std::uint32_t upper = dice.below(3);
        if (upper == 1) addBox(addFloor(plan), "a", 0, d, arm, h, t);
        if (upper == 2) addBox(addFloor(plan), "a", 0, 0, r1 + d, h, t);
        break;
    }
    case Kind::Courtyard: {
        const double side = dice.between(6.5, 9.5);
        const double wide = dice.between(1.4, 2.1);
        addYard(plan.floors[0], 0, 0, side, side, wide, 0.0, t);
        const std::uint32_t pick = dice.below(4);
        if (pick == 0) addBox(plan.floors[0], "c", wide + t + d, wide + 0.9, wide + t + d + 1.2, wide + 2.2, t);
        if (pick == 1) addYard(addFloor(plan), 0, 0, side, side, wide, dice.chance(50) ? d : -d, t);
        if (pick == 2) addBox(addFloor(plan), "a", 0, dice.maybe(d), side, wide, t);
        break;
    }
    case Kind::Touching: {
        Floor& f = plan.floors[0];
        addBox(f, "a", 0, 0, w, h, t);
        const double x = w + t + dice.maybe(d);
        const std::uint32_t pick = dice.below(4);
        if (pick == 0) {
            addPoint(f, "p", x, 0);
            addPoint(f, "q", x, h);
            addWall(f, "w9", "p", "q", t);
        } else {
            const double shift = pick == 1 ? 0.0 : (pick == 2 ? 1.0 : d);
            addBox(f, "b", x, shift, x + 3.4, h + shift, t);
        }
        const std::uint32_t upper = dice.below(3);
        if (upper == 1) addBox(addFloor(plan), "a", 0, 0, w, h, t);
        if (upper == 2) addBox(addFloor(plan), "a", 0, 0, x + (pick == 0 ? 0.0 : 3.4), h, t);
        break;
    }
    case Kind::Crossing: {
        Floor& f = plan.floors[0];
        const double s = dice.maybe(d);
        addBox(f, "a", 1.4, 0, 3.8, 5.6, t);
        if (dice.chance(50)) addBox(f, "b", 0, 1.6, 5.4, 3.9, t);
        else addBox(f, "b", 1.4 + s, 1.6, 5.4, 3.9, t);
        if (dice.chance(50)) addBox(addFloor(plan), "a", 1.4, s, 3.8, 5.6, t);
        break;
    }
    case Kind::Curved: {
        const double bulge = -dice.between(0.2, 0.9);
        const std::vector<double> bend{0.0, bulge, 0.0, 0.0};
        addRoom(plan.floors[0], "c", addRing(plan.floors[0], "c", box(0, 0, w, h), t, bend));
        const std::uint32_t pick = dice.below(4);
        if (pick == 1) addBox(addFloor(plan), "a", 0, dice.maybe(d), w - 1, h, t);
        if (pick == 2) {
            Floor& upper = addFloor(plan);
            addRoom(upper, "c", addRing(upper, "c", box(d, 0, w + d, h), t, bend));
        }
        if (pick == 3) addBox(addFloor(plan), "a", 0, 0, w + d, h, t);
        break;
    }
    case Kind::Stairs: {
        Stair stair;
        stair.id = "s1";
        static const char* const kTypes[] = {"straight", "l_shape", "u_shape", "spiral"};
        stair.type = kTypes[dice.below(4)];
        stair.x = dice.between(1.0, w - 1.0);
        stair.y = dice.between(0.3, 1.6);
        stair.rotation = 90.0 * dice.below(4);
        addBox(plan.floors[0], "a", 0, 0, w, h, t);
        plan.floors[0].stairs.push_back(stair);
        Floor& upper = addFloor(plan);
        if (dice.chance(75)) addBox(upper, "a", 0, 0, w - (dice.chance(50) ? 1.0 : d), h, t);
        if (stair.type == "straight" && stair.rotation == 0.0 && dice.chance(80)) {
            const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
            double right = -1e18;
            for (const Vec2& p : layout.footprint) right = std::max(right, p.x);
            plan.floors[0].stairs[0].x += w + t / 2.0 - d - right;
        }
        break;
    }
    case Kind::Corners: {
        const double s = dice.maybe(d);
        if (dice.chance(50)) {
            addBox(plan.floors[0], "a", 0, 0, 4, 4, 0.2);
            const double y = 4.1 + s - 0.1 * std::numbers::sqrt2 - 1.0;
            Floor& upper = addFloor(plan);
            addRoom(upper, "d", addRing(upper, "d", {{2, y - 1}, {3, y}, {2, y + 1}, {1, y}}));
        } else {
            addBox(plan.floors[0], "a", 0, 0, w, h, t);
            addBox(plan.floors[0], "b", w + t + s, h + t + s, w + t + s + 3.2, h + t + s + 3.4, t);
        }
        break;
    }
    }
    return plan;
}

// Adds a copy of the ground floor on top, moved sideways by a hairline.
void addCopyOnTop(Plan& plan, double shift)
{
    Floor copy = plan.floors.front();
    copy.id = "f" + std::to_string(plan.floors.size() + 1);
    copy.stairs.clear();
    for (Point& p : copy.points) p.x += shift;
    plan.floors.push_back(std::move(copy));
}

// One building of a sweep before it is turned, with the turn and the place it then gets.
struct SeedBuilding {
    Plan plan;
    double angle = 0.0;
    Place place;
    std::string label;
};

// Makes one building of a family from its seed, not yet turned. index is its place in the sweep.
SeedBuilding seedBuilding(Kind kind, std::uint32_t own, int i)
{
    static constexpr double kPitches[] = {5.0, 20.0, 30.0, 30.0, 45.0, 75.0};
    static constexpr double kOverhangs[] = {0.0, 0.3, 0.3, 0.6};
    static constexpr double kSlabs[] = {0.05, 0.1, 0.2, 0.3};
    static constexpr int kAngles[] = {0, 5, 20, 30, 45, 50, 80, 90, 180, 270};
    const std::vector<std::string> types = roofTypeIds();
    Dice dice(own);
    const double d = dice.offset();
    Plan plan = randomShape(kind, dice, d);
    if (dice.chance(15)) addCopyOnTop(plan, d);
    if (kind != Kind::Stairs && dice.chance(30)) {
        for (Floor& floor : plan.floors) {
            for (Point& p : floor.points) p.x = -p.x;
            for (Wall& wall : floor.walls) wall.bulge = -wall.bulge;
        }
    }
    const std::string& type = types[static_cast<std::size_t>(i) % types.size()];
    plan.roof.type = type;
    plan.roof.pitch = kPitches[dice.below(6)];
    plan.roof.overhang = kOverhangs[dice.below(4)];
    plan.roof.parapet = dice.chance(50) ? 0.6 : 0.0;
    plan.roof.direction = dice.chance(50) ? 0.0 : 90.0;
    for (Floor& floor : plan.floors) floor.slabThickness = kSlabs[dice.below(4)];

    const double angle = dice.chance(50) ? static_cast<double>(kAngles[dice.below(10)])
                                         : (dice.chance(50) ? static_cast<double>(dice.below(360)) : dice.between(0.0, 360.0));
    Place place;
    if (dice.chance(75)) {
        place.x = dice.between(-100.0, 100.0);
        place.y = dice.between(-100.0, 100.0);
    }
    if (dice.chance(70)) place.grid = 1e-6;

    std::ostringstream what;
    what.precision(17);
    what << "seed " << own << " " << type << ", offset " << d << ", overhang " << plan.roof.overhang << ", parapet "
         << plan.roof.parapet;
    SeedBuilding out;
    out.angle = angle;
    out.place = place;
    out.label = named(what.str(), angle, place);
    out.plan = std::move(plan);
    return out;
}

// Builds one building of a family from its seed and judges it by the rules. index is its place in the sweep.
void checkSeed(Report& r, Kind kind, std::uint32_t own, int i)
{
    SeedBuilding building = seedBuilding(kind, own, i);
    turn(building.plan, building.angle, building.place);
    r.begin(building.label);
    audit(r, building.plan, kind != Kind::Corners);
}

// Builds count buildings of one family from its seed and judges each by the rules.
void sweep(Kind kind, std::uint32_t seed, int count)
{
    REQUIRE(allRoofTypes().size() >= 5);
    Report r;
    for (int i = 0; i < count; ++i) checkSeed(r, kind, seed + static_cast<std::uint32_t>(i), i);
    requireNone(r);
}

// Builds named buildings of the sweeps again, each in a section with its seed.
void checkNamedSeeds(const std::vector<std::uint32_t>& seeds)
{
    for (const std::uint32_t own : seeds) {
        DYNAMIC_SECTION("seed " << own)
        {
            INFO("seed " << own);
            // Each family counts from a thousand of its own, setbacks from 11000 to touching corners from 20000.
            REQUIRE(own >= 11000);
            REQUIRE(own < 21000);
            Report r;
            checkSeed(r, static_cast<Kind>(own / 1000 - 11), own, static_cast<int>(own % 1000));
            requireNone(r);
        }
    }
}

}

TEST_CASE("edge cases seeded sweep of setbacks", "[core][edges]")
{
    sweep(Kind::Setback, 11000, 300);
}

TEST_CASE("edge cases seeded sweep of hairline rings and offsets", "[core][edges]")
{
    sweep(Kind::Rings, 12000, 300);
}

TEST_CASE("edge cases seeded sweep of L shapes", "[core][edges]")
{
    sweep(Kind::LShape, 13000, 250);
}

TEST_CASE("edge cases seeded sweep of U shapes", "[core][edges]")
{
    sweep(Kind::UShape, 14000, 250);
}

TEST_CASE("edge cases seeded sweep of courtyards", "[core][edges]")
{
    sweep(Kind::Courtyard, 15000, 250);
}

TEST_CASE("edge cases seeded sweep of touching pieces", "[core][edges]")
{
    sweep(Kind::Touching, 16000, 250);
}

TEST_CASE("edge cases seeded sweep of crossing pieces", "[core][edges]")
{
    sweep(Kind::Crossing, 17000, 200);
}

TEST_CASE("edge cases seeded sweep of curved walls", "[core][edges]")
{
    sweep(Kind::Curved, 18000, 120);
}

TEST_CASE("edge cases seeded sweep of stair holes", "[core][edges]")
{
    sweep(Kind::Stairs, 19000, 250);
}

TEST_CASE("edge cases seeded sweep of touching corners", "[core][edges]")
{
    sweep(Kind::Corners, 20000, 250);
}

TEST_CASE("edge cases named seeds where a hole lost its owner", "[core][edges][seeds]")
{
    checkNamedSeeds({12191, 15070, 15118});
}

TEST_CASE("edge cases named seeds where coverings touch at a point", "[core][edges][seeds]")
{
    checkNamedSeeds({13057, 13122, 13232, 14227, 18012, 19027, 19057, 19132});
}

TEST_CASE("edge cases named seeds where a hole became a roof", "[core][edges][seeds]")
{
    checkNamedSeeds({20002, 20020, 20055, 20075, 20085, 20094, 20139, 20208});
}

TEST_CASE("edge cases named seeds where collision or eaves left the slab and section", "[core][edges][seeds]")
{
    checkNamedSeeds({11173, 18034, 18064, 18119, 20040});
}

TEST_CASE("edge cases named seeds where rings touch within rounding", "[core][edges][seeds]")
{
    checkNamedSeeds({20002, 20063, 20088, 20092, 20218});
}

TEST_CASE("edge cases named seeds with a hairline next to a wide part", "[core][edges][seeds]")
{
    checkNamedSeeds({11193, 12088, 12191, 13227, 15070, 18040, 19103});
}

TEST_CASE("edge cases named seeds of curved hips", "[core][edges][seeds]")
{
    checkNamedSeeds({18057, 19062, 20037});
    for (const int radius : {2, 4}) {
        DYNAMIC_SECTION("curved hip radius " << radius)
        {
            const double bulge = -radius * (1.0 - std::sqrt(0.5));
            const double out = radius;
            Plan plan = emptyPlan("hip");
            const std::vector<Vec2> ring{{0, -out}, {out, 0}, {0, out}, {-out, 0}};
            addRoom(plan.floors[0], "o", addRing(plan.floors[0], "o", ring, 0.2, {bulge, bulge, bulge, bulge}));
            turn(plan, 30.0);
            Report r;
            r.begin(named("curved hip radius " + std::to_string(radius), 30.0, {}));
            audit(r, plan);
            requireNone(r);
        }
    }
}

namespace {

// The area the covering faces of the lowest floor cover, seen from above.
double coveringSeenFromAbove(const Plan& plan)
{
    cl::PathsD covering;
    for (const RoofBuild& roof : floorRoofs(plan, 0, floorElevations(plan)).roofs) {
        for (const RoofFace& face : roof.faces) {
            if (face.part != RoofPart::Covering || face.outline.size() < 3) continue;
            const cl::PathsD seen = seenFromAbove(face);
            covering.insert(covering.end(), seen.begin(), seen.end());
        }
    }
    return areaOf(united(covering));
}

// The area bound of the turning test for a plan.
double turningBound(const Plan& plan)
{
    double perimeter = 0.0;
    for (const Floor& floor : plan.floors) perimeter += perimeterOf(pathsOf(inWorld(floorFootprint(floor))));
    return 4.0 * (2.0 * kMerge * perimeter + 16.0 * kLeastArea);
}

// The area of the diamond of the floor above, to its outer faces.
double diamondArea()
{
    const double half = 1.0 + 0.1 * std::numbers::sqrt2;
    return 2.0 * half * half;
}

}

TEST_CASE("edge cases the diamond roof covers the same turned and straight", "[core][edges]")
{
    // The ground floor's eaves, 4.2 m and twice the overhang of 0.3 square, less the diamond above.
    const double wanted = 4.8 * 4.8 - diamondArea();
    Report r;
    for (const double out : {0.0, 1e-7, 4e-6}) {
        for (const char* type : {"flat", "gable", "hip", "shed"}) {
            Plan straight = emptyPlan(type);
            straight.roof.overhang = 0.3;
            straight.roof.parapet = 0.0;
            addBox(straight.floors[0], "a", 0, 0, 4, 4);
            Floor& upper = addFloor(straight);
            const double y = 4.1 + out - 0.1 * std::numbers::sqrt2 - 1.0;
            addRoom(upper, "d", addRing(upper, "d", {{2, y - 1}, {3, y}, {2, y + 1}, {1, y}}));
            const double bound = turningBound(straight);
            const double flat = coveringSeenFromAbove(straight);
            for (const double angle : {0.0, 30.0}) {
                for (const Place& place : {Place{}, Place{97.3, 88.1, 1e-6}}) {
                    Plan plan = straight;
                    turn(plan, angle, place);
                    r.begin(named("diamond out " + std::to_string(out * 1e6) + " " + type, angle, place));
                    const double got = coveringSeenFromAbove(plan);
                    r.expect(std::fabs(flat - wanted) <= bound, "the straight covering is not the eaves less the diamond");
                    r.expect(std::fabs(got - wanted) <= bound, "the covering is not the eaves less the diamond");
                    r.expect(std::fabs(got - flat) <= bound, "the covering differs from the straight one");
                }
            }
        }
    }
    requireNone(r);
}

TEST_CASE("edge cases seed 20103 covers the same turned and straight", "[core][edges]")
{
    // The sweep makes it the diamond with a shed roof and an overhang of 0.6: eaves 5.4 m square less the diamond.
    const double wanted = 5.4 * 5.4 - diamondArea();
    SeedBuilding building = seedBuilding(Kind::Corners, 20103, 103);
    INFO(building.label);
    REQUIRE(building.plan.roof.type == "shed");
    REQUIRE(building.plan.roof.overhang == 0.6);
    REQUIRE(building.plan.floors.size() == 2);
    const double bound = turningBound(building.plan);
    const double straight = coveringSeenFromAbove(building.plan);
    Plan turned = building.plan;
    turn(turned, building.angle, building.place);
    const double got = coveringSeenFromAbove(turned);
    INFO("straight " << straight << ", turned " << got << ", wanted " << wanted << ", bound " << bound);
    CHECK(std::fabs(straight - wanted) <= bound);
    CHECK(std::fabs(got - wanted) <= bound);
    CHECK(std::fabs(got - straight) <= bound);
}

TEST_CASE("edge cases a sawtooth keeps no sliver on its high line", "[core][edges]")
{
    Report r;
    for (const double direction : {0.0, 180.0}) {
        for (const double overhang : {0.0, 0.3}) {
            // The upper floor's outer face runs on along the high line of the roof beside it.
            Plan straight = setback("sawtooth");
            straight.roof.direction = direction;
            straight.roof.overhang = overhang;
            straight.roof.parapet = 0.0;
            const std::vector<double> wanted = areaByPart(straight);
            const double bound = turningBound(straight);
            for (const double angle : {0.0, 7.0, 20.0, 67.0, 70.0, 71.0}) {
                for (const Place& place : {Place{0.0, 0.0, 1e-6}, Place{97.3, 88.1, 1e-6}}) {
                    Plan plan = straight;
                    turn(plan, angle, place);
                    r.begin(named("sawtooth setback direction " + std::to_string(direction) + " overhang " +
                                      std::to_string(overhang),
                                  angle, place));
                    audit(r, plan);
                    const std::vector<double> got = areaByPart(plan);
                    for (std::size_t part = 0; part < wanted.size(); ++part) {
                        r.expect(std::fabs(got[part] - wanted[part]) <= bound,
                                 std::string(roofPartName(static_cast<RoofPart>(part))) + " changes its area");
                    }

                    const std::vector<WorldPolygon> sections = inWorld(roofSectionAreas(plan, 0));
                    const cl::PathsD above = pathsOf(inWorld(floorFootprint(plan.floors[1])));
                    const Vec2 d = roofDirection(plan.roof);
                    const Vec2 n{-d.y, d.x};
                    for (const RoofBuild& roof : floorRoofs(plan, 0, floorElevations(plan)).roofs) {
                        for (const RoofFace& face : roof.faces) {
                            r.expect(face.outline.size() >= 3 && faceArea(face) >= 0.999 * kLeastArea, "a face is under the least area");
                        }
                        if (roof.index < 0 || static_cast<std::size_t>(roof.index) >= sections.size()) {
                            r.fail("a roof has no section");
                            continue;
                        }
                        const WorldPolygon& section = sections[static_cast<std::size_t>(roof.index)];
                        double high = -1e18;
                        for (const Vec2& p : section.outline) high = std::max(high, n.x * p.x + n.y * p.y);
                        const cl::PathsD eaves = wantedCovering(roof, section, above, plan.roof);
                        for (const Upright& edge : uprightFaces(roof, RoofPart::Edge)) {
                            const double off = std::min(std::fabs(n.x * edge.a.x + n.y * edge.a.y - high),
                                                        std::fabs(n.x * edge.b.x + n.y * edge.b.y - high));
                            if (off > kOnSide) continue;
                            const Vec2 middle{(edge.a.x + edge.b.x) / 2.0, (edge.a.y + edge.b.y) / 2.0};
                            const bool onEaves = toPaths(edge.a, eaves) <= kSideTolerance && toPaths(edge.b, eaves) <= kSideTolerance &&
                                                 toPaths(middle, eaves) <= kSideTolerance;
                            r.expect(onEaves, "an Edge face by the high line is off the eaves");
                        }
                    }
                }
            }
        }
    }
    requireNone(r);
}

namespace {

// The paths turned by angle degrees about the origin and moved to a place.
cl::PathsD placedPaths(const cl::PathsD& paths, double angle, const Vec2& at)
{
    const double c = std::cos(angle * std::numbers::pi / 180.0);
    const double s = std::sin(angle * std::numbers::pi / 180.0);
    cl::PathsD out;
    for (const cl::PathD& path : paths) {
        cl::PathD moved;
        for (const cl::PointD& p : path) moved.emplace_back(p.x * c - p.y * s + at.x, p.x * s + p.y * c + at.y);
        out.push_back(std::move(moved));
    }
    return out;
}

// A rectangle as a path that turns left.
cl::PathsD rect(double x0, double y0, double x1, double y1)
{
    return {pathOf({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true)};
}

// The rectangle with a square hole.
cl::PathsD rectWithHole(double x0, double y0, double x1, double y1, double hx, double hy, double side)
{
    cl::PathsD out = rect(x0, y0, x1, y1);
    out.push_back(pathOf({{hx, hy}, {hx + side, hy}, {hx + side, hy + side}, {hx, hy + side}}, false));
    return out;
}

// Says whether the deep rule fails a planted pair at every place and turning, and whether it does so at all.
struct Planted {
    int fails = 0;
    int runs = 0;
};

// Judges one planted pair with a bound of 100, so that only the deep rule judges.
Planted judgePlanted(const std::string& name, const cl::PathsD& got, const cl::PathsD& wanted, bool cleaned)
{
    Planted out;
    for (const double angle : {0.0, 30.0}) {
        for (const Vec2& at : {Vec2{0.0, 0.0}, Vec2{97.3, 88.1}}) {
            Report r;
            r.begin(name);
            expectSame(r, placedPaths(got, angle, at), placedPaths(wanted, angle, at), 100.0, name, cleaned);
            ++out.runs;
            if (r.failed > 0) ++out.fails;
        }
    }
    return out;
}

// Expects a planted pair to fail at every place.
void mustFail(const std::string& name, const cl::PathsD& got, const cl::PathsD& wanted, bool cleaned)
{
    const Planted p = judgePlanted(name, got, wanted, cleaned);
    INFO(name << " cleaned " << cleaned);
    CHECK(p.fails == p.runs);
}

// Expects a planted pair to pass at every place.
void mustPass(const std::string& name, const cl::PathsD& got, const cl::PathsD& wanted, bool cleaned)
{
    const Planted p = judgePlanted(name, got, wanted, cleaned);
    INFO(name << " cleaned " << cleaned);
    CHECK(p.fails == 0);
}

}

TEST_CASE("edge cases planted pairs the deep rule must fail", "[core][edges]")
{
    const cl::PathsD square = rect(0, 0, 4, 4);
    mustFail("corner cut with legs of 1e-4", minus(square, {pathOf({{0, 0}, {1e-4, 0}, {0, 1e-4}}, true)}), square, true);
    const double half = std::tan(2.5 * std::numbers::pi / 180.0);
    const cl::PathsD wedge{pathOf({{0, 0}, {1, -half}, {1, half}}, true)};
    mustFail("last 2 mm of a 5 degree wedge", minus(wedge, rect(-1, -1, 0.002, 1)), wedge, true);
    mustFail("lost strip 4 by 2.7e-5", {}, rect(0, 0, 4, 2.7e-5), true);
    mustFail("lost strip 0.1 by 3.4e-5", {}, rect(0, 0, 0.1, 3.4e-5), true);
    mustFail("lost strip 0.06 by 3.4e-5", {}, rect(0, 0, 0.06, 3.4e-5), true);
    mustFail("lost square of 1.1e-3", {}, rect(0, 0, 1.1e-3, 1.1e-3), true);
    mustFail("filled square hole of 1.1e-3", rect(0, 0, 1, 1), rectWithHole(0, 0, 1, 1, 0.4, 0.4, 1.1e-3), true);
    mustFail("lost 4 m roof", {}, square, true);
    const double small = std::sqrt(9e-7);
    mustFail("square of 9e-7 half lost", rect(0, 0, small / 2.0, small), rect(0, 0, small, small), true);
    mustFail("square of 9e-7 lost, not cleaned", {}, rect(0, 0, small, small), false);
    mustFail("square of 9e-7 filled, not cleaned", rect(0, 0, 1, 1), rectWithHole(0, 0, 1, 1, 0.4, 0.4, small), false);
}

TEST_CASE("edge cases planted pairs the deep rule must pass", "[core][edges]")
{
    const double small = std::sqrt(9e-7);
    mustPass("square of 9e-7 lost", {}, rect(0, 0, small, small), true);
    mustPass("square of 9e-7 filled", rect(0, 0, 1, 1), rectWithHole(0, 0, 1, 1, 0.4, 0.4, small), true);
    for (const bool cleaned : {true, false}) {
        mustPass("lost strip 4 by 9e-7", {}, rect(0, 0, 4, 9e-7), cleaned);
        mustPass("4 m side moved by 2e-5", rect(0, 0, 4 - 2e-5, 4), rect(0, 0, 4, 4), cleaned);
    }
}

TEST_CASE("edge cases the ledge crumbs of the curved hips pass", "[core][edges]")
{
    for (const double radius : {2.0, 4.0}) {
        const double bulge = -radius * (1.0 - std::sqrt(0.5));
        Plan plan = emptyPlan("hip");
        const std::vector<Vec2> ring{{0, -radius}, {radius, 0}, {0, radius}, {-radius, 0}};
        addRoom(plan.floors[0], "o", addRing(plan.floors[0], "o", ring, 0.2, {bulge, bulge, bulge, bulge}));
        turn(plan, 30.0);
        const cl::PathsD open = pathsOf(inWorld(exposedArea(plan, 0)));
        const cl::PathsD roofed = pathsOf(inWorld(roofSectionAreas(plan, 0)));
        const cl::PathsD tops = pathsOf(slabBuild(plan, 0, floorElevations(plan)).tops);
        Report r;
        r.begin("crumbs");
        expectSame(r, tops, minus(open, roofed), 100.0, "ledge crumbs radius " + std::to_string(radius), true);
        INFO("radius " << radius);
        CHECK(r.failed == 0);
    }
}
