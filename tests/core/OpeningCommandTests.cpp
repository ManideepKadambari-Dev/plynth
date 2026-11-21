#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/Openings.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/OpeningCommands.h>
#include <plynth/core/commands/WallCommands.h>

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

// Applies a command, then checks it succeeded and left a valid plan.
void applyOk(Command& c, Project& p)
{
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
}

// Applies a command that must be refused, and checks the project did not change.
void applyRefused(Command& c, Project& p, ErrorCode code, const std::string& word = {})
{
    const Project before = p;
    const Status s = c.apply(p);
    INFO(s.message);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    if (!word.empty()) REQUIRE(s.message.find(word) != std::string::npos);
    REQUIRE(p == before);
}

// Applies, undoes and redoes a command, checking undo gives an equal project and redo the first result.
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

// The square with one door o1 on w1 at offset 2.0.
Project projectWithDoor()
{
    Project p = squareProjectWithWalls();
    AddOpeningCommand add(doorOn("w1", 2.0));
    requireOk(add.apply(p));
    return p;
}

}

TEST_CASE("AddOpeningCommand", "[core][command][opening]") {
    Project p = squareProjectWithWalls();
    SECTION("adds, undoes and redoes with the same id") {
        AddOpeningCommand c(doorOn("w1", 2.0));
        REQUIRE(c.openingId().empty());
        roundTrip(c, p);
        REQUIRE(c.openingId() == "o1");
        REQUIRE(p.plan.floors[0].openings.size() == 1);
        REQUIRE(p.plan.floors[0].openings[0].id == "o1");
        REQUIRE(c.changes() == Change::Openings);
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.name() == "Add opening");
    }
    SECTION("ignores the id it was given and takes the next free one") {
        Opening o = doorOn("w1", 2.0);
        o.id = "o77";
        AddOpeningCommand first(o);
        applyOk(first, p);
        REQUIRE(first.openingId() == "o1");
        AddOpeningCommand second(doorOn("w3", 2.0));
        applyOk(second, p);
        REQUIRE(second.openingId() == "o2");
        second.undo(p);
        AddOpeningCommand third(doorOn("w3", 2.0));
        applyOk(third, p);
        REQUIRE(third.openingId() == "o2");
    }
    SECTION("keeps every field") {
        Opening o = doorOn("w2", 2.0);
        o.type = OpeningType::Window;
        o.shape = "round_arch";
        o.sill = 0.9;
        o.width = 1.2;
        o.height = 1.2;
        o.hinge = HingeEnd::End;
        o.swing = WallSide::Left;
        AddOpeningCommand c(o);
        applyOk(c, p);
        Opening expected = o;
        expected.id = "o1";
        REQUIRE(p.plan.floors[0].openings[0] == expected);
    }
    SECTION("refuses an unknown wall") {
        AddOpeningCommand c(doorOn("w9", 2.0));
        applyRefused(c, p, ErrorCode::NotFound);
    }
    SECTION("refuses what does not fit and leaves the plan alone") {
        AddOpeningCommand past(doorOn("w1", 0.2));
        applyRefused(past, p, ErrorCode::InvalidValue, "past");
        Opening tall = doorOn("w1", 2.0);
        tall.height = 3.5;
        AddOpeningCommand above(tall);
        applyRefused(above, p, ErrorCode::InvalidValue, "above");
        Opening arch = doorOn("w1", 2.0);
        arch.shape = "round_arch";
        arch.width = 1.0;
        arch.height = 0.4;
        AddOpeningCommand lowArch(arch);
        applyRefused(lowArch, p, ErrorCode::InvalidValue, "half the width");
        Opening zero = doorOn("w1", 2.0);
        zero.width = 0.0;
        AddOpeningCommand noWidth(zero);
        applyRefused(noWidth, p, ErrorCode::InvalidValue);
    }
    SECTION("refuses an overlap with another opening") {
        AddOpeningCommand first(doorOn("w1", 2.0));
        applyOk(first, p);
        AddOpeningCommand second(doorOn("w1", 2.3));
        applyRefused(second, p, ErrorCode::InvalidValue, "o1");
    }
}

