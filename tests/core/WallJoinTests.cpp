#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Approx;

namespace {

// Adds a straight wall between two points of the first floor.
void addWall(Project& p, const std::string& id, const std::string& from, const std::string& to, double thickness = 0.3)
{
    Wall w;
    w.id = id;
    w.from = from;
    w.to = to;
    w.thickness = thickness;
    p.plan.floors[0].walls.push_back(w);
}

// A 6 by 4 rectangle p1 (0, 0), p2 (6, 0), p3 (6, 4), p4 (0, 4) with walls w1..w4 0.3 thick and a room r1 on
// them: a kitchen with materials and no dressing, a window o1 on w1 centred 4.5 m along it, an item i1 at (1, 2)
// and i2 at (5, 2), and a light l1 at (1, 1), all in r1.
Project roomProject()
{
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 6.0, 0.0, 0.0}, {"p3", 6.0, 4.0, 0.0}, {"p4", 0.0, 4.0, 0.0}};
    addWall(p, "w1", "p1", "p2");
    addWall(p, "w2", "p2", "p3");
    addWall(p, "w3", "p3", "p4");
    addWall(p, "w4", "p4", "p1");
    Room r;
    r.id = "r1";
    r.type = "kitchen";
    r.walls = {"w1", "w2", "w3", "w4"};
    r.floorMaterial = "oak";
    r.wallMaterial = "plaster";
    r.ceilingMaterial = "white";
    r.dressing = false;
    f.rooms = {r};
    Opening o;
    o.id = "o1";
    o.wall = "w1";
    o.offset = 4.5;
    o.width = 0.8;
    o.type = OpeningType::Window;
    f.openings = {o};
    for (const auto& [id, x] : {std::pair<std::string, double>{"i1", 1.0}, {"i2", 5.0}}) {
        Item item;
        item.id = id;
        item.asset = "chair";
        item.room = "r1";
        item.x = x;
        item.y = 2.0;
        item.size = {0.5, 0.5, 0.9};
        f.items.push_back(item);
    }
    LightMarker light;
    light.id = "l1";
    light.room = "r1";
    light.x = 1.0;
    light.y = 1.0;
    light.z = 2.5;
    f.lights = {light};
    return p;
}

// Applies a command and checks it worked and left a valid plan.
void applyOk(Command& c, Project& p)
{
    const Status s = c.apply(p);
    INFO(s.message);
    REQUIRE(s.ok());
    plynth::test::requirePlanValid(p.plan);
}

// Applies a command and checks it was refused with the code and changed nothing.
void applyRefused(Command& c, Project& p, ErrorCode code)
{
    const Project before = p;
    const Status s = c.apply(p);
    INFO(s.message);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    REQUIRE(p == before);
}

// Applies, undoes and redoes a command: undo gives the start back and redo the same result as the first apply.
void roundTrip(Command& c, Project& p)
{
    const Project before = p;
    applyOk(c, p);
    const Project after = p;
    REQUIRE(after != before);
    c.undo(p);
    REQUIRE(p == before);
    applyOk(c, p);
    REQUIRE(p == after);
}

// The walls of a room as a set.
std::set<Id> wallSet(const Floor& f, const std::string& roomId)
{
    const Room* r = findRoom(f, roomId);
    REQUIRE(r);
    return {r->walls.begin(), r->walls.end()};
}

// The floor area of a room.
double areaOf(const Floor& f, const Room& r)
{
    const RoomShape shape = roomShape(f, r);
    REQUIRE(shape.closed);
    return shape.area;
}

}

