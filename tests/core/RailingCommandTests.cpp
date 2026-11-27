#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Command.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/Railings.h>
#include <plynth/core/commands/DeckCommands.h>
#include <plynth/core/commands/RailingCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A glass railing along the far edge of the porch deck.
Railing porchRailing()
{
    Railing r;
    r.points = {{4.1, 2.0}, {7.0, 2.0}, {7.0, 0.0}};
    r.style = "glass";
    r.height = 1.1;
    r.host = "dk1";
    return r;
}

// The square project with a porch deck dk1 east of wall w2 and one railing rl1 on it.
Project projectWithRailing()
{
    Project p = squareProjectWithWalls();
    Deck deck;
    deck.id = "dk1";
    deck.outline = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    deck.top = 0.6;
    p.plan.floors[0].decks.push_back(deck);
    Railing railing = porchRailing();
    railing.id = "rl1";
    p.plan.floors[0].railings.push_back(railing);
    Railing other;
    other.id = "rl2";
    other.points = {{0.0, 5.0}, {2.0, 5.0}};
    p.plan.floors[0].railings.push_back(other);
    return p;
}

}

TEST_CASE("AddRailingCommand adds, undoes and redoes with the same id", "[railings][commands]")
{
    Project p = projectWithRailing();
    const Project before = p;
    Railing values = porchRailing();
    values.id = "ignored";
    values.style = "spindles";
    AddRailingCommand add("f1", values);
    CHECK(add.newRailingId().empty());
    requireOk(add.apply(p));
    requirePlanValid(p.plan);
    const Id id = add.newRailingId();
    CHECK(id.rfind("rl", 0) == 0);
    CHECK(isValidId(id));
    REQUIRE(p.plan.floors[0].railings.size() == 3);
    const Railing& added = p.plan.floors[0].railings.back();
    CHECK(added.id == id);
    CHECK(added.style == "spindles");
    CHECK(added.host == std::optional<Id>("dk1"));
    CHECK(add.changes() == Change::Outdoor);
    CHECK(add.floorId() == "f1");
    CHECK(add.name() == "Add railing");

    add.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(add.apply(p));
    REQUIRE(p.plan.floors[0].railings.size() == 3);
    CHECK(p.plan.floors[0].railings.back().id == id);
}

TEST_CASE("AddRailingCommand refuses an unknown floor or deck and bad values", "[railings][commands]")
{
    Project p = projectWithRailing();
    const Project before = p;
    AddRailingCommand nowhere("f9", porchRailing());
    CHECK(nowhere.apply(p).code == ErrorCode::NotFound);
    CHECK(p.plan == before.plan);

    Railing lost = porchRailing();
    lost.host = "dk9";
    AddRailingCommand noDeck("f1", lost);
    const Status s = noDeck.apply(p);
    CHECK(s.code == ErrorCode::NotFound);
    CHECK(s.message == "There is no deck dk9 on Floor 1.");
    CHECK(p.plan == before.plan);

    Railing tall = porchRailing();
    tall.height = 1.8;
    AddRailingCommand tooTall("f1", tall);
    CHECK(tooTall.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);

    Railing shortLine = porchRailing();
    shortLine.points = {{4.1, 2.0}, {4.15, 2.0}};
    AddRailingCommand tooShort("f1", shortLine);
    CHECK(tooShort.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);

    Railing unknown = porchRailing();
    unknown.style = "rope_bridge";
    AddRailingCommand noStyle("f1", unknown);
    CHECK(noStyle.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);
}

TEST_CASE("AddRailingCommand works through the command stack", "[railings][commands]")
{
    Project p = projectWithRailing();
    CommandStack stack;
    requireOk(stack.push(std::make_unique<AddRailingCommand>("f1", porchRailing()), p));
    CHECK(p.plan.floors[0].railings.size() == 3);
    REQUIRE(stack.canUndo());
    stack.undo(p);
    CHECK(p.plan.floors[0].railings.size() == 2);
    requireOk(stack.redo(p));
    CHECK(p.plan.floors[0].railings.size() == 3);
}

TEST_CASE("SetRailingCommand replaces every value but the id and undoes", "[railings][commands]")
{
    Project p = projectWithRailing();
    const Project before = p;
    Railing values;
    values.id = "other";
    values.points = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    values.closed = false;
    values.host.reset();
    values.base = 0.3;
    values.height = 0.9;
    values.style = "half_wall";
    values.material = "red_brick";
    SetRailingCommand set("rl1", values);
    requireOk(set.apply(p));
    requirePlanValid(p.plan);
    const Railing& railing = p.plan.floors[0].railings[0];
    CHECK(railing.id == "rl1");
    CHECK(railing.points.size() == 4);
    CHECK_FALSE(railing.host);
    CHECK(railing.base == Approx(0.3));
    CHECK(railing.height == Approx(0.9));
    CHECK(railing.style == "half_wall");
    CHECK(railing.material == std::optional<std::string>("red_brick"));
    CHECK(set.changes() == Change::Outdoor);
    CHECK(set.floorId() == "f1");
    CHECK(set.name() == "Change railing");

    set.undo(p);
    CHECK(p.plan == before.plan);
}

