#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/WallGeometry.h>

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
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

namespace cl = Clipper2Lib;

constexpr double kTouch = 1e-6;

// Adds a point to a floor.
void addPoint(Floor& f, const std::string& id, double x, double y)
{
    f.points.push_back({id, x, y, 0.0});
}

// Adds a straight wall to a floor, with its own height when one is given.
void addWall(Floor& f, const std::string& id, const std::string& from, const std::string& to,
             std::optional<double> height = std::nullopt, double thickness = 0.2)
{
    Wall w;
    w.id = id;
    w.from = from;
    w.to = to;
    w.thickness = thickness;
    w.height = height;
    f.walls.push_back(w);
}

// Turns every point of the floor about the origin by an angle in degrees and moves it.
void turnFloor(Floor& f, double degrees)
{
    const double a = degrees * std::numbers::pi / 180.0;
    for (Point& p : f.points) {
        const double x = p.x;
        const double y = p.y;
        p.x = x * std::cos(a) - y * std::sin(a) + 2.5;
        p.y = x * std::sin(a) + y * std::cos(a) - 1.5;
    }
}

// A window 0.6 by 1.2 on a wall at an offset.
Opening windowOn(const std::string& id, const std::string& wall, double offset)
{
    Opening o;
    o.id = id;
    o.wall = wall;
    o.offset = offset;
    o.type = OpeningType::Window;
    o.width = 0.6;
    o.height = 1.2;
    o.sill = 0.9;
    return o;
}

// The owner's case: an 8 by 6 m room of 3 m walls, cut across the middle by a 0.08 m wall that joins the
// long walls at p5 and p6, with 14 windows on the 3 m walls. With withLow false the low wall is left out.
Floor kerbRoom(double lowThickness, bool withLow = true)
{
    Floor f;
    f.id = "f1";
    f.ceilingHeight = 3.0;
    addPoint(f, "p1", 0.0, 0.0);
    addPoint(f, "p5", 4.0, 0.0);
    addPoint(f, "p2", 8.0, 0.0);
    addPoint(f, "p3", 8.0, 6.0);
    addPoint(f, "p6", 4.0, 6.0);
    addPoint(f, "p4", 0.0, 6.0);
    addWall(f, "w1", "p1", "p5");
    addWall(f, "w2", "p5", "p2");
    addWall(f, "w3", "p2", "p3");
    addWall(f, "w4", "p3", "p6");
    addWall(f, "w5", "p6", "p4");
    addWall(f, "w6", "p4", "p1");
    if (withLow) addWall(f, "w7", "p5", "p6", 0.08, lowThickness);

    int n = 0;
    const auto add = [&](const std::string& wall, std::vector<double> offsets) {
        for (const double offset : offsets) f.openings.push_back(windowOn("o" + std::to_string(++n), wall, offset));
    };
    add("w1", {1.0, 2.8});
    add("w2", {1.0, 2.8});
    add("w3", {1.2, 3.0, 4.8});
    add("w4", {1.0, 2.8});
    add("w5", {1.0, 2.8});
    add("w6", {1.2, 3.0, 4.8});
    return f;
}

// Which side of the line from a to b a point is on, by the sign.
double orientation(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// The smallest distance between two straight segments, 0 when they cross.
double segmentGap(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    if (orientation(a, b, c) * orientation(a, b, d) < 0.0 && orientation(c, d, a) * orientation(c, d, b) < 0.0) return 0.0;
    return std::min({projectOntoSegment(a, c, d).distance, projectOntoSegment(b, c, d).distance,
                     projectOntoSegment(c, a, b).distance, projectOntoSegment(d, a, b).distance});
}

// The smallest distance between an edge of one ring and an edge of another.
double ringGap(const std::vector<Vec2>& r, const std::vector<Vec2>& s)
{
    double best = 1e300;
    for (std::size_t i = 0; i < r.size(); ++i) {
        for (std::size_t j = 0; j < s.size(); ++j) {
            best = std::min(best, segmentGap(r[i], r[(i + 1) % r.size()], s[j], s[(j + 1) % s.size()]));
        }
    }
    return best;
}

// What is wrong with a ring as the outline of a face, or nothing: too few points, two points on one spot, or
// two edges that are not neighbours touching or crossing.
std::string ringFault(const std::vector<Vec2>& ring)
{
    const std::size_t n = ring.size();
    if (n < 3) return "fewer than three points";
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (distance(ring[i], ring[j]) < kTouch) return "two points on one spot";
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (segmentGap(ring[i], ring[(i + 1) % n], ring[j], ring[(j + 1) % n]) < 1e-7) return "two edges touch";
        }
    }
    return {};
}

