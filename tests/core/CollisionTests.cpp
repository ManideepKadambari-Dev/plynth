#include "RoomFixtures.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Rooms.h>
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
#include <cstddef>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// The square with room r1 on its four walls.
Floor squareWithRoom()
{
    Floor f = squareFloorWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms.push_back(r);
    return f;
}

// What the build makes of a floor at level 0.
FloorBuild buildOf(const Floor& f)
{
    FloorBuild b;
    b.id = f.id;
    b.elevation = 0.0;
    b.ceilingHeight = f.ceilingHeight;
    b.wallSolids = wallSolids(f);
    for (const Opening& o : f.openings) {
        const OpeningBuildResult r = openingBuild(f, o, 0.0);
        REQUIRE(r.status.ok());
        b.openings.push_back(r.opening);
    }
    for (const Room& room : f.rooms) {
        const std::optional<RoomBuild> rb = roomBuild(f, room, 0.0);
        if (rb) b.rooms.push_back(*rb);
    }
    return b;
}

// A door or window on a wall with an id.
Opening opening(const std::string& id, const std::string& wall, OpeningType type, double offset, double width,
                double height, double sill)
{
    Opening o;
    o.id = id;
    o.wall = wall;
    o.type = type;
    o.offset = offset;
    o.width = width;
    o.height = height;
    o.sill = sill;
    return o;
}

const CollisionShape* shapeOf(const std::vector<CollisionShape>& shapes, CollisionTargetKind kind)
{
    for (const CollisionShape& s : shapes) {
        if (s.kind == kind) return &s;
    }
    return nullptr;
}

double unionArea(const std::vector<CollisionPiece>& pieces)
{
    Clipper2Lib::PathsD paths;
    for (const CollisionPiece& piece : pieces) {
        Clipper2Lib::PathD path;
        for (const Vec2& p : piece.outline) path.emplace_back(p.x, p.y);
        paths.push_back(path);
    }
    return Clipper2Lib::Area(Clipper2Lib::Union(paths, Clipper2Lib::FillRule::NonZero, 8));
}

double sumArea(const std::vector<CollisionPiece>& pieces)
{
    double sum = 0.0;
    for (const CollisionPiece& piece : pieces) sum += signedArea(piece.outline);
    return sum;
}

double sumArea(const std::vector<std::vector<Vec2>>& rings)
{
    double sum = 0.0;
    for (const auto& ring : rings) sum += signedArea(ring);
    return sum;
}

bool covers(const CollisionPiece& piece, Vec2 at, double z)
{
    return z > piece.bottom && z < piece.top && insidePolygon(at, piece.outline);
}

void requireAllConvex(const std::vector<CollisionPiece>& pieces)
{
    for (const CollisionPiece& piece : pieces) {
        REQUIRE(isConvexRing(piece.outline));
        REQUIRE(piece.top > piece.bottom);
    }
}

}

TEST_CASE("collision kind names round trip", "[collision]") {
    for (const CollisionTargetKind kind : {CollisionTargetKind::Walls, CollisionTargetKind::Floor,
                                           CollisionTargetKind::Ceiling, CollisionTargetKind::Thresholds,
                                           CollisionTargetKind::Slab, CollisionTargetKind::Stair, CollisionTargetKind::Roof}) {
        REQUIRE(collisionTargetKindFromName(collisionTargetKindName(kind)) == kind);
    }
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Walls) == "walls");
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Floor) == "floor");
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Slab) == "slab");
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Stair) == "stair");
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Roof) == "roof");
    REQUIRE_FALSE(collisionTargetKindFromName("furniture").has_value());
}

