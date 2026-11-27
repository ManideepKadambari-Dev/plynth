#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/Decks.h>
#include <plynth/core/commands/DeckCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A porch deck in front of the square, east of wall w2.
Deck porch(double top = 0.6)
{
    Deck deck;
    deck.outline = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    deck.top = top;
    return deck;
}

// The square project with one deck dk1 on it and a railing rl1 standing on that deck.
Project projectWithDeck()
{
    Project p = squareProjectWithWalls();
    Deck deck = porch();
    deck.id = "dk1";
    p.plan.floors[0].decks.push_back(deck);
    Railing railing;
    railing.id = "rl1";
    railing.points = {{4.1, 2.0}, {7.0, 2.0}};
    railing.host = "dk1";
    railing.base = 0.0;
    p.plan.floors[0].railings.push_back(railing);
    Railing free;
    free.id = "rl2";
    free.points = {{0.0, 5.0}, {2.0, 5.0}};
    p.plan.floors[0].railings.push_back(free);
    return p;
}

}

TEST_CASE("AddDeckCommand adds, undoes and redoes with the same id", "[decks][commands]")
{
    Project p = squareProjectWithWalls();
    const Project before = p;
    Deck values = porch();
    values.id = "ignored";
    AddDeckCommand add("f1", values);
    CHECK(add.newDeckId().empty());
    requireOk(add.apply(p));
    requirePlanValid(p.plan);
    const Id id = add.newDeckId();
    CHECK(id.rfind("dk", 0) == 0);
    CHECK(isValidId(id));
    REQUIRE(p.plan.floors[0].decks.size() == 1);
    CHECK(p.plan.floors[0].decks[0].id == id);
    CHECK(p.plan.floors[0].decks[0].top == Approx(0.6));
    CHECK(add.changes() == Change::Outdoor);
    CHECK(add.floorId() == "f1");
    CHECK(add.name() == "Add deck");

    add.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(add.apply(p));
    REQUIRE(p.plan.floors[0].decks.size() == 1);
    CHECK(p.plan.floors[0].decks[0].id == id);
}

TEST_CASE("AddDeckCommand refuses an unknown floor and bad values", "[decks][commands]")
{
    Project p = squareProjectWithWalls();
    const Project before = p;
    AddDeckCommand nowhere("f9", porch());
    CHECK(nowhere.apply(p).code == ErrorCode::NotFound);
    CHECK(p.plan == before.plan);

    Deck thick = porch();
    thick.thickness = 2.0;
    AddDeckCommand bad("f1", thick);
    const Status s = bad.apply(p);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message == "The deck thickness must be from 0.05 to 1 metre.");
    CHECK(p.plan == before.plan);

    Deck small = porch();
    small.outline = {{0.0, 0.0}, {0.05, 0.0}, {0.05, 0.05}};
    AddDeckCommand tiny("f1", small);
    CHECK(tiny.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);
}

TEST_CASE("AddDeckCommand works through the command stack", "[decks][commands]")
{
    Project p = squareProjectWithWalls();
    CommandStack stack;
    requireOk(stack.push(std::make_unique<AddDeckCommand>("f1", porch()), p));
    CHECK(p.plan.floors[0].decks.size() == 1);
    REQUIRE(stack.canUndo());
    stack.undo(p);
    CHECK(p.plan.floors[0].decks.empty());
    requireOk(stack.redo(p));
    CHECK(p.plan.floors[0].decks.size() == 1);
}

TEST_CASE("SetDeckCommand replaces every value but the id and undoes", "[decks][commands]")
{
    Project p = projectWithDeck();
    const Project before = p;
    Deck values = porch(0.9);
    values.id = "other";
    values.thickness = 0.25;
    values.material = "oak_planks";
    values.skirt = false;
    values.skirtMaterial = "painted_wood";
    SetDeckCommand set("dk1", values);
    requireOk(set.apply(p));
    requirePlanValid(p.plan);
    const Deck& deck = p.plan.floors[0].decks[0];
    CHECK(deck.id == "dk1");
    CHECK(deck.top == Approx(0.9));
    CHECK(deck.thickness == Approx(0.25));
    CHECK(deck.material == std::optional<std::string>("oak_planks"));
    CHECK_FALSE(deck.skirt);
    CHECK(deck.skirtMaterial == std::optional<std::string>("painted_wood"));
    // The railing keeps its host, so it stands on the new top.
    CHECK(p.plan.floors[0].railings[0].host == std::optional<Id>("dk1"));
    CHECK(*deckTopAt(p.plan.floors[0], 5.0, 1.0) == Approx(0.9));
    CHECK(set.changes() == Change::Outdoor);
    CHECK(set.floorId() == "f1");
    CHECK(set.name() == "Change deck");

    set.undo(p);
    CHECK(p.plan == before.plan);
}

