#include "SquareFixtures.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/OpeningAssets.h>
#include <plynth/core/WallGeometry.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// An asset choice of one category and a size.
AssetChoice choice(const std::string& id, const std::string& category, double width, double height)
{
    AssetChoice a;
    a.id = id;
    a.categories = {category};
    a.size = {width, 0.1, height};
    return a;
}

// The door of the square fixture: 0.9 by 2.1 on w1 at offset 2.
Opening squareDoor()
{
    Opening o = test::doorOn("w1", 2.0);
    o.id = "o1";
    return o;
}

// Turns an asset's own x and y (front is -y) into world X and Y by a placement's turn.
Vec2 turned(const OpeningAssetBuild& b, double x, double y)
{
    const double c = std::cos(b.rotationZ);
    const double s = std::sin(b.rotationZ);
    return {x * c - y * s, x * s + y * c};
}

}

TEST_CASE("The asset with the least stretch is picked, up to 10 percent", "[openingassets]")
{
    const Opening door = squareDoor();
    CHECK_THAT(openingAssetStretch(door, {0.9, 0.1, 2.1}), WithinAbs(0.0, 1e-12));
    CHECK_THAT(openingAssetStretch(door, {0.99, 0.1, 2.1}), WithinAbs(0.1, 1e-12));
    CHECK_THAT(openingAssetStretch(door, {0.9, 0.1, 1.89}), WithinAbs(0.1, 1e-12));

    const std::vector<AssetChoice> assets = {choice("door_a", "door", 0.95, 2.1), choice("door_b", "door", 0.9, 2.1)};
    CHECK(chooseOpeningAsset(door, true, assets, 0) == std::optional<std::string>("door_b"));

    CHECK_FALSE(chooseOpeningAsset(door, true, {choice("wide", "door", 0.999, 2.1)}, 0).has_value());
    CHECK_FALSE(chooseOpeningAsset(door, true, {choice("tall", "door", 0.9, 2.331)}, 0).has_value());
    CHECK(chooseOpeningAsset(door, true, {choice("edge", "door", 0.99, 2.1)}, 0) == std::optional<std::string>("edge"));
    CHECK(chooseOpeningAsset(door, true, {choice("short", "door", 0.9, 1.89)}, 0) == std::optional<std::string>("short"));

    // Equal stretch goes to the smaller id.
    CHECK(chooseOpeningAsset(door, true, {choice("door_z", "door", 0.9, 2.1), choice("door_c", "door", 0.9, 2.1)}, 0)
          == std::optional<std::string>("door_c"));
}

TEST_CASE("A named frame always wins and other openings get none", "[openingassets]")
{
    Opening door = squareDoor();
    door.frame = "my_door";
    CHECK(chooseOpeningAsset(door, false, {}, 0) == std::optional<std::string>("my_door"));
    CHECK(chooseOpeningAsset(door, true, {choice("door_b", "door", 0.9, 2.1)}, 7) == std::optional<std::string>("my_door"));

    const std::vector<AssetChoice> assets = {choice("door_b", "door", 0.9, 2.1), choice("win", "window", 0.9, 2.1)};
    Opening plainDoor = squareDoor();
    CHECK_FALSE(chooseOpeningAsset(plainDoor, false, assets, 0).has_value());

    Opening arch = squareDoor();
    arch.shape = "round_arch";
    CHECK_FALSE(chooseOpeningAsset(arch, true, assets, 0).has_value());

    Opening plain = squareDoor();
    plain.type = OpeningType::Plain;
    CHECK_FALSE(chooseOpeningAsset(plain, true, assets, 0).has_value());

    CHECK_FALSE(chooseOpeningAsset(squareDoor(), true, {choice("win", "window", 0.9, 2.1)}, 0).has_value());
    Opening window = squareDoor();
    window.type = OpeningType::Window;
    CHECK(chooseOpeningAsset(window, true, assets, 0) == std::optional<std::string>("win"));
}

TEST_CASE("Seeded frame choices repeat whatever the order", "[openingassets]")
{
    std::vector<AssetChoice> assets = {choice("d1", "door", 0.9, 2.1), choice("d2", "door", 0.92, 2.1),
                                       choice("d3", "door", 0.88, 2.05), choice("d4", "door", 0.95, 2.2)};
    bool varied = false;
    for (std::uint32_t seed = 1; seed < 30; ++seed) {
        const auto first = chooseOpeningAsset(squareDoor(), true, assets, seed);
        REQUIRE(first.has_value());
        std::vector<AssetChoice> reversed(assets.rbegin(), assets.rend());
        CHECK(chooseOpeningAsset(squareDoor(), true, reversed, seed) == first);
        CHECK(chooseOpeningAsset(squareDoor(), true, assets, seed) == first);
        if (*first != "d1") varied = true;
    }
    CHECK(varied);
}