TEST_CASE("isConvexRing accepts anticlockwise convex rings only", "[collision]") {
    REQUIRE(isConvexRing({{0, 0}, {1, 0}, {1, 1}, {0, 1}}));
    REQUIRE_FALSE(isConvexRing({{0, 0}, {0, 1}, {1, 1}, {1, 0}}));
    REQUIRE_FALSE(isConvexRing({{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}));
    REQUIRE(isConvexRing({{0, 0}, {0.5, 0}, {1, 0}, {1, 1}, {0, 1}}));
    REQUIRE_FALSE(isConvexRing({{0, 0}, {1, 0}}));
}

TEST_CASE("walls of the square room are four closed convex pieces", "[collision]") {
    const Floor f = squareWithRoom();
    const FloorBuild b = buildOf(f);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->id == "f1");
    REQUIRE(walls->pieces.size() == 4);
    requireAllConvex(walls->pieces);
    for (const CollisionPiece& piece : walls->pieces) {
        REQUIRE(piece.bottom == Approx(0.0).margin(1e-9));
        REQUIRE(piece.top == Approx(3.0).margin(1e-9));
    }

    double solid = 0.0;
    for (const WallSolid& s : b.wallSolids) {
        solid += signedArea(s.outline.points);
        for (const WallRing& hole : s.holes) solid += signedArea(hole.points);
    }
    REQUIRE(solid == Approx(3.2).margin(1e-6));
    REQUIRE(unionArea(walls->pieces) == Approx(solid).margin(1e-6));
}

TEST_CASE("a door cuts its wall into left, right and above pieces", "[collision]") {
    Floor f = squareWithRoom();
    f.openings.push_back(opening("o1", "w1", OpeningType::Door, 2.0, 0.9, 2.1, 0.0));
    const FloorBuild b = buildOf(f);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->pieces.size() == 6);
    requireAllConvex(walls->pieces);

    // w1 comes first in wall order: left, above the door, right.
    int full = 0;
    int above = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const CollisionPiece& piece = walls->pieces[i];
        if (piece.bottom < 1e-9) {
            REQUIRE(piece.top == Approx(3.0).margin(1e-9));
            ++full;
        } else {
            REQUIRE(piece.bottom == Approx(2.1).margin(1e-9));
            REQUIRE(piece.top == Approx(3.0).margin(1e-9));
            ++above;
        }
    }
    REQUIRE(full == 2);
    REQUIRE(above == 1);

    for (const CollisionPiece& piece : walls->pieces) {
        REQUIRE_FALSE(covers(piece, {2.0, 0.0}, 1.0));
        REQUIRE_FALSE(covers(piece, {1.7, 0.0}, 2.0));
    }
    REQUIRE(std::any_of(walls->pieces.begin(), walls->pieces.end(),
                        [](const CollisionPiece& p) { return covers(p, {2.0, 0.0}, 2.5); }));
    REQUIRE(unionArea(walls->pieces) == Approx(3.2).margin(1e-6));
}

TEST_CASE("a door that reaches the wall top has no piece above it", "[collision]") {
    Floor f = squareWithRoom();
    f.openings.push_back(opening("o1", "w1", OpeningType::Door, 2.0, 0.9, 3.0, 0.0));
    const std::vector<CollisionShape> shapes = floorCollision(f, buildOf(f));
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->pieces.size() == 5);
}

TEST_CASE("a window does not split its wall", "[collision]") {
    Floor f = squareWithRoom();
    f.openings.push_back(opening("o1", "w1", OpeningType::Window, 2.0, 1.2, 1.2, 0.9));
    const std::vector<CollisionShape> shapes = floorCollision(f, buildOf(f));
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->pieces.size() == 4);
}

TEST_CASE("a plain opening with a sill gets a piece below it", "[collision]") {
    Floor f = squareWithRoom();
    f.openings.push_back(opening("o1", "w1", OpeningType::Plain, 2.0, 1.0, 1.0, 0.5));
    const FloorBuild b = buildOf(f);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->pieces.size() == 7);
    requireAllConvex(walls->pieces);

    bool below = false;
    bool above = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const CollisionPiece& piece = walls->pieces[i];
        if (piece.bottom < 1e-9 && piece.top < 0.6) {
            below = true;
            REQUIRE(piece.top == Approx(0.5).margin(1e-9));
        }
        if (piece.bottom > 1.0) {
            above = true;
            REQUIRE(piece.bottom == Approx(1.5).margin(1e-9));
        }
    }
    REQUIRE(below);
    REQUIRE(above);
    for (const CollisionPiece& piece : walls->pieces) REQUIRE_FALSE(covers(piece, {2.0, 0.0}, 1.0));
    REQUIRE(unionArea(walls->pieces) == Approx(3.2).margin(1e-6));
}

