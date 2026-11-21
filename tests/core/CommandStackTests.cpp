#include "TestSupport.h"

#include <plynth/core/CommandStack.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace plynth;

namespace {

// Pushes a command and requires it to work and keep the plan valid.
void pushOk(CommandStack& stack, Project& p, std::unique_ptr<Command> c, bool* merged = nullptr)
{
    plynth::test::requireOk(stack.push(std::move(c), p, merged));
    plynth::test::requirePlanValid(p.plan);
}

// A command that only counts how often it was applied and undone.
class CountingCommand : public Command {
public:
    CountingCommand(int& applied, int& undone) : applied_(applied), undone_(undone) {}
    std::string name() const override { return "Count"; }
    Status apply(Project&) override
    {
        ++applied_;
        return {};
    }
    void undo(Project&) override { ++undone_; }
    unsigned changes() const override { return 0; }

private:
    int& applied_;
    int& undone_;
};

// A command that works once and fails when applied again, to test a failing redo.
class FailsOnRedo : public Command {
public:
    std::string name() const override { return "Fails on redo"; }
    Status apply(Project&) override
    {
        if (applied_) return fail(ErrorCode::InvalidValue, "no second time");
        applied_ = true;
        return {};
    }
    void undo(Project&) override {}
    unsigned changes() const override { return 0; }

private:
    bool applied_ = false;
};

}

TEST_CASE("stack starts empty", "[core][stack]") {
    CommandStack stack;
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.nextUndo() == nullptr);
    REQUIRE(stack.nextRedo() == nullptr);
    REQUIRE(stack.isAtSaved());
    REQUIRE(stack.count() == 0);
    REQUIRE(stack.position() == 0);
    Project p = makeNewProject();
    stack.undo(p);
    plynth::test::requireOk(stack.redo(p));
    REQUIRE(p == makeNewProject());
}

TEST_CASE("undo and redo with names", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    const Project start = p;
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    pushOk(stack, p, std::make_unique<SetScaleCommand>(50.0));
    const Project end = p;

    REQUIRE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.nextUndo()->name() == "Set scale");
    REQUIRE(stack.count() == 2);

    stack.undo(p);
    REQUIRE(stack.nextUndo()->name() == "Add point");
    REQUIRE(stack.nextRedo()->name() == "Set scale");
    REQUIRE(stack.canRedo());
    stack.undo(p);
    REQUIRE(p == start);
    REQUIRE_FALSE(stack.canUndo());

    plynth::test::requireOk(stack.redo(p));
    plynth::test::requireOk(stack.redo(p));
    REQUIRE(p == end);
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.position() == 2);
}

TEST_CASE("a new push drops the redo list", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 2.0, 2.0));
    stack.undo(p);
    REQUIRE(stack.canRedo());
    pushOk(stack, p, std::make_unique<SetScaleCommand>(10.0));
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.count() == 2);
    REQUIRE(stack.nextUndo()->name() == "Set scale");
}

TEST_CASE("a refused command is not recorded", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    stack.undo(p);
    const Project before = p;
    const Status s = stack.push(std::make_unique<AddPointCommand>("f9", 0.0, 0.0), p);
    REQUIRE(s.code == ErrorCode::NotFound);
    REQUIRE(p == before);
    REQUIRE(stack.count() == 1);
    REQUIRE(stack.canRedo());
}

TEST_CASE("redo applies again and gives the same ids", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 2.0, 2.0));
    const Project end = p;
    stack.undo(p);
    stack.undo(p);
    plynth::test::requireOk(stack.redo(p));
    plynth::test::requireOk(stack.redo(p));
    REQUIRE(p == end);
}

TEST_CASE("a failing redo drops the redo list", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<FailsOnRedo>());
    pushOk(stack, p, std::make_unique<SetScaleCommand>(5.0));
    stack.undo(p);
    stack.undo(p);
    const Status s = stack.redo(p);
    REQUIRE(s.code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.count() == 0);
}

TEST_CASE("saved point", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    REQUIRE(stack.isAtSaved());
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    REQUIRE_FALSE(stack.isAtSaved());
    stack.markSaved();
    REQUIRE(stack.isAtSaved());
    stack.undo(p);
    REQUIRE_FALSE(stack.isAtSaved());
    plynth::test::requireOk(stack.redo(p));
    REQUIRE(stack.isAtSaved());
    pushOk(stack, p, std::make_unique<SetScaleCommand>(5.0));
    REQUIRE_FALSE(stack.isAtSaved());
    stack.undo(p);
    REQUIRE(stack.isAtSaved());
}

TEST_CASE("a push after undoing past the saved point loses the saved state", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 2.0, 2.0));
    stack.markSaved();
    stack.undo(p);
    stack.undo(p);
    pushOk(stack, p, std::make_unique<SetScaleCommand>(5.0));
    REQUIRE_FALSE(stack.isAtSaved());
    stack.undo(p);
    REQUIRE_FALSE(stack.isAtSaved());
    plynth::test::requireOk(stack.redo(p));
    REQUIRE_FALSE(stack.isAtSaved());
}

