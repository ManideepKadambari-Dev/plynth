#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/commands/StairCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A project with two floors, f1 below f2.
Project twoFloorProject()
{
    Project p = makeNewProject();
    Floor f2;
    f2.id = "f2";
    f2.name = "Floor 2";
    p.plan.floors.push_back(f2);
    return p;
}

// A stair at a position.
Stair stairAt(double x, double y)
{
    Stair s;
    s.x = x;
    s.y = y;
    return s;
}

}

TEST_CASE("AddStairCommand adds, undoes and redoes with the same id", "[stairs][commands]")
{
    Project p = twoFloorProject();
    const Project before = p;
    AddStairCommand add("f1", stairAt(1.0, 2.0));
    REQUIRE(add.newStairId().empty());
    requireOk(add.apply(p));
    requirePlanValid(p.plan);
    CHECK(add.newStairId() == "s1");
    REQUIRE(p.plan.floors[0].stairs.size() == 1);
    CHECK(p.plan.floors[0].stairs[0].id == "s1");
    CHECK(p.plan.floors[0].stairs[0].x == Approx(1.0));
    CHECK(add.changes() == Change::Stairs);
    CHECK(add.floorId() == "f1");

    add.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(add.apply(p));
    CHECK(p.plan.floors[0].stairs[0].id == "s1");
}

TEST_CASE("AddStairCommand on the top floor is refused", "[stairs][commands]")
{
    Project p = twoFloorProject();
    const Project before = p;
    AddStairCommand add("f2", stairAt(1.0, 1.0));
    const Status s = add.apply(p);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message == "Add a floor above before placing a stair.");
    CHECK(p.plan == before.plan);

    Project single = makeNewProject();
    AddStairCommand alone("f1", stairAt(1.0, 1.0));
    CHECK(alone.apply(single).message == "Add a floor above before placing a stair.");
}

TEST_CASE("AddStairCommand refuses a missing floor and bad values", "[stairs][commands]")
{
    Project p = twoFloorProject();
    const Project before = p;
    CHECK(AddStairCommand("f9", stairAt(0, 0)).apply(p).code == ErrorCode::NotFound);

    Stair bad = stairAt(0, 0);
    bad.type = "ladder";
    CHECK(AddStairCommand("f1", bad).apply(p).code == ErrorCode::InvalidValue);
    bad = stairAt(0, 0);
    bad.width = 5.0;
    CHECK(AddStairCommand("f1", bad).apply(p).code == ErrorCode::InvalidValue);
    bad = stairAt(std::numeric_limits<double>::quiet_NaN(), 0);
    CHECK(AddStairCommand("f1", bad).apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);
}

TEST_CASE("AddStairCommand normalizes the rotation and a second stair gets s2", "[stairs][commands]")
{
    Project p = twoFloorProject();
    Stair s = stairAt(0, 0);
    s.rotation = -90.0;
    AddStairCommand first("f1", s);
    requireOk(first.apply(p));
    CHECK(p.plan.floors[0].stairs[0].rotation == Approx(270.0));
    AddStairCommand second("f1", stairAt(3, 3));
    requireOk(second.apply(p));
    CHECK(second.newStairId() == "s2");
    requirePlanValid(p.plan);
}

TEST_CASE("MoveStairCommand moves, undoes and redoes", "[stairs][commands]")
{
    Project p = twoFloorProject();
    AddStairCommand add("f1", stairAt(1.0, 1.0));
    requireOk(add.apply(p));
    const Project before = p;

    MoveStairCommand move("s1", 2.5, 3.5, 450.0);
    requireOk(move.apply(p));
    requirePlanValid(p.plan);
    CHECK(p.plan.floors[0].stairs[0].x == Approx(2.5));
    CHECK(p.plan.floors[0].stairs[0].y == Approx(3.5));
    CHECK(p.plan.floors[0].stairs[0].rotation == Approx(90.0));
    CHECK(move.floorId() == "f1");
    CHECK(move.changes() == Change::Stairs);

    move.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(move.apply(p));
    CHECK(p.plan.floors[0].stairs[0].x == Approx(2.5));
}

TEST_CASE("MoveStairCommand refuses an unknown stair and bad numbers", "[stairs][commands]")
{
    Project p = twoFloorProject();
    AddStairCommand add("f1", stairAt(1.0, 1.0));
    requireOk(add.apply(p));
    const Project before = p;
    CHECK(MoveStairCommand("s9", 0, 0, 0).apply(p).code == ErrorCode::NotFound);
    CHECK(MoveStairCommand("s1", std::numeric_limits<double>::infinity(), 0, 0).apply(p).code == ErrorCode::InvalidValue);
    CHECK(MoveStairCommand("s1", 0, 0, std::numeric_limits<double>::quiet_NaN()).apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);
}

