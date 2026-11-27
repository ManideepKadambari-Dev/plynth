#include "FakeDetector.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/DetectionCommands.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/StairCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// Applies a command, checks the plan is still valid, undoes and redoes it, and returns the state after it.
Project applyUndoRedo(Command& c, Project& p)
{
    const Project before = p;
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
    const Project after = p;
    REQUIRE(after != before);
    c.undo(p);
    REQUIRE(p == before);
    requireOk(c.apply(p));
    REQUIRE(p == after);
    return after;
}

// Applies a command that must be refused and checks nothing changed.
void refused(Command& c, Project& p, ErrorCode code)
{
    const Project before = p;
    const Status s = c.apply(p);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    REQUIRE(p == before);
}

// A project with floors f1 and f2, one point on f2.
Project twoFloors()
{
    Project p = makeNewProject();
    AddFloorCommand add("Upper");
    requireOk(add.apply(p));
    p.plan.floors[1].points.push_back({"p1", 2.0, 2.0, 0.0});
    return p;
}

}

TEST_CASE("AddFloorCommand adds a floor on top or at a position with a stable id", "[core][command][floor]") {
    Project p = makeNewProject();
    p.settings.defaults.ceilingHeight = 2.7;

    AddFloorCommand top("  Upper  ");
    REQUIRE(top.changes() == Change::Floors);
    const Project after = applyUndoRedo(top, p);
    REQUIRE(top.newFloorId() == "f2");
    REQUIRE(top.floorId() == "f2");
    REQUIRE(after.plan.floors.size() == 2);
    REQUIRE(after.plan.floors[1].name == "Upper");
    REQUIRE(after.plan.floors[1].ceilingHeight == 2.7);
    REQUIRE(after.plan.floors[1].slabThickness == kDefaultSlabThickness);
    REQUIRE_FALSE(after.plan.floors[1].image.has_value());

    AddFloorCommand below("Cellar", 0);
    requireOk(below.apply(p));
    REQUIRE(p.plan.floors[0].name == "Cellar");
    REQUIRE(p.plan.floors[0].id == "f3");
    below.undo(p);
    requireOk(below.apply(p));
    REQUIRE(p.plan.floors[0].id == "f3");
    requirePlanValid(p.plan);
}

TEST_CASE("AddFloorCommand puts a new floor first with id f2 on a plan that has f1", "[core][command][floor]") {
    Project p = makeNewProject();
    AddFloorCommand c("Below", 0);
    requireOk(c.apply(p));
    REQUIRE(p.plan.floors[0].id == "f2");
    REQUIRE(p.plan.floors[1].id == "f1");
}

TEST_CASE("AddFloorCommand refuses bad values", "[core][command][floor]") {
    Project p = makeNewProject();
    AddFloorCommand empty("   ");
    refused(empty, p, ErrorCode::InvalidValue);
    AddFloorCommand past("A", 5);
    refused(past, p, ErrorCode::InvalidValue);
}

TEST_CASE("RemoveFloorCommand removes a floor and its roof entries and brings them back", "[core][command][floor]") {
    RoofSection a;
    a.id = "rf1";
    a.floor = "f2";
    RoofSection b;
    b.id = "rf2";
    b.floor = "f1";
    RoofSection c2;
    c2.id = "rf3";
    c2.floor = "f2";
    Project p = twoFloors();
    p.plan.roofSections = {a, b, c2};
    const Project fresh = p;

    RemoveFloorCommand c("f2");
    const unsigned all = Change::Floors | Change::Stairs | Change::Roofs | Change::Lights;
    REQUIRE((c.changes() & all) == all);
    requireOk(c.apply(p));
    REQUIRE(p.plan.floors.size() == 1);
    REQUIRE(p.plan.roofSections.size() == 1);
    REQUIRE(p.plan.roofSections[0].id == "rf2");
    requirePlanValid(p.plan);
    c.undo(p);
    REQUIRE(p == fresh);
}

TEST_CASE("RemoveFloorCommand refuses the only floor and unknown floors", "[core][command][floor]") {
    Project p = makeNewProject();
    RemoveFloorCommand only("f1");
    refused(only, p, ErrorCode::InvalidValue);
    Project two = twoFloors();
    RemoveFloorCommand unknown("f9");
    refused(unknown, two, ErrorCode::NotFound);
}