TEST_CASE("markNeverSaved and clear", "[core][stack]") {
    CommandStack stack;
    Project p = makeNewProject();
    stack.markNeverSaved();
    REQUIRE_FALSE(stack.isAtSaved());
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    stack.undo(p);
    REQUIRE_FALSE(stack.isAtSaved());
    stack.markSaved();
    REQUIRE(stack.isAtSaved());
    stack.clear();
    REQUIRE(stack.isAtSaved());
    REQUIRE(stack.count() == 0);
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("moves with the same key merge into one step", "[core][stack][merge]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    const Project beforeDrag = p;

    bool merged = true;
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 1.0, 1.0, 5), &merged);
    REQUIRE_FALSE(merged);
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 2.0, 2.0, 5), &merged);
    REQUIRE(merged);
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 3.0, 3.0, 5), &merged);
    REQUIRE(merged);
    REQUIRE(stack.count() == 2);
    REQUIRE(p.plan.floors[0].points[0].x == 3.0);

    stack.undo(p);
    REQUIRE(p == beforeDrag);
    plynth::test::requireOk(stack.redo(p));
    REQUIRE(p.plan.floors[0].points[0].x == 3.0);
}

TEST_CASE("two drags are two steps", "[core][stack][merge]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 1.0, 1.0, 5));
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 2.0, 2.0, 6));
    REQUIRE(stack.count() == 3);
    stack.undo(p);
    REQUIRE(p.plan.floors[0].points[0].x == 1.0);
}

TEST_CASE("moves do not merge with key 0 or another point", "[core][stack][merge]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 5.0, 5.0));
    bool merged = true;
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 1.0, 1.0), &merged);
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 2.0, 2.0), &merged);
    REQUIRE_FALSE(merged);
    REQUIRE(stack.count() == 4);
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 3.0, 3.0, 9));
    pushOk(stack, p, std::make_unique<MovePointCommand>("p2", 4.0, 4.0, 9), &merged);
    REQUIRE_FALSE(merged);
    REQUIRE(stack.count() == 6);
}

TEST_CASE("merging never crosses the saved point", "[core][stack][merge]") {
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 1.0, 1.0, 5));
    stack.markSaved();
    bool merged = true;
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 2.0, 2.0, 5), &merged);
    REQUIRE_FALSE(merged);
    REQUIRE(stack.count() == 3);
    REQUIRE_FALSE(stack.isAtSaved());
    stack.undo(p);
    REQUIRE(stack.isAtSaved());
    REQUIRE(p.plan.floors[0].points[0].x == 1.0);

    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 3.0, 3.0, 5), &merged);
    REQUIRE_FALSE(merged);
    pushOk(stack, p, std::make_unique<MovePointCommand>("p1", 4.0, 4.0, 5), &merged);
    REQUIRE(merged);
}

TEST_CASE("the limit drops the oldest commands", "[core][stack][limit]") {
    CommandStack stack(3);
    Project p = makeNewProject();
    for (int i = 0; i < 5; ++i) pushOk(stack, p, std::make_unique<AddPointCommand>("f1", i, 0.0));
    REQUIRE(stack.count() == 3);
    REQUIRE(stack.position() == 3);
    REQUIRE(p.plan.floors[0].points.size() == 5);
    stack.undo(p);
    stack.undo(p);
    stack.undo(p);
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE(p.plan.floors[0].points.size() == 2);
}

TEST_CASE("the limit moves or loses the saved point", "[core][stack][limit]") {
    CommandStack stack(3);
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 1.0, 0.0));
    stack.markSaved();
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 2.0, 0.0));
    pushOk(stack, p, std::make_unique<AddPointCommand>("f1", 3.0, 0.0));
    REQUIRE(stack.count() == 3);
    stack.undo(p);
    stack.undo(p);
    REQUIRE(stack.isAtSaved());
    REQUIRE(p.plan.floors[0].points.size() == 2);

    CommandStack lost(2);
    Project q = makeNewProject();
    lost.markSaved();
    pushOk(lost, q, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    pushOk(lost, q, std::make_unique<AddPointCommand>("f1", 1.0, 0.0));
    pushOk(lost, q, std::make_unique<AddPointCommand>("f1", 2.0, 0.0));
    lost.undo(q);
    lost.undo(q);
    REQUIRE_FALSE(lost.isAtSaved());
}

TEST_CASE("default limit is 1000", "[core][stack][limit]") {
    REQUIRE(kDefaultUndoLimit == 1000);
}

TEST_CASE("commands are applied once and undone once", "[core][stack]") {
    int applied = 0;
    int undone = 0;
    CommandStack stack;
    Project p = makeNewProject();
    pushOk(stack, p, std::make_unique<CountingCommand>(applied, undone));
    REQUIRE(applied == 1);
    stack.undo(p);
    REQUIRE(undone == 1);
    plynth::test::requireOk(stack.redo(p));
    REQUIRE(applied == 2);
    stack.undo(p);
    stack.undo(p);
    REQUIRE(undone == 2);
}