TEST_CASE("MoveOpeningCommand", "[core][command][opening]") {
    Project p = projectWithDoor();
    SECTION("moves along the wall, undoes and redoes") {
        MoveOpeningCommand c("o1", "w1", 3.0, 0.0);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].openings[0].offset == 3.0);
        REQUIRE(c.changes() == Change::Openings);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("changes the sill and keeps everything else") {
        p.plan.floors[0].openings[0].type = OpeningType::Window;
        p.plan.floors[0].openings[0].height = 1.0;
        p.plan.floors[0].openings[0].hinge = HingeEnd::End;
        MoveOpeningCommand c("o1", "w1", 2.0, 0.5);
        roundTrip(c, p);
        const Opening& o = p.plan.floors[0].openings[0];
        REQUIRE(o.sill == 0.5);
        REQUIRE(o.height == 1.0);
        REQUIRE(o.type == OpeningType::Window);
        REQUIRE(o.hinge == HingeEnd::End);
    }
    SECTION("moves to another wall of the same floor") {
        MoveOpeningCommand c("o1", "w3", 1.5, 0.0);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].openings[0].wall == "w3");
        REQUIRE(p.plan.floors[0].openings[0].offset == 1.5);
    }
    SECTION("a wall on another floor is invalid_value") {
        Floor up;
        up.id = "f2";
        up.name = "Up";
        up.points = {{"p50", 0, 0, 0.0}, {"p51", 4, 0, 0.0}};
        Wall w;
        w.id = "w50";
        w.from = "p50";
        w.to = "p51";
        up.walls = {w};
        p.plan.floors.push_back(up);
        MoveOpeningCommand c("o1", "w50", 2.0, 0.0);
        applyRefused(c, p, ErrorCode::InvalidValue, "another floor");
    }
    SECTION("refusals") {
        MoveOpeningCommand unknown("o9", "w1", 2.0, 0.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        MoveOpeningCommand noWall("o1", "w9", 2.0, 0.0);
        applyRefused(noWall, p, ErrorCode::NotFound);
        MoveOpeningCommand past("o1", "w1", 0.1, 0.0);
        applyRefused(past, p, ErrorCode::InvalidValue, "past");
        MoveOpeningCommand high("o1", "w1", 2.0, 1.5);
        applyRefused(high, p, ErrorCode::InvalidValue, "above");
        MoveOpeningCommand nan("o1", "w1", std::numeric_limits<double>::quiet_NaN(), 0.0);
        applyRefused(nan, p, ErrorCode::InvalidValue);
    }
    SECTION("refuses an overlap but not a move past itself") {
        AddOpeningCommand other(doorOn("w1", 3.2));
        applyOk(other, p);
        MoveOpeningCommand onto("o1", "w1", 2.8, 0.0);
        applyRefused(onto, p, ErrorCode::InvalidValue, "o2");
        MoveOpeningCommand small("o1", "w1", 2.2, 0.0);
        applyOk(small, p);
    }
    SECTION("merges with the same non zero key and opening only") {
        MoveOpeningCommand first("o1", "w1", 2.5, 0.0, 9);
        applyOk(first, p);
        MoveOpeningCommand same("o1", "w1", 3.0, 0.3, 9);
        applyOk(same, p);
        REQUIRE(first.mergeWith(same));
        first.undo(p);
        REQUIRE(p.plan.floors[0].openings[0].offset == 2.0);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].openings[0].offset == 3.0);
        REQUIRE(p.plan.floors[0].openings[0].sill == 0.3);

        MoveOpeningCommand otherKey("o1", "w1", 2.0, 0.0, 10);
        MoveOpeningCommand otherOpening("o2", "w1", 2.0, 0.0, 9);
        MoveOpeningCommand noKeyA("o1", "w1", 2.0, 0.0);
        MoveOpeningCommand noKeyB("o1", "w1", 2.1, 0.0);
        ResizeOpeningCommand resize("o1", 2.0, 0.9, 0.0, 2.1, 9);
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(first.mergeWith(otherOpening));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
        REQUIRE_FALSE(first.mergeWith(resize));
    }
}

