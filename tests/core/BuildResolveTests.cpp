#include "SquareFixtures.h"

#include <plynth/core/BuildResolve.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/catalog/BuildChoices.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace plynth;

namespace {

// An asset choice.
AssetChoice choiceOf(const std::string& id, const std::string& category, ItemSize size, std::vector<std::string> tags = {},
                     std::vector<AssetSupport> supports = {})
{
    AssetChoice a;
    a.id = id;
    a.categories = {category};
    a.tags = std::move(tags);
    a.size = size;
    a.supports = std::move(supports);
    return a;
}

// Doors, two shelves of near sizes with boards, books and floor materials.
BuildChoices library()
{
    BuildChoices c;
    const std::vector<AssetSupport> boards = {{-0.36, -0.13, 0.36, 0.13, 0.02, 0.4}, {-0.36, -0.13, 0.36, 0.13, 0.47, 0.4}};
    c.assets = {choiceOf("books", "decor", {0.1, 0.2, 0.25}, {"book"}), choiceOf("door_a", "door", {0.9, 0.05, 2.1}),
                choiceOf("shelf_a", "shelf", {0.8, 0.3, 1.8}, {}, boards), choiceOf("shelf_b", "shelf", {0.76, 0.28, 1.8}, {}, boards)};
    c.materials = {{"oak_planks", {"floor"}}, {"tile", {"floor"}}};
    return c;
}

// The square with room r1, a door, a shelf and a generator item.
Project resolveProject()
{
    Project p = test::squareProjectWithWalls();
    Floor& f = p.plan.floors[0];
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms = {r};
    Opening door = test::doorOn("w1", 2.0);
    door.id = "o1";
    f.openings = {door};
    Item shelf;
    shelf.id = "i1";
    shelf.asset = "shelf_a";
    shelf.room = "r1";
    shelf.x = 2.0;
    shelf.y = 3.5;
    shelf.rotation = 180.0;
    shelf.size = {0.8, 0.3, 1.8};
    Item column;
    column.id = "i2";
    column.room = "r1";
    column.x = 1.0;
    column.y = 1.0;
    column.size = {0.3, 0.3, 3.0};
    column.generator = GeneratorUse{"column", {}};
    f.items = {shelf, column};
    return p;
}

}

TEST_CASE("seed 0 resolves only the picked frames and the props", "[resolve]")
{
    const Project p = resolveProject();
    const ResolvedBuild r = resolveBuild(p, library());
    CHECK(r.changes.empty());
    REQUIRE(r.frames.size() == 1);
    CHECK(r.frames[0].opening == "o1");
    CHECK(r.frames[0].asset == "door_a");
    const Opening& o = r.project.plan.floors[0].openings[0];
    CHECK(o.frame == std::optional<std::string>("door_a"));
    CHECK_FALSE(o.autoFrame);

    Project expected = p;
    expected.plan.floors[0].openings[0].frame = "door_a";
    expected.plan.floors[0].openings[0].autoFrame = false;
    CHECK(r.project == expected);

    REQUIRE(r.dressing.size() == 1);
    CHECK(r.dressing[0].floor == "f1");
    REQUIRE_FALSE(r.dressing[0].props.empty());
    for (const DressingPlacement& prop : r.dressing[0].props) CHECK(prop.host == "i1");
}

TEST_CASE("resolving a resolved project without choices changes nothing", "[resolve]")
{
    VariationSettings v;
    v.seed = 5;
    Project p = resolveProject();
    p.settings.variation = v;
    const ResolvedBuild once = resolveBuild(p, library());
    const ResolvedBuild again = resolveBuild(once.project, BuildChoices{});
    CHECK(again.project == once.project);
    CHECK(again.changes.empty());
    CHECK(again.frames.empty());
    CHECK(again.dressing[0].props.empty());
}

TEST_CASE("dressing off gives no props and a generator item keeps its place", "[resolve]")
{
    Project p = resolveProject();
    p.plan.floors[0].rooms[0].dressing = false;
    p.settings.variation.seed = 3;
    const ResolvedBuild r = resolveBuild(p, library());
    CHECK(r.dressing[0].props.empty());
    CHECK(*findItem(r.project.plan.floors[0], "i2") == *findItem(p.plan.floors[0], "i2"));
}

TEST_CASE("props sit on the varied host and a seed repeats byte for byte", "[resolve]")
{
    Project p = resolveProject();
    bool sawSwap = false;
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        p.settings.variation.seed = seed;
        const ResolvedBuild r = resolveBuild(p, library());
        const Item* shelf = findItem(r.project.plan.floors[0], "i1");
        REQUIRE(shelf != nullptr);
        if (shelf->asset == "shelf_b") sawSwap = true;
        for (const DressingPlacement& prop : r.dressing[0].props) {
            CHECK(prop.host == "i1");
            // The props stand on the shelf as it is in the resolved plan: inside its footprint.
            CHECK(std::fabs(prop.position.x - shelf->x) <= shelf->size.width / 2.0 + 1e-9);
            CHECK(std::fabs(prop.position.y - shelf->y) <= shelf->size.depth / 2.0 + 1e-9);
        }
        const ResolvedBuild twice = resolveBuild(p, library());
        CHECK(planToJson(twice.project.plan).dump() == planToJson(r.project.plan).dump());
        CHECK(twice == r);
    }
    CHECK(sawSwap);
}

TEST_CASE("no catalogs offer no choices", "[resolve]")
{
    CHECK(buildChoicesFrom(nullptr, nullptr) == BuildChoices{});
}
