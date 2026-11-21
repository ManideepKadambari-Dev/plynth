#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/SettingsCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <vector>

using namespace plynth;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A project with a small plan: floor f1 with points p1 and p2 and wall w1.
Project smallProject()
{
    Project p = makeNewProject();
    p.plan = plynth::test::smallPlan();
    return p;
}

// A project with two floors, a few points and a room and an opening on w1.
Project richProject()
{
    Project p;
    p.plan = plynth::test::fullPlan();
    return p;
}

// Applies a command, then checks it succeeded and left a valid plan.
void applyOk(Command& c, Project& p)
{
    plynth::test::requireOk(c.apply(p));
    plynth::test::requirePlanValid(p.plan);
}

// Applies a command that must be refused, and checks the project did not change.
void applyRefused(Command& c, Project& p, ErrorCode code)
{
    const Project before = p;
    const Status s = c.apply(p);
    INFO(s.message);
    REQUIRE(s.code == code);
    REQUIRE_FALSE(s.message.empty());
    REQUIRE(p == before);
}

// Applies, undoes and redoes a command, checking the plan is equal to before after undo and to the
// first result after redo.
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

}

TEST_CASE("SetFloorImageCommand", "[core][command][floor]") {
    Project p = smallProject();
    SECTION("sets, undoes and redoes") {
        SetFloorImageCommand c("f1", std::string("D:/images/plan.png"));
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].image == "D:/images/plan.png");
        REQUIRE(c.name() == "Set plan image");
        REQUIRE(c.changes() == Change::FloorImage);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("replaces an older image and undo brings it back") {
        p.plan.floors[0].image = "plans/a.png";
        SetFloorImageCommand c("f1", std::string("plans/b.png"));
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].image == "plans/b.png");
        c.undo(p);
        REQUIRE(p.plan.floors[0].image == "plans/a.png");
    }
    SECTION("clears an image") {
        p.plan.floors[0].image = "plans/a.png";
        SetFloorImageCommand c("f1", std::nullopt);
        roundTrip(c, p);
        REQUIRE_FALSE(p.plan.floors[0].image);
        REQUIRE(c.name() == "Clear plan image");
    }
    SECTION("refusals") {
        SetFloorImageCommand unknown("f9", std::string("a.png"));
        applyRefused(unknown, p, ErrorCode::NotFound);
        SetFloorImageCommand empty("f1", std::string(""));
        applyRefused(empty, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("SetScaleCommand", "[core][command][floor]") {
    Project p = smallProject();
    SECTION("sets, undoes and redoes") {
        SetScaleCommand c(52.4);
        roundTrip(c, p);
        REQUIRE(p.plan.scale.pixelsPerMetre == 52.4);
        REQUIRE(c.changes() == Change::Scale);
        REQUIRE(c.floorId().empty());
    }
    SECTION("changes and clears") {
        p.plan.scale.pixelsPerMetre = 10.0;
        SetScaleCommand change(20.0);
        applyOk(change, p);
        change.undo(p);
        REQUIRE(p.plan.scale.pixelsPerMetre == 10.0);
        SetScaleCommand clear(std::nullopt);
        roundTrip(clear, p);
        REQUIRE_FALSE(p.plan.scale.pixelsPerMetre);
    }
    SECTION("refusals") {
        for (double bad : {0.0, -1.0, kNaN, std::numeric_limits<double>::infinity()}) {
            SetScaleCommand c(bad);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
    }
}

TEST_CASE("AddPointCommand", "[core][command][point]") {
    Project p = smallProject();
    SECTION("adds with a new id, undoes, and redo gives the same id") {
        AddPointCommand c("f1", 1.5, 2.0, 0.1);
        REQUIRE(c.pointId().empty());
        applyOk(c, p);
        REQUIRE(c.pointId() == "p3");
        REQUIRE(p.plan.floors[0].points.size() == 3);
        REQUIRE(p.plan.floors[0].points[2] == Point{"p3", 1.5, 2.0, 0.1});
        const Project after = p;
        c.undo(p);
        REQUIRE(p.plan.floors[0].points.size() == 2);
        applyOk(c, p);
        REQUIRE(c.pointId() == "p3");
        REQUIRE(p == after);
        REQUIRE(c.changes() == Change::Points);
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.name() == "Add point");
    }
    SECTION("ids are unique across floors") {
        Project q = richProject();
        AddPointCommand c("f2", 0.0, 0.0);
        applyOk(c, q);
        REQUIRE(c.pointId() == "p5");
        REQUIRE(q.plan.floors[1].points.size() == 1);
    }
    SECTION("a failed first apply leaves the id empty") {
        AddPointCommand c("f9", 0.0, 0.0);
        applyRefused(c, p, ErrorCode::NotFound);
        REQUIRE(c.pointId().empty());
    }
    SECTION("refusals") {
        AddPointCommand noFloor("f9", 0.0, 0.0);
        applyRefused(noFloor, p, ErrorCode::NotFound);
        AddPointCommand badX("f1", kNaN, 0.0);
        applyRefused(badX, p, ErrorCode::InvalidValue);
        AddPointCommand badY("f1", 0.0, std::numeric_limits<double>::infinity());
        applyRefused(badY, p, ErrorCode::InvalidValue);
        AddPointCommand badRadius("f1", 0.0, 0.0, -0.5);
        applyRefused(badRadius, p, ErrorCode::InvalidValue);
        AddPointCommand nanRadius("f1", 0.0, 0.0, kNaN);
        applyRefused(nanRadius, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("MovePointCommand", "[core][command][point]") {
    Project p = smallProject();
    SECTION("moves, undoes and redoes") {
        MovePointCommand c("p2", 5.0, 6.0);
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].points[1].x == 5.0);
        REQUIRE(p.plan.floors[0].points[1].y == 6.0);
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.changes() == Change::Points);
    }
    SECTION("works on a point of another floor") {
        Project q = richProject();
        q.plan.floors[1].points = {{"p9", 1.0, 1.0, 0.0}};
        MovePointCommand c("p9", 2.0, 3.0);
        roundTrip(c, q);
        REQUIRE(c.floorId() == "f2");
    }
    SECTION("refusals") {
        MovePointCommand unknown("p9", 1.0, 1.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        MovePointCommand nan("p1", kNaN, 1.0);
        applyRefused(nan, p, ErrorCode::InvalidValue);
        MovePointCommand wallId("w1", 1.0, 1.0);
        applyRefused(wallId, p, ErrorCode::NotFound);
    }
    SECTION("merging takes the later target and keeps the first old place") {
        MovePointCommand first("p1", 1.0, 1.0, 7);
        MovePointCommand second("p1", 2.0, 2.0, 7);
        applyOk(first, p);
        applyOk(second, p);
        REQUIRE(first.mergeWith(second));
        first.undo(p);
        REQUIRE(p.plan.floors[0].points[0].x == 0.0);
        applyOk(first, p);
        REQUIRE(p.plan.floors[0].points[0].x == 2.0);
    }
    SECTION("merging needs the same point and the same non zero key") {
        MovePointCommand first("p1", 1.0, 1.0, 7);
        MovePointCommand otherPoint("p2", 2.0, 2.0, 7);
        MovePointCommand otherKey("p1", 2.0, 2.0, 8);
        MovePointCommand noKeyA("p1", 1.0, 1.0, 0);
        MovePointCommand noKeyB("p1", 2.0, 2.0, 0);
        SetScaleCommand scale(1.0);
        REQUIRE_FALSE(first.mergeWith(otherPoint));
        REQUIRE_FALSE(first.mergeWith(otherKey));
        REQUIRE_FALSE(noKeyA.mergeWith(noKeyB));
        REQUIRE_FALSE(first.mergeWith(scale));
    }
}

TEST_CASE("RemovePointCommand", "[core][command][point]") {
    Project p = smallProject();
    p.plan.floors[0].points.push_back({"p3", 9.0, 9.0, 0.2});
    SECTION("removes a free point and undo puts it back in place") {
        RemovePointCommand c("p3");
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].points.size() == 2);
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("undo restores the order when the point was in the middle") {
        p.plan.floors[0].walls.clear();
        const Project before = p;
        RemovePointCommand c("p1");
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].points.front().id == "p2");
        c.undo(p);
        REQUIRE(p == before);
    }
    SECTION("refusals") {
        RemovePointCommand unknown("p9");
        applyRefused(unknown, p, ErrorCode::NotFound);
        RemovePointCommand usedFrom("p1");
        applyRefused(usedFrom, p, ErrorCode::InUse);
        RemovePointCommand usedTo("p2");
        applyRefused(usedTo, p, ErrorCode::InUse);
    }
}