TEST_CASE("a curved wall gives one convex piece per arc segment", "[collision]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.2;
    w.bulge = 1.0;
    f.walls.push_back(w);
    const WallCurve curve = wallCurve(f, f.walls[0]);
    REQUIRE(curve.curved);
    const int segments = arcSegmentCount(curve.radius, curve.sweep);

    const std::vector<CollisionShape> shapes = floorCollision(f, buildOf(f));
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(static_cast<int>(walls->pieces.size()) == segments);
    requireAllConvex(walls->pieces);

    double band = 0.0;
    for (const auto& ring : wallOutline(f, f.walls[0])) band += signedArea(ring);
    REQUIRE(unionArea(walls->pieces) == Approx(band).margin(1e-6));
}

TEST_CASE("a door in a curved wall keeps every piece convex", "[collision]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 6.0, 0.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.2;
    w.bulge = 1.0;
    f.walls.push_back(w);
    f.openings.push_back(opening("o1", "w1", OpeningType::Door, 3.5, 0.9, 2.1, 0.0));
    const FloorBuild fb = buildOf(f);
    const std::vector<CollisionShape> shapes = floorCollision(f, fb);
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    requireAllConvex(walls->pieces);
    const std::size_t above = static_cast<std::size_t>(std::count_if(
        walls->pieces.begin(), walls->pieces.end(), [](const CollisionPiece& p) { return p.bottom > 2.0; }));
    REQUIRE(above >= 1);
}

TEST_CASE("floor and ceiling slabs are 0.1 thick with the room's area", "[collision]") {
    const Floor f = squareWithRoom();
    const FloorBuild b = buildOf(f);
    REQUIRE(b.rooms.size() == 1);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);

    const CollisionShape* floorShape = shapeOf(shapes, CollisionTargetKind::Floor);
    const CollisionShape* ceilingShape = shapeOf(shapes, CollisionTargetKind::Ceiling);
    REQUIRE(floorShape != nullptr);
    REQUIRE(ceilingShape != nullptr);
    REQUIRE(floorShape->id == "r1");
    REQUIRE(ceilingShape->id == "r1");
    requireAllConvex(floorShape->pieces);
    requireAllConvex(ceilingShape->pieces);
    for (const CollisionPiece& p : floorShape->pieces) {
        REQUIRE(p.bottom == Approx(-0.1).margin(1e-9));
        REQUIRE(p.top == Approx(0.0).margin(1e-9));
    }
    for (const CollisionPiece& p : ceilingShape->pieces) {
        REQUIRE(p.bottom == Approx(3.0).margin(1e-9));
        REQUIRE(p.top == Approx(3.1).margin(1e-9));
    }
    REQUIRE(sumArea(floorShape->pieces) == Approx(b.rooms[0].area).margin(1e-6));
    REQUIRE(sumArea(ceilingShape->pieces) == Approx(b.rooms[0].area).margin(1e-6));
}

TEST_CASE("an L shaped room gives convex pieces that add up to its area", "[collision]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 2.0, 0.0},
                {"p4", 2.0, 2.0, 0.0}, {"p5", 2.0, 4.0, 0.0}, {"p6", 0.0, 4.0, 0.0}};
    Room room;
    room.id = "r1";
    for (int i = 0; i < 6; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = "p" + std::to_string(i + 1);
        w.to = "p" + std::to_string((i + 1) % 6 + 1);
        w.thickness = 0.2;
        f.walls.push_back(w);
        room.walls.push_back(w.id);
    }
    f.rooms.push_back(room);
    const FloorBuild b = buildOf(f);
    REQUIRE(b.rooms.size() == 1);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);
    const CollisionShape* floorShape = shapeOf(shapes, CollisionTargetKind::Floor);
    REQUIRE(floorShape != nullptr);
    REQUIRE(floorShape->pieces.size() >= 2);
    requireAllConvex(floorShape->pieces);
    REQUIRE(sumArea(floorShape->pieces) == Approx(b.rooms[0].area).margin(1e-6));
    REQUIRE(unionArea(floorShape->pieces) == Approx(b.rooms[0].area).margin(1e-6));
}

