#include "SquareFixtures.h"

#include <plynth/core/Dressing.h>
#include <plynth/core/Items.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/WallGeometry.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A rule that only this test file adds, to show a rule is one file. It places nothing outside room r_test.
class TestMarkerRule final : public DressingRule {
public:
    std::string_view id() const override { return "test_marker"; }
    std::string_view name() const override { return "Test marker"; }
    int order() const override { return 5; }
    void place(const DressingInput& in, std::vector<DressingPlacement>& out) const override
    {
        if (in.room.id != "r_test") return;
        DressingPlacement p;
        p.host = "none";
        p.asset = "marker";
        p.position = {2.0, 2.0};
        p.size = {0.1, 0.1, 0.1};
        out.push_back(p);
    }
};

const DressingRuleRegistrar testRegistrar(std::make_unique<TestMarkerRule>());

// An asset choice.
AssetChoice asset(const std::string& id, std::vector<std::string> categories, std::vector<std::string> tags,
                  ItemSize size, std::vector<AssetSupport> supports = {})
{
    AssetChoice a;
    a.id = id;
    a.categories = std::move(categories);
    a.tags = std::move(tags);
    a.size = size;
    a.supports = std::move(supports);
    return a;
}

// A shelf with four boards, two kinds of book that fit, one too tall, a table with a top and a low shelf,
// two table props and a picture.
std::vector<AssetChoice> library()
{
    std::vector<AssetSupport> boards;
    for (double z : {0.02, 0.45, 0.9, 1.35}) boards.push_back({-0.38, -0.14, 0.38, 0.14, z, 0.4});
    return {asset("shelf", {"shelf"}, {}, {0.8, 0.3, 1.8}, boards),
            asset("book_a", {"decor"}, {"book"}, {0.03, 0.2, 0.25}),
            asset("book_b", {"decor"}, {"book"}, {0.05, 0.22, 0.3}),
            asset("book_tall", {"decor"}, {"book"}, {0.04, 0.2, 0.6}),
            asset("table", {"table"}, {}, {1.2, 0.8, 0.75},
                  {{-0.55, -0.35, 0.55, 0.35, 0.2, 0.3}, {-0.6, -0.4, 0.6, 0.4, 0.75, 1.0}}),
            asset("vase", {"decor"}, {"tabletop"}, {0.15, 0.15, 0.3}),
            asset("bowl", {"decor"}, {"tabletop"}, {0.25, 0.25, 0.1}),
            asset("picture", {"art"}, {"wall_art"}, {0.6, 0.03, 0.45})};
}

// An item standing in room r1.
Item itemOf(const std::string& id, const std::string& assetId, double x, double y, ItemSize size, double rotation = 0.0)
{
    Item i;
    i.id = id;
    i.asset = assetId;
    i.room = "r1";
    i.x = x;
    i.y = y;
    i.rotation = rotation;
    i.size = size;
    return i;
}

// The square floor with room r1.
Floor dressedFloor()
{
    Floor f = test::squareFloorWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    f.rooms = {r};
    return f;
}

// The props of a rule.
std::vector<DressingPlacement> byRule(const std::vector<DressingPlacement>& all, const std::string& rule)
{
    std::vector<DressingPlacement> out;
    for (const DressingPlacement& p : all) {
        if (p.rule == rule) out.push_back(p);
    }
    return out;
}

// A prop's position in its host item's own axes (as in asset.blend), undoing the plan mapping.
Vec2 toLocal(const Item& host, Vec2 position)
{
    const double wx = position.x - host.x;
    const double wy = -(position.y - host.y);
    const double t = host.rotation * kPi / 180.0;
    return {wx * std::cos(t) - wy * std::sin(t), wx * std::sin(t) + wy * std::cos(t)};
}

// Checks every book stands on one of its shelf's boards, inside it and under the board above.
void checkBooksOnShelf(const std::vector<DressingPlacement>& books, const Item& shelf, const AssetChoice& shelfAsset)
{
    REQUIRE_FALSE(books.empty());
    std::map<int, double> rowWidth;
    for (const DressingPlacement& b : books) {
        CHECK(b.host == shelf.id);
        CHECK(b.rotation == shelf.rotation);
        const Vec2 local = toLocal(shelf, b.position);
        const AssetSupport* board = nullptr;
        int index = 0;
        for (std::size_t i = 0; i < shelfAsset.supports.size(); ++i) {
            if (std::fabs(shelf.z + shelfAsset.supports[i].z - b.z) < 1e-9) {
                board = &shelfAsset.supports[i];
                index = static_cast<int>(i);
            }
        }
        REQUIRE(board != nullptr);
        CHECK(local.x - b.size.width / 2.0 >= board->x0 - 1e-9);
        CHECK(local.x + b.size.width / 2.0 <= board->x1 + 1e-9);
        CHECK(local.y - b.size.depth / 2.0 >= board->y0 - 1e-9);
        CHECK(local.y + b.size.depth / 2.0 <= board->y1 + 1e-9);
        CHECK(b.size.height <= board->clearance + 1e-9);
        CHECK(b.asset != "book_tall");
        rowWidth[index] += b.size.width;
    }
    for (const auto& [index, width] : rowWidth) {
        INFO("board " << index);
        CHECK(width <= 0.8 * 0.76 + 1e-9);
        CHECK(width >= 0.4 * 0.76 - 0.05);
    }
}