TEST_CASE("SetFloorNameCommand and SetSlabThicknessCommand", "[core][command][floor]") {
    Project p = twoFloors();
    SetFloorNameCommand name("f2", " Attic ");
    REQUIRE(name.floorId() == "f2");
    const Project after = applyUndoRedo(name, p);
    REQUIRE(after.plan.floors[1].name == "Attic");
    SetFloorNameCommand empty("f2", " ");
    refused(empty, p, ErrorCode::InvalidValue);
    SetFloorNameCommand unknown("f9", "x");
    refused(unknown, p, ErrorCode::NotFound);

    SetSlabThicknessCommand slab("f1", 0.3);
    REQUIRE(slab.changes() == Change::Floors);
    const Project thick = applyUndoRedo(slab, p);
    REQUIRE(thick.plan.floors[0].slabThickness == 0.3);
    for (const double bad : {0.01, 1.5, std::numeric_limits<double>::quiet_NaN()}) {
        SetSlabThicknessCommand c("f1", bad);
        refused(c, p, ErrorCode::InvalidValue);
    }
    SetSlabThicknessCommand unknownSlab("f9", 0.2);
    refused(unknownSlab, p, ErrorCode::NotFound);

    const Project before = p;
    CommandStack stack;
    requireOk(stack.push(std::make_unique<SetSlabThicknessCommand>("f1", 0.25, 7), p));
    requireOk(stack.push(std::make_unique<SetSlabThicknessCommand>("f1", 0.35, 7), p));
    REQUIRE(p.plan.floors[0].slabThickness == 0.35);
    stack.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("SetFloorImageOffsetCommand moves the image and, by default, the marks", "[core][command][floor]") {
    Project p = twoFloors();
    Floor& f2 = p.plan.floors[1];
    Item item;
    item.id = "i1";
    item.asset = "sofa";
    item.x = 1.0;
    item.y = 1.0;
    item.size = {1, 1, 1};
    f2.items.push_back(item);
    Stair stair;
    stair.id = "s1";
    stair.x = 3.0;
    stair.y = 3.0;
    f2.stairs.push_back(stair);
    RoofSection rs;
    rs.id = "rf1";
    rs.floor = "f2";
    rs.x = 2.0;
    rs.y = 2.0;
    p.plan.roofSections.push_back(rs);

    SetFloorImageOffsetCommand c("f2", {1.0, 0.0});
    REQUIRE((c.changes() & Change::FloorImage) != 0);
    REQUIRE(c.floorId() == "f2");
    const Project after = applyUndoRedo(c, p);
    const Floor& moved = after.plan.floors[1];
    REQUIRE(moved.imageOffset == ImageOffset{1.0, 0.0});
    REQUIRE(moved.points[0].x == Approx(3.0));
    REQUIRE(moved.points[0].y == Approx(2.0));
    REQUIRE(moved.items[0].x == Approx(2.0));
    REQUIRE(moved.stairs[0].x == Approx(4.0));
    REQUIRE(after.plan.roofSections[0].x == Approx(3.0));

    SetFloorImageOffsetCommand marksStay("f2", {2.0, 0.0}, false);
    REQUIRE(marksStay.changes() == Change::FloorImage);
    const Project still = applyUndoRedo(marksStay, p);
    REQUIRE(still.plan.floors[1].points[0].x == Approx(3.0));
    REQUIRE(still.plan.floors[1].imageOffset.x == 2.0);

    SetFloorImageOffsetCommand bad("f2", {std::numeric_limits<double>::infinity(), 0.0});
    refused(bad, p, ErrorCode::InvalidValue);
    SetFloorImageOffsetCommand unknown("f9", {1.0, 0.0});
    refused(unknown, p, ErrorCode::NotFound);
}

TEST_CASE("two image offset drags with one merge key undo as one", "[core][command][floor]") {
    Project p = twoFloors();
    const Project before = p;
    CommandStack stack;
    requireOk(stack.push(std::make_unique<SetFloorImageOffsetCommand>("f2", ImageOffset{0.5, 0.0}, true, 3), p));
    requireOk(stack.push(std::make_unique<SetFloorImageOffsetCommand>("f2", ImageOffset{1.0, 0.0}, true, 3), p));
    REQUIRE(p.plan.floors[1].points[0].x == Approx(3.0));
    stack.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("SetScaleCommand scales stairs, items and anchors about the image offset", "[core][command][scale]") {
    Project p = twoFloors();
    p.plan.scale.pixelsPerMetre = 100.0;
    Floor& f2 = p.plan.floors[1];
    f2.imageOffset = {1.0, 1.0};
    f2.points[0].x = 3.0;
    f2.points[0].y = 1.0;
    Stair stair;
    stair.id = "s1";
    stair.x = 2.0;
    stair.y = 3.0;
    f2.stairs.push_back(stair);
    RoofSection rs;
    rs.id = "rf1";
    rs.floor = "f2";
    rs.x = 3.0;
    rs.y = 3.0;
    p.plan.roofSections.push_back(rs);

    SetScaleCommand c(50.0);
    const Project before = p;
    requireOk(c.apply(p));
    REQUIRE(c.movedMarks());
    const unsigned all = Change::Stairs | Change::Roofs | Change::Items | Change::Lights;
    REQUIRE((c.changes() & all) == all);
    REQUIRE(p.plan.floors[1].points[0].x == Approx(5.0));
    REQUIRE(p.plan.floors[1].points[0].y == Approx(1.0));
    REQUIRE(p.plan.floors[1].stairs[0].x == Approx(3.0));
    REQUIRE(p.plan.floors[1].stairs[0].y == Approx(5.0));
    REQUIRE(p.plan.roofSections[0].x == Approx(5.0));
    requirePlanValid(p.plan);
    c.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("detection applied on a floor with an image offset lands that far over", "[core][command][floor][detection]") {
    DetectionResult proposals = fixedProposals();
    proposeRooms(proposals);

    Project plain = makeNewProject();
    ApplyDetectionCommand a("f1", proposals);
    requireOk(a.apply(plain));

    Project shifted = makeNewProject();
    shifted.plan.floors[0].imageOffset = {1.0, 0.0};
    ApplyDetectionCommand b("f1", proposals);
    requireOk(b.apply(shifted));

    const Floor& pf = plain.plan.floors[0];
    const Floor& sf = shifted.plan.floors[0];
    REQUIRE(pf.points.size() == sf.points.size());
    REQUIRE(sf.points.size() == 6);
    for (std::size_t i = 0; i < pf.points.size(); ++i) {
        REQUIRE(sf.points[i].x == Approx(pf.points[i].x + 1.0));
        REQUIRE(sf.points[i].y == Approx(pf.points[i].y));
    }
    REQUIRE(sf.rooms.size() == pf.rooms.size());
    requirePlanValid(shifted.plan);
}

TEST_CASE("delete removes named stairs first and undo brings them back", "[core][command][delete][stairs]") {
    Project p = twoFloors();
    Stair stair;
    stair.id = "s1";
    stair.x = 1.0;
    stair.y = 1.0;
    p.plan.floors[0].stairs.push_back(stair);

    const std::vector<Id> ids = {"s1"};
    DeleteBuild build = makeDeleteCommand(p.plan, ids);
    requireOk(build.status);
    REQUIRE(build.removes.stairs == std::vector<Id>{"s1"});
    const Project before = p;
    requireOk(build.command->apply(p));
    REQUIRE(p.plan.floors[0].stairs.empty());
    requirePlanValid(p.plan);
    build.command->undo(p);
    REQUIRE(p == before);

    const std::vector<Id> mixed = {"s1", "p1"};
    REQUIRE(makeDeleteCommand(p.plan, mixed).status.code == ErrorCode::InvalidValue);
}

TEST_CASE("nextFloorName counts on from the floors and skips names in use", "[core][floor]")
{
    Plan plan = makeEmptyPlan();
    REQUIRE(plan.floors.size() == 1);
    REQUIRE(plan.floors[0].name == "Floor 1");
    REQUIRE(nextFloorName(plan) == "Floor 2");

    SECTION("a plan without floors starts at Floor 1") {
        plan.floors.clear();
        REQUIRE(nextFloorName(plan) == "Floor 1");
    }
    SECTION("floors named otherwise keep the count") {
        plan.floors[0].name = "Ground";
        Floor upper = plan.floors[0];
        upper.id = "f2";
        upper.name = "Attic";
        plan.floors.push_back(upper);
        REQUIRE(nextFloorName(plan) == "Floor 3");
    }
    SECTION("after a deleted floor the next number that is free is taken") {
        Floor second = plan.floors[0];
        second.id = "f2";
        second.name = "Floor 2";
        Floor third = plan.floors[0];
        third.id = "f3";
        third.name = "Floor 3";
        plan.floors.push_back(second);
        plan.floors.push_back(third);
        plan.floors.erase(plan.floors.begin() + 1);
        // Two floors, but "Floor 3" is taken, so the count plus one would repeat a name.
        REQUIRE(nextFloorName(plan) == "Floor 4");
    }
    SECTION("several taken names in a row are all skipped") {
        plan.floors[0].name = "Floor 2";
        Floor other = plan.floors[0];
        other.id = "f2";
        other.name = "Floor 3";
        plan.floors.push_back(other);
        Floor last = plan.floors[0];
        last.id = "f3";
        last.name = "Floor 4";
        plan.floors.push_back(last);
        REQUIRE(nextFloorName(plan) == "Floor 5");
    }
    SECTION("names are compared exactly") {
        plan.floors[0].name = "floor 2";
        REQUIRE(nextFloorName(plan) == "Floor 2");
        plan.floors[0].name = "Floor 2 ";
        REQUIRE(nextFloorName(plan) == "Floor 2");
    }
    SECTION("the name is accepted by AddFloorCommand") {
        Project project = makeNewProject();
        AddFloorCommand add(nextFloorName(project.plan));
        requireOk(add.apply(project));
        REQUIRE(project.plan.floors.back().name == "Floor 2");
        REQUIRE(nextFloorName(project.plan) == "Floor 3");
    }
}
