#include "SquareFixtures.h"

#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/FinishCommands.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/LightCommands.h>
#include <plynth/core/commands/RoomCommands.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <string>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// The square project with room r1 and no lights.
Project roomProject()
{
    Project p = test::squareProjectWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    p.plan.floors[0].rooms = {r};
    return p;
}

// A ceiling light at a place.
LightMarker lightAt(double x, double y, double z = 2.9)
{
    LightMarker l;
    l.x = x;
    l.y = y;
    l.z = z;
    return l;
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

}

TEST_CASE("adding a light gives it an id, its room and its height", "[commands][lights]")
{
    Project p = roomProject();
    AddLightCommand add("f1", lightAt(2.0, 2.0));
    applyUndoRedo(p, add);
    CHECK(add.changes() == Change::Lights);
    CHECK(add.floorId() == "f1");
    REQUIRE(p.plan.floors[0].lights.size() == 1);
    const LightMarker& l = p.plan.floors[0].lights[0];
    CHECK(l.id == add.newLightId());
    CHECK(l.id.front() == 'l');
    CHECK(l.room == std::optional<Id>("r1"));

    AddLightCommand outside("f1", lightAt(6.0, 2.0, 1.0));
    test::requireOk(outside.apply(p));
    CHECK_FALSE(findLight(p.plan.floors[0], outside.newLightId())->room.has_value());

    AddLightCommand high("f1", lightAt(1.0, 1.0, 3.5));
    refused(p, high, ErrorCode::InvalidValue);
    AddLightCommand bad("f1", lightAt(std::numeric_limits<double>::quiet_NaN(), 1.0));
    refused(p, bad, ErrorCode::InvalidValue);
    AddLightCommand nowhere("f9", lightAt(1.0, 1.0));
    refused(p, nowhere, ErrorCode::NotFound);
}

TEST_CASE("moving a light follows its room and drags merge", "[commands][lights]")
{
    Project p = roomProject();
    AddLightCommand add("f1", lightAt(2.0, 2.0));
    test::requireOk(add.apply(p));
    const Id id = add.newLightId();

    MoveLightCommand out(id, 6.0, 2.0);
    applyUndoRedo(p, out);
    CHECK(out.changes() == Change::Lights);
    CHECK_FALSE(findLight(p.plan.floors[0], id)->room.has_value());
    out.undo(p);
    CHECK(findLight(p.plan.floors[0], id)->room == std::optional<Id>("r1"));

    MoveLightCommand first(id, 1.0, 1.0, 7);
    test::requireOk(first.apply(p));
    MoveLightCommand second(id, 1.5, 1.5, 7);
    test::requireOk(second.apply(p));
    CHECK(first.mergeWith(second));
    MoveLightCommand other(id, 1.5, 1.5, 8);
    CHECK_FALSE(first.mergeWith(other));
    first.undo(p);
    CHECK_THAT(findLight(p.plan.floors[0], id)->x, WithinAbs(2.0, 1e-12));

    MoveLightCommand missing("l_none", 1.0, 1.0);
    refused(p, missing, ErrorCode::NotFound);
}

TEST_CASE("a light's kind and height change and it can be removed", "[commands][lights]")
{
    Project p = roomProject();
    AddLightCommand add("f1", lightAt(2.0, 2.0));
    test::requireOk(add.apply(p));
    const Id id = add.newLightId();

    SetLightCommand set(id, LightKind::Wall, 1.8);
    applyUndoRedo(p, set);
    CHECK(findLight(p.plan.floors[0], id)->kind == LightKind::Wall);
    CHECK(findLight(p.plan.floors[0], id)->z == 1.8);
    SetLightCommand tooHigh(id, LightKind::Wall, 3.2);
    refused(p, tooHigh, ErrorCode::InvalidValue);

    AddLightCommand second("f1", lightAt(1.0, 1.0));
    test::requireOk(second.apply(p));
    RemoveLightCommand remove(id);
    applyUndoRedo(p, remove);
    CHECK(remove.changes() == Change::Lights);
    CHECK(findLight(p.plan.floors[0], id) == nullptr);
    remove.undo(p);
    CHECK(p.plan.floors[0].lights[0].id == id);
    RemoveLightCommand gone("l_none");
    refused(p, gone, ErrorCode::NotFound);
}