// What is wrong with the prism Blender raises from a solid, or nothing. The rings must be simple, the holes
// inside the outline and apart, and with points on one spot merged every edge of the prism must have exactly
// two faces: the solid is closed and manifold.
std::string solidFault(const WallSolid& s)
{
    if (!(s.height > 0.0)) return "no height";
    std::vector<const WallRing*> rings{&s.outline};
    for (const WallRing& h : s.holes) rings.push_back(&h);
    for (const WallRing* r : rings) {
        if (r->edges.size() != r->points.size()) return "the edges do not match the points";
        if (std::string fault = ringFault(r->points); !fault.empty()) return fault;
    }
    if (!(signedArea(s.outline.points) > 0.0)) return "the outline does not run anticlockwise";
    for (std::size_t i = 0; i < s.holes.size(); ++i) {
        const std::vector<Vec2>& hole = s.holes[i].points;
        if (!(signedArea(hole) < 0.0)) return "a hole does not run clockwise";
        for (const Vec2& p : hole) {
            if (!insidePolygon(p, s.outline.points)) return "a hole leaves the outline";
        }
        if (ringGap(hole, s.outline.points) < 1e-7) return "a hole touches the outline";
        for (std::size_t j = i + 1; j < s.holes.size(); ++j) {
            if (ringGap(hole, s.holes[j].points) < 1e-7) return "two holes touch";
            if (insidePolygon(s.holes[j].points.front(), hole)) return "a hole is inside a hole";
        }
    }

    std::map<std::tuple<double, double, double>, int> ids;
    const auto id = [&](Vec2 p, double z) {
        const auto key = std::make_tuple(p.x, p.y, z);
        const auto found = ids.find(key);
        if (found != ids.end()) return found->second;
        const int next = static_cast<int>(ids.size());
        ids.emplace(key, next);
        return next;
    };
    std::map<std::pair<int, int>, int> faces;
    const auto edge = [&](int a, int b) { ++faces[{std::min(a, b), std::max(a, b)}]; };
    for (const WallRing* r : rings) {
        const std::size_t n = r->points.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 p = r->points[i];
            const Vec2 q = r->points[(i + 1) % n];
            const int b0 = id(p, 0.0);
            const int b1 = id(q, 0.0);
            const int t0 = id(p, s.height);
            const int t1 = id(q, s.height);
            edge(b0, b1);
            edge(b1, t1);
            edge(t1, t0);
            edge(t0, b0);
            edge(b0, b1);
            edge(t0, t1);
        }
    }
    for (const auto& [key, count] : faces) {
        if (count != 2) return "an edge of the prism does not have two faces";
    }
    return {};
}

// A solid as Clipper paths: the outline and its holes.
cl::PathsD pathsOf(const WallSolid& s)
{
    cl::PathsD paths;
    const auto add = [&](const std::vector<Vec2>& ring) {
        cl::PathD path;
        for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
        paths.push_back(std::move(path));
    };
    add(s.outline.points);
    for (const WallRing& h : s.holes) add(h.points);
    return paths;
}

// The area two solids share, seen from above.
double sharedArea(const WallSolid& a, const WallSolid& b)
{
    return std::fabs(cl::Area(cl::Intersect(pathsOf(a), pathsOf(b), cl::FillRule::NonZero, 8)));
}

// The area of a solid seen from above: the outline less its holes.
double areaOf(const WallSolid& s)
{
    double area = signedArea(s.outline.points);
    for (const WallRing& h : s.holes) area += signedArea(h.points);
    return area;
}

