#include "DormerFixtures.h"

#include <plynth/core/Command.h>
#include <plynth/core/commands/DormerCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;
using namespace plynth::test;

namespace {

// Applies the command, undoes it and applies it again, checking the project each time.
template <typename C, typename Check>
void roundTrip(Project& p, C& cmd, Check checkApplied)
{
    const Plan before = p.plan;
    REQUIRE(cmd.apply(p).ok());
    checkApplied(p);
    cmd.undo(p);
    CHECK(p.plan == before);
    REQUIRE(cmd.apply(p).ok());
    checkApplied(p);
}

// Checks the command is refused with the code and leaves the plan as it was.
void checkRefused(Project& p, Command& cmd, ErrorCode code)
{
    const Plan before = p.plan;
    CHECK(cmd.apply(p).code == code);
    CHECK(p.plan == before);
}

// The house with gableDormer already on it.
Project houseWithDormer()
{
    Project p = gableHouse();
    p.plan.floors[0].dormers.push_back(gableDormer());
    return p;
}

}

TEST_CASE("adding a dormer gives it a new id that stays the same on redo", "[core][dormers][commands]") {
    Project p = gableHouse();
    AddDormerCommand cmd("f1", gableDormer("ignored"));
    CHECK(cmd.newDormerId().empty());
    CHECK(cmd.changes() == Change::Roofs);
    CHECK(cmd.floorId() == "f1");
    CHECK(cmd.name() == "Add dormer");
    Id first;
    roundTrip(p, cmd, [&](const Project& q) {
        REQUIRE(q.plan.floors[0].dormers.size() == 1);
        const Dormer& d = q.plan.floors[0].dormers[0];
        CHECK(d.id.rfind(std::string(kDormerIdPrefix), 0) == 0);
        CHECK(d.id == cmd.newDormerId());
        if (first.empty()) first = d.id;
        CHECK(d.id == first);
        CHECK(d.width == 1.6);
        CHECK(checkPlan(q.plan).ok());
    });
}

TEST_CASE("adding a dormer that breaks a rule or does not fit is refused", "[core][dormers][commands]") {
    Project p = gableHouse();
    AddDormerCommand noFloor("f9", gableDormer());
    checkRefused(p, noFloor, ErrorCode::NotFound);

    Dormer narrow = gableDormer();
    narrow.width = 0.2;
    AddDormerCommand badValue("f1", narrow);
    checkRefused(p, badValue, ErrorCode::InvalidValue);

    AddDormerCommand offRoof("f1", gableDormer("dm1", 20.0, 20.0));
    checkRefused(p, offRoof, ErrorCode::InvalidValue);

    Dormer deep = gableDormer();
    deep.depth = 3.5;
    AddDormerCommand tooBig("f1", deep);
    checkRefused(p, tooBig, ErrorCode::InvalidValue);

    Dormer tall = gableDormer();
    tall.windowHeight = 2.5;
    AddDormerCommand window("f1", tall);
    checkRefused(p, window, ErrorCode::InvalidValue);

    Dormer eyebrow = gableDormer();
    eyebrow.type = "eyebrow";
    AddDormerCommand unknown("f1", eyebrow);
    checkRefused(p, unknown, ErrorCode::InvalidValue);

    Project flat = gableHouse();
    flat.plan.roof.type = "flat";
    AddDormerCommand onFlat("f1", gableDormer());
    checkRefused(flat, onFlat, ErrorCode::InvalidValue);
}

TEST_CASE("adding a dormer with the default values to a 30 degree roof works", "[core][dormers][commands]") {
    Project p = makeNewProject();
    addBoxRoom(p.plan.floors[0], 1, 0.0, 0.0, 10.0, 8.0);
    p.plan.roof.type = "gable";
    p.plan.roof.pitch = 30.0;
    Dormer d;
    d.x = 5.0;
    d.y = 7.6;
    AddDormerCommand cmd("f1", d);
    CHECK(cmd.apply(p).ok());
    CHECK(p.plan.floors[0].dormers.size() == 1);
}