TEST_CASE("room changes, scale, image offset and delete carry the lights", "[commands][lights]")
{
    Project p = test::squareProjectWithWalls();
    p.plan.scale.pixelsPerMetre = 100.0;
    AddLightCommand add("f1", lightAt(2.0, 2.0));
    test::requireOk(add.apply(p));
    const Id id = add.newLightId();
    CHECK_FALSE(findLight(p.plan.floors[0], id)->room.has_value());

    AddRoomCommand room("f1", {2.0, 2.0});
    applyUndoRedo(p, room);
    const Id roomId = room.roomId();
    CHECK(findLight(p.plan.floors[0], id)->room == std::optional<Id>(roomId));
    RemoveRoomCommand removeRoom(roomId);
    applyUndoRedo(p, removeRoom);
    CHECK_FALSE(findLight(p.plan.floors[0], id)->room.has_value());
    removeRoom.undo(p);
    CHECK(findLight(p.plan.floors[0], id)->room == std::optional<Id>(roomId));

    SetScaleCommand scale(50.0);
    applyUndoRedo(p, scale);
    CHECK_THAT(findLight(p.plan.floors[0], id)->x, WithinAbs(4.0, 1e-9));
    scale.undo(p);

    SetFloorImageOffsetCommand offset("f1", {1.0, 0.5}, true);
    applyUndoRedo(p, offset);
    CHECK_THAT(findLight(p.plan.floors[0], id)->x, WithinAbs(3.0, 1e-9));
    CHECK_THAT(findLight(p.plan.floors[0], id)->y, WithinAbs(2.5, 1e-9));
    offset.undo(p);

    const Id ids[] = {id};
    DeleteBuild del = makeDeleteCommand(p.plan, ids);
    test::requireOk(del.status);
    CHECK(del.removes.lights == std::vector<Id>{id});
    applyUndoRedo(p, *del.command);
    CHECK(findLight(p.plan.floors[0], id) == nullptr);
}

TEST_CASE("trim, variation, room dressing and opening frame commands", "[commands][finish]")
{
    Project p = roomProject();
    Opening door = test::doorOn("w1", 2.0);
    door.id = "o1";
    p.plan.floors[0].openings = {door};

    TrimSettings trim;
    trim.cornice.enabled = false;
    trim.skirting.height = 0.12;
    SetTrimCommand setTrim(trim);
    applyUndoRedo(p, setTrim);
    CHECK(setTrim.changes() == Change::Settings);
    CHECK(p.settings.trim == trim);
    TrimSettings bad;
    bad.sill.depth = 0.9;
    SetTrimCommand badTrim(bad);
    refused(p, badTrim, ErrorCode::InvalidValue);

    VariationSettings variation;
    variation.seed = 7;
    variation.materials = false;
    SetVariationCommand setVariation(variation);
    applyUndoRedo(p, setVariation);
    CHECK(setVariation.changes() == Change::Settings);
    CHECK(p.settings.variation == variation);

    SetRoomDressingCommand dressing("r1", false);
    applyUndoRedo(p, dressing);
    CHECK(dressing.changes() == Change::Rooms);
    CHECK_FALSE(p.plan.floors[0].rooms[0].dressing);
    SetRoomDressingCommand noRoom("r9", false);
    refused(p, noRoom, ErrorCode::NotFound);

    SetOpeningFrameCommand named("o1", std::string("door_a"), true);
    applyUndoRedo(p, named);
    CHECK(named.changes() == Change::Openings);
    CHECK(p.plan.floors[0].openings[0].frame == std::optional<std::string>("door_a"));
    SetOpeningFrameCommand generated("o1", std::nullopt, false);
    applyUndoRedo(p, generated);
    CHECK_FALSE(p.plan.floors[0].openings[0].frame.has_value());
    CHECK_FALSE(p.plan.floors[0].openings[0].autoFrame);
    SetOpeningFrameCommand badName("o1", std::string("door a"), true);
    refused(p, badName, ErrorCode::InvalidValue);
    SetOpeningFrameCommand noOpening("o9", std::nullopt, true);
    refused(p, noOpening, ErrorCode::NotFound);
}
