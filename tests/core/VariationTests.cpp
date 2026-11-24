#include "SquareFixtures.h"

#include <plynth/core/Items.h>
#include <plynth/core/Variation.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// An asset choice of a category and size.
AssetChoice chair(const std::string& id, const std::string& category, double width, double depth, double height = 0.9)
{
    AssetChoice a;
    a.id = id;
    a.categories = {category};
    a.size = {width, depth, height};
    return a;
}

// A material choice of one group.
MaterialChoice material(const std::string& id, const std::string& group)
{
    return {id, {group}};
}

// Choices with sofas and chairs of several sizes, shelves for the wall, and materials of each group.
BuildChoices choices()
{
    BuildChoices c;
    c.assets = {chair("sofa_a", "sofa", 1.8, 0.9), chair("sofa_b", "sofa", 1.6, 0.85), chair("sofa_c", "sofa", 1.4, 0.9),
                chair("sofa_wide", "sofa", 2.0, 0.9), chair("sofa_small", "sofa", 1.2, 0.9),
                chair("chair_a", "chair", 0.5, 0.5), chair("chair_b", "chair", 0.45, 0.5), chair("chair_c", "chair", 0.52, 0.48),
                chair("shelf_a", "shelf", 0.8, 0.3, 1.0), chair("shelf_b", "shelf", 0.7, 0.28, 1.0)};
    c.materials = {material("oak_planks", "floor"), material("tile", "floor"), material("stone_floor", "floor"),
                   material("white_paint", "wall"), material("green_paint", "wall"), material("red_brick", "exterior"),
                   material("render", "exterior"), material("cedar", "exterior")};
    return c;
}

// The square with room r1, a sofa, two chairs, a shelf on w2 and a generator item.
Plan variedFixture()
{
    Plan plan = makeEmptyPlan();
    Floor f = test::squareFloorWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms = {r};
    auto item = [](const std::string& id, const std::string& asset, double x, double y, ItemSize size, double rotation) {
        Item i;
        i.id = id;
        i.asset = asset;
        i.room = "r1";
        i.x = x;
        i.y = y;
        i.rotation = rotation;
        i.size = size;
        return i;
    };
    f.items = {item("i1", "sofa_a", 2.0, 3.2, {1.8, 0.9, 0.9}, 180.0), item("i2", "chair_a", 1.0, 1.0, {0.5, 0.5, 0.9}, 30.0),
               item("i3", "chair_b", 3.0, 1.0, {0.45, 0.5, 0.9}, 0.0)};
    Item shelf = item("i4", "shelf_a", 0.0, 0.0, {0.8, 0.3, 1.0}, 0.0);
    const ItemPose pose = wallItemPose(f, *findWall(f, "w2"), WallSide::Right, 2.0, shelf.size);
    shelf.x = pose.position.x;
    shelf.y = pose.position.y;
    shelf.rotation = pose.rotation;
    shelf.wall = "w2";
    shelf.z = 1.2;
    Item generator = item("i5", "", 1.0, 2.0, {0.6, 0.6, 1.0}, 0.0);
    generator.generator = GeneratorUse{"plant", {}};
    f.items.push_back(shelf);
    f.items.push_back(generator);
    plan.floors[0] = f;
    return plan;
}

// The sizes of the marked assets, by item id.
const Item* itemIn(const Plan& plan, const std::string& id)
{
    return findItem(plan.floors[0], id);
}

// The problems of an item as kinds and other ids.
std::set<std::pair<int, std::string>> problems(const Floor& f, const Item& i)
{
    std::set<std::pair<int, std::string>> out;
    for (const ItemProblem& p : checkItem(f, i)) out.insert({static_cast<int>(p.kind), p.other});
    return out;
}

}

TEST_CASE("Seed 0 builds the plan as marked", "[variation]")
{
    const Plan plan = variedFixture();
    const VariationResult r = variedPlan(plan, MaterialDefaults{}, VariationSettings{}, choices());
    CHECK(r.plan == plan);
    CHECK(r.changes.empty());
}

TEST_CASE("A seed repeats whatever the order of the choices", "[variation]")
{
    const Plan plan = variedFixture();
    BuildChoices c = choices();
    BuildChoices shuffled = c;
    std::reverse(shuffled.assets.begin(), shuffled.assets.end());
    std::rotate(shuffled.materials.begin(), shuffled.materials.begin() + 3, shuffled.materials.end());
    for (std::uint32_t seed : {1u, 7u, 1234u}) {
        VariationSettings s;
        s.seed = seed;
        const VariationResult a = variedPlan(plan, MaterialDefaults{}, s, c);
        CHECK(variedPlan(plan, MaterialDefaults{}, s, c) == a);
        CHECK(variedPlan(plan, MaterialDefaults{}, s, shuffled) == a);
    }
}