TEST_CASE("AddWallCommand", "[core][command][wall]") {
    Project p = smallProject();
    p.plan.floors[0].points.push_back({"p3", 2.0, 2.0, 0.0});
    SECTION("adds with a new id and redo gives the same id") {
        AddWallCommand c("p2", "p3", 0.3, 1.2);
        applyOk(c, p);
        REQUIRE(c.wallId() == "w2");
        const Wall& w = p.plan.floors[0].walls[1];
        REQUIRE(w.from == "p2");
        REQUIRE(w.to == "p3");
        REQUIRE(w.thickness == 0.3);
        REQUIRE(w.height == 1.2);
        const Project after = p;
        c.undo(p);
        REQUIRE(p.plan.floors[0].walls.size() == 1);
        applyOk(c, p);
        REQUIRE(c.wallId() == "w2");
        REQUIRE(p == after);
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.changes() == Change::Walls);
        REQUIRE(c.name() == "Add wall");
    }
    SECTION("defaults") {
        AddWallCommand c("p1", "p3");
        applyOk(c, p);
        const Wall& w = p.plan.floors[0].walls.back();
        REQUIRE(w.thickness == kDefaultWallThickness);
        REQUIRE_FALSE(w.height);
    }
    SECTION("refusals") {
        AddWallCommand unknownFrom("p9", "p2");
        applyRefused(unknownFrom, p, ErrorCode::NotFound);
        AddWallCommand unknownTo("p1", "p9");
        applyRefused(unknownTo, p, ErrorCode::NotFound);
        REQUIRE(unknownTo.wallId().empty());
        AddWallCommand same("p1", "p1");
        applyRefused(same, p, ErrorCode::InvalidValue);
        AddWallCommand duplicate("p1", "p2");
        applyRefused(duplicate, p, ErrorCode::AlreadyExists);
        AddWallCommand reverse("p2", "p1");
        applyRefused(reverse, p, ErrorCode::AlreadyExists);
        AddWallCommand thin("p1", "p3", 0.0);
        applyRefused(thin, p, ErrorCode::InvalidValue);
        AddWallCommand nanThick("p1", "p3", kNaN);
        applyRefused(nanThick, p, ErrorCode::InvalidValue);
        AddWallCommand lowHeight("p1", "p3", 0.2, -1.0);
        applyRefused(lowHeight, p, ErrorCode::InvalidValue);
    }
    SECTION("points on different floors are refused") {
        Project q = richProject();
        q.plan.floors[1].points = {{"p9", 0.0, 0.0, 0.0}};
        AddWallCommand c("p1", "p9");
        applyRefused(c, q, ErrorCode::InvalidValue);
    }
}

