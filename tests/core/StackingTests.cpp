#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/CommandStack.h>
#include <plynth/core/Items.h>
#include <plynth/core/Stacking.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/GeneratorCommands.h>
#include <plynth/core/commands/ItemCommands.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/StackCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

const ItemSize kRug{2.0, 1.4, 0.01};
const ItemSize kTable{1.2, 0.8, 0.75};
const ItemSize kLamp{0.3, 0.3, 0.5};
const ItemSize kBook{0.2, 0.3, 0.05};
const ItemSize kSofa{2.0, 0.9, 0.85};
const ItemSize kTv{1.23, 0.06, 0.71};

// The split house (6 by 4, a wall across at x 3) with rooms r1 on the left and r2 on the right.
Project houseWithRooms()
{
    Project p = splitHouseProject();
    AddRoomCommand a("f1", {1.5, 2.0});
    AddRoomCommand b("f1", {4.5, 2.0});
    requireOk(a.apply(p));
    requireOk(b.apply(p));
    return p;
}

// An item as the caller would give it, optionally standing on another.
Item newItem(const std::string& asset, double x, double y, ItemSize size, std::optional<Id> on = {})
{
    Item it;
    it.asset = asset;
    it.x = x;
    it.y = y;
    it.size = size;
    it.on = std::move(on);
    return it;
}

// Places an item on the first floor and gives its id.
Id place(Project& p, const Item& item)
{
    PlaceItemCommand c("f1", item);
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
    return c.itemId();
}

// The item with this id on the first floor.
const Item& get(const Project& p, const Id& id)
{
    const Item* item = findItem(p.plan.floors[0], id);
    REQUIRE(item);
    return *item;
}

// Applies a command, checks it worked and left a valid plan.
void applyOk(Command& c, Project& p)
{
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
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

// Pushes a command on an undo stack and checks it worked and left a valid plan.
void pushOk(CommandStack& stack, std::unique_ptr<Command> c, Project& p)
{
    requireOk(stack.push(std::move(c), p));
    requirePlanValid(p.plan);
}

// An item for a hand made floor.
Item bare(const std::string& id, double x, double y, ItemSize size, std::optional<Id> on = {})
{
    Item it = newItem("thing", x, y, size, std::move(on));
    it.id = id;
    return it;
}

// Says whether the problems hold an overlaps_item entry between two items, either way round.
bool overlapBetween(const std::vector<ItemProblem>& problems, const Id& a, const Id& b)
{
    for (const ItemProblem& problem : problems) {
        if (problem.kind != ItemProblemKind::OverlapsItem) continue;
        if ((problem.item == a && problem.other == b) || (problem.item == b && problem.other == a)) return true;
    }
    return false;
}

// A table at (1.5, 2) with a lamp on it 0.3 m to its right and a book on the lamp.
struct TableStack {
    Project p = houseWithRooms();
    Id table;
    Id lamp;
    Id book;

    TableStack()
    {
        table = place(p, newItem("table", 1.5, 2.0, kTable));
        lamp = place(p, newItem("lamp", 1.8, 2.0, kLamp, table));
        book = place(p, newItem("book", 1.8, 2.0, kBook, lamp));
    }
};

}

TEST_CASE("isUnderlay finds flat floor items and itemTop adds the height", "[stacking]") {
    Item rug = bare("r", 0, 0, kRug);
    REQUIRE(isUnderlay(rug));
    Item thick = bare("t", 0, 0, {1, 1, kUnderlayHeight});
    REQUIRE_FALSE(isUnderlay(thick));
    Item hung = rug;
    hung.wall = "w1";
    REQUIRE_FALSE(isUnderlay(hung));
    Item stacked = rug;
    stacked.on = "x";
    REQUIRE_FALSE(isUnderlay(stacked));
    Item table = bare("t", 0, 0, kTable);
    table.z = 0.2;
    REQUIRE(itemTop(table) == Approx(0.95));
}