TEST_CASE("a wall drawn across a room splits it in one undo step", "[core][command][room][wall]") {
    Project p = roomProject();
    DrawWallCommand c("f1", WallEnd::onWall("w1", 2.0), WallEnd::onWall("w3", 4.0));
    roundTrip(c, p);
    const Floor& f = p.plan.floors[0];
    REQUIRE(f.rooms.size() == 2);
    REQUIRE(c.newRoomIds().size() == 1);
    const Id made = c.newRoomIds()[0];
    REQUIRE(f.rooms[0].id == "r1");
    REQUIRE(f.rooms[1].id == made);

    SECTION("the larger part keeps the id") {
        REQUIRE(areaOf(f, f.rooms[0]) > areaOf(f, f.rooms[1]));
        REQUIRE(wallSet(f, "r1").count("w2") == 1);
        REQUIRE(wallSet(f, made).count("w4") == 1);
        REQUIRE(wallSet(f, "r1").count(c.wallId()) == 1);
        REQUIRE(wallSet(f, made).count(c.wallId()) == 1);
    }
    SECTION("the new part copies the type, materials and dressing") {
        const Room& part = f.rooms[1];
        REQUIRE(part.type == std::optional<std::string>("kitchen"));
        REQUIRE(part.floorMaterial == std::optional<std::string>("oak"));
        REQUIRE(part.wallMaterial == std::optional<std::string>("plaster"));
        REQUIRE(part.ceilingMaterial == std::optional<std::string>("white"));
        REQUIRE_FALSE(part.dressing);
    }
    SECTION("items and lights in the new part move to it, the rest stay") {
        REQUIRE(findItem(f, "i1")->room == made);
        REQUIRE(findItem(f, "i2")->room == std::optional<Id>("r1"));
        REQUIRE(findLight(f, "l1")->room == made);
    }
    SECTION("the window stays on the piece that holds it") {
        const Opening* o = findOpening(f, "o1");
        REQUIRE(o->wall != "w1");
        REQUIRE(o->offset == Approx(2.5));
    }
    SECTION("the two parts cover the old room") {
        const Project start = roomProject();
        const double whole = areaOf(start.plan.floors[0], start.plan.floors[0].rooms[0]);
        const double parts = areaOf(f, f.rooms[0]) + areaOf(f, f.rooms[1]);
        // The new wall takes its band out of the floor between the inner faces of w1 and w3: 3.7 m by 0.3 m.
        REQUIRE(parts == Approx(whole - 3.7 * 0.3).margin(1e-6));
    }
    SECTION("undo makes it one room again and redo gives the same ids") {
        c.undo(p);
        REQUIRE(p == roomProject());
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].rooms[1].id == made);
    }
}

TEST_CASE("a wall drawn between two points of a room's ring splits it", "[core][command][room][wall]") {
    Project p = roomProject();
    DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::atPoint("p3"));
    roundTrip(c, p);
    const Floor& f = p.plan.floors[0];
    REQUIRE(f.rooms.size() == 2);
    REQUIRE(wallSet(f, f.rooms[0].id).size() == 3);
    REQUIRE(wallSet(f, f.rooms[1].id).size() == 3);
    // Two equal halves: the first part in closed area order keeps the id.
    REQUIRE(f.rooms[0].id == "r1");
}

TEST_CASE("walls that do not cut a room leave it whole", "[core][command][room][wall]") {
    Project p = roomProject();
    SECTION("a wall touching the room at one point only") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::newPoint({2.0, 2.0}));
        applyOk(c, p);
        REQUIRE(c.newRoomIds().empty());
        REQUIRE(p.plan.floors[0].rooms.size() == 1);
        REQUIRE(wallSet(p.plan.floors[0], "r1") == std::set<Id>{"w1", "w2", "w3", "w4"});
    }
    SECTION("a wall outside the room") {
        DrawWallCommand c("f1", WallEnd::atPoint("p2"), WallEnd::newPoint({9.0, 0.0}));
        applyOk(c, p);
        DrawWallCommand d("f1", WallEnd::atPoint(c.toPointId()), WallEnd::atPoint("p3"));
        applyOk(d, p);
        REQUIRE(d.newRoomIds().empty());
        REQUIRE(p.plan.floors[0].rooms.size() == 1);
    }
    SECTION("a wall from a ring point to an outside point") {
        DrawWallCommand c("f1", WallEnd::onWall("w2", 2.0), WallEnd::newPoint({9.0, 2.0}));
        applyOk(c, p);
        REQUIRE(c.newRoomIds().empty());
        REQUIRE(p.plan.floors[0].rooms.size() == 1);
    }
}