TEST_CASE("Furniture swaps keep the marker and fit the rules", "[variation]")
{
    const Plan plan = variedFixture();
    const BuildChoices c = choices();
    int swaps = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        VariationSettings s;
        s.seed = seed;
        s.materials = false;
        const VariationResult r = variedPlan(plan, MaterialDefaults{}, s, c);
        for (const VariationChange& change : r.changes) {
            INFO("seed " << seed << " item " << change.object);
            REQUIRE(change.target == VariationTarget::Item);
            ++swaps;
            const Item* before = itemIn(plan, change.object);
            const Item* after = itemIn(r.plan, change.object);
            REQUIRE(before != nullptr);
            REQUIRE(after != nullptr);
            CHECK(change.from == before->asset);
            CHECK(change.to == after->asset);
            CHECK(after->id == before->id);
            CHECK(after->rotation == before->rotation);
            CHECK(after->z == before->z);
            CHECK(after->room == before->room);
            const AssetChoice* from = nullptr;
            const AssetChoice* to = nullptr;
            for (const AssetChoice& a : c.assets) {
                if (a.id == change.from) from = &a;
                if (a.id == change.to) to = &a;
            }
            REQUIRE(from != nullptr);
            REQUIRE(to != nullptr);
            CHECK(from->categories == to->categories);
            CHECK(after->size == to->size);
            CHECK(to->size.width / before->size.width >= kVariationMinWidth - 1e-9);
            CHECK(to->size.width / before->size.width <= kVariationMaxWidth + 1e-9);
            CHECK(to->size.depth / before->size.depth >= kVariationMinDepth - 1e-9);
            CHECK(to->size.depth / before->size.depth <= kVariationMaxDepth + 1e-9);
            if (before->wall) {
                const WallItemSpot spot = wallItemSpot(r.plan.floors[0], *after);
                CHECK(spot.valid);
                CHECK(std::fabs(spot.gap) < 1e-6);
                CHECK_THAT(spot.offset, WithinAbs(2.0, 1e-6));
            } else {
                CHECK(after->x == before->x);
                CHECK(after->y == before->y);
            }
            const auto was = problems(plan.floors[0], *before);
            for (const auto& p : problems(r.plan.floors[0], *after)) CHECK(was.count(p) == 1);
            CHECK(change.to != "sofa_wide");
            CHECK(change.to != "sofa_small");
        }
        CHECK(*itemIn(r.plan, "i5") == *itemIn(plan, "i5"));
    }
    CHECK(swaps > 0);
}

TEST_CASE("Material variation follows the groups and leaves the defaults alone", "[variation]")
{
    Plan plan = variedFixture();
    findWall(plan.floors[0], "w3")->materialOutside = "glass_curtain";
    findWall(plan.floors[0], "w1")->materialInside = "green_paint";
    findWall(plan.floors[0], "w4")->materialInside = "wallpaper";
    const MaterialDefaults defaults;
    const BuildChoices c = choices();
    bool floorChanged = false, outsideChanged = false;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        VariationSettings s;
        s.seed = seed;
        s.furniture = false;
        const VariationResult r = variedPlan(plan, defaults, s, c);
        const Floor& f = r.plan.floors[0];
        const Room& room = f.rooms[0];
        INFO("seed " << seed);
        const std::string floorUsed = room.floorMaterial.value_or(*defaults.floor);
        CHECK((floorUsed == "oak_planks" || floorUsed == "tile" || floorUsed == "stone_floor"));
        if (floorUsed != "oak_planks") floorChanged = true;
        const std::string wallUsed = room.wallMaterial.value_or(*defaults.wallInside);
        CHECK((wallUsed == "white_paint" || wallUsed == "green_paint"));
        CHECK(findWall(f, "w3")->materialOutside == std::optional<std::string>("glass_curtain"));
        CHECK(findWall(f, "w4")->materialInside == std::optional<std::string>("wallpaper"));
        const auto out1 = findWall(f, "w1")->materialOutside.value_or(*defaults.wallOutside);
        for (const char* id : {"w2", "w4"}) CHECK(findWall(f, id)->materialOutside.value_or(*defaults.wallOutside) == out1);
        if (out1 != "red_brick") outsideChanged = true;
        CHECK(!room.ceilingMaterial.has_value());
        for (const VariationChange& change : r.changes) {
            CHECK(change.target != VariationTarget::Item);
            CHECK(change.from != change.to);
        }
        CHECK(r.plan.floors[0].items == plan.floors[0].items);
    }
    CHECK(floorChanged);
    CHECK(outsideChanged);
    CHECK(defaults == MaterialDefaults{});

    // A marked floor material outside the floor group is left alone.
    plan.floors[0].rooms[0].floorMaterial = "carpet";
    VariationSettings s;
    s.seed = 3;
    CHECK(variedPlan(plan, defaults, s, c).plan.floors[0].rooms[0].floorMaterial == std::optional<std::string>("carpet"));
}

TEST_CASE("Both switches off or no candidates change nothing", "[variation]")
{
    const Plan plan = variedFixture();
    VariationSettings off;
    off.seed = 9;
    off.furniture = false;
    off.materials = false;
    CHECK(variedPlan(plan, MaterialDefaults{}, off, choices()).plan == plan);
    VariationSettings on;
    on.seed = 9;
    const VariationResult none = variedPlan(plan, MaterialDefaults{}, on, BuildChoices{});
    CHECK(none.plan == plan);
    CHECK(none.changes.empty());
    CHECK(variationTargetName(VariationTarget::RoomFloor) == "room_floor");
    CHECK(variationTargetName(VariationTarget::Outside) == "outside");
}
