#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Command.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/Items.h>
#include <plynth/core/Snapping.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/ItemCommands.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

const ItemSize kSofa{2.0, 0.9, 0.85};
const ItemSize kTv{1.23, 0.06, 0.71};
const ItemSize kTable{1.2, 0.8, 0.75};

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

// An item as the caller would give it.
Item newItem(const std::string& asset, double x, double y, ItemSize size, double rotation = 0.0, double z = 0.0)
{
    Item it;
    it.asset = asset;
    it.x = x;
    it.y = y;
    it.z = z;
    it.rotation = rotation;
    it.size = size;
    return it;
}

// A TV mounted on a wall, roughly placed.
Item newTv(const std::string& wall, double x, double y, double z = 0.8)
{
    Item tv = newItem("tv", x, y, kTv, 0.0, z);
    tv.wall = wall;
    return tv;
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

// Applies, undoes and redoes a command, checking the project equals before after undo and the first result after redo.
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

// Places an item through PlaceItemCommand and returns its id.
Id place(Project& p, const Item& item)
{
    PlaceItemCommand c("f1", item);
    applyOk(c, p);
    return c.itemId();
}

// The item with an id on the first floor.
const Item& get(const Project& p, const Id& id)
{
    const Item* it = findItem(p.plan.floors[0], id);
    REQUIRE(it != nullptr);
    return *it;
}

// The gap of a wall item from its wall, which is about zero when it is flush.
double gapOf(const Project& p, const Id& id)
{
    const WallItemSpot spot = wallItemSpot(p.plan.floors[0], get(p, id));
    REQUIRE(spot.valid);
    return spot.gap;
}

}

TEST_CASE("PlaceItemCommand places, undoes and redoes with the same id", "[core][command][item]") {
    Project p = houseWithRooms();
    const Project before = p;
    PlaceItemCommand c("f1", newItem("sofa", 4.5, 2.0, kSofa, 90.0));
    REQUIRE(c.itemId().empty());
    REQUIRE(c.name() == "Place item");
    REQUIRE(c.floorId() == "f1");
    REQUIRE(c.changes() == Change::Items);
    applyOk(c, p);
    REQUIRE(c.itemId() == "i1");
    const Item& it = get(p, "i1");
    REQUIRE(it.asset == "sofa");
    REQUIRE(it.room == "r2");
    REQUIRE(it.rotation == Approx(90.0));
    REQUIRE(it.size == kSofa);
    const Project after = p;
    c.undo(p);
    REQUIRE(p == before);
    applyOk(c, p);
    REQUIRE(c.itemId() == "i1");
    REQUIRE(p == after);

    PlaceItemCommand second("f1", newItem("table", 1.5, 2.0, kTable));
    applyOk(second, p);
    REQUIRE(second.itemId() == "i2");
    REQUIRE(get(p, "i2").room == "r1");
    REQUIRE(p.plan.floors[0].items.size() == 2);
}