TEST_CASE("a wall across the notch of an L shaped room is outside it", "[core][command][room][wall]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 2.0, 0.0},
                {"p4", 2.0, 2.0, 0.0}, {"p5", 2.0, 4.0, 0.0}, {"p6", 0.0, 4.0, 0.0}};
    for (int i = 0; i < 6; ++i) {
        addWall(p, "w" + std::to_string(i + 1), "p" + std::to_string(i + 1), "p" + std::to_string((i + 1) % 6 + 1));
    }
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4", "w5", "w6"};
    f.rooms = {r};
    plynth::test::requirePlanValid(p.plan);

    SECTION("outside: no split") {
        DrawWallCommand c("f1", WallEnd::atPoint("p3"), WallEnd::atPoint("p5"));
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].rooms.size() == 1);
    }
    SECTION("inside: split") {
        DrawWallCommand c("f1", WallEnd::atPoint("p1"), WallEnd::atPoint("p4"));
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].rooms.size() == 2);
    }
}

TEST_CASE("a curved wall can cut a room and a room with a curved wall can be cut", "[core][command][room][wall]") {
    Project p = roomProject();
    SECTION("a curved cutting wall") {
        addWall(p, "w9", "p1", "p3");
        p.plan.floors[0].walls.back().bulge = 0.8;
        SplitRoomsByWallCommand c("w9");
        roundTrip(c, p);
        REQUIRE(c.newRoomIds().size() == 1);
        const Floor& f = p.plan.floors[0];
        REQUIRE(f.rooms.size() == 2);
        // The arc bends to the right of p1 to p3, toward p4, so the part with p2 is larger and keeps the id.
        REQUIRE(wallSet(f, "r1").count("w2") == 1);
        REQUIRE(areaOf(f, f.rooms[0]) > areaOf(f, f.rooms[1]));
    }
    SECTION("a room with a curved wall") {
        p.plan.floors[0].walls[1].bulge = -1.0;
        DrawWallCommand c("f1", WallEnd::onWall("w1", 3.0), WallEnd::onWall("w3", 3.0));
        applyOk(c, p);
        const Floor& f = p.plan.floors[0];
        REQUIRE(f.rooms.size() == 2);
        REQUIRE(wallSet(f, "r1").count("w2") == 1);
    }
}

TEST_CASE("a chain of walls that closes across a room splits it at the last wall", "[core][command][room][wall]") {
    Project p = roomProject();
    DrawWallCommand first("f1", WallEnd::onWall("w1", 2.0), WallEnd::newPoint({2.0, 2.0}));
    applyOk(first, p);
    REQUIRE(p.plan.floors[0].rooms.size() == 1);
    DrawWallCommand second("f1", WallEnd::atPoint(first.toPointId()), WallEnd::onWall("w3", 4.0));
    applyOk(second, p);
    const Floor& f = p.plan.floors[0];
    REQUIRE(f.rooms.size() == 2);
    REQUIRE(wallSet(f, "r1").count(first.wallId()) == 1);
    REQUIRE(wallSet(f, second.newRoomIds()[0]).count(first.wallId()) == 1);
}

TEST_CASE("a wall inside a room's hole does not split the room", "[core][command][room][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.points.push_back({"p5", 2.0, 1.0, 0.0});
    f.points.push_back({"p6", 4.0, 1.0, 0.0});
    f.points.push_back({"p7", 4.0, 3.0, 0.0});
    f.points.push_back({"p8", 2.0, 3.0, 0.0});
    addWall(p, "w5", "p5", "p6", 0.1);
    addWall(p, "w6", "p6", "p7", 0.1);
    addWall(p, "w7", "p7", "p8", 0.1);
    addWall(p, "w8", "p8", "p5", 0.1);
    f.rooms[0].walls = {"w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8"};
    f.items.clear();
    f.lights.clear();
    plynth::test::requirePlanValid(p.plan);
    REQUIRE(roomShape(f, f.rooms[0]).closed);

    DrawWallCommand c("f1", WallEnd::atPoint("p5"), WallEnd::atPoint("p7"));
    applyOk(c, p);
    REQUIRE(c.newRoomIds().empty());
    REQUIRE(p.plan.floors[0].rooms.size() == 1);
}