// The area every wall band of the floor covers together.
double bandsArea(const Floor& f)
{
    cl::PathsD bands;
    for (const Wall& w : f.walls) {
        for (const std::vector<Vec2>& band : wallOutline(f, w)) {
            cl::PathD path;
            for (const Vec2& p : band) path.emplace_back(p.x, p.y);
            bands.push_back(std::move(path));
        }
    }
    return std::fabs(cl::Area(cl::Union(bands, cl::FillRule::NonZero, 8)));
}

// Every point of the rings of a solid.
std::vector<Vec2> cornersOf(const WallSolid& s)
{
    std::vector<Vec2> points = s.outline.points;
    for (const WallRing& h : s.holes) points.insert(points.end(), h.points.begin(), h.points.end());
    return points;
}

// The first corner of one solid that lies on an edge of another solid without being one of that edge's own
// end points, exactly. Empty when the solids meet only corner to corner.
std::optional<Vec2> partialEdgeCorner(const WallSolid& a, const WallSolid& b)
{
    std::vector<const WallRing*> rings{&b.outline};
    for (const WallRing& h : b.holes) rings.push_back(&h);
    for (const Vec2& p : cornersOf(a)) {
        for (const WallRing* r : rings) {
            const std::size_t n = r->points.size();
            for (std::size_t i = 0; i < n; ++i) {
                const Vec2 e0 = r->points[i];
                const Vec2 e1 = r->points[(i + 1) % n];
                if (projectOntoSegment(p, e0, e1).distance >= kTouch) continue;
                if (!(p == e0) && !(p == e1)) return p;
            }
        }
    }
    return std::nullopt;
}

// Checks that every wall solid of the floor is a closed manifold prism, that no two solids overlap, that
// solids that touch meet corner to corner with no corner in the middle of another's edge, and that together
// they cover the wall bands. Returns the solids.
std::vector<WallSolid> requireCleanSolids(const Floor& f)
{
    const std::vector<WallSolid> solids = wallSolids(f);
    REQUIRE_FALSE(solids.empty());
    double total = 0.0;
    for (std::size_t i = 0; i < solids.size(); ++i) {
        INFO("solid " << i << " height " << solids[i].height);
        CHECK(solidFault(solids[i]) == "");
        total += areaOf(solids[i]);
        for (std::size_t j = 0; j < solids.size(); ++j) {
            if (i == j) continue;
            INFO("against solid " << j << " height " << solids[j].height);
            if (j > i) CHECK(sharedArea(solids[i], solids[j]) < 1e-6);
            const std::optional<Vec2> corner = partialEdgeCorner(solids[i], solids[j]);
            const std::string where = corner ? std::to_string(corner->x) + " " + std::to_string(corner->y) : "";
            CHECK(where == "");
        }
    }
    CHECK(total == Approx(bandsArea(f)).margin(1e-6));
    return solids;
}

// Whether a point is one of the corners of a solid, exactly.
bool hasCorner(const WallSolid& s, Vec2 p)
{
    const std::vector<Vec2> corners = cornersOf(s);
    return std::find(corners.begin(), corners.end(), p) != corners.end();
}

// The solids of one height.
std::vector<WallSolid> ofHeight(const std::vector<WallSolid>& solids, double height)
{
    std::vector<WallSolid> out;
    for (const WallSolid& s : solids) {
        if (std::fabs(s.height - height) < 1e-9) out.push_back(s);
    }
    return out;
}

// Whether every corner of a lower solid that touches a taller solid is a corner of it.
bool lowCornersOnTall(const std::vector<WallSolid>& low, const std::vector<WallSolid>& tall)
{
    for (const WallSolid& l : low) {
        for (const Vec2& p : cornersOf(l)) {
            for (const WallSolid& t : tall) {
                std::vector<const WallRing*> rings{&t.outline};
                for (const WallRing& h : t.holes) rings.push_back(&h);
                bool onEdge = false;
                for (const WallRing* r : rings) {
                    for (std::size_t i = 0; i < r->points.size(); ++i) {
                        const Vec2 a = r->points[i];
                        const Vec2 b = r->points[(i + 1) % r->points.size()];
                        if (projectOntoSegment(p, a, b).distance < kTouch) onEdge = true;
                    }
                }
                if (onEdge && !hasCorner(t, p)) return false;
            }
        }
    }
    return true;
}

}