TEST_CASE("convexPieces of an L polygon and a polygon with a hole", "[collision]") {
    WorldPolygon l;
    l.outline = {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};
    const auto lPieces = convexPieces(l);
    REQUIRE(lPieces.size() == 2);
    for (const auto& ring : lPieces) REQUIRE(isConvexRing(ring));
    REQUIRE(sumArea(lPieces) == Approx(12.0).margin(1e-9));

    WorldPolygon column;
    column.outline = {{0, 0}, {6, 0}, {6, 6}, {0, 6}};
    column.holes = {{{2, 2}, {2, 4}, {4, 4}, {4, 2}}};
    const auto pieces = convexPieces(column);
    REQUIRE(pieces.size() == 4);
    for (const auto& ring : pieces) {
        REQUIRE(isConvexRing(ring));
        REQUIRE_FALSE(insidePolygon({3.0, 3.0}, ring));
    }
    REQUIRE(sumArea(pieces) == Approx(32.0).margin(1e-9));
}

TEST_CASE("convexPieces merges strips of a slanted shape and ignores degenerate input", "[collision]") {
    WorldPolygon trapezoid;
    trapezoid.outline = {{0, 0}, {4, 0}, {3, 2}, {1, 2}};
    const auto one = convexPieces(trapezoid);
    REQUIRE(one.size() == 1);
    REQUIRE(sumArea(one) == Approx(6.0).margin(1e-9));

    WorldPolygon fan;
    fan.outline = {{0, 0}, {4, 0}, {4, 1}, {2, 3}, {0, 1}};
    const auto merged = convexPieces(fan);
    REQUIRE(merged.size() == 1);
    REQUIRE(isConvexRing(merged[0]));
    REQUIRE(sumArea(merged) == Approx(4.0 + 4.0).margin(1e-9));

    REQUIRE(convexPieces(WorldPolygon{}).empty());
    WorldPolygon line;
    line.outline = {{0, 0}, {1, 0}, {2, 0}};
    REQUIRE(convexPieces(line).empty());
}

TEST_CASE("a door gives a threshold shape after the rooms", "[collision]") {
    Floor f = squareWithRoom();
    f.openings.push_back(opening("o1", "w1", OpeningType::Door, 2.0, 0.9, 2.1, 0.0));
    const FloorBuild b = buildOf(f);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);
    REQUIRE(shapes.size() == 4);
    REQUIRE(shapes[0].kind == CollisionTargetKind::Walls);
    REQUIRE(shapes[1].kind == CollisionTargetKind::Floor);
    REQUIRE(shapes[2].kind == CollisionTargetKind::Ceiling);
    REQUIRE(shapes[3].kind == CollisionTargetKind::Thresholds);
    REQUIRE(shapes[3].id == "f1");
    requireAllConvex(shapes[3].pieces);
    for (const CollisionPiece& p : shapes[3].pieces) {
        REQUIRE(p.bottom == Approx(-0.1).margin(1e-9));
        REQUIRE(p.top == Approx(0.0).margin(1e-9));
    }
    REQUIRE(sumArea(shapes[3].pieces) == Approx(0.9 * 0.2).margin(1e-6));
}

TEST_CASE("a window with a sill and an empty floor give no extra shapes", "[collision]") {
    Floor f = squareFloorWithWalls();
    f.openings.push_back(opening("o1", "w1", OpeningType::Window, 2.0, 1.2, 1.2, 0.9));
    const std::vector<CollisionShape> shapes = floorCollision(f, buildOf(f));
    REQUIRE(shapes.size() == 1);
    REQUIRE(shapes[0].kind == CollisionTargetKind::Walls);

    Floor empty;
    empty.id = "f1";
    REQUIRE(floorCollision(empty, buildOf(empty)).empty());
}