TEST_CASE("roomSplitBy finds nothing for a wall the room already lists", "[core][rooms]") {
    const Project p = roomProject();
    const Floor& f = p.plan.floors[0];
    REQUIRE_FALSE(roomSplitBy(f, f.rooms[0], "w1").has_value());
    REQUIRE_FALSE(roomSplitBy(f, f.rooms[0], "w99").has_value());
    SplitRoomsByWallCommand none("w99");
    Project q = p;
    applyRefused(none, q, ErrorCode::NotFound);
}

TEST_CASE("a new wall takes the thickness of the wall it joins", "[core][command][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.walls[1].thickness = 0.1; // w2, at p2 and p3
    const auto drawn = [&](DrawWallCommand& c) {
        applyOk(c, p);
        return findWall(p.plan.floors[0], c.wallId())->thickness;
    };

    SECTION("from a point: the first wall of that point") {
        DrawWallCommand c("f1", WallEnd::atPoint("p2"), WallEnd::newPoint({9.0, 0.0}));
        REQUIRE(drawn(c) == 0.3);
    }
    SECTION("from a place on a wall: that wall") {
        DrawWallCommand c("f1", WallEnd::onWall("w2", 1.0), WallEnd::newPoint({9.0, 1.0}));
        REQUIRE(drawn(c) == 0.1);
    }
    SECTION("to a wall when the start joins nothing") {
        DrawWallCommand c("f1", WallEnd::newPoint({9.0, 1.0}), WallEnd::onWall("w2", 1.0));
        REQUIRE(drawn(c) == 0.1);
    }
    SECTION("both ends joined with different thickness: the start wins") {
        DrawWallCommand c("f1", WallEnd::onWall("w2", 2.0), WallEnd::onWall("w4", 2.0));
        REQUIRE(drawn(c) == 0.1);
    }
    SECTION("the same ends the other way round") {
        DrawWallCommand c("f1", WallEnd::onWall("w4", 2.0), WallEnd::onWall("w2", 2.0));
        REQUIRE(drawn(c) == 0.3);
    }
    SECTION("a given thickness wins") {
        DrawWallCommand c("f1", WallEnd::onWall("w2", 2.0), WallEnd::newPoint({9.0, 2.0}), 0.25);
        REQUIRE(drawn(c) == 0.25);
    }
    SECTION("joining nothing uses the thickness for unjoined walls") {
        DrawWallCommand c("f1", WallEnd::newPoint({9.0, 1.0}), WallEnd::newPoint({9.0, 3.0}), std::nullopt, std::nullopt, 0.15);
        REQUIRE(drawn(c) == 0.15);
        DrawWallCommand d("f1", WallEnd::newPoint({10.0, 1.0}), WallEnd::newPoint({10.0, 3.0}));
        REQUIRE(drawn(d) == kDefaultWallThickness);
    }
    SECTION("a bad unjoined thickness is refused only when it is used") {
        DrawWallCommand used("f1", WallEnd::newPoint({9.0, 1.0}), WallEnd::newPoint({9.0, 3.0}), std::nullopt, std::nullopt, 0.0);
        applyRefused(used, p, ErrorCode::InvalidValue);
        DrawWallCommand unused("f1", WallEnd::atPoint("p2"), WallEnd::newPoint({9.0, 0.0}), std::nullopt, std::nullopt, 0.0);
        REQUIRE(drawn(unused) == 0.3);
    }
    SECTION("the chosen thickness is kept for redo") {
        DrawWallCommand c("f1", WallEnd::atPoint("p2"), WallEnd::newPoint({9.0, 0.0}));
        roundTrip(c, p);
        REQUIRE(findWall(p.plan.floors[0], c.wallId())->thickness == 0.3);
    }
    SECTION("changing one wall's thickness changes only that wall") {
        DrawWallCommand c("f1", WallEnd::atPoint("p2"), WallEnd::newPoint({9.0, 0.0}));
        applyOk(c, p);
        SetWallThicknessCommand set(c.wallId(), 0.5);
        applyOk(set, p);
        REQUIRE(findWall(p.plan.floors[0], "w1")->thickness == 0.3);
        REQUIRE(findWall(p.plan.floors[0], c.wallId())->thickness == 0.5);
    }
}