TEST_CASE("a 0.08 m wall joined to 3 m walls gives closed solids that meet corner to corner", "[core][build][lowwalls]") {
    for (const double angle : {0.0, 37.0}) {
        for (const double thickness : {0.2, 0.12, 0.3}) {
            INFO("angle " << angle << " low wall thickness " << thickness);
            Floor f = kerbRoom(thickness);
            turnFloor(f, angle);
            const std::vector<WallSolid> solids = requireCleanSolids(f);

            const std::vector<WallSolid> tall = ofHeight(solids, 3.0);
            const std::vector<WallSolid> low = ofHeight(solids, 0.08);
            REQUIRE(tall.size() == 1);
            REQUIRE(tall[0].holes.size() == 1);
            REQUIRE_FALSE(low.empty());
            REQUIRE(tall.size() + low.size() == solids.size());
            REQUIRE(lowCornersOnTall(low, tall));
            for (const WallRing* r : {&tall[0].outline, &tall[0].holes[0]}) {
                for (const WallEdge& e : r->edges) {
                    REQUIRE(e.wall != "w7");
                    REQUIRE(e.side.has_value());
                }
            }
            for (const WallSolid& l : low) {
                for (const WallEdge& e : l.outline.edges) REQUIRE(e.wall == "w7");
            }

            REQUIRE(f.openings.size() == 14);
            for (const Opening& o : f.openings) {
                INFO("opening " << o.id);
                REQUIRE(openingBuild(f, o, 0.0).status.ok());
            }
        }
    }
}

TEST_CASE("a low wall of the same thickness leaves the tall solid exactly as it was", "[core][build][lowwalls]") {
    // The tall solid already has a corner where each square band end of the split tall wall stops, and the
    // low wall's corners are those same points.
    const std::vector<WallSolid> a = ofHeight(wallSolids(kerbRoom(0.2)), 3.0);
    const std::vector<WallSolid> b = wallSolids(kerbRoom(0.2, false));
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    REQUIRE(a[0] == b[0]);
}

TEST_CASE("a low wall in the middle of a tall wall splits the tall face at its corners", "[core][build][lowwalls]") {
    for (const double angle : {0.0, 37.0}) {
        for (const double thickness : {0.12, 0.2, 0.3}) {
            INFO("angle " << angle << " low wall thickness " << thickness);
            Floor f;
            f.id = "f1";
            addPoint(f, "p1", 0.0, 0.0);
            addPoint(f, "p2", 3.0, 0.0);
            addPoint(f, "p3", 6.0, 0.0);
            addPoint(f, "p4", 3.0, 2.0);
            addWall(f, "w1", "p1", "p2");
            addWall(f, "w2", "p2", "p3");
            addWall(f, "w3", "p2", "p4", 0.08, thickness);
            turnFloor(f, angle);
            const std::vector<WallSolid> solids = requireCleanSolids(f);
            const std::vector<WallSolid> tall = ofHeight(solids, 3.0);
            const std::vector<WallSolid> low = ofHeight(solids, 0.08);
            REQUIRE(tall.size() == 1);
            REQUIRE_FALSE(low.empty());
            REQUIRE(lowCornersOnTall(low, tall));
        }
    }
}