TEST_CASE("walls of different heights get their own tops", "[collision]") {
    Floor f = squareWithRoom();
    f.walls[1].height = 2.0;
    const std::vector<CollisionShape> shapes = floorCollision(f, buildOf(f));
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->pieces.size() == 4);
    REQUIRE(walls->pieces[1].top == Approx(2.0).margin(1e-9));
    REQUIRE(walls->pieces[0].top == Approx(3.0).margin(1e-9));
    requireAllConvex(walls->pieces);
}

TEST_CASE("rounded corners keep the wall pieces convex and the corners closed", "[collision]") {
    Floor f = squareWithRoom();
    f.points[1].cornerRadius = 0.6;
    f.points[3].cornerRadius = 0.4;
    const FloorBuild b = buildOf(f);
    const std::vector<CollisionShape> shapes = floorCollision(f, b);
    const CollisionShape* walls = shapeOf(shapes, CollisionTargetKind::Walls);
    REQUIRE(walls != nullptr);
    REQUIRE(walls->pieces.size() > 4);
    requireAllConvex(walls->pieces);

    double solid = 0.0;
    for (const WallSolid& s : b.wallSolids) {
        solid += signedArea(s.outline.points);
        for (const WallRing& hole : s.holes) solid += signedArea(hole.points);
    }
    REQUIRE(unionArea(walls->pieces) == Approx(solid).margin(1e-6));
}

namespace {

namespace cl = Clipper2Lib;

constexpr int kDigits = 8;
constexpr cl::FillRule kFill = cl::FillRule::NonZero;
// Rounding to kDigits leaves seams this narrow where two paths share an edge, so only a wider part counts.
constexpr double kSeam = 1e-7;

// A ring as a path that turns anticlockwise for an outline and clockwise for a hole.
cl::PathD pathOf(std::vector<Vec2> ring, bool outline)
{
    if ((signedArea(ring) > 0.0) != outline) std::reverse(ring.begin(), ring.end());
    cl::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// The area some paths cover together, with no cleaning at all.
cl::PathsD united(const cl::PathsD& paths)
{
    return paths.empty() ? paths : cl::Union(paths, kFill, kDigits);
}

// What is in the first set and not in the second.
cl::PathsD minus(const cl::PathsD& a, const cl::PathsD& b)
{
    return a.empty() || b.empty() ? united(a) : cl::Difference(a, b, kFill, kDigits);
}

// The area of paths that do not overlap.
double areaOf(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathD& path : paths) {
        std::vector<Vec2> ring;
        for (const cl::PointD& p : path) ring.push_back({p.x, p.y});
        sum += signedArea(ring);
    }
    return sum;
}

// The separate pieces of the paths, each with its holes.
std::vector<cl::PathsD> piecesOf(const cl::PathsD& paths)
{
    std::vector<cl::PathsD> out;
    if (paths.empty()) return out;
    cl::ClipperD clipper(kDigits);
    clipper.AddSubject(paths);
    cl::PolyTreeD tree;
    clipper.Execute(cl::ClipType::Union, kFill, tree);
    const auto visit = [&out](const auto& self, const cl::PolyPathD& node) -> void {
        for (std::size_t i = 0; i < node.Count(); ++i) {
            const cl::PolyPathD& child = *node.Child(i);
            if (!child.IsHole()) {
                cl::PathsD piece{child.Polygon()};
                for (std::size_t h = 0; h < child.Count(); ++h) piece.push_back(child.Child(h)->Polygon());
                out.push_back(std::move(piece));
            }
            self(self, child);
        }
    };
    visit(visit, tree);
    return out;
}

// How much of the paths is wider than a seam: every piece shrunk by kSeam, however small the piece is.
double leakArea(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathsD& piece : piecesOf(paths)) {
        sum += areaOf(cl::InflatePaths(piece, -kSeam, cl::JoinType::Miter, cl::EndType::Polygon, 10.0, kDigits));
    }
    return sum;
}

// A roof face seen from above, whichever way it looks.
cl::PathsD fromAbove(const RoofFace& face)
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