TEST_CASE("ResizeOpeningCommand", "[core][command][opening]") {
    Project p = projectWithDoor();
    SECTION("gives a new box, undoes and redoes") {
        ResizeOpeningCommand c("o1", 2.25, 1.5, 0.2, 2.0);
        roundTrip(c, p);
        const Opening& o = p.plan.floors[0].openings[0];
        REQUIRE(o.offset == 2.25);
        REQUIRE(o.width == 1.5);
        REQUIRE(o.sill == 0.2);
        REQUIRE(o.height == 2.0);
        REQUIRE(o.type == OpeningType::Door);
        REQUIRE(c.changes() == Change::Openings);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("refusals") {
        ResizeOpeningCommand unknown("o9", 2.0, 0.9, 0.0, 2.1);
        applyRefused(unknown, p, ErrorCode::NotFound);
        ResizeOpeningCommand wide("o1", 2.0, 5.0, 0.0, 2.1);
        applyRefused(wide, p, ErrorCode::InvalidValue, "past");
        ResizeOpeningCommand tall("o1", 2.0, 0.9, 0.0, 3.2);
        applyRefused(tall, p, ErrorCode::InvalidValue, "above");
        ResizeOpeningCommand zero("o1", 2.0, 0.0, 0.0, 2.1);
        applyRefused(zero, p, ErrorCode::InvalidValue);
        p.plan.floors[0].openings[0].shape = "round_arch";
        ResizeOpeningCommand low("o1", 2.0, 1.0, 0.0, 0.4);
        applyRefused(low, p, ErrorCode::InvalidValue, "half the width");
    }
    SECTION("merges with the same non zero key and opening only") {
        ResizeOpeningCommand first("o1", 2.0, 1.0, 0.0, 2.1, 3);
        applyOk(first, p);
        ResizeOpeningCommand same("o1", 2.0, 1.4, 0.0, 2.1, 3);
        applyOk(same, p);
        REQUIRE(first.mergeWith(same));
        first.undo(p);
        REQUIRE(p.plan.floors[0].openings[0].width == 0.9);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].openings[0].width == 1.4);
        ResizeOpeningCommand otherKey("o1", 2.0, 1.0, 0.0, 2.1, 4);
        ResizeOpeningCommand noKeyA("o1", 2.0, 1.0, 0.0, 2.1);
        ResizeOpeningCommand noKeyB("o1", 2.0, 1.1, 0.0, 2.1);
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
    }
}

TEST_CASE("SetOpeningStyleCommand", "[core][command][opening]") {
    Project p = projectWithDoor();
    SECTION("sets type, shape, hinge and swing and keeps sizes") {
        OpeningStyle style;
        style.type = OpeningType::Window;
        style.shape = "round_arch";
        style.hinge = HingeEnd::End;
        style.swing = WallSide::Left;
        SetOpeningStyleCommand c("o1", style);
        roundTrip(c, p);
        const Opening& o = p.plan.floors[0].openings[0];
        REQUIRE(o.type == OpeningType::Window);
        REQUIRE(o.shape == "round_arch");
        REQUIRE(o.hinge == HingeEnd::End);
        REQUIRE(o.swing == WallSide::Left);
        REQUIRE(o.width == 0.9);
        REQUIRE(o.height == 2.1);
        REQUIRE(o.sill == 0.0);
        REQUIRE(c.changes() == Change::Openings);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("a round arch on a 0.9 by 0.4 opening is refused") {
        p.plan.floors[0].openings[0].height = 0.4;
        p.plan.floors[0].openings[0].sill = 1.0;
        OpeningStyle style;
        style.shape = "round_arch";
        SetOpeningStyleCommand c("o1", style);
        applyRefused(c, p, ErrorCode::InvalidValue, "half the width");
    }
    SECTION("refusals") {
        SetOpeningStyleCommand unknown("o9", OpeningStyle{});
        applyRefused(unknown, p, ErrorCode::NotFound);
        OpeningStyle bad;
        bad.shape = "no_such_shape";
        SetOpeningStyleCommand c("o1", bad);
        applyRefused(c, p, ErrorCode::InvalidValue, "no_such_shape");
    }
}

TEST_CASE("RemoveOpeningCommand", "[core][command][opening]") {
    Project p = projectWithDoor();
    AddOpeningCommand second(doorOn("w3", 2.0));
    applyOk(second, p);
    SECTION("removes, undoes at the same place and redoes") {
        RemoveOpeningCommand c("o1");
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].openings.size() == 1);
        REQUIRE(p.plan.floors[0].openings[0].id == "o2");
        c.undo(p);
        REQUIRE(p.plan.floors[0].openings[0].id == "o1");
        REQUIRE(c.changes() == Change::Openings);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("an unknown opening") {
        RemoveOpeningCommand c("o9");
        applyRefused(c, p, ErrorCode::NotFound);
    }
}

