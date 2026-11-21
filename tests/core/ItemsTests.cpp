#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Items.h>
#include <plynth/core/WallGeometry.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Approx;

namespace {

// The 4 by 4 square with 0.2 m walls w1 to w4 and one room r1 inside it.
Floor roomFloor()
{
    Floor f = plynth::test::squareFloorWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms = {r};
    return f;
}

// An item with the given pieces. rotation in degrees.
Item makeItem(const std::string& id, const std::string& asset, double x, double y, ItemSize size, double rotation = 0.0, double z = 0.0)
{
    Item it;
    it.id = id;
    it.asset = asset;
    it.x = x;
    it.y = y;
    it.z = z;
    it.rotation = rotation;
    it.size = size;
    return it;
}

// The kinds of a list of problems, in order.
std::vector<ItemProblemKind> kindsOf(const std::vector<ItemProblem>& problems)
{
    std::vector<ItemProblemKind> kinds;
    for (const ItemProblem& p : problems) kinds.push_back(p.kind);
    return kinds;
}

// Says whether the list holds a problem of a kind, with the given other id when one is given.
bool has(const std::vector<ItemProblem>& problems, ItemProblemKind kind, const Id& other = {})
{
    return std::any_of(problems.begin(), problems.end(), [&](const ItemProblem& p) {
        return p.kind == kind && (other.empty() || p.other == other);
    });
}

const ItemSize kTv{1.23, 0.06, 0.71};
const ItemSize kSofa{2.0, 0.9, 0.85};
const ItemSize kTable{1.2, 0.8, 0.75};

// Hangs a TV on a wall of the floor with the pose function and adds it to the floor.
Item hangTv(Floor& f, const std::string& wall, WallSide side, double offset, double z)
{
    Item tv = makeItem("i1", "tv", 0, 0, kTv, 0.0, z);
    tv.wall = wall;
    const ItemPose pose = wallItemPose(f, *findWall(f, wall), side, offset, kTv);
    plynth::test::requireOk(pose.status);
    tv.x = pose.position.x;
    tv.y = pose.position.y;
    tv.rotation = pose.rotation;
    f.items.push_back(tv);
    return tv;
}

}

TEST_CASE("normalizeRotation brings degrees into 0 up to 360", "[core][items]") {
    REQUIRE(normalizeRotation(-90.0) == Approx(270.0));
    REQUIRE(normalizeRotation(360.0) == 0.0);
    REQUIRE(normalizeRotation(725.0) == Approx(5.0));
    REQUIRE(normalizeRotation(0.0) == 0.0);
    REQUIRE(normalizeRotation(-360.0) == 0.0);
    REQUIRE(normalizeRotation(359.5) == Approx(359.5));
    REQUIRE(std::isnan(normalizeRotation(std::numeric_limits<double>::quiet_NaN())));
    REQUIRE(std::isinf(normalizeRotation(std::numeric_limits<double>::infinity())));
}

TEST_CASE("itemFront turns clockwise from down the plan", "[core][items]") {
    Item it = makeItem("i1", "sofa", 0, 0, kSofa);
    REQUIRE(itemFront(it).x == Approx(0.0).margin(1e-12));
    REQUIRE(itemFront(it).y == Approx(1.0));
    it.rotation = 90.0;
    REQUIRE(itemFront(it).x == Approx(-1.0));
    REQUIRE(itemFront(it).y == Approx(0.0).margin(1e-12));
    it.rotation = 180.0;
    REQUIRE(itemFront(it).y == Approx(-1.0));
}

TEST_CASE("itemFootprint corners match the header order and are clockwise on screen", "[core][items]") {
    Item it = makeItem("i1", "sofa", 2.0, 2.0, {2.0, 1.0, 0.8});
    SECTION("rotation 0") {
        const ItemFootprint f = itemFootprint(it);
        const Vec2 expected[4] = {{1.0, 1.5}, {3.0, 1.5}, {3.0, 2.5}, {1.0, 2.5}};
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(f.corners[i].x == Approx(expected[i].x));
            REQUIRE(f.corners[i].y == Approx(expected[i].y));
        }
        REQUIRE(signedArea(f.corners) == Approx(2.0));
        REQUIRE(f.centre == Vec2{2.0, 2.0});
        REQUIRE(f.front.y == Approx(1.0));
    }
    SECTION("rotation 90") {
        it.rotation = 90.0;
        const ItemFootprint f = itemFootprint(it);
        const Vec2 expected[4] = {{2.5, 1.0}, {2.5, 3.0}, {1.5, 3.0}, {1.5, 1.0}};
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(f.corners[i].x == Approx(expected[i].x));
            REQUIRE(f.corners[i].y == Approx(expected[i].y));
        }
        REQUIRE(signedArea(f.corners) == Approx(2.0));
        REQUIRE(f.front.x == Approx(-1.0));
    }
}