TEST_CASE("Doors face their swing side and hang on their hinge jamb", "[openingassets]")
{
    const Floor floor = test::squareFloorWithWalls();
    const ItemSize size{0.88, 0.05, 2.05};
    for (HingeEnd hinge : {HingeEnd::Start, HingeEnd::End}) {
        for (WallSide swing : {WallSide::Left, WallSide::Right}) {
            Opening door = squareDoor();
            door.hinge = hinge;
            door.swing = swing;
            const OpeningBuildResult made = openingBuild(floor, door, 0.0);
            REQUIRE(made.status.ok());
            const OpeningBuild& ob = made.opening;
            const OpeningAssetBuild b = openingAssetBuild(ob, 0.0, "door_b", size, true);
            INFO("hinge " << hingeEndName(hinge) << " swing " << wallSideName(swing));
            CHECK(b.asset == "door_b");

            const Vec2 front = turned(b, 0.0, -1.0);
            const double sign = swing == WallSide::Right ? 1.0 : -1.0;
            CHECK_THAT(front.x, WithinAbs(sign * ob.right.x, 1e-9));
            CHECK_THAT(front.y, WithinAbs(sign * ob.right.y, 1e-9));

            // The leaf hangs at the asset's own -x; scale carries it to the hinge jamb.
            const Vec2 hingeOffset = turned(b, -size.width / 2.0 * b.scale.x, 0.0);
            const double along = hinge == HingeEnd::Start ? -door.width / 2.0 : door.width / 2.0;
            CHECK_THAT(b.location.x + hingeOffset.x, WithinAbs(ob.centre.x + ob.tangent.x * along, 1e-9));
            CHECK_THAT(b.location.y + hingeOffset.y, WithinAbs(ob.centre.y + ob.tangent.y * along, 1e-9));
            CHECK_THAT(std::fabs(b.scale.x), WithinAbs(0.9 / 0.88, 1e-12));
            CHECK(b.scale.y == 1.0);
            CHECK_THAT(b.scale.z, WithinAbs(2.1 / 2.05, 1e-12));
        }
    }
    // One pair is the asset as drawn, its opposite is too, and the other two are mirrored.
    Opening door = squareDoor();
    door.hinge = HingeEnd::Start;
    door.swing = WallSide::Right;
    const OpeningAssetBuild a = openingAssetBuild(openingBuild(floor, door, 0.0).opening, 0.0, "d", size, true);
    door.hinge = HingeEnd::End;
    door.swing = WallSide::Left;
    const OpeningAssetBuild b = openingAssetBuild(openingBuild(floor, door, 0.0).opening, 0.0, "d", size, true);
    door.hinge = HingeEnd::End;
    door.swing = WallSide::Right;
    const OpeningAssetBuild c = openingAssetBuild(openingBuild(floor, door, 0.0).opening, 0.0, "d", size, true);
    CHECK((a.scale.x > 0.0) == (b.scale.x > 0.0));
    CHECK((a.scale.x > 0.0) != (c.scale.x > 0.0));
}

TEST_CASE("Windows face outside, or the right side between two rooms", "[openingassets]")
{
    const Floor floor = test::squareFloorWithWalls();
    Opening window = squareDoor();
    window.type = OpeningType::Window;
    window.height = 1.2;
    window.sill = 0.9;
    const OpeningBuild ob = openingBuild(floor, window, 0.0).opening;
    const ItemSize size{0.9, 0.08, 1.2};
    const OpeningAssetBuild right = openingAssetBuild(ob, 0.0, "win", size, true);
    const OpeningAssetBuild left = openingAssetBuild(ob, 0.0, "win", size, false);
    CHECK_THAT(turned(right, 0.0, -1.0).x, WithinAbs(ob.right.x, 1e-9));
    CHECK_THAT(turned(right, 0.0, -1.0).y, WithinAbs(ob.right.y, 1e-9));
    CHECK_THAT(turned(left, 0.0, -1.0).y, WithinAbs(-ob.right.y, 1e-9));
    CHECK(right.scale.x > 0.0);
    CHECK(left.scale.x > 0.0);
    CHECK_THAT(right.location.z, WithinAbs(0.9, 1e-12));
}

TEST_CASE("Opening assets sit on the wall's centre line at the opening's level", "[openingassets]")
{
    Floor floor = test::squareFloorWithWalls();
    // w5 runs diagonally from p1 to p3; w2 is bent into a curve.
    Wall diagonal;
    diagonal.id = "w5";
    diagonal.from = "p1";
    diagonal.to = "p3";
    floor.walls.push_back(diagonal);
    findWall(floor, "w2")->bulge = 0.4;

    for (const char* wallId : {"w1", "w5", "w2"}) {
        INFO(wallId);
        Opening o = squareDoor();
        o.wall = wallId;
        const OpeningBuildResult made = openingBuild(floor, o, 3.2);
        REQUIRE(made.status.ok());
        const OpeningAssetBuild b = openingAssetBuild(made.opening, 3.2, "door_b", {0.9, 0.05, 2.1}, true);
        CHECK(b.location.x == made.opening.centre.x);
        CHECK(b.location.y == made.opening.centre.y);
        CHECK_THAT(b.location.z, WithinAbs(3.2, 1e-12));
        const WallCurve curve = wallCurve(floor, *findWall(floor, wallId));
        const CurveProjection onWall = projectOntoCurve({b.location.x, -b.location.y}, curve);
        CHECK(onWall.distance < 1e-9);
        CHECK_THAT(onWall.offset, WithinAbs(2.0, 1e-6));
    }
}