TEST_CASE("the wall height commands", "[core][command][opening][height]") {
    Project p = projectWithDoor();
    SECTION("SetWallHeightCommand refuses 1.1 with a 2.1 door on the wall and accepts after removing it") {
        SetWallHeightCommand c("w1", 1.1);
        applyRefused(c, p, ErrorCode::InvalidValue, "o1");
        RemoveOpeningCommand remove("o1");
        applyOk(remove, p);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].walls[0].height == 1.1);
        REQUIRE(c.changes() == Change::Walls);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("an empty height clears it") {
        p.plan.floors[0].walls[0].height = 2.5;
        SetWallHeightCommand c("w1", std::nullopt);
        roundTrip(c, p);
        REQUIRE_FALSE(p.plan.floors[0].walls[0].height);
        REQUIRE(c.name() == "Clear wall height");
    }
    SECTION("clearing refuses when the opening would then be too high") {
        p.plan.floors[0].ceilingHeight = 2.0;
        p.plan.floors[0].walls[0].height = 2.5;
        SetWallHeightCommand c("w1", std::nullopt);
        applyRefused(c, p, ErrorCode::InvalidValue, "o1");
    }
    SECTION("other refusals") {
        SetWallHeightCommand unknown("w9", 1.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        for (double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
            SetWallHeightCommand c("w2", bad);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
    }
    SECTION("openings on other walls do not matter") {
        SetWallHeightCommand c("w2", 1.0);
        applyOk(c, p);
    }
    SECTION("merges with the same non zero key and wall only") {
        SetWallHeightCommand first("w2", 2.0, 5);
        applyOk(first, p);
        SetWallHeightCommand same("w2", 1.5, 5);
        applyOk(same, p);
        REQUIRE(first.mergeWith(same));
        first.undo(p);
        REQUIRE_FALSE(p.plan.floors[0].walls[1].height);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].walls[1].height == 1.5);
        SetWallHeightCommand otherWall("w3", 1.0, 5);
        SetWallHeightCommand otherKey("w2", 1.0, 6);
        SetWallHeightCommand noKeyA("w2", 1.0);
        SetWallHeightCommand noKeyB("w2", 1.2);
        REQUIRE_FALSE(first.mergeWith(otherWall));
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
    }
}

TEST_CASE("SetCeilingHeightCommand", "[core][command][opening][height]") {
    Project p = projectWithDoor();
    SECTION("sets, undoes and redoes") {
        SetCeilingHeightCommand c("f1", 2.8);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].ceilingHeight == 2.8);
        REQUIRE(c.changes() == Change::Floors);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("2.0 is refused naming the door") {
        SetCeilingHeightCommand c("f1", 2.0);
        applyRefused(c, p, ErrorCode::InvalidValue, "o1");
    }
    SECTION("a wall with its own height keeps its door fitting") {
        p.plan.floors[0].walls[0].height = 3.0;
        SetCeilingHeightCommand c("f1", 2.0);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].ceilingHeight == 2.0);
        REQUIRE(openingProblems(p.plan.floors[0]).empty());
    }
    SECTION("the first opening that would not fit is named") {
        AddOpeningCommand window(doorOn("w3", 2.0));
        applyOk(window, p);
        SetCeilingHeightCommand c("f1", 2.0);
        applyRefused(c, p, ErrorCode::InvalidValue, "o1");
    }
    SECTION("refusals") {
        SetCeilingHeightCommand unknown("f9", 3.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        for (double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            SetCeilingHeightCommand c("f1", bad);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
    }
    SECTION("merges with the same non zero key and floor only") {
        SetCeilingHeightCommand first("f1", 2.8, 2);
        applyOk(first, p);
        SetCeilingHeightCommand same("f1", 2.6, 2);
        applyOk(same, p);
        REQUIRE(first.mergeWith(same));
        first.undo(p);
        REQUIRE(p.plan.floors[0].ceilingHeight == 3.0);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].ceilingHeight == 2.6);
        SetCeilingHeightCommand otherKey("f1", 2.5, 3);
        SetCeilingHeightCommand otherFloor("f2", 2.5, 2);
        SetCeilingHeightCommand noKeyA("f1", 2.5);
        SetCeilingHeightCommand noKeyB("f1", 2.4);
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(first.mergeWith(otherFloor));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
    }
}

TEST_CASE("the plan stays valid and undo gives an equal plan after a run of opening commands", "[core][command][opening]") {
    Project p = squareProjectWithWalls();
    const Project before = p;
    std::vector<std::unique_ptr<Command>> run;
    run.push_back(std::make_unique<AddOpeningCommand>(doorOn("w1", 2.0)));
    run.push_back(std::make_unique<AddOpeningCommand>(doorOn("w2", 2.0)));
    run.push_back(std::make_unique<MoveOpeningCommand>("o1", "w3", 2.0, 0.0));
    run.push_back(std::make_unique<ResizeOpeningCommand>("o2", 2.0, 1.2, 0.0, 2.0));
    OpeningStyle style;
    style.shape = "pointed_arch";
    run.push_back(std::make_unique<SetOpeningStyleCommand>("o2", style));
    run.push_back(std::make_unique<SetWallHeightCommand>("w4", 1.5));
    run.push_back(std::make_unique<RemoveOpeningCommand>("o1"));
    for (auto& c : run) applyOk(*c, p);
    for (std::size_t i = run.size(); i-- > 0;) run[i]->undo(p);
    REQUIRE(p == before);
}