// Says whether no two props overlap.
bool noOverlaps(const std::vector<DressingPlacement>& props)
{
    for (std::size_t i = 0; i < props.size(); ++i) {
        for (std::size_t j = i + 1; j < props.size(); ++j) {
            if (dressingOverlaps(props[i], props[j])) return false;
        }
    }
    return true;
}

}

TEST_CASE("Without tagged assets a room gets no props", "[dressing]")
{
    Floor f = dressedFloor();
    f.items = {itemOf("i1", "shelf", 1.0, 0.5, {0.8, 0.3, 1.8})};
    std::vector<AssetChoice> untagged = library();
    for (AssetChoice& a : untagged) a.tags.clear();
    CHECK(dressRoom(f, f.rooms[0], untagged, 0).empty());
    CHECK(dressRoom(f, f.rooms[0], {}, 3).empty());
}

TEST_CASE("Books stand inside shelf boards, also on turned and wall shelves", "[dressing]")
{
    const std::vector<AssetChoice> lib = library();
    const AssetChoice& shelf = lib[0];
    Floor f = dressedFloor();
    Item standing = itemOf("i1", "shelf", 1.0, 0.5, shelf.size);
    Item turned = itemOf("i2", "shelf", 2.5, 2.0, shelf.size, 30.0);
    Item onWall = itemOf("i3", "shelf", 0.0, 0.0, shelf.size);
    const ItemPose pose = wallItemPose(f, *findWall(f, "w2"), WallSide::Right, 2.5, shelf.size);
    REQUIRE(pose.status.ok());
    onWall.x = pose.position.x;
    onWall.y = pose.position.y;
    onWall.rotation = pose.rotation;
    onWall.wall = "w2";
    onWall.z = 0.6;
    f.items = {standing, turned, onWall};

    for (std::uint32_t seed : {0u, 4u}) {
        const std::vector<DressingPlacement> props = dressRoom(f, f.rooms[0], lib, seed);
        const std::vector<DressingPlacement> books = byRule(props, "books");
        for (const Item& host : f.items) {
            INFO("seed " << seed << " shelf " << host.id);
            std::vector<DressingPlacement> mine;
            for (const DressingPlacement& b : books) {
                if (b.host == host.id) mine.push_back(b);
            }
            checkBooksOnShelf(mine, host, shelf);
        }
        for (const DressingPlacement& p : props) CHECK(p.room == "r1");
        CHECK(noOverlaps(props));
    }
}

TEST_CASE("Table props use the top support only", "[dressing]")
{
    Floor f = dressedFloor();
    const Item table = itemOf("i1", "table", 2.0, 2.0, {1.2, 0.8, 0.75}, 90.0);
    f.items = {table};
    for (std::uint32_t seed = 0; seed < 12; ++seed) {
        const std::vector<DressingPlacement> props = byRule(dressRoom(f, f.rooms[0], library(), seed), "tabletop");
        INFO("seed " << seed);
        CHECK(props.size() >= 1);
        CHECK(props.size() <= 3);
        for (const DressingPlacement& p : props) {
            CHECK_THAT(p.z, WithinAbs(0.75, 1e-12));
            CHECK(p.host == "i1");
            const Vec2 local = toLocal(table, p.position);
            CHECK(std::fabs(local.x) + p.size.width / 2.0 <= 0.6 + 1e-9);
            CHECK(std::fabs(local.y) + p.size.depth / 2.0 <= 0.4 + 1e-9);
        }
        CHECK(noOverlaps(props));
    }
}