TEST_CASE("RemoveWallCommand", "[core][command][wall]") {
    SECTION("removes a free wall and undo puts it back in place") {
        Project p = richProject();
        // w3 has opening o2, so take it off first to make the wall free.
        p.plan.floors[0].openings.pop_back();
        const Project before = p;
        RemoveWallCommand c("w3");
        roundTrip(c, p);
        REQUIRE(p.plan.floors[0].walls.size() == 2);
        REQUIRE(c.floorId() == "f1");
        REQUIRE(c.changes() == Change::Walls);
        REQUIRE(c.name() == "Remove wall");
        c.undo(p);
        REQUIRE(p == before);
    }
    SECTION("undo restores the order when the wall was first") {
        Project p = smallProject();
        p.plan.floors[0].points.push_back({"p3", 2.0, 2.0, 0.0});
        AddWallCommand add("p2", "p3");
        applyOk(add, p);
        const Project before = p;
        RemoveWallCommand c("w1");
        applyOk(c, p);
        REQUIRE(p.plan.floors[0].walls.front().id == "w2");
        c.undo(p);
        REQUIRE(p == before);
    }
    SECTION("refusals") {
        Project p = richProject();
        RemoveWallCommand unknown("w9");
        applyRefused(unknown, p, ErrorCode::NotFound);
        RemoveWallCommand inRoom("w2");
        applyRefused(inRoom, p, ErrorCode::InUse);
        RemoveWallCommand withOpening("w3");
        applyRefused(withOpening, p, ErrorCode::InUse);
    }
}