TEST_CASE("a low wall at a corner of tall walls meets them corner to corner", "[core][build][lowwalls]") {
    for (const double angle : {0.0, 37.0}) {
        for (int layout = 0; layout < 3; ++layout) {
            INFO("angle " << angle << " layout " << layout);
            Floor f;
            f.id = "f1";
            addPoint(f, "p1", 0.0, 0.0);
            addPoint(f, "p2", 4.0, 0.0);
            addPoint(f, "p3", 4.0, 3.0);
            addPoint(f, "p4", 7.0, 0.0);
            addWall(f, "w1", "p1", "p2");
            if (layout == 0) {
                // One tall and one low wall turning the corner.
                addWall(f, "w2", "p2", "p3", 0.08);
            } else if (layout == 1) {
                // Two tall walls turning the corner and a low wall going on from it.
                addWall(f, "w2", "p2", "p3");
                addWall(f, "w3", "p2", "p4", 0.08);
            } else {
                // A low wall going on from a tall wall's end, in line with it, thinner.
                addWall(f, "w3", "p2", "p4", 0.08, 0.12);
            }
            turnFloor(f, angle);
            const std::vector<WallSolid> solids = requireCleanSolids(f);
            const std::vector<WallSolid> tall = ofHeight(solids, 3.0);
            const std::vector<WallSolid> low = ofHeight(solids, 0.08);
            REQUIRE(tall.size() == 1);
            REQUIRE(low.size() == 1);
            REQUIRE(lowCornersOnTall(low, tall));
        }
    }
}

TEST_CASE("two low walls meeting each other and a tall wall give clean solids", "[core][build][lowwalls]") {
    for (const double angle : {0.0, 37.0}) {
        for (const double second : {0.08, 1.0}) {
            for (const bool both : {false, true}) {
                INFO("angle " << angle << " second low height " << second << " both from the tall wall " << both);
                Floor f;
                f.id = "f1";
                addPoint(f, "p1", 0.0, 0.0);
                addPoint(f, "p2", 3.0, 0.0);
                addPoint(f, "p3", 6.0, 0.0);
                addPoint(f, "p4", 3.0, 2.0);
                addPoint(f, "p5", 5.0, 2.0);
                addPoint(f, "p6", 1.0, 2.0);
                addWall(f, "w1", "p1", "p2");
                addWall(f, "w2", "p2", "p3");
                addWall(f, "w3", "p2", "p4", 0.08);
                if (both) {
                    addWall(f, "w4", "p2", "p6", second, 0.12);
                } else {
                    addWall(f, "w4", "p4", "p5", second);
                }
                turnFloor(f, angle);
                const std::vector<WallSolid> solids = requireCleanSolids(f);
                const std::vector<WallSolid> tall = ofHeight(solids, 3.0);
                REQUIRE(tall.size() == 1);
                REQUIRE(lowCornersOnTall(ofHeight(solids, 0.08), tall));
                if (second > 0.08) {
                    REQUIRE(lowCornersOnTall(ofHeight(solids, second), tall));
                    REQUIRE(lowCornersOnTall(ofHeight(solids, 0.08), ofHeight(solids, second)));
                }
            }
        }
    }
}

TEST_CASE("walls of one height and solids that do not touch are left exactly as they were", "[core][build][lowwalls]") {
    Floor square = squareFloorWithWalls();
    const std::vector<WallSolid> alone = wallSolids(square);
    REQUIRE(alone.size() == 1);
    REQUIRE(alone[0].outline.points.size() == 4);
    REQUIRE(alone[0].holes.size() == 1);
    REQUIRE(alone[0].holes[0].points.size() == 4);
    REQUIRE(solidFault(alone[0]) == "");

    Floor apart = square;
    addPoint(apart, "p8", 7.0, 0.0);
    addPoint(apart, "p9", 9.0, 0.0);
    addWall(apart, "w8", "p8", "p9", 0.08);
    const std::vector<WallSolid> solids = requireCleanSolids(apart);
    REQUIRE(solids.size() == 2);
    REQUIRE(solids[0] == alone[0]);
    REQUIRE(solids[1].height == Approx(0.08).margin(1e-12));
    REQUIRE(solids[1].outline.points.size() == 4);

    Floor raised = kerbRoom(0.12);
    for (Wall& w : raised.walls) w.height.reset();
    const std::vector<WallSolid> same = requireCleanSolids(raised);
    REQUIRE(same.size() == 1);
    REQUIRE(same == wallSolids(raised));
}