TEST_CASE("joinedThickness", "[core][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.walls[1].thickness = 0.1;
    f.points.push_back({"p9", 9.0, 9.0, 0.0});
    REQUIRE(joinedThickness(f, WallEnd::atPoint("p3")) == std::optional<double>(0.1));
    REQUIRE(joinedThickness(f, WallEnd::atPoint("p1")) == std::optional<double>(0.3));
    REQUIRE(joinedThickness(f, WallEnd::onWall("w2", 1.0)) == std::optional<double>(0.1));
    REQUIRE_FALSE(joinedThickness(f, WallEnd::atPoint("p9")).has_value());
    REQUIRE_FALSE(joinedThickness(f, WallEnd::atPoint("p99")).has_value());
    REQUIRE_FALSE(joinedThickness(f, WallEnd::onWall("w99", 1.0)).has_value());
    REQUIRE_FALSE(joinedThickness(f, WallEnd::newPoint({1.0, 1.0})).has_value());
}

TEST_CASE("JoinPointCommand joins a loose wall end onto a point", "[core][command][point][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.points.push_back({"p5", 9.0, 4.0, 0.0});
    f.points.push_back({"p6", 6.1, 4.1, 0.0});
    addWall(p, "w5", "p5", "p6", 0.1);

    JoinPointCommand c("p6", WallEnd::atPoint("p3"));
    roundTrip(c, p);
    const Floor& g = p.plan.floors[0];
    REQUIRE(c.targetPointId() == "p3");
    REQUIRE(c.name() == "Join point");
    REQUIRE(c.floorId() == "f1");
    REQUIRE_FALSE(findPoint(g, "p6"));
    REQUIRE(findWall(g, "w5")->to == "p3");
    REQUIRE(findWall(g, "w5")->thickness == 0.3);
    REQUIRE(findWall(g, "w2")->thickness == 0.3);
    REQUIRE(c.newRoomIds().empty());
}

TEST_CASE("JoinPointCommand onto a wall splits it and takes its thickness", "[core][command][point][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.walls[1].thickness = 0.12;
    f.points.push_back({"p5", 9.0, 2.0, 0.0});
    f.points.push_back({"p6", 6.2, 2.0, 0.0});
    addWall(p, "w5", "p5", "p6", 0.3);

    JoinPointCommand c("p6", WallEnd::onWall("w2", 2.0));
    roundTrip(c, p);
    const Floor& g = p.plan.floors[0];
    const Point* joined = findPoint(g, c.targetPointId());
    REQUIRE(joined);
    REQUIRE(joined->x == Approx(6.0));
    REQUIRE(joined->y == Approx(2.0));
    REQUIRE(findWall(g, "w5")->to == c.targetPointId());
    REQUIRE(findWall(g, "w5")->thickness == 0.12);
    REQUIRE(g.walls.size() == 6);
    REQUIRE(g.rooms[0].walls.size() == 5);
    REQUIRE(roomShape(g, g.rooms[0]).closed);
}

TEST_CASE("JoinPointCommand splits a room the joined wall now cuts", "[core][command][point][room]") {
    Project p = roomProject();
    DrawWallCommand loose("f1", WallEnd::onWall("w1", 2.0), WallEnd::newPoint({2.0, 3.8}));
    applyOk(loose, p);
    REQUIRE(p.plan.floors[0].rooms.size() == 1);
    JoinPointCommand c(loose.toPointId(), WallEnd::onWall("w3", 4.0));
    roundTrip(c, p);
    REQUIRE(c.newRoomIds().size() == 1);
    REQUIRE(p.plan.floors[0].rooms.size() == 2);
}

TEST_CASE("JoinPointCommand keeps a curved wall's thickness when the joined one would bend it too tightly",
          "[core][command][point][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.walls[1].thickness = 3.5;
    f.points.push_back({"p5", 9.0, 2.0, 0.0});
    f.points.push_back({"p6", 6.1, 2.0, 0.0});
    addWall(p, "w5", "p5", "p6", 0.2);
    // Over 3 m with a bend of 1.4 the radius is about 1.5, too tight for a 3.5 m thick wall but fine at 0.2 m.
    f.walls.back().bulge = 1.4;
    plynth::test::requirePlanValid(p.plan);

    JoinPointCommand c("p6", WallEnd::onWall("w2", 2.0));
    applyOk(c, p);
    REQUIRE(findWall(p.plan.floors[0], "w5")->thickness == 0.2);
    REQUIRE(findWall(p.plan.floors[0], "w5")->to == c.targetPointId());
}