TEST_CASE("itemsStackedOn lists every item above, each after the one it stands on", "[stacking]") {
    Floor f;
    f.id = "f1";
    f.items.push_back(bare("s", 0, 0, kBook, Id("l")));
    f.items.push_back(bare("l", 0, 0, kLamp, Id("t")));
    f.items.push_back(bare("b", 0, 0, kBook, Id("t")));
    f.items.push_back(bare("t", 0, 0, kTable));
    REQUIRE(itemsStackedOn(f, "t") == std::vector<Id>{"l", "b", "s"});
    REQUIRE(itemsStackedOn(f, "l") == std::vector<Id>{"s"});
    REQUIRE(itemsStackedOn(f, "s").empty());
    REQUIRE(itemsStackedOn(f, "nothing").empty());

    Floor loop;
    loop.items.push_back(bare("a", 0, 0, kBook, Id("b")));
    loop.items.push_back(bare("b", 0, 0, kBook, Id("a")));
    REQUIRE(itemsStackedOn(loop, "a") == std::vector<Id>{"b"});
}

TEST_CASE("stackTargetAt gives the highest item under the point", "[stacking]") {
    Floor f;
    f.id = "f1";
    f.items.push_back(bare("r", 1.5, 2.0, kRug));
    Item table = bare("t", 1.5, 2.0, kTable, Id("r"));
    table.z = 0.01;
    f.items.push_back(table);
    Item lamp = bare("l", 1.5, 2.0, kLamp, Id("t"));
    lamp.z = 0.76;
    f.items.push_back(lamp);
    Item tv = bare("tv", 4.0, 2.0, kTv);
    tv.wall = "w1";
    f.items.push_back(tv);

    REQUIRE(stackTargetAt(f, 1.5, 2.0, "") == Id("l"));
    REQUIRE(stackTargetAt(f, 1.0, 2.0, "") == Id("t"));
    REQUIRE(stackTargetAt(f, 0.7, 2.0, "") == Id("r"));
    REQUIRE_FALSE(stackTargetAt(f, 5.0, 2.0, ""));
    REQUIRE_FALSE(stackTargetAt(f, 4.0, 2.0, ""));
    REQUIRE(stackTargetAt(f, 1.5, 2.0, "l") == Id("t"));
    REQUIRE(stackTargetAt(f, 1.5, 2.0, "t") == Id("r"));
    REQUIRE_FALSE(stackTargetAt(f, 1.5, 2.0, "r"));
}

TEST_CASE("stackTargetAt follows a turned footprint", "[stacking]") {
    Floor f;
    Item table = bare("t", 1.5, 2.0, kTable);
    table.rotation = 90.0;
    f.items.push_back(table);
    REQUIRE(stackTargetAt(f, 1.5, 2.5, "") == Id("t"));
    REQUIRE_FALSE(stackTargetAt(f, 2.0, 2.0, ""));
}

TEST_CASE("automaticStackTarget only answers for underlays", "[stacking]") {
    Floor f;
    f.items.push_back(bare("r", 1.5, 2.0, kRug));
    f.items.push_back(bare("t", 1.5, 2.0, kTable));
    REQUIRE(automaticStackTarget(f, 0.7, 2.0, "") == Id("r"));
    REQUIRE_FALSE(automaticStackTarget(f, 1.5, 2.0, ""));
    REQUIRE_FALSE(automaticStackTarget(f, 5.0, 2.0, ""));
    REQUIRE(automaticStackTarget(f, 1.5, 2.0, "t") == Id("r"));

    Floor rugs;
    rugs.items.push_back(bare("a", 1.5, 2.0, kRug));
    rugs.items.push_back(bare("b", 1.5, 2.0, {1.0, 1.0, 0.02}));
    REQUIRE(automaticStackTarget(rugs, 1.5, 2.0, "") == Id("b"));
}