TEST_CASE("CommandGroup applies as one step", "[core][command][group]") {
    Project p = smallProject();
    const Project before = p;

    auto group = std::make_unique<CommandGroup>("Draw corner");
    REQUIRE(group->empty());
    group->add(std::make_unique<AddPointCommand>("f1", 4.0, 4.0));
    group->add(std::make_unique<AddWallCommand>("p2", "p3"));
    group->add(std::make_unique<SetScaleCommand>(80.0));
    REQUIRE_FALSE(group->empty());
    REQUIRE(group->name() == "Draw corner");
    REQUIRE(group->changes() == (Change::Points | Change::Walls | Change::Scale));
    REQUIRE(group->floorId().empty());

    SECTION("applies, undoes and redoes") {
        applyOk(*group, p);
        REQUIRE(p.plan.floors[0].points.size() == 3);
        REQUIRE(p.plan.floors[0].walls.size() == 2);
        REQUIRE(p.plan.scale.pixelsPerMetre == 80.0);
        const Project after = p;
        group->undo(p);
        REQUIRE(p == before);
        applyOk(*group, p);
        REQUIRE(p == after);
    }
    SECTION("rolls back when a later command fails") {
        group->add(std::make_unique<AddWallCommand>("p1", "p9"));
        applyRefused(*group, p, ErrorCode::NotFound);
        REQUIRE(p == before);
    }
    SECTION("rolls back when the second command fails") {
        CommandGroup two("Two");
        two.add(std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
        two.add(std::make_unique<RemovePointCommand>("p1"));
        applyRefused(two, p, ErrorCode::InUse);
        REQUIRE(p == before);
    }
    SECTION("a shared floor is reported") {
        CommandGroup same("Same floor");
        same.add(std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
        same.add(std::make_unique<SetFloorImageCommand>("f1", std::string("a.png")));
        REQUIRE(same.floorId() == "f1");
        CommandGroup different("Different floors");
        different.add(std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
        different.add(std::make_unique<AddPointCommand>("f2", 1.0, 1.0));
        REQUIRE(different.floorId().empty());
    }
}

TEST_CASE("an empty group does nothing", "[core][command][group]") {
    Project p = smallProject();
    const Project before = p;
    CommandGroup g("Nothing");
    applyOk(g, p);
    g.undo(p);
    REQUIRE(p == before);
    REQUIRE(g.changes() == 0);
}

TEST_CASE("a long chain of commands undoes back to the start", "[core][command]") {
    Project p = makeNewProject();
    const Project start = p;
    std::vector<std::unique_ptr<Command>> done;
    auto run = [&](std::unique_ptr<Command> c) {
        applyOk(*c, p);
        done.push_back(std::move(c));
    };
    run(std::make_unique<SetScaleCommand>(100.0));
    run(std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    run(std::make_unique<AddPointCommand>("f1", 4.0, 0.0));
    run(std::make_unique<AddPointCommand>("f1", 4.0, 3.0));
    run(std::make_unique<AddWallCommand>("p1", "p2"));
    run(std::make_unique<AddWallCommand>("p2", "p3"));
    run(std::make_unique<MovePointCommand>("p3", 5.0, 3.5));
    run(std::make_unique<SetFloorImageCommand>("f1", std::string("plans/a.png")));
    run(std::make_unique<RemoveWallCommand>("w2"));
    run(std::make_unique<RemovePointCommand>("p3"));
    REQUIRE(p.plan.floors[0].points.size() == 2);

    const Project end = p;
    for (auto it = done.rbegin(); it != done.rend(); ++it) (*it)->undo(p);
    REQUIRE(p == start);
    for (auto& c : done) applyOk(*c, p);
    REQUIRE(p == end);
}

TEST_CASE("checkDefaultSizes", "[core][settings]") {
    REQUIRE(checkDefaultSizes(DefaultSizes{}).ok());

    const auto refused = [](DefaultSizes d) {
        const Status s = checkDefaultSizes(d);
        INFO(s.message);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE_FALSE(s.message.empty());
    };
    DefaultSizes d;
    d.ceilingHeight = 0.0;
    refused(d);
    d = {};
    d.doorWidth = -1.0;
    refused(d);
    d = {};
    d.doorHeight = kNaN;
    refused(d);
    d = {};
    d.windowWidth = 0.0;
    refused(d);
    d = {};
    d.windowHeight = std::numeric_limits<double>::infinity();
    refused(d);
    d = {};
    d.windowSill = -0.1;
    refused(d);
    d = {};
    d.ceilingHeight = 2.0; // the 2.1 m door no longer fits
    refused(d);
    d = {};
    d.ceilingHeight = 2.0;
    d.doorHeight = 1.9;    // the window sill 0.9 plus height 1.2 is 2.1
    refused(d);
    d = {};
    d.windowSill = 0.0;
    d.windowHeight = 3.0;  // exactly the ceiling height is fine
    REQUIRE(checkDefaultSizes(d).ok());
}

TEST_CASE("SetDefaultSizesCommand", "[core][command][settings]") {
    Project p = makeNewProject();
    SECTION("sets, undoes and redoes") {
        DefaultSizes sizes;
        sizes.doorHeight = 2.0;
        sizes.windowSill = 1.0;
        SetDefaultSizesCommand c(sizes);
        roundTrip(c, p);
        REQUIRE(p.settings.defaults == sizes);
        REQUIRE(c.changes() == Change::Settings);
        REQUIRE(c.floorId().empty());
    }
    SECTION("refuses sizes that break the rules and changes nothing") {
        DefaultSizes sizes;
        sizes.ceilingHeight = 2.0;
        SetDefaultSizesCommand c(sizes);
        applyRefused(c, p, ErrorCode::InvalidValue);
        REQUIRE(p.settings.defaults == DefaultSizes{});
    }
}
