#include "SquareFixtures.h"

#include <plynth/core/CommandStack.h>
#include <plynth/core/commands/CanopyCommands.h>
#include <plynth/core/commands/DeleteCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace plynth;

namespace {

// A canopy over a box on the plan, with the canopy defaults.
Canopy canopyOver(double x0, double y0, double x1, double y1)
{
    Canopy c;
    c.outline = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return c;
}

// Applies a command, checks the plan, undoes it and checks the project is as before, then applies it again.
void applyUndoRedo(Project& p, Command& c)
{
    const Project before = p;
    test::requireOk(c.apply(p));
    test::requirePlanValid(p.plan);
    const Project after = p;
    c.undo(p);
    CHECK(p == before);
    test::requireOk(c.apply(p));
    CHECK(p == after);
}

// Says that a command is refused and leaves the project as it was.
void refused(Project& p, Command& c, ErrorCode code)
{
    const Project before = p;
    CHECK(c.apply(p).code == code);
    CHECK(p == before);
}

// The square project with one canopy in front of it, and that canopy's id.
Project projectWithCanopy(Id& id)
{
    Project p = test::squareProjectWithWalls();
    AddCanopyCommand add("f1", canopyOver(1.0, 4.1, 3.0, 6.0));
    test::requireOk(add.apply(p));
    id = add.newCanopyId();
    return p;
}

}

TEST_CASE("adding a canopy gives it a new id and undoes", "[commands][canopies]")
{
    Project p = test::squareProjectWithWalls();
    Canopy values = canopyOver(1.0, 4.1, 3.0, 6.0);
    values.id = "ignored";
    values.height = 2.5;
    AddCanopyCommand add("f1", values);
    CHECK(add.newCanopyId().empty());
    applyUndoRedo(p, add);
    CHECK(add.name() == "Add canopy");
    CHECK(add.changes() == Change::Outdoor);
    CHECK(add.floorId() == "f1");
    REQUIRE(p.plan.floors[0].canopies.size() == 1);
    const Canopy& c = p.plan.floors[0].canopies[0];
    CHECK(c.id == add.newCanopyId());
    CHECK(c.id.rfind("cn", 0) == 0);
    CHECK(c.height == 2.5);
    CHECK(c.outline == values.outline);
}

TEST_CASE("adding a canopy refuses an unknown floor and bad values", "[commands][canopies]")
{
    Project p = test::squareProjectWithWalls();
    AddCanopyCommand nowhere("f9", canopyOver(0.0, 5.0, 2.0, 6.0));
    refused(p, nowhere, ErrorCode::NotFound);

    Canopy low = canopyOver(0.0, 5.0, 2.0, 6.0);
    low.height = 0.2;
    AddCanopyCommand tooLow("f1", low);
    refused(p, tooLow, ErrorCode::InvalidValue);

    Canopy crossing = canopyOver(0.0, 5.0, 2.0, 6.0);
    crossing.outline = {{0.0, 5.0}, {2.0, 6.0}, {2.0, 5.0}, {0.0, 6.0}};
    AddCanopyCommand crossed("f1", crossing);
    refused(p, crossed, ErrorCode::InvalidValue);

    Canopy unknown = canopyOver(0.0, 5.0, 2.0, 6.0);
    unknown.roof.type = "nonesuch";
    AddCanopyCommand untyped("f1", unknown);
    refused(p, untyped, ErrorCode::InvalidValue);
}

TEST_CASE("changing a canopy keeps its id and undoes", "[commands][canopies]")
{
    Id id;
    Project p = projectWithCanopy(id);
    Canopy values = canopyOver(1.0, 4.1, 3.5, 7.0);
    values.id = "other";
    values.roof.type = "gable";
    values.roof.pitch = 25.0;
    values.material = "roof_tiles";
    SetCanopyCommand set(id, values);
    applyUndoRedo(p, set);
    CHECK(set.name() == "Change canopy");
    CHECK(set.changes() == Change::Outdoor);
    CHECK(set.floorId() == "f1");
    const Canopy* c = findCanopy(p.plan.floors[0], id);
    REQUIRE(c);
    CHECK(c->id == id);
    CHECK(c->roof.type == "gable");
    CHECK(c->material == std::optional<std::string>("roof_tiles"));
    CHECK(c->outline == values.outline);
}