// Which side of the line from a to b the point is on.
int sideOf(const Vec2& a, const Vec2& b, const Vec2& p)
{
    const double v = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
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

// Adds a rectangular room of walls whose ids are made from key, clockwise on screen from the top left.
void addBox(Floor& floor, const std::string& key, double x0, double y0, double x1, double y1, double thickness)
{
    const std::vector<Vec2> corners{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        addPoint(floor, "p" + key + std::to_string(i), corners[i].x, corners[i].y);
    }
    Room room;
    room.id = "r" + key;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const std::string id = "w" + key + std::to_string(i);
        addWall(floor, id, "p" + key + std::to_string(i), "p" + key + std::to_string((i + 1) % corners.size()),
                thickness);
        room.walls.push_back(id);
    }
    floor.rooms.push_back(std::move(room));
}

// Two boxes of 0.2 m walls across each other, the second with its west side moved by shift, and a copy of the
// floor on top moved east by copyShift, all mirrored east to west, under a steep gable with a parapet. This is
// the crossing family of the edge sweeps; shift -5e-5 with copyShift 5e-5 is its seed 7361546.
Plan crossingGable(double shift, double copyShift)
{
    Plan plan = makeEmptyPlan();
    Floor& ground = plan.floors[0];
    addBox(ground, "a", 1.4, 0.0, 3.8, 5.6, 0.2);
    addBox(ground, "b", 1.4 + shift, 1.6, 5.4, 3.9, 0.2);
    Floor copy = ground;
    copy.id = "f2";
    for (Point& p : copy.points) p.x += copyShift;
    plan.floors.push_back(std::move(copy));
    for (Floor& floor : plan.floors) {
        for (Point& p : floor.points) p.x = -p.x;
        for (Wall& wall : floor.walls) wall.bulge = -wall.bulge;
    }
    plan.floors[0].slabThickness = 0.1;
    plan.floors[1].slabThickness = 0.05;
    plan.roof.type = std::string(kGableRoofId);
    plan.roof.pitch = 75.0;
    plan.roof.overhang = 0.0;
    plan.roof.parapet = 0.6;
    plan.roof.direction = 90.0;
    return plan;
}

// Turns a plan by angle degrees about the origin, rounds each point to the grid (0 for none) and turns the
// roof's direction with it.
void turn(Plan& plan, double angle, double grid)
{
    const double c = std::cos(angle * std::numbers::pi / 180.0);
    const double s = std::sin(angle * std::numbers::pi / 180.0);
    const auto snap = [grid](double v) { return grid > 0.0 ? std::round(v / grid) * grid : v; };
    for (Floor& floor : plan.floors) {
        for (Point& p : floor.points) {
            const double x = p.x * c - p.y * s;
            const double y = p.x * s + p.y * c;
            p.x = snap(x);
            p.y = snap(y);
        }
    }
    plan.roof.direction = std::fmod(plan.roof.direction.value_or(0.0) + angle, 360.0);
}

// What a plan's roof hulls do outside their covering, seen from above.
struct HullCheck {
    int hulls = 0;
    std::vector<double> coverings; // the area of each floor's roof covering seen from above
    std::vector<std::string> faults;
};

// Builds the roofs and their collision on every floor and finds each hull's part outside its roof's covering.
HullCheck checkRoofHulls(const Plan& plan)
{
    HullCheck out;
    const std::vector<double> levels = floorElevations(plan);
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        FloorBuild build;
        build.id = plan.floors[i].id;
        build.elevation = levels[i];
        build.ceilingHeight = plan.floors[i].ceilingHeight;
        build.roofs = floorRoofs(plan, i, levels).roofs;
        const std::vector<CollisionShape> shapes = floorCollision(plan.floors[i], build);
        double floorCovering = 0.0;
        for (const RoofBuild& roof : build.roofs) {
            cl::PathsD seen;
            for (const RoofFace& face : roof.faces) {
                if (face.part != RoofPart::Covering || face.outline.size() < 3) continue;
                const cl::PathsD one = fromAbove(face);
                seen.insert(seen.end(), one.begin(), one.end());
            }
            const cl::PathsD covering = united(seen);
            floorCovering += areaOf(covering);
            for (const CollisionShape& shape : shapes) {
                if (shape.kind != CollisionTargetKind::Roof || shape.index != roof.index) continue;
                for (std::size_t h = 0; h < shape.hulls.size(); ++h) {
                    ++out.hulls;
                    std::ostringstream what;
                    what.precision(10);
                    what << "floor " << i << " roof " << roof.index << " hull " << h << ": ";
                    const std::vector<Vec2> ring = convexAround(shape.hulls[h]);
                    if (ring.size() < 3) {
                        out.faults.push_back(what.str() + "flat seen from above");
                        continue;
                    }
                    const cl::PathsD outside = minus(cl::PathsD{pathOf(ring, true)}, covering);
                    const double leak = leakArea(outside);
                    if (leak > 1e-12) {
                        what << areaOf(outside) << " m2 outside the covering, " << leak << " m2 of it wider than a seam";
                        out.faults.push_back(what.str());
                    }
                }
            }
        }
        out.coverings.push_back(floorCovering);
    }
    return out;
}