TEST_CASE("PlaceItemCommand room, rotation and refusals", "[core][command][item]") {
    Project p = houseWithRooms();

    SECTION("an item outside every room has no room") {
        const Id id = place(p, newItem("sofa", 10.0, 10.0, kSofa));
        REQUIRE_FALSE(get(p, id).room);
    }
    SECTION("a given room is kept") {
        Item it = newItem("sofa", 4.5, 2.0, kSofa);
        it.room = "r1";
        const Id id = place(p, it);
        REQUIRE(get(p, id).room == "r1");
    }
    SECTION("rotation is normalized") {
        const Id id = place(p, newItem("sofa", 1.5, 2.0, kSofa, -90.0));
        REQUIRE(get(p, id).rotation == Approx(270.0));
    }
    SECTION("the id of the caller is ignored") {
        Item it = newItem("sofa", 1.5, 2.0, kSofa);
        it.id = "i77";
        REQUIRE(place(p, it) == "i1");
    }
    SECTION("refusals leave the project as it was") {
        {
            PlaceItemCommand c("f9", newItem("sofa", 1.5, 2.0, kSofa));
            applyRefused(c, p, ErrorCode::NotFound);
        }
        {
            Item it = newItem("sofa 1", 1.5, 2.0, kSofa);
            PlaceItemCommand c("f1", it);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
        {
            Item it = newItem("sofa", 1.5, 2.0, {0.0, 1.0, 1.0});
            PlaceItemCommand c("f1", it);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
        {
            Item it = newItem("sofa", 1.5, 2.0, kSofa, 0.0, -0.1);
            PlaceItemCommand c("f1", it);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
        {
            Item it = newItem("sofa", std::numeric_limits<double>::quiet_NaN(), 2.0, kSofa);
            PlaceItemCommand c("f1", it);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
        {
            Item it = newItem("sofa", 1.5, 2.0, kSofa);
            it.room = "r9";
            PlaceItemCommand c("f1", it);
            applyRefused(c, p, ErrorCode::NotFound);
        }
        {
            Item it = newItem("sofa", 1.5, 2.0, kSofa);
            it.wall = "w99";
            PlaceItemCommand c("f1", it);
            applyRefused(c, p, ErrorCode::NotFound);
        }
        {
            PlaceItemCommand c("f1", newTv("w1", 1.4, 1.0, 2.9));
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
        {
            Item shelf = newItem("shelf", 1.5, 1.0, {3.5, 0.3, 0.3}, 0.0, 1.0);
            shelf.wall = "w1";
            PlaceItemCommand c("f1", shelf);
            applyRefused(c, p, ErrorCode::InvalidValue);
        }
    }
    SECTION("overlaps and misfits are not refused") {
        place(p, newItem("table", 1.5, 2.0, kTable));
        place(p, newItem("table", 1.6, 2.0, kTable));
        place(p, newItem("sofa", 5.9, 3.9, kSofa));
        REQUIRE(p.plan.floors[0].items.size() == 3);
    }
}

TEST_CASE("a wall item placed with a rough position ends flush", "[core][command][item][wall]") {
    Project p = houseWithRooms();
    const Id id = place(p, newTv("w1", 1.4, 1.0));
    const Item& tv = get(p, id);
    REQUIRE(tv.wall == "w1");
    REQUIRE(tv.x == Approx(1.4));
    REQUIRE(tv.y == Approx(0.15 + 0.03));
    REQUIRE(tv.rotation == Approx(0.0).margin(1e-9));
    REQUIRE(tv.z == Approx(0.8));
    REQUIRE(tv.room == "r1");
    REQUIRE(gapOf(p, id) == Approx(0.0).margin(1e-9));
    REQUIRE(checkItem(p.plan.floors[0], tv).empty());
}

TEST_CASE("MoveItemCommand moves floor items and slides wall items", "[core][command][item]") {
    Project p = houseWithRooms();
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id tv = place(p, newTv("w1", 1.4, 1.0));

    SECTION("a floor item goes to the position and takes the room there") {
        MoveItemCommand c(sofa, {4.5, 3.0});
        REQUIRE(c.name() == "Move item");
        REQUIRE(c.changes() == Change::Items);
        roundTrip(c, p);
        REQUIRE(get(p, sofa).x == 4.5);
        REQUIRE(get(p, sofa).y == 3.0);
        REQUIRE(get(p, sofa).room == "r2");
        REQUIRE(c.floorId() == "f1");
    }
    SECTION("moving out of every room clears the room") {
        MoveItemCommand c(sofa, {20.0, 3.0});
        applyOk(c, p);
        REQUIRE_FALSE(get(p, sofa).room);
    }
    SECTION("a wall item slides along its wall and stays flush") {
        MoveItemCommand c(tv, {2.2, 1.6});
        roundTrip(c, p);
        REQUIRE(get(p, tv).x == Approx(2.2));
        REQUIRE(get(p, tv).y == Approx(0.18));
        REQUIRE(get(p, tv).wall == "w1");
        REQUIRE(gapOf(p, tv) == Approx(0.0).margin(1e-9));
    }
    SECTION("a wall item is kept inside its wall's length") {
        MoveItemCommand c(tv, {2.99, 1.0});
        applyOk(c, p);
        REQUIRE(get(p, tv).x == Approx(3.0 - 0.615));
    }
    SECTION("refusals") {
        MoveItemCommand unknown("i99", {1.0, 1.0});
        applyRefused(unknown, p, ErrorCode::NotFound);
        MoveItemCommand bad(sofa, {std::numeric_limits<double>::infinity(), 1.0});
        applyRefused(bad, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("item moves merge only with the same non zero key", "[core][command][item]") {
    Project p = houseWithRooms();
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Project start = p;

    SECTION("key 5 merges into one undo step") {
        CommandStack stack;
        bool merged = true;
        requireOk(stack.push(std::make_unique<MoveItemCommand>(sofa, Vec2{2.0, 2.0}, 5), p, &merged));
        REQUIRE_FALSE(merged);
        requireOk(stack.push(std::make_unique<MoveItemCommand>(sofa, Vec2{2.5, 2.5}, 5), p, &merged));
        REQUIRE(merged);
        REQUIRE(get(p, sofa).x == 2.5);
        stack.undo(p);
        REQUIRE(p == start);
        requireOk(stack.redo(p));
        REQUIRE(get(p, sofa).x == 2.5);
        REQUIRE(get(p, sofa).y == 2.5);
    }
    SECTION("key 0 never merges") {
        CommandStack stack;
        bool merged = true;
        requireOk(stack.push(std::make_unique<MoveItemCommand>(sofa, Vec2{2.0, 2.0}, 0), p, &merged));
        requireOk(stack.push(std::make_unique<MoveItemCommand>(sofa, Vec2{2.5, 2.5}, 0), p, &merged));
        REQUIRE_FALSE(merged);
        stack.undo(p);
        REQUIRE(get(p, sofa).x == 2.0);
    }
    SECTION("different keys or items do not merge") {
        const Id other = place(p, newItem("table", 1.0, 3.0, kTable));
        CommandStack stack;
        bool merged = true;
        requireOk(stack.push(std::make_unique<MoveItemCommand>(sofa, Vec2{2.0, 2.0}, 5), p, &merged));
        requireOk(stack.push(std::make_unique<MoveItemCommand>(sofa, Vec2{2.5, 2.5}, 6), p, &merged));
        REQUIRE_FALSE(merged);
        requireOk(stack.push(std::make_unique<MoveItemCommand>(other, Vec2{2.5, 3.0}, 6), p, &merged));
        REQUIRE_FALSE(merged);
    }
}

TEST_CASE("MoveItemOnWallCommand slides and lifts a wall item", "[core][command][item][wall]") {
    Project p = houseWithRooms();
    const Id tv = place(p, newTv("w1", 1.4, 1.0));
    const Id sofa = place(p, newItem("sofa", 4.5, 2.0, kSofa));

    SECTION("offset and height change and it stays flush") {
        MoveItemOnWallCommand c(tv, 2.0, 1.1);
        REQUIRE(c.name() == "Move item on wall");
        REQUIRE(c.changes() == Change::Items);
        roundTrip(c, p);
        REQUIRE(get(p, tv).x == Approx(2.0));
        REQUIRE(get(p, tv).z == Approx(1.1));
        REQUIRE(gapOf(p, tv) == Approx(0.0).margin(1e-9));
    }
    SECTION("a top above the wall is refused and nothing changes") {
        MoveItemOnWallCommand c(tv, 2.0, 2.5);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("an item that is not on a wall, or unknown") {
        MoveItemOnWallCommand floorItem(sofa, 2.0, 1.0);
        applyRefused(floorItem, p, ErrorCode::InvalidValue);
        MoveItemOnWallCommand unknown("i99", 2.0, 1.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        MoveItemOnWallCommand nan(tv, std::numeric_limits<double>::quiet_NaN(), 1.0);
        applyRefused(nan, p, ErrorCode::InvalidValue);
    }
    SECTION("merging") {
        const Project start = p;
        CommandStack stack;
        bool merged = false;
        requireOk(stack.push(std::make_unique<MoveItemOnWallCommand>(tv, 1.8, 1.0, 7), p, &merged));
        requireOk(stack.push(std::make_unique<MoveItemOnWallCommand>(tv, 2.1, 1.2, 7), p, &merged));
        REQUIRE(merged);
        REQUIRE(get(p, tv).z == Approx(1.2));
        stack.undo(p);
        REQUIRE(p == start);
    }
}

TEST_CASE("SetItemHeightCommand sets z", "[core][command][item]") {
    Project p = houseWithRooms();
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id tv = place(p, newTv("w1", 1.4, 1.0));

    SECTION("a floor item") {
        SetItemHeightCommand c(sofa, 0.3);
        REQUIRE(c.changes() == Change::Items);
        roundTrip(c, p);
        REQUIRE(get(p, sofa).z == 0.3);
    }
    SECTION("a wall item keeps within its wall") {
        SetItemHeightCommand ok(tv, 1.0);
        applyOk(ok, p);
        SetItemHeightCommand tooHigh(tv, 2.5);
        applyRefused(tooHigh, p, ErrorCode::InvalidValue);
    }
    SECTION("refusals") {
        SetItemHeightCommand below(sofa, -0.5);
        applyRefused(below, p, ErrorCode::InvalidValue);
        SetItemHeightCommand unknown("i99", 0.5);
        applyRefused(unknown, p, ErrorCode::NotFound);
    }
    SECTION("merging") {
        CommandStack stack;
        bool merged = false;
        requireOk(stack.push(std::make_unique<SetItemHeightCommand>(sofa, 0.1, 3), p, &merged));
        requireOk(stack.push(std::make_unique<SetItemHeightCommand>(sofa, 0.2, 3), p, &merged));
        REQUIRE(merged);
        stack.undo(p);
        REQUIRE(get(p, sofa).z == 0.0);
    }
}

TEST_CASE("RotateItemCommand turns floor items only", "[core][command][item]") {
    Project p = houseWithRooms();
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id tv = place(p, newTv("w1", 1.4, 1.0));

    SECTION("a floor item is turned and normalized") {
        RotateItemCommand c(sofa, 450.0);
        REQUIRE(c.changes() == Change::Items);
        roundTrip(c, p);
        REQUIRE(get(p, sofa).rotation == Approx(90.0));
        REQUIRE(get(p, sofa).x == 1.5);
    }
    SECTION("a wall item is refused") {
        RotateItemCommand c(tv, 90.0);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("unknown and not finite") {
        RotateItemCommand unknown("i99", 90.0);
        applyRefused(unknown, p, ErrorCode::NotFound);
        RotateItemCommand nan(sofa, std::numeric_limits<double>::quiet_NaN());
        applyRefused(nan, p, ErrorCode::InvalidValue);
    }
    SECTION("merging") {
        CommandStack stack;
        bool merged = false;
        requireOk(stack.push(std::make_unique<RotateItemCommand>(sofa, 15.0, 9), p, &merged));
        requireOk(stack.push(std::make_unique<RotateItemCommand>(sofa, 30.0, 9), p, &merged));
        REQUIRE(merged);
        REQUIRE(get(p, sofa).rotation == Approx(30.0));
        stack.undo(p);
        REQUIRE(get(p, sofa).rotation == 0.0);
    }
}

TEST_CASE("SwapItemAssetCommand changes asset and size", "[core][command][item]") {
    Project p = houseWithRooms();
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa, 90.0));
    const Id tv = place(p, newTv("w1", 1.4, 1.0, 0.8));

    SECTION("a floor item keeps position and rotation") {
        SwapItemAssetCommand c(sofa, "armchair", {0.9, 0.9, 0.9});
        REQUIRE(c.changes() == Change::Items);
        roundTrip(c, p);
        const Item& it = get(p, sofa);
        REQUIRE(it.asset == "armchair");
        REQUIRE(it.size == ItemSize{0.9, 0.9, 0.9});
        REQUIRE(it.x == 1.5);
        REQUIRE(it.rotation == Approx(90.0));
        REQUIRE(it.room == "r1");
    }
    SECTION("a wall item keeps its centre height and stays flush for a deeper TV") {
        const double centre = itemCentreHeight(get(p, tv));
        SwapItemAssetCommand c(tv, "big_tv", {1.5, 0.2, 0.9});
        roundTrip(c, p);
        const Item& it = get(p, tv);
        REQUIRE(it.asset == "big_tv");
        REQUIRE(itemCentreHeight(it) == Approx(centre));
        REQUIRE(it.y == Approx(0.15 + 0.1));
        REQUIRE(gapOf(p, tv) == Approx(0.0).margin(1e-9));
        REQUIRE(it.wall == "w1");
    }
    SECTION("a wall item gets its room worked out again") {
        p.plan.floors[0].items[1].room = "r2";
        SwapItemAssetCommand c(tv, "big_tv", {1.5, 0.2, 0.9});
        applyOk(c, p);
        REQUIRE(get(p, tv).room == "r1");
        c.undo(p);
        REQUIRE(get(p, tv).room == "r2");
    }
    SECTION("the same asset with a new size updates the size") {
        SwapItemAssetCommand c(sofa, "sofa", {2.2, 0.9, 0.85});
        applyOk(c, p);
        REQUIRE(get(p, sofa).size.width == 2.2);
    }
    SECTION("refusals") {
        SwapItemAssetCommand unknown("i99", "sofa", kSofa);
        applyRefused(unknown, p, ErrorCode::NotFound);
        SwapItemAssetCommand badName(sofa, "so fa", kSofa);
        applyRefused(badName, p, ErrorCode::InvalidValue);
        SwapItemAssetCommand badSize(sofa, "sofa", {1.0, 0.0, 1.0});
        applyRefused(badSize, p, ErrorCode::InvalidValue);
        SwapItemAssetCommand tooTall(tv, "mirror", {1.0, 0.1, 3.5});
        applyRefused(tooTall, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("SetItemWallCommand mounts and takes off", "[core][command][item][wall]") {
    Project p = houseWithRooms();
    const Id tv = place(p, newItem("tv", 1.4, 1.0, kTv));

    SECTION("mounting takes z from the defaults when z was 0") {
        SetItemWallCommand c(tv, Id("w1"));
        REQUIRE(c.changes() == Change::Items);
        roundTrip(c, p);
        const Item& it = get(p, tv);
        REQUIRE(it.wall == "w1");
        REQUIRE(it.z == Approx(1.2 - 0.355));
        REQUIRE(itemCentreHeight(it) == Approx(1.2));
        REQUIRE(it.y == Approx(0.18));
        REQUIRE(gapOf(p, tv) == Approx(0.0).margin(1e-9));
    }
    SECTION("mounting keeps a z above zero") {
        SetItemHeightCommand height(tv, 1.5);
        applyOk(height, p);
        SetItemWallCommand c(tv, Id("w1"));
        applyOk(c, p);
        REQUIRE(get(p, tv).z == 1.5);
    }
    SECTION("the project's wall item centre is used") {
        p.settings.defaults.wallItemCentre = 1.5;
        SetItemWallCommand c(tv, Id("w1"));
        applyOk(c, p);
        REQUIRE(itemCentreHeight(get(p, tv)) == Approx(1.5));
    }
    SECTION("taking it off keeps position, rotation and z") {
        SetItemWallCommand mount(tv, Id("w1"));
        applyOk(mount, p);
        const Item mounted = get(p, tv);
        SetItemWallCommand off(tv, std::nullopt);
        REQUIRE(off.name() == "Take item off wall");
        roundTrip(off, p);
        const Item& it = get(p, tv);
        REQUIRE_FALSE(it.wall);
        REQUIRE(it.x == mounted.x);
        REQUIRE(it.y == mounted.y);
        REQUIRE(it.rotation == mounted.rotation);
        REQUIRE(it.z == mounted.z);
    }
    SECTION("an unknown wall, an unknown item and a wall of another floor") {
        SetItemWallCommand noWall(tv, Id("w99"));
        applyRefused(noWall, p, ErrorCode::NotFound);
        SetItemWallCommand noItem("i99", Id("w1"));
        applyRefused(noItem, p, ErrorCode::NotFound);

        Floor upstairs;
        upstairs.id = "f2";
        upstairs.name = "Upstairs";
        addPoint(upstairs, "p50", 0, 0);
        addPoint(upstairs, "p51", 4, 0);
        addWall(upstairs, "w50", "p50", "p51", 0.2);
        p.plan.floors.push_back(upstairs);
        SetItemWallCommand other(tv, Id("w50"));
        applyRefused(other, p, ErrorCode::InvalidValue);
    }
}

TEST_CASE("RemoveItemCommand puts the item back at the same index", "[core][command][item]") {
    Project p = houseWithRooms();
    place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id middle = place(p, newItem("table", 1.0, 3.0, kTable));
    place(p, newItem("table", 5.0, 3.0, kTable));
    const Project before = p;

    RemoveItemCommand c(middle);
    REQUIRE(c.name() == "Remove item");
    REQUIRE(c.changes() == Change::Items);
    applyOk(c, p);
    REQUIRE(c.floorId() == "f1");
    REQUIRE(p.plan.floors[0].items.size() == 2);
    REQUIRE(findItem(p.plan.floors[0], middle) == nullptr);
    c.undo(p);
    REQUIRE(p == before);
    REQUIRE(p.plan.floors[0].items[1].id == middle);

    RemoveItemCommand unknown("i99");
    applyRefused(unknown, p, ErrorCode::NotFound);
}

TEST_CASE("AddRoomCommand and RemoveRoomCommand keep item rooms right", "[core][command][item][room]") {
    Project p = splitHouseProject();
    const Id inside = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id other = place(p, newItem("table", 4.5, 2.0, kTable));
    const Id outside = place(p, newItem("table", 9.0, 9.0, kTable));
    REQUIRE_FALSE(get(p, inside).room);

    SECTION("a new room gives itself to the items inside it with no room, and undo takes it back") {
        AddRoomCommand c("f1", {1.5, 2.0});
        REQUIRE((c.changes() & Change::Items) != 0);
        REQUIRE((c.changes() & Change::Rooms) != 0);
        roundTrip(c, p);
        REQUIRE(get(p, inside).room == "r1");
        REQUIRE_FALSE(get(p, other).room);
        REQUIRE_FALSE(get(p, outside).room);
    }
    SECTION("an item that already has a room keeps it") {
        MoveItemCommand keep(inside, {1.5, 2.0});
        applyOk(keep, p);
        Project q = p;
        q.plan.floors[0].items[0].room = std::nullopt;
        AddRoomCommand first("f1", {1.5, 2.0});
        applyOk(first, q);
        REQUIRE(get(q, inside).room == "r1");
    }
    SECTION("removing a room clears it from its items and undo restores it") {
        AddRoomCommand add("f1", {1.5, 2.0});
        applyOk(add, p);
        AddRoomCommand add2("f1", {4.5, 2.0});
        applyOk(add2, p);
        REQUIRE(get(p, inside).room == "r1");
        REQUIRE(get(p, other).room == "r2");
        RemoveRoomCommand c("r1");
        REQUIRE((c.changes() & Change::Items) != 0);
        roundTrip(c, p);
        REQUIRE_FALSE(get(p, inside).room);
        REQUIRE(get(p, other).room == "r2");
    }
}

TEST_CASE("RemoveWallCommand is in use while an item hangs on the wall", "[core][command][item][wall]") {
    Project p = splitHouseProject();
    const Id tv = place(p, newTv("w1", 1.4, 1.0));
    RemoveWallCommand c("w1");
    const Project before = p;
    const Status s = c.apply(p);
    REQUIRE(s.code == ErrorCode::InUse);
    REQUIRE(s.message.find(tv) != std::string::npos);
    REQUIRE(p == before);

    SetItemWallCommand off(tv, std::nullopt);
    applyOk(off, p);
    RemoveWallCommand again("w1");
    applyOk(again, p);
}

TEST_CASE("makeDeleteCommand handles items", "[core][command][item][delete]") {
    Project p = houseWithRooms();
    const Id tv = place(p, newTv("w1", 1.4, 1.0));
    const Id sofa = place(p, newItem("sofa", 1.5, 2.0, kSofa));
    const Id other = place(p, newItem("table", 4.5, 2.0, kTable));

    // Builds, runs, undoes and redoes a delete, returning what it said it removes.
    const auto run = [&](Project& project, const std::vector<Id>& ids) {
        const Project before = project;
        DeleteBuild build = makeDeleteCommand(project.plan, ids);
        requireOk(build.status);
        requireOk(build.command->apply(project));
        requirePlanValid(project.plan);
        const Project after = project;
        build.command->undo(project);
        REQUIRE(project == before);
        requireOk(build.command->apply(project));
        REQUIRE(project == after);
        return build.removes;
    };

    SECTION("an item id removes the item") {
        const DeleteSet gone = run(p, {sofa});
        REQUIRE(gone.items == std::vector<Id>{sofa});
        REQUIRE(gone.detached.empty());
        REQUIRE(findItem(p.plan.floors[0], sofa) == nullptr);
        REQUIRE(findItem(p.plan.floors[0], tv) != nullptr);
    }
    SECTION("a wall takes its wall items off it and lists them as detached") {
        const Item mounted = get(p, tv);
        const DeleteSet gone = run(p, {"w1"});
        REQUIRE(gone.items.empty());
        REQUIRE(gone.detached == std::vector<Id>{tv});
        REQUIRE(std::find(gone.walls.begin(), gone.walls.end(), "w1") != gone.walls.end());
        const Item& it = get(p, tv);
        REQUIRE_FALSE(it.wall);
        REQUIRE(it.x == mounted.x);
        REQUIRE(it.y == mounted.y);
        REQUIRE(it.z == mounted.z);
        REQUIRE_FALSE(it.room);
        REQUIRE(p.plan.floors[0].items.size() == 3);
    }
    SECTION("a named wall item on a removed wall is removed, not detached") {
        const DeleteSet gone = run(p, {"w1", tv});
        REQUIRE(gone.items == std::vector<Id>{tv});
        REQUIRE(gone.detached.empty());
        REQUIRE(findItem(p.plan.floors[0], tv) == nullptr);
    }
    SECTION("a removed room clears the room of its items") {
        const DeleteSet gone = run(p, {"r2"});
        REQUIRE(gone.rooms == std::vector<Id>{"r2"});
        REQUIRE_FALSE(get(p, other).room);
        REQUIRE(get(p, sofa).room == "r1");
    }
    SECTION("an unknown id is not found") {
        const DeleteBuild build = makeDeleteCommand(p.plan, std::vector<Id>{"i99"});
        REQUIRE(build.status.code == ErrorCode::NotFound);
    }
}

TEST_CASE("SplitWallCommand moves wall items past the split to the new wall", "[core][command][item][wall]") {
    Project p = splitHouseProject();
    const Id nearTv = place(p, newTv("w1", 0.9, 1.0));
    const Id farTv = place(p, newTv("w1", 2.3, 1.0));
    const Item farBefore = get(p, farTv);

    SplitWallCommand c("w1", 1.5);
    REQUIRE((c.changes() & Change::Items) != 0);
    const Project before = p;
    applyOk(c, p);
    REQUIRE(get(p, nearTv).wall == "w1");
    REQUIRE(get(p, farTv).wall == c.newWallId());
    REQUIRE(get(p, farTv).x == farBefore.x);
    REQUIRE(get(p, farTv).y == farBefore.y);
    REQUIRE(gapOf(p, farTv) == Approx(0.0).margin(1e-9));
    REQUIRE(gapOf(p, nearTv) == Approx(0.0).margin(1e-9));
    c.undo(p);
    REQUIRE(p == before);
}

TEST_CASE("pickAt finds items before walls", "[core][command][item][snap]") {
    Project p = houseWithRooms();
    const Id tv = place(p, newTv("w1", 1.4, 1.0));
    const Id sofa = place(p, newItem("sofa", 1.5, 2.5, kSofa));
    const Floor& f = p.plan.floors[0];

    SECTION("on the wall item gives the item") {
        const Pick hit = pickAt(f, {1.4, 0.18}, 0.02);
        REQUIRE(hit.kind == PickKind::Item);
        REQUIRE(hit.id == tv);
    }
    SECTION("beside it on the wall gives the wall") {
        const Pick hit = pickAt(f, {0.4, 0.0}, 0.02);
        REQUIRE(hit.kind == PickKind::Wall);
        REQUIRE(hit.id == "w1");
    }
    SECTION("on a floor item gives the item, beside it the room") {
        REQUIRE(pickAt(f, {1.5, 2.5}, 0.02) == Pick{PickKind::Item, sofa});
        REQUIRE(pickAt(f, {1.5, 1.2}, 0.02) == Pick{PickKind::Room, "r1"});
    }
    SECTION("within the radius of a footprint counts") {
        REQUIRE(pickAt(f, {1.5, 2.5 + 0.45 + 0.03}, 0.05) == Pick{PickKind::Item, sofa});
        REQUIRE(pickAt(f, {1.5, 2.5 + 0.45 + 0.08}, 0.05).kind != PickKind::Item);
    }
    SECTION("the last item in the list is on top") {
        const Id top = place(p, newItem("table", 1.5, 2.5, kTable));
        REQUIRE(pickAt(p.plan.floors[0], {1.5, 2.5}, 0.02) == Pick{PickKind::Item, top});
    }
}

TEST_CASE("wall items are kept on the wall or refused when too wide", "[core][command][item][wall]") {
    Project p = houseWithRooms();
    addPoint(p.plan.floors[0], "p20", 8, 0);
    addPoint(p.plan.floors[0], "p21", 9, 0);
    addWall(p.plan.floors[0], "w20", "p20", "p21", 0.2);

    SECTION("a sofa wider than the wall is refused on attach and on place") {
        const Id sofa = place(p, newItem("sofa", 8.5, 1.0, kSofa));
        SetItemWallCommand attach(sofa, Id("w20"));
        applyRefused(attach, p, ErrorCode::InvalidValue);
        Item onWall = newItem("sofa", 8.5, 1.0, kSofa);
        onWall.wall = "w20";
        PlaceItemCommand c("f1", onWall);
        applyRefused(c, p, ErrorCode::InvalidValue);
    }
    SECTION("an item that fits the wall is slid inward from an end on attach") {
        const Id sofa = place(p, newItem("sofa", 2.85, 1.0, kSofa));
        SetItemWallCommand attach(sofa, Id("w1"));
        applyOk(attach, p);
        REQUIRE(get(p, sofa).x == Approx(2.0));
        REQUIRE(wallItemSpot(p.plan.floors[0], get(p, sofa)).offset + 1.0 <= 3.0 + 1e-9);
        REQUIRE(gapOf(p, sofa) == Approx(0.0).margin(1e-9));
    }
    SECTION("place near an end slides the item onto the wall") {
        PlaceItemCommand c("f1", newTv("w1", 0.2, 1.0));
        applyOk(c, p);
        REQUIRE(get(p, c.itemId()).x == Approx(0.615));
    }
    SECTION("move on wall refuses an offset past either end") {
        const Id tv = place(p, newTv("w1", 1.4, 1.0));
        MoveItemOnWallCommand low(tv, 0.3, 1.0);
        applyRefused(low, p, ErrorCode::InvalidValue);
        MoveItemOnWallCommand high(tv, 2.6, 1.0);
        applyRefused(high, p, ErrorCode::InvalidValue);
        MoveItemOnWallCommand edge(tv, 0.7, 1.0);
        applyOk(edge, p);
        MoveItemOnWallCommand farEdge(tv, 3.0 - 0.615, 1.0);
        applyOk(farEdge, p);
    }
}