TEST_CASE("JoinPointCommand refusals change nothing", "[core][command][point][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.points.push_back({"p5", 9.0, 0.0, 0.0});
    addWall(p, "w5", "p2", "p5");
    f.points.push_back({"p6", 9.0, 9.0, 0.0});

    SECTION("the point itself") {
        JoinPointCommand c("p1", WallEnd::atPoint("p1"));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a point a wall of it already runs to") {
        JoinPointCommand c("p1", WallEnd::atPoint("p2"));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a wall that ends at it") {
        JoinPointCommand c("p1", WallEnd::onWall("w1", 1.0));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a new point") {
        JoinPointCommand c("p1", WallEnd::newPoint({1.0, 1.0}));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("two walls on the same two points") {
        JoinPointCommand c("p1", WallEnd::atPoint("p3"));
        applyRefused(c, p, ErrorCode::AlreadyExists);
    }
    SECTION("unknown ids") {
        JoinPointCommand point("p99", WallEnd::atPoint("p1"));
        applyRefused(point, p, ErrorCode::NotFound);
        JoinPointCommand target("p6", WallEnd::atPoint("p99"));
        applyRefused(target, p, ErrorCode::NotFound);
        JoinPointCommand wall("p6", WallEnd::onWall("w99", 1.0));
        applyRefused(wall, p, ErrorCode::NotFound);
    }
    SECTION("another floor") {
        Floor up;
        up.id = "f2";
        up.name = "Up";
        up.points = {{"p20", 0.0, 0.0, 0.0}};
        p.plan.floors.push_back(up);
        JoinPointCommand c("p6", WallEnd::atPoint("p20"));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("a place covered by an opening, after the checks pass") {
        JoinPointCommand c("p6", WallEnd::onWall("w1", 4.5));
        applyRefused(c, p, ErrorCode::InUse);
    }
    SECTION("a place too near the end of the wall") {
        JoinPointCommand c("p6", WallEnd::onWall("w3", 0.0));
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("a drag that ends in a join is one undo step", "[core][command][point][wall]") {
    Project p = roomProject();
    Floor& f = p.plan.floors[0];
    f.points.push_back({"p5", 9.0, 4.0, 0.0});
    f.points.push_back({"p6", 8.0, 6.0, 0.0});
    addWall(p, "w5", "p5", "p6", 0.1);
    const Project start = p;

    CommandStack stack;
    REQUIRE(stack.push(std::make_unique<MovePointCommand>("p6", 7.0, 5.0, 7), p).ok());
    REQUIRE(stack.push(std::make_unique<MovePointCommand>("p6", 6.0, 4.0, 7), p).ok());
    bool merged = false;
    REQUIRE(stack.push(std::make_unique<JoinPointCommand>("p6", WallEnd::atPoint("p3"), 7), p, &merged).ok());
    REQUIRE(merged);
    REQUIRE(stack.count() == 1);
    REQUIRE(stack.nextUndo()->name() == "Join point");
    REQUIRE((stack.nextUndo()->changes() & Change::Walls) != 0);
    const Project joined = p;
    REQUIRE_FALSE(findPoint(p.plan.floors[0], "p6"));
    REQUIRE(findWall(p.plan.floors[0], "w5")->thickness == 0.3);

    SECTION("undo and redo") {
        stack.undo(p);
        REQUIRE(p == start);
        REQUIRE(stack.redo(p).ok());
        REQUIRE(p == joined);
    }
    SECTION("no move merges after the join") {
        REQUIRE(stack.push(std::make_unique<MovePointCommand>("p3", 6.5, 4.0, 7), p).ok());
        REQUIRE(stack.count() == 2);
    }
    SECTION("a join of another key or point stays its own step") {
        stack.undo(p);
        REQUIRE(stack.push(std::make_unique<MovePointCommand>("p6", 6.0, 4.0, 8), p).ok());
        REQUIRE(stack.push(std::make_unique<JoinPointCommand>("p6", WallEnd::atPoint("p3"), 9), p, &merged).ok());
        REQUIRE_FALSE(merged);
        REQUIRE(stack.count() == 2);
    }
}

TEST_CASE("snapping lines positions up with nearby points", "[core][snap]") {
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 7.0, 3.0, 0.0}};
    addWall(p, "w1", "p1", "p2");
    SnapOptions o;
    o.radius = 0.1;

    SECTION("x lines up with a point and says which") {
        const SnapResult r = snapPosition(f, {7.05, 1.0}, o);
        REQUIRE(r.kind == SnapKind::Align);
        REQUIRE(r.position == Vec2{7.0, 1.0});
        REQUIRE(r.alignedX == std::optional<Vec2>(Vec2{7.0, 3.0}));
        REQUIRE_FALSE(r.alignedY.has_value());
    }
    SECTION("a slightly lower end comes out level, lined up with the closer of two equal points") {
        const SnapResult r = snapPosition(f, {8.0, 0.04}, o);
        REQUIRE(r.kind == SnapKind::Align);
        REQUIRE(r.position == Vec2{8.0, 0.0});
        REQUIRE(r.alignedY == std::optional<Vec2>(Vec2{4.0, 0.0}));
    }
    SECTION("x and y together") {
        const SnapResult r = snapPosition(f, {7.06, 0.05}, o);
        REQUIRE(r.kind == SnapKind::Align);
        REQUIRE(r.position == Vec2{7.0, 0.0});
        REQUIRE(r.alignedX.has_value());
        REQUIRE(r.alignedY.has_value());
    }
    SECTION("points and walls win over alignment") {
        REQUIRE(snapPosition(f, {7.05, 3.02}, o).kind == SnapKind::Point);
        REQUIRE(snapPosition(f, {2.0, 0.05}, o).kind == SnapKind::Wall);
    }
    SECTION("alignment wins over the grid for its coordinate, the other goes to the grid") {
        SnapOptions grid = o;
        grid.gridSize = 0.5;
        const SnapResult r = snapPosition(f, {7.07, 1.2}, grid);
        REQUIRE(r.kind == SnapKind::Align);
        REQUIRE(r.position == Vec2{7.0, 1.0});
        const SnapResult plain = snapPosition(f, {5.3, 1.2}, grid);
        REQUIRE(plain.kind == SnapKind::Grid);
        REQUIRE(plain.position == Vec2{5.5, 1.0});
    }
    SECTION("the nearest coordinate wins") {
        f.points.push_back({"p4", 7.04, 9.0, 0.0});
        REQUIRE(snapPosition(f, {7.05, 1.0}, o).position.x == 7.04);
    }
    SECTION("extra places and ignored points") {
        SnapOptions extra = o;
        extra.alignTo = {{10.0, 10.0}};
        const SnapResult r = snapPosition(f, {10.03, 1.0}, extra);
        REQUIRE(r.kind == SnapKind::Align);
        REQUIRE(r.position.x == 10.0);
        SnapOptions ignored = o;
        ignored.ignorePoints = {"p3"};
        REQUIRE(snapPosition(f, {7.05, 1.0}, ignored).kind == SnapKind::None);
    }
    SECTION("with Shift only the free coordinate lines up") {
        SnapOptions shift = o;
        shift.orthogonalFrom = Vec2{9.0, 1.03};
        const SnapResult r = snapPosition(f, {7.04, 1.5}, shift);
        REQUIRE(r.kind == SnapKind::Align);
        REQUIRE(r.position == Vec2{7.0, 1.03});
        REQUIRE_FALSE(r.alignedY.has_value());
    }
    SECTION("off with radius 0 or toAlignment false") {
        REQUIRE(snapPosition(f, {7.05, 1.0}, SnapOptions{}).kind == SnapKind::None);
        SnapOptions off = o;
        off.toAlignment = false;
        REQUIRE(snapPosition(f, {7.05, 1.0}, off).kind == SnapKind::None);
    }
    SECTION("an aligned position is a new point for a wall end") {
        const WallEnd e = wallEndFromSnap(snapPosition(f, {7.05, 1.0}, o));
        REQUIRE(e.kind == WallEnd::Kind::NewPoint);
        REQUIRE(e.position == Vec2{7.0, 1.0});
    }
}