// The first faults of a list, one to a line, for a failure message.
std::string listed(const std::vector<std::string>& faults)
{
    std::string out;
    for (std::size_t i = 0; i < faults.size() && i < 15; ++i) out += "\n    " + faults[i];
    return out;
}

}

TEST_CASE("convexPieces keeps its pieces inside a face whose edge rises by less than the merge distance", "[collision]") {
    // A bottom edge that rises 1e-5 over 4 m, and one that rises 2e-5 in two steps of 1e-5.
    WorldPolygon once;
    once.outline = {{0, 0}, {4, 1e-5}, {4, 3}, {0, 3}};
    WorldPolygon twice;
    twice.outline = {{0, 0}, {2, 1e-5}, {4, 2e-5}, {4, 3}, {0, 3}};
    for (const WorldPolygon& face : {once, twice}) {
        cl::PathsD pieces;
        for (const std::vector<Vec2>& ring : convexPieces(face)) pieces.push_back(pathOf(ring, true));
        REQUIRE_FALSE(pieces.empty());
        const cl::PathsD outside = minus(pieces, cl::PathsD{pathOf(face.outline, true)});
        INFO("pieces outside the face: " << areaOf(outside) << " m2, " << leakArea(outside) << " m2 wider than a seam");
        REQUIRE(leakArea(outside) <= 1e-12);
    }
}

TEST_CASE("roof hulls of edge seed 7361546 lie inside their covering", "[collision][roofs]") {
    // Seed 7361546 of the crossing sweep: offset 5e-5, gable, overhang 0, parapet 0.6, two floors, turned 289
    // degrees at the origin on a 1e-6 grid.
    Plan plan = crossingGable(-5e-5, 5e-5);
    turn(plan, 289.0, 1e-6);
    const HullCheck check = checkRoofHulls(plan);
    INFO(check.hulls << " roof hulls, " << check.faults.size() << " outside their covering" << listed(check.faults));
    REQUIRE(check.coverings.size() == 2);
    // The upper floor's covering as the sweep measured it, so this is the same building.
    REQUIRE(check.coverings[1] == Approx(19.0801259).margin(1e-6));
    REQUIRE(check.hulls > 0);
    REQUIRE(check.faults.empty());
}

TEST_CASE("roof hulls of crossing gables lie inside their covering at any turn and grid", "[collision][roofs]") {
    std::vector<std::string> faults;
    int builds = 0;
    for (const double step : {1e-5, 2.5e-5, 5e-5}) {
        for (const double side : {-1.0, 1.0}) {
            for (const double angle : {0.0, 20.0, 45.0, 110.0, 200.0, 289.0, 333.0}) {
                for (const double grid : {0.0, 1e-6}) {
                    Plan plan = crossingGable(side * step, step);
                    turn(plan, angle, grid);
                    const HullCheck check = checkRoofHulls(plan);
                    ++builds;
                    std::ostringstream label;
                    label << "offset " << side * step << ", turned " << angle << ", grid " << grid;
                    if (check.hulls == 0) faults.push_back(label.str() + ": no roof hulls");
                    for (const std::string& fault : check.faults) faults.push_back(label.str() + ", " + fault);
                }
            }
        }
    }
    INFO(builds << " buildings, " << faults.size() << " faults" << listed(faults));
    REQUIRE(builds == 84);
    REQUIRE(faults.empty());
}