TEST_CASE("centre height and the default wall item height", "[core][items]") {
    Item it = makeItem("i1", "tv", 0, 0, kTv, 0.0, 0.845);
    REQUIRE(itemCentreHeight(it) == Approx(1.2));
    DefaultSizes d;
    REQUIRE(defaultWallItemZ(d, 0.71) == Approx(1.2 - 0.355));
    REQUIRE(defaultWallItemZ(d, 3.0) == 0.0);
    d.wallItemCentre = 1.5;
    REQUIRE(defaultWallItemZ(d, 1.0) == Approx(1.0));
}

TEST_CASE("wallItemPose hangs an item flush on either face", "[core][items][wall]") {
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0, 0, 0}, {"p2", 4, 0, 0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.2;
    f.walls = {w};

    const ItemPose right = wallItemPose(f, f.walls[0], WallSide::Right, 2.0, kTv);
    plynth::test::requireOk(right.status);
    REQUIRE(right.position.x == Approx(2.0));
    REQUIRE(right.position.y == Approx(0.13));
    REQUIRE(right.rotation == Approx(0.0).margin(1e-9));
    REQUIRE(right.offset == Approx(2.0));

    const ItemPose left = wallItemPose(f, f.walls[0], WallSide::Left, 2.0, kTv);
    REQUIRE(left.position.x == Approx(2.0));
    REQUIRE(left.position.y == Approx(-0.13));
    REQUIRE(left.rotation == Approx(180.0));

    const ItemPose edge = wallItemPose(f, f.walls[0], WallSide::Right, 0.1, kTv);
    REQUIRE(edge.offset == Approx(0.615));
    REQUIRE(edge.position.x == Approx(0.615));
    const ItemPose farEdge = wallItemPose(f, f.walls[0], WallSide::Right, 9.0, kTv);
    REQUIRE(farEdge.offset == Approx(4.0 - 0.615));

    const ItemPose wide = wallItemPose(f, f.walls[0], WallSide::Right, 1.0, {5.0, 0.1, 1.0});
    REQUIRE(wide.offset == Approx(2.0));

    Wall other = w;
    other.id = "w9";
    REQUIRE(wallItemPose(f, other, WallSide::Right, 1.0, kTv).status.code == ErrorCode::NotFound);
    REQUIRE(wallItemPose(f, w, WallSide::Right, std::numeric_limits<double>::quiet_NaN(), kTv).status.code
            == ErrorCode::InvalidValue);
}

TEST_CASE("wallItemSpot reads back side, offset and a flush gap", "[core][items][wall]") {
    Floor f = roomFloor();
    const Item right = hangTv(f, "w1", WallSide::Right, 2.0, 0.8);
    WallItemSpot spot = wallItemSpot(f, right);
    REQUIRE(spot.valid);
    REQUIRE(spot.wall == "w1");
    REQUIRE(spot.side == WallSide::Right);
    REQUIRE(spot.offset == Approx(2.0));
    REQUIRE(spot.gap == Approx(0.0).margin(1e-9));

    f.items.clear();
    const Item left = hangTv(f, "w1", WallSide::Left, 1.5, 0.8);
    spot = wallItemSpot(f, left);
    REQUIRE(spot.valid);
    REQUIRE(spot.side == WallSide::Left);
    REQUIRE(spot.offset == Approx(1.5));
    REQUIRE(spot.gap == Approx(0.0).margin(1e-9));

    Item floorItem = makeItem("i2", "table", 2, 2, kTable);
    REQUIRE_FALSE(wallItemSpot(f, floorItem).valid);
    floorItem.wall = "w9";
    REQUIRE_FALSE(wallItemSpot(f, floorItem).valid);
}

