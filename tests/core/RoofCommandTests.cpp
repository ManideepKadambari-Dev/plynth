#include "RoomFixtures.h"

#include <plynth/core/Command.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/commands/RoofCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace plynth;
using namespace plynth::test;

namespace {

// A project with one closed room 6 by 4 m outside.
Project oneRoomProject()
{
    Project p = makeNewProject();
    Floor& f = p.plan.floors[0];
    addRectangle(f, 1, 0.1, 0.1, 5.9, 3.9, 0.2);
    Room r;
    r.id = "r1";
    r.walls = {"w11", "w12", "w13", "w14"};
    f.rooms.push_back(r);
    return p;
}

// Gable settings.
RoofSettings gable(double pitch = 35.0)
{
    RoofSettings s;
    s.type = "gable";
    s.pitch = pitch;
    return s;
}

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

}

TEST_CASE("setting the roof default applies, undoes and redoes", "[roofs]")
{
    Project p = oneRoomProject();
    SetRoofDefaultCommand cmd(gable(40.0));
    CHECK(cmd.changes() == Change::Roofs);
    roundTrip(p, cmd, [](const Project& q) {
        CHECK(q.plan.roof.type == "gable");
        CHECK(q.plan.roof.pitch == 40.0);
    });
}

TEST_CASE("a roof default that breaks the rules is refused and changes nothing", "[roofs]")
{
    Project p = oneRoomProject();
    const Plan before = p.plan;
    SetRoofDefaultCommand low(gable(2.0));
    CHECK(low.apply(p).code == ErrorCode::InvalidValue);
    RoofSettings unknown;
    unknown.type = "dome";
    SetRoofDefaultCommand dome(unknown);
    CHECK(dome.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before);
}

TEST_CASE("adding a roof section entry applies, undoes and redoes with the same id", "[roofs]")
{
    Project p = oneRoomProject();
    AddRoofSectionCommand cmd("f1", 3.0, 2.0, gable(), std::string("roof_tiles"));
    CHECK(cmd.floorId() == "f1");
    roundTrip(p, cmd, [](const Project& q) {
        REQUIRE(q.plan.roofSections.size() == 1);
        CHECK(q.plan.roofSections[0].id == "rf1");
        CHECK(q.plan.roofSections[0].floor == "f1");
        CHECK(q.plan.roofSections[0].settings.type == "gable");
        CHECK(q.plan.roofSections[0].material == std::optional<std::string>("roof_tiles"));
    });
    CHECK(cmd.newSectionId() == "rf1");
}

TEST_CASE("adding a roof section entry is refused for bad places and values", "[roofs]")
{
    Project p = oneRoomProject();
    AddRoofSectionCommand first("f1", 3.0, 2.0, gable());
    REQUIRE(first.apply(p).ok());
    const Plan before = p.plan;

    AddRoofSectionCommand outside("f1", 20.0, 20.0, gable());
    const Status none = outside.apply(p);
    CHECK(none.code == ErrorCode::InvalidValue);
    CHECK(none.message == "There is no roof section there.");

    AddRoofSectionCommand again("f1", 1.0, 1.0, gable());
    const Status taken = again.apply(p);
    CHECK(taken.code == ErrorCode::AlreadyExists);
    CHECK(taken.message.find("rf1") != std::string::npos);

    AddRoofSectionCommand noFloor("f9", 3.0, 2.0, gable());
    CHECK(noFloor.apply(p).code == ErrorCode::NotFound);
    CHECK(p.plan == before);

    Project q = oneRoomProject();
    AddRoofSectionCommand lowPitch("f1", 3.0, 2.0, gable(2.0));
    CHECK(lowPitch.apply(q).code == ErrorCode::InvalidValue);
    AddRoofSectionCommand badMaterial("f1", 3.0, 2.0, gable(), std::string("bad name"));
    CHECK(badMaterial.apply(q).code == ErrorCode::InvalidValue);
    CHECK(q.plan.roofSections.empty());
}

TEST_CASE("setting a roof section entry applies, undoes and redoes", "[roofs]")
{
    Project p = oneRoomProject();
    AddRoofSectionCommand add("f1", 3.0, 2.0, gable());
    REQUIRE(add.apply(p).ok());
    RoofSettings hip;
    hip.type = "hip";
    SetRoofSectionCommand cmd("rf1", hip, std::string("roof_tiles"));
    roundTrip(p, cmd, [](const Project& q) {
        CHECK(q.plan.roofSections[0].settings.type == "hip");
        CHECK(q.plan.roofSections[0].material == std::optional<std::string>("roof_tiles"));
        CHECK(q.plan.roofSections[0].x == 3.0);
    });
    CHECK(cmd.floorId() == "f1");
    CHECK(cmd.changes() == Change::Roofs);

    SetRoofSectionCommand missing("rf9", hip, {});
    CHECK(missing.apply(p).code == ErrorCode::NotFound);
    SetRoofSectionCommand bad("rf1", gable(1.0), {});
    CHECK(bad.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan.roofSections[0].settings.type == "hip");
}

TEST_CASE("removing a roof section entry applies, undoes and redoes in place", "[roofs]")
{
    Project p = oneRoomProject();
    AddRoofSectionCommand a("f1", 3.0, 2.0, gable());
    REQUIRE(a.apply(p).ok());
    RoofSection extra;
    extra.id = "rf7";
    extra.floor = "f1";
    p.plan.roofSections.push_back(extra);

    RemoveRoofSectionCommand cmd("rf1");
    const Plan before = p.plan;
    REQUIRE(cmd.apply(p).ok());
    REQUIRE(p.plan.roofSections.size() == 1);
    CHECK(p.plan.roofSections[0].id == "rf7");
    CHECK(cmd.floorId() == "f1");
    cmd.undo(p);
    CHECK(p.plan == before);
    REQUIRE(cmd.apply(p).ok());
    CHECK(p.plan.roofSections.size() == 1);

    RemoveRoofSectionCommand missing("rf9");
    CHECK(missing.apply(p).code == ErrorCode::NotFound);
}