TEST_CASE("Pictures keep clear of openings, wall items and tall furniture and face the room", "[dressing]")
{
    Floor f = dressedFloor();
    Opening door = test::doorOn("w1", 2.0);
    door.id = "o1";
    f.openings = {door};
    Item tv = itemOf("i1", "tv", 0.0, 0.0, {1.2, 0.08, 0.7});
    const ItemPose tvPose = wallItemPose(f, *findWall(f, "w2"), WallSide::Right, 2.0, tv.size);
    REQUIRE(tvPose.status.ok());
    tv.x = tvPose.position.x;
    tv.y = tvPose.position.y;
    tv.rotation = tvPose.rotation;
    tv.wall = "w2";
    tv.z = 1.0;
    const Item wardrobe = itemOf("i2", "wardrobe", 2.0, 3.6, {1.6, 0.5, 2.0}, 180.0);
    f.items = {tv, wardrobe};

    for (std::uint32_t seed : {0u, 1u, 9u}) {
        const std::vector<DressingPlacement> pictures = byRule(dressRoom(f, f.rooms[0], library(), seed), "pictures");
        INFO("seed " << seed);
        REQUIRE(pictures.size() == 1);
        const DressingPlacement& p = pictures[0];
        CHECK(p.host == "w4");
        CHECK(p.asset == "picture");
        CHECK_THAT(p.z + p.size.height / 2.0, WithinAbs(1.5, 1e-12));
        // Flush on the inside face of w4 (x = 0.1), facing into the room.
        CHECK_THAT(p.position.x, WithinAbs(0.1 + p.size.depth / 2.0, 1e-6));
        Item box;
        box.x = p.position.x;
        box.y = p.position.y;
        box.rotation = p.rotation;
        box.size = p.size;
        const Vec2 front = itemFront(box);
        CHECK(roomAt(f, {p.position.x + front.x * 0.5, p.position.y + front.y * 0.5}) == "r1");
    }

    // With the door, the television and the wardrobe gone, every wall can take one, but a room takes three.
    f.openings.clear();
    f.items.clear();
    const std::vector<DressingPlacement> all = byRule(dressRoom(f, f.rooms[0], library(), 2), "pictures");
    CHECK(all.size() == 3);
    CHECK(noOverlaps(all));
}

TEST_CASE("A room gets at most 40 props", "[dressing]")
{
    Floor f = dressedFloor();
    for (int i = 0; i < 6; ++i) f.items.push_back(itemOf("i" + std::to_string(i + 1), "shelf", 0.6 + 0.55 * i, 1.0 + (i % 2), {0.8, 0.3, 1.8}, 90.0));
    const std::vector<DressingPlacement> props = dressRoom(f, f.rooms[0], library(), 0);
    CHECK(props.size() == kMaxDressingPerRoom);
}

TEST_CASE("Dressing repeats for a seed whatever the asset order", "[dressing]")
{
    Floor f = dressedFloor();
    f.items = {itemOf("i1", "shelf", 1.0, 0.5, {0.8, 0.3, 1.8}), itemOf("i2", "table", 2.0, 2.0, {1.2, 0.8, 0.75})};
    std::vector<AssetChoice> lib = library();
    std::vector<AssetChoice> reversed(lib.rbegin(), lib.rend());
    const auto first = dressRoom(f, f.rooms[0], lib, 5);
    CHECK(dressRoom(f, f.rooms[0], reversed, 5) == first);
    CHECK(dressRoom(f, f.rooms[0], lib, 5) == first);
    bool differs = false;
    for (std::uint32_t seed = 6; seed < 12 && !differs; ++seed) differs = dressRoom(f, f.rooms[0], lib, seed) != first;
    CHECK(differs);
}

TEST_CASE("Generator items never carry props", "[dressing]")
{
    Floor f = dressedFloor();
    Item gen = itemOf("i1", "shelf", 1.0, 0.5, {0.8, 0.3, 1.8});
    gen.generator = GeneratorUse{"bookcase", {}};
    f.items = {gen};
    CHECK(byRule(dressRoom(f, f.rooms[0], library(), 0), "books").empty());
    gen.asset.clear();
    f.items = {gen};
    CHECK(byRule(dressRoom(f, f.rooms[0], library(), 0), "books").empty());
}

TEST_CASE("Dressing rules are registry entries and a test rule is one file", "[dressing]")
{
    std::vector<std::string> ids;
    for (const DressingRule* r : allDressingRules()) ids.emplace_back(r->id());
    CHECK(ids == std::vector<std::string>{"test_marker", "books", "tabletop", "pictures"});
    REQUIRE(findDressingRule("test_marker") != nullptr);
    CHECK_FALSE(registerDressingRule(std::make_unique<TestMarkerRule>()));

    Floor f = dressedFloor();
    f.rooms[0].id = "r_test";
    const std::vector<DressingPlacement> props = dressRoom(f, f.rooms[0], library(), 0);
    REQUIRE(props.size() == 1);
    CHECK(props[0].rule == "test_marker");
    CHECK(props[0].room == "r_test");

    // A room whose walls do not close gets nothing.
    Floor open = dressedFloor();
    open.rooms[0].walls = {"w1", "w2"};
    CHECK(dressRoom(open, open.rooms[0], library(), 0).empty());
}