TEST_CASE("wallItemPose on a curved wall puts the back on the arc face", "[core][items][wall]") {
    Floor f = roomFloor();
    f.walls[0].bulge = 0.5;
    const WallCurve curve = wallCurve(f, f.walls[0]);
    REQUIRE(curve.curved);
    for (WallSide side : {WallSide::Right, WallSide::Left}) {
        f.items.clear();
        const Item tv = hangTv(f, "w1", side, curve.length / 2.0, 0.8);
        const WallItemSpot spot = wallItemSpot(f, tv);
        REQUIRE(spot.valid);
        REQUIRE(spot.side == side);
        REQUIRE(spot.offset == Approx(curve.length / 2.0).margin(1e-6));
        REQUIRE(spot.gap == Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("snapItemPosition slides a footprint onto a wall face", "[core][items][snap]") {
    const Floor f = roomFloor();
    const Item sofa = makeItem("i1", "sofa", 0, 0, kSofa);

    SECTION("0.05 m from a face goes flush") {
        const ItemSnap s = snapItemPosition(f, sofa, {2.0, 0.6}, {0.0, 0.1});
        REQUIRE(s.position.x == Approx(2.0));
        REQUIRE(s.position.y == Approx(0.1 + 0.45));
        REQUIRE(s.walls == std::vector<Id>{"w1"});
        REQUIRE_FALSE(s.toGrid);
    }
    SECTION("further than the radius stays") {
        const ItemSnap s = snapItemPosition(f, sofa, {2.0, 0.9}, {0.0, 0.1});
        REQUIRE(s.position.y == Approx(0.9));
        REQUIRE(s.walls.empty());
    }
    SECTION("near a corner it touches both walls") {
        const ItemSnap s = snapItemPosition(f, sofa, {1.14, 0.59}, {0.0, 0.1});
        REQUIRE(s.position.x == Approx(0.1 + 1.0));
        REQUIRE(s.position.y == Approx(0.1 + 0.45));
        REQUIRE(s.walls.size() == 2);
        REQUIRE(std::find(s.walls.begin(), s.walls.end(), "w1") != s.walls.end());
        REQUIRE(std::find(s.walls.begin(), s.walls.end(), "w4") != s.walls.end());
    }
    SECTION("a turned item uses its turned sides") {
        Item turned = sofa;
        turned.rotation = 90.0;
        const ItemSnap s = snapItemPosition(f, turned, {0.58, 2.0}, {0.0, 0.1});
        REQUIRE(s.position.x == Approx(0.1 + 0.45));
        REQUIRE(s.position.y == Approx(2.0));
        REQUIRE(s.walls == std::vector<Id>{"w4"});
    }
    SECTION("the grid rounds the centre when no wall is near") {
        const ItemSnap s = snapItemPosition(f, sofa, {2.2, 2.3}, {0.5, 0.1});
        REQUIRE(s.position.x == Approx(2.0));
        REQUIRE(s.position.y == Approx(2.5));
        REQUIRE(s.toGrid);
        REQUIRE(s.walls.empty());
    }
    SECTION("a wall snap replaces the grid on the axis it moves") {
        const ItemSnap s = snapItemPosition(f, sofa, {2.1, 0.6}, {0.5, 0.1});
        REQUIRE(s.position.x == Approx(2.0));
        REQUIRE(s.position.y == Approx(0.1 + 0.45));
    }
    SECTION("no radius means no wall snapping") {
        const ItemSnap s = snapItemPosition(f, sofa, {2.0, 0.6}, {});
        REQUIRE(s.position == Vec2{2.0, 0.6});
        REQUIRE(s.walls.empty());
        REQUIRE_FALSE(s.toGrid);
    }
}

TEST_CASE("checkItem reports where an item stands", "[core][items][problems]") {
    Floor f = roomFloor();

    SECTION("an item well inside the room has no problems") {
        f.items = {makeItem("i1", "sofa", 2, 2, kSofa)};
        REQUIRE(checkItem(f, f.items[0]).empty());
        REQUIRE(itemProblems(f).empty());
    }
    SECTION("half outside the room") {
        f.items = {makeItem("i1", "sofa", 3.8, 2, kSofa)};
        const auto problems = checkItem(f, f.items[0]);
        REQUIRE(has(problems, ItemProblemKind::OutsideRoom, "r1"));
        REQUIRE_FALSE(has(problems, ItemProblemKind::NoRoom));
    }
    SECTION("centre in no room") {
        f.items = {makeItem("i1", "sofa", 10, 10, kSofa)};
        const auto problems = checkItem(f, f.items[0]);
        REQUIRE(has(problems, ItemProblemKind::NoRoom));
        REQUIRE_FALSE(has(problems, ItemProblemKind::OutsideRoom));
    }
    SECTION("flush against a wall is fine") {
        f.items = {makeItem("i1", "sofa", 2, 0.1 + 0.45, kSofa)};
        REQUIRE(checkItem(f, f.items[0]).empty());
    }
    SECTION("two tables that overlap report each other") {
        f.items = {makeItem("i1", "table", 2, 2, kTable), makeItem("i2", "table", 2.5, 2.2, kTable)};
        const auto all = itemProblems(f);
        REQUIRE(all.size() == 2);
        REQUIRE(all[0].item == "i1");
        REQUIRE(all[0].kind == ItemProblemKind::OverlapsItem);
        REQUIRE(all[0].other == "i2");
        REQUIRE(all[1].item == "i2");
        REQUIRE(all[1].other == "i1");
        REQUIRE(all[0].message == "Table i1 overlaps Table i2.");
    }
    SECTION("tables that only touch do not overlap") {
        f.items = {makeItem("i1", "table", 1.0, 2, kTable), makeItem("i2", "table", 2.2, 2, kTable)};
        REQUIRE(itemProblems(f).empty());
    }
    SECTION("a TV above a low cabinet does not overlap it") {
        f.items = {makeItem("i1", "cabinet", 2, 2, {1.0, 0.4, 0.5}), makeItem("i2", "tv", 2, 2, kTv, 0.0, 1.0)};
        REQUIRE_FALSE(has(itemProblems(f), ItemProblemKind::OverlapsItem));
        f.items[1].z = 0.4;
        REQUIRE(has(itemProblems(f), ItemProblemKind::OverlapsItem));
    }
    SECTION("an item standing in for one on the floor replaces it") {
        f.items = {makeItem("i1", "table", 1.5, 2, kTable), makeItem("i2", "table", 3.0, 2, kTable)};
        REQUIRE(itemProblems(f).empty());
        Item moved = f.items[1];
        moved.x = 2.2;
        REQUIRE(has(checkItem(f, moved), ItemProblemKind::OverlapsItem, "i1"));
        const Item same = f.items[0];
        REQUIRE_FALSE(has(checkItem(f, same), ItemProblemKind::OverlapsItem));
        const Item fresh = makeItem("", "table", 1.5, 2, kTable);
        REQUIRE(has(checkItem(f, fresh), ItemProblemKind::OverlapsItem, "i1"));
    }
    SECTION("a table pushed into a wall band") {
        f.items = {makeItem("i1", "table", 2, 0.3, kTable)};
        const auto problems = checkItem(f, f.items[0]);
        REQUIRE(has(problems, ItemProblemKind::OverlapsWall, "w1"));
        REQUIRE(has(problems, ItemProblemKind::OutsideRoom));
    }
    SECTION("a wall item's own wall is not reported") {
        hangTv(f, "w1", WallSide::Right, 2.0, 0.8);
        REQUIRE(checkItem(f, f.items[0]).empty());
    }
    SECTION("a TV over a window") {
        Opening o;
        o.id = "o1";
        o.wall = "w1";
        o.offset = 2.0;
        o.type = OpeningType::Window;
        o.width = 1.2;
        o.height = 1.2;
        o.sill = 0.9;
        f.openings = {o};
        hangTv(f, "w1", WallSide::Right, 2.0, 0.8);
        REQUIRE(has(checkItem(f, f.items[0]), ItemProblemKind::OverlapsOpening, "o1"));
        f.items[0].z = 2.2;
        REQUIRE_FALSE(has(checkItem(f, f.items[0]), ItemProblemKind::OverlapsOpening));
        f.items[0].z = 0.1;
        REQUIRE_FALSE(has(checkItem(f, f.items[0]), ItemProblemKind::OverlapsOpening));
    }
    SECTION("a wardrobe taller than the ceiling") {
        f.items = {makeItem("i1", "wardrobe", 2, 2, {1.0, 0.6, 3.2})};
        REQUIRE(has(checkItem(f, f.items[0]), ItemProblemKind::TooTall));
        f.items[0].size.height = 2.9;
        REQUIRE_FALSE(has(checkItem(f, f.items[0]), ItemProblemKind::TooTall));
    }
    SECTION("a wall item whose wall point moved is off its wall") {
        hangTv(f, "w1", WallSide::Right, 2.0, 0.8);
        REQUIRE_FALSE(has(checkItem(f, f.items[0]), ItemProblemKind::OffWall));
        findPoint(f, "p1")->y = 0.3;
        findPoint(f, "p2")->y = 0.3;
        const auto problems = checkItem(f, f.items[0]);
        REQUIRE(has(problems, ItemProblemKind::OffWall, "w1"));
    }
    SECTION("a wall item turned to face its wall is off it") {
        hangTv(f, "w1", WallSide::Right, 2.0, 0.8);
        f.items[0].rotation = 180.0;
        REQUIRE(has(checkItem(f, f.items[0]), ItemProblemKind::OffWall));
    }
}

TEST_CASE("checkItem compares the stored size with the library", "[core][items][problems]") {
    Floor f = roomFloor();
    f.items = {makeItem("i1", "sofa", 2, 2, kSofa)};

    SECTION("no lookup checks neither") {
        REQUIRE(checkItem(f, f.items[0]).empty());
    }
    SECTION("an empty lookup answer is a missing asset") {
        const AssetLookup lookup = [](std::string_view) { return std::optional<ItemSize>(); };
        const auto problems = checkItem(f, f.items[0], lookup);
        REQUIRE(kindsOf(problems) == std::vector<ItemProblemKind>{ItemProblemKind::MissingAsset});
        REQUIRE(problems[0].message.find("sofa") != std::string::npos);
    }
    SECTION("the same size is quiet") {
        const AssetLookup lookup = [](std::string_view) { return std::optional<ItemSize>(ItemSize{2.0, 0.9, 0.85}); };
        REQUIRE(checkItem(f, f.items[0], lookup).empty());
    }
    SECTION("another size is reported") {
        const AssetLookup lookup = [](std::string_view) { return std::optional<ItemSize>(ItemSize{2.2, 0.9, 0.85}); };
        REQUIRE(kindsOf(checkItem(f, f.items[0], lookup)) == std::vector<ItemProblemKind>{ItemProblemKind::SizeChanged});
        REQUIRE(kindsOf(itemProblems(f, lookup)) == std::vector<ItemProblemKind>{ItemProblemKind::SizeChanged});
    }
}

TEST_CASE("problems come in kind order and have names", "[core][items][problems]") {
    Floor f = roomFloor();
    f.items = {makeItem("i1", "wardrobe", 2, 0.3, {1.0, 0.6, 3.2}), makeItem("i2", "wardrobe", 2.2, 0.35, {1.0, 0.6, 3.2})};
    const AssetLookup lookup = [](std::string_view) { return std::optional<ItemSize>(); };
    const auto problems = checkItem(f, f.items[0], lookup);
    const std::vector<ItemProblemKind> kinds = kindsOf(problems);
    REQUIRE(std::is_sorted(kinds.begin(), kinds.end()));
    REQUIRE(has(problems, ItemProblemKind::OverlapsItem, "i2"));
    REQUIRE(has(problems, ItemProblemKind::TooTall));
    REQUIRE(has(problems, ItemProblemKind::MissingAsset));
    REQUIRE(itemProblemName(ItemProblemKind::NoRoom) == "no_room");
    REQUIRE(itemProblemName(ItemProblemKind::OutsideRoom) == "outside_room");
    REQUIRE(itemProblemName(ItemProblemKind::OverlapsItem) == "overlaps_item");
    REQUIRE(itemProblemName(ItemProblemKind::OverlapsWall) == "overlaps_wall");
    REQUIRE(itemProblemName(ItemProblemKind::OverlapsOpening) == "overlaps_opening");
    REQUIRE(itemProblemName(ItemProblemKind::OffWall) == "off_wall");
    REQUIRE(itemProblemName(ItemProblemKind::TooTall) == "too_tall");
    REQUIRE(itemProblemName(ItemProblemKind::MissingAsset) == "missing_asset");
    REQUIRE(itemProblemName(ItemProblemKind::SizeChanged) == "size_changed");
}

TEST_CASE("checkItemPlacement refuses in the order of its header", "[core][items][check]") {
    Floor f = roomFloor();
    const Item good = makeItem("i1", "sofa", 2, 2, kSofa);
    plynth::test::requireOk(checkItemPlacement(f, good));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto refused = [&](const Item& item, ErrorCode code) {
        const Status s = checkItemPlacement(f, item);
        INFO(s.message);
        REQUIRE(s.code == code);
        REQUIRE_FALSE(s.message.empty());
    };
    Item it = good;
    it.asset = "sofa 1";
    refused(it, ErrorCode::InvalidValue);
    it = good;
    it.x = nan;
    refused(it, ErrorCode::InvalidValue);
    it = good;
    it.z = nan;
    refused(it, ErrorCode::InvalidValue);
    it = good;
    it.size.depth = std::numeric_limits<double>::infinity();
    refused(it, ErrorCode::InvalidValue);
    it = good;
    it.size.width = 0.0;
    refused(it, ErrorCode::InvalidValue);
    it = good;
    it.z = -0.1;
    refused(it, ErrorCode::InvalidValue);
    it = good;
    it.room = "r9";
    refused(it, ErrorCode::NotFound);
    it = good;
    it.wall = "w9";
    refused(it, ErrorCode::NotFound);

    SECTION("a bad value wins over an unknown room") {
        it = good;
        it.room = "r9";
        it.z = -1.0;
        refused(it, ErrorCode::InvalidValue);
    }
    SECTION("an unknown room wins over a wall item that is too high") {
        it = makeItem("i1", "tv", 2, 0.13, kTv, 0.0, 2.9);
        it.wall = "w1";
        it.room = "r9";
        refused(it, ErrorCode::NotFound);
        it.room = "r1";
        refused(it, ErrorCode::InvalidValue);
        it.z = 1.0;
        plynth::test::requireOk(checkItemPlacement(f, it));
    }
    SECTION("a wall item wider than its wall") {
        it = makeItem("i1", "shelf", 2, 0.2, {4.5, 0.3, 0.3}, 0.0, 1.0);
        it.wall = "w1";
        const Status s = checkItemPlacement(f, it);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message.find("4.50 m") != std::string::npos);
    }
    SECTION("a half wall is lower") {
        f.walls[0].height = 1.0;
        it = makeItem("i1", "tv", 2, 0.13, kTv, 0.0, 0.5);
        it.wall = "w1";
        REQUIRE(checkItemPlacement(f, it).code == ErrorCode::InvalidValue);
        it.z = 0.2;
        plynth::test::requireOk(checkItemPlacement(f, it));
    }
    SECTION("a floor item high above the floor is not refused") {
        it = good;
        it.z = 5.0;
        plynth::test::requireOk(checkItemPlacement(f, it));
    }
}

TEST_CASE("a wall item above a lowered wall is too tall", "[core][items][problems]") {
    Floor f = roomFloor();
    hangTv(f, "w1", WallSide::Right, 2.0, 1.0);
    REQUIRE_FALSE(has(checkItem(f, f.items[0]), ItemProblemKind::TooTall));
    f.walls[0].height = 1.5;
    const auto problems = checkItem(f, f.items[0]);
    REQUIRE(has(problems, ItemProblemKind::TooTall, "w1"));
    REQUIRE(has(itemProblems(f), ItemProblemKind::TooTall, "w1"));
    f.walls[0].height = 1.71;
    REQUIRE_FALSE(has(checkItem(f, f.items[0]), ItemProblemKind::TooTall));
}

TEST_CASE("nearestWallForMount finds the wall within the radius", "[core][items][wall]") {
    const Floor f = roomFloor();
    REQUIRE(nearestWallForMount(f, {2.0, 0.5}, 0.5) == "w1");
    REQUIRE(nearestWallForMount(f, {2.0, 0.5}, 0.2).empty());
    REQUIRE(nearestWallForMount(f, {0.5, 2.0}, 0.5) == "w4");
    REQUIRE(nearestWallForMount(f, {3.7, 2.0}, 0.5) == "w2");
    REQUIRE(nearestWallForMount(f, {2.0, 2.0}, 0.5).empty());
    REQUIRE(nearestWallForMount(f, {2.0, 0.05}, 0.0) == "w1");
}