TEST_CASE("two moves with one merge key undo as one step", "[stairs][commands]")
{
    Project p = twoFloorProject();
    CommandStack stack;
    requireOk(stack.push(std::make_unique<AddStairCommand>("f1", stairAt(1.0, 1.0)), p));
    const Plan beforeMoves = p.plan;

    bool merged = false;
    requireOk(stack.push(std::make_unique<MoveStairCommand>("s1", 2.0, 2.0, 0.0, 7), p, &merged));
    CHECK_FALSE(merged);
    requireOk(stack.push(std::make_unique<MoveStairCommand>("s1", 3.0, 4.0, 90.0, 7), p, &merged));
    CHECK(merged);
    CHECK(p.plan.floors[0].stairs[0].x == Approx(3.0));
    CHECK(stack.count() == 2);

    stack.undo(p);
    CHECK(p.plan == beforeMoves);
    requireOk(stack.redo(p));
    CHECK(p.plan.floors[0].stairs[0].y == Approx(4.0));
    CHECK(p.plan.floors[0].stairs[0].rotation == Approx(90.0));

    requireOk(stack.push(std::make_unique<MoveStairCommand>("s1", 5.0, 5.0, 0.0, 0), p, &merged));
    CHECK_FALSE(merged);
    requireOk(stack.push(std::make_unique<MoveStairCommand>("s1", 6.0, 6.0, 0.0, 0), p, &merged));
    CHECK_FALSE(merged);
}

TEST_CASE("SetStairCommand replaces the values but not the id", "[stairs][commands]")
{
    Project p = twoFloorProject();
    AddStairCommand add("f1", stairAt(1.0, 1.0));
    requireOk(add.apply(p));
    const Project before = p;

    Stair values = stairAt(4.0, 5.0);
    values.id = "s77";
    values.type = "spiral";
    values.width = 1.4;
    values.turn = StairTurn::Left;
    values.rotation = 180.0;
    values.material = "grey_tile";
    SetStairCommand set("s1", values);
    requireOk(set.apply(p));
    requirePlanValid(p.plan);
    const Stair& now = p.plan.floors[0].stairs[0];
    CHECK(now.id == "s1");
    CHECK(now.type == "spiral");
    CHECK(now.width == Approx(1.4));
    CHECK(now.turn == StairTurn::Left);
    CHECK(now.material == "grey_tile");
    CHECK(set.floorId() == "f1");

    set.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(set.apply(p));
    CHECK(p.plan.floors[0].stairs[0].type == "spiral");
}

TEST_CASE("SetStairCommand refuses what checkStair refuses", "[stairs][commands]")
{
    Project p = twoFloorProject();
    AddStairCommand add("f1", stairAt(1.0, 1.0));
    requireOk(add.apply(p));
    const Project before = p;

    CHECK(SetStairCommand("s9", stairAt(0, 0)).apply(p).code == ErrorCode::NotFound);
    Stair bad = stairAt(0, 0);
    bad.type = "ladder";
    const Status s = SetStairCommand("s1", bad).apply(p);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message.find("straight") != std::string::npos);
    bad = stairAt(0, 0);
    bad.material = "not valid";
    CHECK(SetStairCommand("s1", bad).apply(p).code == ErrorCode::InvalidValue);
    bad = stairAt(0, 0);
    bad.width = 0.2;
    CHECK(SetStairCommand("s1", bad).apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);
}

TEST_CASE("RemoveStairCommand removes and puts the stair back in place", "[stairs][commands]")
{
    Project p = twoFloorProject();
    for (int i = 0; i < 3; ++i) {
        AddStairCommand add("f1", stairAt(i, i));
        requireOk(add.apply(p));
    }
    const Project before = p;

    RemoveStairCommand remove("s2");
    requireOk(remove.apply(p));
    requirePlanValid(p.plan);
    REQUIRE(p.plan.floors[0].stairs.size() == 2);
    CHECK(p.plan.floors[0].stairs[1].id == "s3");
    CHECK(remove.floorId() == "f1");
    CHECK(remove.changes() == Change::Stairs);

    remove.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(remove.apply(p));
    CHECK(p.plan.floors[0].stairs.size() == 2);

    CHECK(RemoveStairCommand("s9").apply(p).code == ErrorCode::NotFound);
}
