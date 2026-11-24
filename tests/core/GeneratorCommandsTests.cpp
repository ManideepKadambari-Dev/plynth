#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/CommandStack.h>
#include <plynth/core/Items.h>
#include <plynth/core/commands/GeneratorCommands.h>
#include <plynth/core/commands/ItemCommands.h>
#include <plynth/core/commands/RoomCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

Project houseWithRooms()
{
    Project p = splitHouseProject();
    AddRoomCommand a("f1", {1.5, 2.0});
    AddRoomCommand b("f1", {4.5, 2.0});
    requireOk(a.apply(p));
    requireOk(b.apply(p));
    return p;
}

GeneratorUse columnUse(double diameter, const std::string& shape = "round")
{
    return GeneratorUse{"column", {{"diameter", GeneratorValue{diameter}}, {"shape", GeneratorValue{shape}}}};
}

Id placeColumn(Project& p, double x, double y)
{
    Item it;
    it.x = x;
    it.y = y;
    it.size = {0.3, 0.3, 2.7};
    it.generator = columnUse(0.3);
    PlaceItemCommand c("f1", it);
    requireOk(c.apply(p));
    return c.itemId();
}

const Item& get(const Project& p, const Id& id)
{
    const Item* it = findItem(p.plan.floors[0], id);
    REQUIRE(it != nullptr);
    return *it;
}

}

TEST_CASE("SetItemGeneratorCommand changes values and size and keeps the rest", "[generatorcommands]") {
    Project p = houseWithRooms();
    const Id id = placeColumn(p, 4.5, 2.0);
    const Project before = p;
    SetItemGeneratorCommand c(id, columnUse(0.5, "square"), {0.5, 0.5, 2.7});
    REQUIRE_FALSE(c.name().empty());
    REQUIRE(c.changes() == Change::Items);
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
    REQUIRE(c.floorId() == "f1");
    const Item& it = get(p, id);
    REQUIRE(it.size == ItemSize{0.5, 0.5, 2.7});
    REQUIRE(std::get<double>(it.generator->params[0].value) == Approx(0.5));
    REQUIRE(std::get<std::string>(it.generator->params[1].value) == "square");
    REQUIRE(it.x == Approx(4.5));
    REQUIRE(it.y == Approx(2.0));
    REQUIRE(it.room == "r2");
    c.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("SetItemGeneratorCommand refuses an unknown item and bad input", "[generatorcommands]") {
    Project p = houseWithRooms();
    const Id id = placeColumn(p, 4.5, 2.0);
    const Project before = p;
    SetItemGeneratorCommand unknown("i99", columnUse(0.5), {0.5, 0.5, 2.7});
    REQUIRE(unknown.apply(p).code == ErrorCode::NotFound);
    SetItemGeneratorCommand flat(id, columnUse(0.5), {0.5, 0.5, 0.0});
    REQUIRE(flat.apply(p).code == ErrorCode::InvalidValue);
    GeneratorUse bad = columnUse(0.5);
    bad.name = "Bad";
    SetItemGeneratorCommand badName(id, bad, {0.5, 0.5, 2.7});
    REQUIRE(badName.apply(p).code == ErrorCode::InvalidValue);
    REQUIRE(p == before);
}

TEST_CASE("SetItemGeneratorCommand keeps a wall item flush for the new depth", "[generatorcommands]") {
    Project p = houseWithRooms();
    Item panel;
    panel.x = 1.4;
    panel.y = 1.0;
    panel.z = 0.5;
    panel.size = {1.0, 0.05, 1.0};
    panel.wall = "w1";
    panel.generator = GeneratorUse{"screen", {{"depth", GeneratorValue{0.05}}}};
    PlaceItemCommand place("f1", panel);
    requireOk(place.apply(p));
    const Id id = place.itemId();
    const Project before = p;
    SetItemGeneratorCommand c(id, GeneratorUse{"screen", {{"depth", GeneratorValue{0.3}}}}, {1.0, 0.3, 1.0});
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
    const WallItemSpot spot = wallItemSpot(p.plan.floors[0], get(p, id));
    REQUIRE(spot.valid);
    REQUIRE(spot.gap == Approx(0.0).margin(1e-9));
    REQUIRE(get(p, id).wall == "w1");
    REQUIRE(get(p, id).size.depth == Approx(0.3));
    c.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("SetItemGeneratorCommand merges with the same key", "[generatorcommands]") {
    Project p = houseWithRooms();
    const Id id = placeColumn(p, 4.5, 2.0);
    const Project start = p;
    CommandStack stack;
    bool merged = false;
    requireOk(stack.push(std::make_unique<SetItemGeneratorCommand>(id, columnUse(0.4), ItemSize{0.4, 0.4, 2.7}, 5), p, &merged));
    requireOk(stack.push(std::make_unique<SetItemGeneratorCommand>(id, columnUse(0.6), ItemSize{0.6, 0.6, 2.7}, 5), p, &merged));
    REQUIRE(merged);
    REQUIRE(get(p, id).size.width == Approx(0.6));
    requireOk(stack.push(std::make_unique<SetItemGeneratorCommand>(id, columnUse(0.7), ItemSize{0.7, 0.7, 2.7}, 6), p, &merged));
    REQUIRE_FALSE(merged);
    stack.undo(p);
    REQUIRE(get(p, id).size.width == Approx(0.6));
    stack.undo(p);
    REQUIRE(p == start);
}

TEST_CASE("SetItemGeneratorCommand turns an asset item into a generator item", "[generatorcommands]") {
    Project p = houseWithRooms();
    Item sofa;
    sofa.asset = "sofa";
    sofa.x = 4.5;
    sofa.y = 2.0;
    sofa.size = {2.0, 0.9, 0.85};
    PlaceItemCommand place("f1", sofa);
    requireOk(place.apply(p));
    const Id id = place.itemId();
    const Project before = p;
    SetItemGeneratorCommand c(id, columnUse(0.3), {0.3, 0.3, 2.7});
    requireOk(c.apply(p));
    REQUIRE(get(p, id).asset.empty());
    REQUIRE(get(p, id).generator.has_value());
    c.undo(p);
    REQUIRE(p == before);
    REQUIRE(get(p, id).asset == "sofa");
}