TEST_CASE("SetRailingCommand refuses an unknown railing or deck and bad values", "[railings][commands]")
{
    Project p = projectWithRailing();
    const Project before = p;
    SetRailingCommand missing("rl9", porchRailing());
    CHECK(missing.apply(p).code == ErrorCode::NotFound);

    Railing lost = porchRailing();
    lost.host = "dk9";
    SetRailingCommand noDeck("rl1", lost);
    CHECK(noDeck.apply(p).code == ErrorCode::NotFound);

    Railing low = porchRailing();
    low.height = 0.1;
    SetRailingCommand tooLow("rl1", low);
    const Status s = tooLow.apply(p);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message == "The railing height must be from 0.3 to 3 metres.");
    CHECK(p.plan == before.plan);
}

TEST_CASE("SetRailingCommand merges a drag into one undo step", "[railings][commands]")
{
    Project p = projectWithRailing();
    const Project before = p;
    CommandStack stack;
    for (const double x : {7.5, 8.0, 8.5}) {
        Railing values = porchRailing();
        values.points.back().x = x;
        values.points[1].x = x;
        requireOk(stack.push(std::make_unique<SetRailingCommand>("rl1", values, 7), p));
    }
    CHECK(p.plan.floors[0].railings[0].points[1].x == Approx(8.5));
    stack.undo(p);
    CHECK(p.plan == before.plan);
    CHECK_FALSE(stack.canUndo());

    // Without a key, or with another railing, nothing merges.
    SetRailingCommand first("rl1", porchRailing(), 0);
    SetRailingCommand second("rl1", porchRailing(), 0);
    CHECK_FALSE(first.mergeWith(second));
    SetRailingCommand keyed("rl1", porchRailing(), 3);
    Railing elsewhere;
    elsewhere.points = {{0.0, 5.0}, {3.0, 5.0}};
    SetRailingCommand otherRailing("rl2", elsewhere, 3);
    CHECK_FALSE(keyed.mergeWith(otherRailing));
}

TEST_CASE("RemoveRailingCommand removes and puts back in place", "[railings][commands]")
{
    Project p = projectWithRailing();
    const Project before = p;
    RemoveRailingCommand remove("rl1");
    requireOk(remove.apply(p));
    requirePlanValid(p.plan);
    REQUIRE(p.plan.floors[0].railings.size() == 1);
    CHECK(p.plan.floors[0].railings[0].id == "rl2");
    CHECK(remove.changes() == Change::Outdoor);
    CHECK(remove.floorId() == "f1");
    CHECK(remove.name() == "Remove railing");
    remove.undo(p);
    CHECK(p.plan == before.plan);

    RemoveRailingCommand missing("rl9");
    CHECK(missing.apply(p).code == ErrorCode::NotFound);
    CHECK(p.plan == before.plan);
}

TEST_CASE("removing a deck leaves its railings standing on their base, and undo puts them back on it",
          "[railings][commands][decks]")
{
    Project p = projectWithRailing();
    const Project before = p;
    p.plan.floors[0].railings[0].base = 0.2;
    const Project withBase = p;
    const std::vector<double> levels = floorElevations(p.plan);
    CHECK(railingRuns(p.plan, p.plan.floors[0], p.plan.floors[0].railings[0], levels)[0].start.z == Approx(0.6));

    RemoveDeckCommand remove("dk1");
    requireOk(remove.apply(p));
    requirePlanValid(p.plan);
    REQUIRE(p.plan.floors[0].railings.size() == 2);
    const Railing& kept = p.plan.floors[0].railings[0];
    CHECK_FALSE(kept.host);
    CHECK(kept.points == before.plan.floors[0].railings[0].points);
    CHECK(railingRuns(p.plan, p.plan.floors[0], kept, levels)[0].start.z == Approx(0.2));

    remove.undo(p);
    CHECK(p.plan == withBase.plan);
}

TEST_CASE("railings added round a deck's open edges stand on it", "[railings][commands][decks]")
{
    Project p = projectWithRailing();
    const Deck& deck = p.plan.floors[0].decks[0];
    const std::vector<std::vector<PlanXY>> lines = openDeckEdges(p.plan.floors[0], deck);
    REQUIRE(lines.size() == 1);
    Railing round;
    round.points = lines[0];
    round.host = "dk1";
    round.style = "flat_bars";
    AddRailingCommand add("f1", round);
    requireOk(add.apply(p));
    requirePlanValid(p.plan);
    const Railing& added = p.plan.floors[0].railings.back();
    const std::vector<RailingRun> runs = railingRuns(p.plan, p.plan.floors[0], added, floorElevations(p.plan));
    CHECK(runs.size() == 3);
    for (const RailingRun& run : runs) CHECK(run.start.z == Approx(0.6));
}