TEST_CASE("SetDeckCommand refuses an unknown deck and bad values", "[decks][commands]")
{
    Project p = projectWithDeck();
    const Project before = p;
    SetDeckCommand missing("dk9", porch());
    CHECK(missing.apply(p).code == ErrorCode::NotFound);
    CHECK(missing.apply(p).message == "There is no deck dk9.");
    SetDeckCommand high("dk1", porch(12.0));
    const Status s = high.apply(p);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message == "The deck top must be from 0 to 10 metres.");
    CHECK(p.plan == before.plan);
}

TEST_CASE("SetDeckCommand changes with one merge key undo as one step", "[decks][commands]")
{
    Project p = projectWithDeck();
    const Project before = p;
    Deck first = porch();
    first.outline[1].x = 7.5;
    Deck second = porch();
    second.outline[1].x = 8.0;
    second.outline[2].x = 8.0;

    SetDeckCommand a("dk1", first, 7);
    SetDeckCommand b("dk1", second, 7);
    requireOk(a.apply(p));
    requireOk(b.apply(p));
    CHECK(a.mergeWith(b));
    CHECK(p.plan.floors[0].decks[0].outline[2].x == Approx(8.0));
    a.undo(p);
    CHECK(p.plan == before.plan);

    // After the merge a redo gives the last values.
    requireOk(a.apply(p));
    CHECK(p.plan.floors[0].decks[0].outline[1].x == Approx(8.0));

    SetDeckCommand unkeyed("dk1", first);
    SetDeckCommand unkeyedNext("dk1", second);
    CHECK_FALSE(unkeyed.mergeWith(unkeyedNext));
    SetDeckCommand otherKey("dk1", second, 8);
    CHECK_FALSE(a.mergeWith(otherKey));
    SetDeckCommand otherDeck("dk2", second, 7);
    CHECK_FALSE(a.mergeWith(otherDeck));
}

TEST_CASE("SetDeckCommand drags merge on the command stack", "[decks][commands]")
{
    Project p = projectWithDeck();
    const Project before = p;
    CommandStack stack;
    for (double x : {7.2, 7.4, 7.6}) {
        Deck values = porch();
        values.outline[1].x = x;
        values.outline[2].x = x;
        requireOk(stack.push(std::make_unique<SetDeckCommand>("dk1", values, 3), p));
    }
    CHECK(p.plan.floors[0].decks[0].outline[1].x == Approx(7.6));
    stack.undo(p);
    CHECK(p.plan == before.plan);
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("RemoveDeckCommand removes a deck, frees its railings and undoes", "[decks][commands]")
{
    Project p = projectWithDeck();
    Deck second = porch();
    second.id = "dk2";
    second.outline = {{-3.0, 0.0}, {-0.1, 0.0}, {-0.1, 2.0}, {-3.0, 2.0}};
    p.plan.floors[0].decks.insert(p.plan.floors[0].decks.begin(), second);
    const Project before = p;

    RemoveDeckCommand remove("dk1");
    requireOk(remove.apply(p));
    requirePlanValid(p.plan);
    REQUIRE(p.plan.floors[0].decks.size() == 1);
    CHECK(p.plan.floors[0].decks[0].id == "dk2");
    CHECK_FALSE(p.plan.floors[0].railings[0].host);
    CHECK(p.plan.floors[0].railings[0].points == before.plan.floors[0].railings[0].points);
    CHECK(p.plan.floors[0].railings[0].base == Approx(0.0));
    CHECK(p.plan.floors[0].railings[1] == before.plan.floors[0].railings[1]);
    CHECK(remove.changes() == Change::Outdoor);
    CHECK(remove.floorId() == "f1");
    CHECK(remove.name() == "Remove deck");

    remove.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(remove.apply(p));
    CHECK(p.plan.floors[0].decks.size() == 1);
    remove.undo(p);
    CHECK(p.plan == before.plan);
}

TEST_CASE("RemoveDeckCommand refuses an unknown deck", "[decks][commands]")
{
    Project p = projectWithDeck();
    const Project before = p;
    RemoveDeckCommand remove("dk9");
    const Status s = remove.apply(p);
    CHECK(s.code == ErrorCode::NotFound);
    CHECK(s.message == "There is no deck dk9.");
    CHECK(p.plan == before.plan);
}