TEST_CASE("checkStack refuses loops, walls and unknown items", "[stacking]") {
    Floor f;
    f.items.push_back(bare("t", 1.5, 2.0, kTable));
    f.items.push_back(bare("l", 1.5, 2.0, kLamp, Id("t")));
    f.items.push_back(bare("s", 1.5, 2.0, kBook, Id("l")));
    f.items.push_back(bare("b", 1.0, 2.0, kBook));
    Item tv = bare("tv", 4.0, 2.0, kTv);
    tv.wall = "w1";
    f.items.push_back(tv);

    requireOk(checkStack(f, "b", "t"));
    requireOk(checkStack(f, "b", "s"));
    REQUIRE(checkStack(f, "t", "t").code == ErrorCode::InvalidValue);
    REQUIRE(checkStack(f, "t", "l").code == ErrorCode::InvalidValue);
    REQUIRE(checkStack(f, "t", "s").code == ErrorCode::InvalidValue);
    REQUIRE(checkStack(f, "tv", "t").code == ErrorCode::InvalidValue);
    REQUIRE(checkStack(f, "b", "tv").code == ErrorCode::InvalidValue);
    REQUIRE(checkStack(f, "x", "t").code == ErrorCode::NotFound);
    REQUIRE(checkStack(f, "b", "x").code == ErrorCode::NotFound);
    REQUIRE_FALSE(checkStack(f, "t", "l").message.empty());
}

TEST_CASE("PlaceItemCommand puts an item on the top of the item it stands on", "[stacking]") {
    Project p = houseWithRooms();
    const Id table = place(p, newItem("table", 1.5, 2.0, kTable));
    Item lamp = newItem("lamp", 1.5, 2.0, kLamp, table);
    lamp.z = 0.2;
    const Id id = place(p, lamp);
    REQUIRE(get(p, id).on == table);
    REQUIRE(get(p, id).z == Approx(0.75));

    PlaceItemCommand unknown("f1", newItem("lamp", 1.5, 2.0, kLamp, Id("i99")));
    applyRefused(unknown, p, ErrorCode::NotFound);

    Item tv = newItem("tv", 1.5, 0.3, kTv);
    tv.wall = "w1";
    tv.z = 1.0;
    const Id tvId = place(p, tv);
    PlaceItemCommand onTv("f1", newItem("lamp", 1.5, 0.3, kLamp, tvId));
    applyRefused(onTv, p, ErrorCode::InvalidValue);
    Item hungOnTable = tv;
    hungOnTable.on = table;
    PlaceItemCommand hung("f1", hungOnTable);
    applyRefused(hung, p, ErrorCode::InvalidValue);
}

TEST_CASE("SetItemOnCommand stacks and unstacks as one undo step", "[stacking]") {
    Project p = houseWithRooms();
    const Id rug = place(p, newItem("rug", 1.5, 2.0, kRug));
    const Id table = place(p, newItem("table", 1.5, 2.0, kTable));
    const Id lamp = place(p, newItem("lamp", 1.5, 2.0, kLamp, table));
    const Project before = p;

    CommandStack stack;
    auto on = std::make_unique<SetItemOnCommand>(table, rug);
    REQUIRE(on->name() == "Put on item");
    REQUIRE(on->changes() == Change::Items);
    pushOk(stack, std::move(on), p);
    REQUIRE(stack.nextUndo()->floorId() == "f1");
    REQUIRE(get(p, table).on == rug);
    REQUIRE(get(p, table).z == Approx(0.01));
    REQUIRE(get(p, lamp).z == Approx(0.76));
    const Project stacked = p;

    stack.undo(p);
    REQUIRE(p == before);
    requireOk(stack.redo(p));
    REQUIRE(p == stacked);

    auto off = std::make_unique<SetItemOnCommand>(table, std::nullopt);
    REQUIRE(off->name() == "Put on floor");
    pushOk(stack, std::move(off), p);
    REQUIRE_FALSE(get(p, table).on);
    REQUIRE(get(p, table).z == 0.0);
    REQUIRE(get(p, lamp).z == Approx(0.75));
    stack.undo(p);
    REQUIRE(p == stacked);

    SetItemOnCommand again(table, rug);
    applyOk(again, p);
    REQUIRE(p == stacked);
}