TEST_CASE("adding a dormer over another one is refused", "[core][dormers][commands]") {
    Project p = houseWithDormer();
    AddDormerCommand over("f1", gableDormer("x", 4.5, 5.5));
    const Plan before = p.plan;
    const Status s = over.apply(p);
    CHECK(s.code == ErrorCode::InvalidValue);
    CHECK(s.message == "The dormer would overlap dormer dm1.");
    CHECK(p.plan == before);

    AddDormerCommand beside("f1", gableDormer("x", 6.2, 5.5));
    CHECK(beside.apply(p).ok());
    CHECK(p.plan.floors[0].dormers.size() == 2);
}

TEST_CASE("changing a dormer replaces its values and undoes", "[core][dormers][commands]") {
    Project p = houseWithDormer();
    Dormer values = shedDormer("other", 4.0, 5.5);
    SetDormerCommand cmd("dm1", values);
    CHECK(cmd.changes() == Change::Roofs);
    roundTrip(p, cmd, [](const Project& q) {
        REQUIRE(q.plan.floors[0].dormers.size() == 1);
        const Dormer& d = q.plan.floors[0].dormers[0];
        CHECK(d.id == "dm1");
        CHECK(d.type == "shed");
        CHECK(d.pitch == 10.0);
    });
    CHECK(cmd.floorId() == "f1");
}

TEST_CASE("changing a dormer so it no longer fits is refused", "[core][dormers][commands]") {
    Project p = houseWithDormer();
    p.plan.floors[0].dormers.push_back(gableDormer("dm2", 6.2, 5.5));

    SetDormerCommand missing("dm9", gableDormer());
    checkRefused(p, missing, ErrorCode::NotFound);

    Dormer deep = gableDormer();
    deep.depth = 3.5;
    SetDormerCommand tooBig("dm1", deep);
    checkRefused(p, tooBig, ErrorCode::InvalidValue);

    Dormer bad = gableDormer();
    bad.pitch = 80.0;
    SetDormerCommand badValue("dm1", bad);
    checkRefused(p, badValue, ErrorCode::InvalidValue);

    SetDormerCommand onto("dm2", gableDormer("dm2", 4.5, 5.5));
    checkRefused(p, onto, ErrorCode::InvalidValue);

    SetDormerCommand stays("dm1", gableDormer("dm1", 4.0, 5.4));
    CHECK(stays.apply(p).ok());
}

TEST_CASE("changes of one dormer with the same merge key undo as one step", "[core][dormers][commands]") {
    Project p = houseWithDormer();
    const Plan before = p.plan;
    SetDormerCommand first("dm1", gableDormer("dm1", 3.9, 5.5), 7);
    REQUIRE(first.apply(p).ok());
    SetDormerCommand second("dm1", gableDormer("dm1", 3.8, 5.5), 7);
    REQUIRE(second.apply(p).ok());
    CHECK(first.mergeWith(second));
    CHECK(p.plan.floors[0].dormers[0].x == 3.8);
    first.undo(p);
    CHECK(p.plan == before);
    REQUIRE(first.apply(p).ok());
    CHECK(p.plan.floors[0].dormers[0].x == 3.8);

    SetDormerCommand other("dm1", gableDormer("dm1", 3.7, 5.5), 8);
    CHECK_FALSE(first.mergeWith(other));
    SetDormerCommand unkeyed("dm1", gableDormer("dm1", 3.7, 5.5));
    CHECK_FALSE(unkeyed.mergeWith(unkeyed));
}

TEST_CASE("removing a dormer puts it back in its place on undo", "[core][dormers][commands]") {
    Project p = houseWithDormer();
    p.plan.floors[0].dormers.push_back(shedDormer());
    RemoveDormerCommand cmd("dm1");
    CHECK(cmd.changes() == Change::Roofs);
    CHECK(cmd.name() == "Remove dormer");
    roundTrip(p, cmd, [](const Project& q) {
        REQUIRE(q.plan.floors[0].dormers.size() == 1);
        CHECK(q.plan.floors[0].dormers[0].id == "dm2");
    });
    CHECK(cmd.floorId() == "f1");

    RemoveDormerCommand missing("dm9");
    checkRefused(p, missing, ErrorCode::NotFound);
}