TEST_CASE("changing a canopy refuses an unknown canopy and bad values", "[commands][canopies]")
{
    Id id;
    Project p = projectWithCanopy(id);
    SetCanopyCommand missing("cn_missing", canopyOver(0.0, 5.0, 2.0, 6.0));
    refused(p, missing, ErrorCode::NotFound);

    Canopy thick = canopyOver(0.0, 5.0, 2.0, 6.0);
    thick.thickness = 3.0;
    SetCanopyCommand bad(id, thick);
    refused(p, bad, ErrorCode::InvalidValue);

    Canopy steep = canopyOver(0.0, 5.0, 2.0, 6.0);
    steep.roof.type = "gable";
    steep.roof.pitch = 80.0;
    SetCanopyCommand tooSteep(id, steep);
    refused(p, tooSteep, ErrorCode::InvalidValue);
}

TEST_CASE("changes of one canopy with the same merge key undo as one step", "[commands][canopies]")
{
    Id id;
    Project p = projectWithCanopy(id);
    const Project before = p;
    CommandStack stack;
    Canopy first = canopyOver(1.0, 4.1, 3.0, 6.5);
    Canopy second = canopyOver(1.0, 4.1, 3.0, 7.0);
    test::requireOk(stack.push(std::make_unique<SetCanopyCommand>(id, first, 7), p));
    bool merged = false;
    test::requireOk(stack.push(std::make_unique<SetCanopyCommand>(id, second, 7), p, &merged));
    CHECK(merged);
    CHECK(findCanopy(p.plan.floors[0], id)->outline == second.outline);
    stack.undo(p);
    CHECK(p == before);
    test::requireOk(stack.redo(p));
    CHECK(findCanopy(p.plan.floors[0], id)->outline == second.outline);

    // A different key, or none, is its own step.
    test::requireOk(stack.push(std::make_unique<SetCanopyCommand>(id, first, 8), p, &merged));
    CHECK_FALSE(merged);
    test::requireOk(stack.push(std::make_unique<SetCanopyCommand>(id, second), p, &merged));
    CHECK_FALSE(merged);
    test::requireOk(stack.push(std::make_unique<SetCanopyCommand>(id, first), p, &merged));
    CHECK_FALSE(merged);
}

TEST_CASE("removing a canopy puts it back in its place on undo", "[commands][canopies]")
{
    Id id;
    Project p = projectWithCanopy(id);
    AddCanopyCommand second("f1", canopyOver(5.0, 0.0, 7.0, 2.0));
    test::requireOk(second.apply(p));
    RemoveCanopyCommand remove(id);
    applyUndoRedo(p, remove);
    CHECK(remove.name() == "Remove canopy");
    CHECK(remove.changes() == Change::Outdoor);
    CHECK(remove.floorId() == "f1");
    REQUIRE(p.plan.floors[0].canopies.size() == 1);
    CHECK(p.plan.floors[0].canopies[0].id == second.newCanopyId());
    remove.undo(p);
    REQUIRE(p.plan.floors[0].canopies.size() == 2);
    CHECK(p.plan.floors[0].canopies[0].id == id);

    RemoveCanopyCommand missing("cn_missing");
    refused(p, missing, ErrorCode::NotFound);
}

TEST_CASE("the delete command removes a named canopy as one undo step", "[commands][canopies][delete]")
{
    Id id;
    Project p = projectWithCanopy(id);
    const Project before = p;
    const std::vector<Id> named{id};
    DeleteBuild build = makeDeleteCommand(p.plan, named);
    test::requireOk(build.status);
    CHECK(build.removes.canopies == named);
    CHECK(build.removes.walls.empty());
    REQUIRE(build.command);
    test::requireOk(build.command->apply(p));
    CHECK(p.plan.floors[0].canopies.empty());
    test::requirePlanValid(p.plan);
    build.command->undo(p);
    CHECK(p == before);

    // Removing every wall leaves the canopy where it is.
    std::vector<Id> walls;
    for (const Wall& w : p.plan.floors[0].walls) walls.push_back(w.id);
    DeleteBuild wallsOnly = makeDeleteCommand(p.plan, walls);
    test::requireOk(wallsOnly.status);
    CHECK(wallsOnly.removes.canopies.empty());
    test::requireOk(wallsOnly.command->apply(p));
    CHECK(p.plan.floors[0].canopies.size() == 1);
}