TEST_CASE("SetItemOnCommand refuses loops, walls, other floors and unknown items", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    SetItemOnCommand loop(s.table, s.book);
    applyRefused(loop, p, ErrorCode::InvalidValue);
    SetItemOnCommand self(s.table, s.table);
    applyRefused(self, p, ErrorCode::InvalidValue);
    SetItemOnCommand unknown("i99", s.table);
    applyRefused(unknown, p, ErrorCode::NotFound);
    SetItemOnCommand unknownLower(s.table, Id("i99"));
    applyRefused(unknownLower, p, ErrorCode::NotFound);

    Item tv = newItem("tv", 1.5, 0.3, kTv);
    tv.wall = "w1";
    tv.z = 1.0;
    const Id tvId = place(p, tv);
    SetItemOnCommand hung(tvId, s.table);
    applyRefused(hung, p, ErrorCode::InvalidValue);
    SetItemOnCommand onHung(s.book, tvId);
    applyRefused(onHung, p, ErrorCode::InvalidValue);

    Floor upstairs;
    upstairs.id = "f2";
    upstairs.items.push_back(bare("i90", 1.0, 1.0, kTable));
    p.plan.floors.push_back(upstairs);
    SetItemOnCommand otherFloor(s.lamp, Id("i90"));
    applyRefused(otherFloor, p, ErrorCode::InvalidValue);
}

TEST_CASE("PutItemUnderCommand puts the other item on the dropped one", "[stacking]") {
    Project p = houseWithRooms();
    const Id table = place(p, newItem("table", 1.5, 2.0, kTable));
    const Id lamp = place(p, newItem("lamp", 1.5, 2.0, kLamp, table));
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Project before = p;

    PutItemUnderCommand under(sofa, table);
    REQUIRE(under.name() == "Put under");
    REQUIRE(under.changes() == Change::Items);
    applyOk(under, p);
    REQUIRE(under.floorId() == "f1");
    REQUIRE_FALSE(get(p, sofa).on);
    REQUIRE(get(p, table).on == sofa);
    REQUIRE(get(p, table).z == Approx(0.85));
    REQUIRE(get(p, lamp).z == Approx(1.6));
    under.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("PutItemUnderCommand swaps a dropped item that stood on the other one", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    PutItemUnderCommand under(s.lamp, s.table);
    applyOk(under, p);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.lamp).z == 0.0);
    REQUIRE(get(p, s.book).on == s.lamp);
    REQUIRE(get(p, s.book).z == Approx(0.5));
    REQUIRE(get(p, s.table).on == s.lamp);
    REQUIRE(get(p, s.table).z == Approx(0.5));
    under.undo(p);
    REQUIRE(p == before);

    Item tv = newItem("tv", 1.5, 0.3, kTv);
    tv.wall = "w1";
    tv.z = 1.0;
    const Id tvId = place(p, tv);
    PutItemUnderCommand underTv(tvId, s.table);
    applyRefused(underTv, p, ErrorCode::InvalidValue);
    PutItemUnderCommand unknown(s.lamp, "i99");
    applyRefused(unknown, p, ErrorCode::NotFound);
}

TEST_CASE("Moving the lower item moves the items on it in one undo step", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    MoveItemCommand move(s.table, {4.5, 2.0});
    applyOk(move, p);
    REQUIRE(get(p, s.lamp).x == Approx(4.8));
    REQUIRE(get(p, s.lamp).y == Approx(2.0));
    REQUIRE(get(p, s.lamp).room == Id("r2"));
    REQUIRE(get(p, s.lamp).z == Approx(0.75));
    REQUIRE(get(p, s.book).x == Approx(4.8));
    REQUIRE(get(p, s.book).room == Id("r2"));
    REQUIRE(get(p, s.book).on == s.lamp);
    move.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("A drag of the lower item undoes as one step and redoes the same", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    CommandStack stack;
    pushOk(stack, std::make_unique<MoveItemCommand>(s.table, Vec2{2.0, 2.0}, 7), p);
    pushOk(stack, std::make_unique<MoveItemCommand>(s.table, Vec2{4.5, 2.0}, 7), p);
    REQUIRE(stack.count() == 1);
    REQUIRE(get(p, s.lamp).x == Approx(4.8));
    const Project after = p;
    stack.undo(p);
    REQUIRE(p == before);
    requireOk(stack.redo(p));
    REQUIRE(p == after);
}

TEST_CASE("Moving an upper item keeps it on while over the lower item and lets it down otherwise", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    MoveItemCommand within(s.lamp, {1.2, 2.2});
    applyOk(within, p);
    REQUIRE(get(p, s.lamp).on == s.table);
    REQUIRE(get(p, s.lamp).z == Approx(0.75));
    REQUIRE(get(p, s.book).x == Approx(1.2));
    within.undo(p);
    REQUIRE(p == before);

    MoveItemCommand off(s.lamp, {1.5, 3.2});
    applyOk(off, p);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.lamp).z == 0.0);
    REQUIRE(get(p, s.book).on == s.lamp);
    REQUIRE(get(p, s.book).z == Approx(0.5));
    REQUIRE(get(p, s.book).y == Approx(3.2));
    off.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("A drag that leaves the lower item and comes back redoes the same", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    CommandStack stack;
    pushOk(stack, std::make_unique<MoveItemCommand>(s.lamp, Vec2{1.5, 3.2}, 9), p);
    pushOk(stack, std::make_unique<MoveItemCommand>(s.lamp, Vec2{1.5, 2.0}, 9), p);
    REQUIRE(stack.count() == 1);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.lamp).z == 0.0);
    const Project after = p;
    stack.undo(p);
    REQUIRE(p == before);
    requireOk(stack.redo(p));
    REQUIRE(p == after);
}

