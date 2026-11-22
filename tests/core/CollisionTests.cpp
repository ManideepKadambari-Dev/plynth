#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Collision.h>
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
                                           CollisionTargetKind::Ceiling, CollisionTargetKind::Thresholds}) {
        REQUIRE(collisionTargetKindFromName(collisionTargetKindName(kind)) == kind);
    }
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Walls) == "walls");
    REQUIRE(collisionTargetKindName(CollisionTargetKind::Floor) == "floor");
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