TEST_CASE("Turning the lower item turns the items on it round its centre", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    CommandStack stack;
    pushOk(stack, std::make_unique<RotateItemCommand>(s.table, 45.0, 3), p);
    pushOk(stack, std::make_unique<RotateItemCommand>(s.table, 90.0, 3), p);
    REQUIRE(stack.count() == 1);
    REQUIRE(get(p, s.lamp).x == Approx(1.5));
    REQUIRE(get(p, s.lamp).y == Approx(2.3));
    REQUIRE(get(p, s.lamp).rotation == Approx(90.0));
    REQUIRE(get(p, s.book).y == Approx(2.3));
    REQUIRE(get(p, s.book).rotation == Approx(90.0));
    REQUIRE(get(p, s.lamp).on == s.table);
    stack.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("Swapping or resizing the lower item lifts the items on it", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    SwapItemAssetCommand swap(s.table, "desk", {1.4, 0.7, 0.9});
    applyOk(swap, p);
    REQUIRE(get(p, s.lamp).z == Approx(0.9));
    REQUIRE(get(p, s.book).z == Approx(1.4));
    swap.undo(p);
    REQUIRE(p == before);

    Item plinth;
    plinth.x = 4.5;
    plinth.y = 2.0;
    plinth.size = {1.0, 1.0, 0.4};
    plinth.generator = GeneratorUse{"plinth", {{"height", GeneratorValue{0.4}}}};
    const Id base = place(p, plinth);
    const Id vase = place(p, newItem("vase", 4.5, 2.0, kLamp, base));
    REQUIRE(get(p, vase).z == Approx(0.4));
    const Project withPlinth = p;
    SetItemGeneratorCommand taller(base, GeneratorUse{"plinth", {{"height", GeneratorValue{0.6}}}}, {1.0, 1.0, 0.6});
    applyOk(taller, p);
    REQUIRE(get(p, vase).z == Approx(0.6));
    taller.undo(p);
    REQUIRE(p == withPlinth);
}

TEST_CASE("Setting heights keeps stacks or takes an item off", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    SetItemHeightCommand lift(s.table, 0.1);
    applyOk(lift, p);
    REQUIRE(get(p, s.lamp).z == Approx(0.85));
    REQUIRE(get(p, s.book).z == Approx(1.35));
    lift.undo(p);
    REQUIRE(p == before);

    SetItemHeightCommand same(s.lamp, 0.75);
    applyOk(same, p);
    REQUIRE(get(p, s.lamp).on == s.table);
    REQUIRE(p == before);

    SetItemHeightCommand away(s.lamp, 1.0);
    applyOk(away, p);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.lamp).z == Approx(1.0));
    REQUIRE(get(p, s.book).z == Approx(1.5));
    away.undo(p);
    REQUIRE(p == before);

    CommandStack stack;
    pushOk(stack, std::make_unique<SetItemHeightCommand>(s.lamp, 1.0, 5), p);
    pushOk(stack, std::make_unique<SetItemHeightCommand>(s.lamp, 0.75, 5), p);
    REQUIRE(stack.count() == 1);
    const Project after = p;
    REQUIRE_FALSE(get(p, s.lamp).on);
    stack.undo(p);
    REQUIRE(p == before);
    requireOk(stack.redo(p));
    REQUIRE(p == after);
}

TEST_CASE("Removing the lower item lets the items on it down to where it stood", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    RemoveItemCommand remove(s.table);
    applyOk(remove, p);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.lamp).z == 0.0);
    REQUIRE(get(p, s.book).on == s.lamp);
    REQUIRE(get(p, s.book).z == Approx(0.5));
    remove.undo(p);
    REQUIRE(p == before);

    RemoveItemCommand middle(s.lamp);
    applyOk(middle, p);
    REQUIRE(get(p, s.book).on == s.table);
    REQUIRE(get(p, s.book).z == Approx(0.75));
    middle.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("Deleting a stack together undoes as one step", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    const std::vector<Id> ids{s.table, s.lamp};
    DeleteBuild build = makeDeleteCommand(p.plan, ids);
    requireOk(build.status);
    applyOk(*build.command, p);
    REQUIRE(p.plan.floors[0].items.size() == 1);
    REQUIRE_FALSE(get(p, s.book).on);
    REQUIRE(get(p, s.book).z == 0.0);
    build.command->undo(p);
    REQUIRE(p == before);
}

TEST_CASE("Hanging an item on a wall takes it off its stack", "[stacking]") {
    TableStack s;
    Project& p = s.p;
    const Project before = p;
    SetItemWallCommand mountTable(s.table, Id("w6"));
    applyOk(mountTable, p);
    REQUIRE_FALSE(get(p, s.table).on);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.lamp).z == 0.0);
    REQUIRE(get(p, s.book).z == Approx(0.5));
    mountTable.undo(p);
    REQUIRE(p == before);

    SetItemWallCommand mountLamp(s.lamp, Id("w6"));
    applyOk(mountLamp, p);
    REQUIRE_FALSE(get(p, s.lamp).on);
    REQUIRE(get(p, s.book).on == s.table);
    REQUIRE(get(p, s.book).z == Approx(0.75));
    mountLamp.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("itemProblems leaves out rugs under floor items and items on what they stand on", "[stacking]") {
    Project p = houseWithRooms();
    const Id rug = place(p, newItem("rug", 1.5, 2.0, kRug));
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id table = place(p, newItem("table", 1.5, 2.2, kTable));
    const Id mat = place(p, newItem("mat", 1.0, 2.0, {1.0, 1.0, 0.02}));
    const Floor& floor = p.plan.floors[0];
    const std::vector<ItemProblem> problems = itemProblems(floor);
    REQUIRE_FALSE(overlapBetween(problems, rug, sofa));
    REQUIRE_FALSE(overlapBetween(problems, rug, table));
    REQUIRE(overlapBetween(problems, sofa, table));
    REQUIRE(overlapBetween(problems, rug, mat));

    Floor stacked = floor;
    Item* upper = findItem(stacked, table);
    upper->on = sofa;
    upper->z = 0.5;
    REQUIRE_FALSE(overlapBetween(itemProblems(stacked), sofa, table));

    Floor onRug = floor;
    findItem(onRug, sofa)->on = rug;
    findItem(onRug, sofa)->z = 0.01;
    REQUIRE_FALSE(overlapBetween(itemProblems(onRug), rug, sofa));
}

TEST_CASE("Putting an item that stands on nothing on the floor changes nothing", "[stacking]") {
    Project p = houseWithRooms();
    Item tv = newItem("tv", 1.5, 0.3, kTv);
    tv.wall = "w1";
    tv.z = 1.0;
    const Id tvId = place(p, tv);
    Item shelf = newItem("shelf", 1.5, 2.0, kTable);
    shelf.z = 0.3;
    const Id shelfId = place(p, shelf);
    const Project before = p;
    SetItemOnCommand tvDown(tvId, std::nullopt);
    applyOk(tvDown, p);
    SetItemOnCommand shelfDown(shelfId, std::nullopt);
    applyOk(shelfDown, p);
    REQUIRE(p == before);
}
