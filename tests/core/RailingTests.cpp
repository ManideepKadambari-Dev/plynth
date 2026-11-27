#include "BuildFixtures.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/Collision.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/Materials.h>
#include <plynth/core/Railings.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A glass style with nothing to hold its glass, for the glass check.
class LooseGlassStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "test_loose_glass"; }
    std::string_view name() const override { return "Loose glass"; }
    int sortKey() const override { return 900; }
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        RailingPiece glass;
        glass.role = RailingPiece::Role::Glass;
        PartPrism pane;
        pane.outline = {{0.0, -0.006}, {input.length, -0.006}, {input.length, 0.006}, {0.0, 0.006}};
        pane.top = input.height;
        glass.prisms.push_back(pane);
        return {glass};
    }
};

// A glass style that stands its glass between posts.
class GlassOnPostsStyle final : public RailingStyle {
public:
    std::string_view id() const override { return "test_glass_on_posts"; }
    std::string_view name() const override { return "Glass on posts"; }
    int sortKey() const override { return 901; }
    std::vector<RailingPiece> run(const RailingRunInput& input) const override
    {
        std::vector<RailingPiece> out = LooseGlassStyle().run(input);
        RailingPiece post;
        post.role = RailingPiece::Role::Post;
        PartPrism box;
        box.outline = {{-0.03, -0.03}, {0.03, -0.03}, {0.03, 0.03}, {-0.03, 0.03}};
        box.top = input.height;
        post.prisms.push_back(box);
        out.push_back(post);
        return out;
    }
};

// Registers the test styles once.
void registerTestStyles()
{
    registerRailingStyle(std::make_unique<LooseGlassStyle>());
    registerRailingStyle(std::make_unique<GlassOnPostsStyle>());
}

// A railing along these plan points with this style and height.
Railing railingOf(std::vector<PlanXY> points, std::string style = "posts_rails", double height = 1.0, bool closed = false)
{
    Railing r;
    r.id = "rl1";
    r.points = std::move(points);
    r.style = std::move(style);
    r.height = height;
    r.closed = closed;
    return r;
}

// The part maker with this id.
const PartMaker* maker(std::string_view id)
{
    for (const PartMaker* m : allPartMakers()) {
        if (m->id() == id) return m;
    }
    return nullptr;
}

// What the railing maker gives for one floor of a project.
PartOutput railingParts(const Project& project, std::size_t floorIndex = 0)
{
    const PartMaker* railings = maker("railings");
    REQUIRE(railings != nullptr);
    const std::vector<double> levels = floorElevations(project.plan);
    const std::vector<FloorBuild> floors;
    PartInput input;
    input.project = &project;
    input.floor = &project.plan.floors[floorIndex];
    input.floorIndex = floorIndex;
    input.levels = &levels;
    input.floors = &floors;
    return railings->floorParts(input);
}

// The square project with these railings (and decks) on its floor.
Project railingProject(std::vector<Railing> railings, std::vector<Deck> decks = {})
{
    Project p = squareProjectWithWalls();
    p.plan.floors[0].railings = std::move(railings);
    p.plan.floors[0].decks = std::move(decks);
    return p;
}

// Says whether the segments from a to b and from c to d cross or touch.
bool segmentsMeet(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const auto side = [](Vec2 p, Vec2 q, Vec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
    const double s1 = side(a, b, c);
    const double s2 = side(a, b, d);
    const double s3 = side(c, d, a);
    const double s4 = side(c, d, b);
    return ((s1 > 1e-12 && s2 < -1e-12) || (s1 < -1e-12 && s2 > 1e-12)) &&
           ((s3 > 1e-12 && s4 < -1e-12) || (s3 < -1e-12 && s4 > 1e-12));
}

// Checks that a prism is a closed solid with flat ends: a simple ring with an area, finite, its top above its
// bottom. Its sides are flat by being upright and its ends by being level.
void checkSolidPrism(const PartPrism& prism)
{
    REQUIRE(prism.outline.size() >= 3);
    for (const Vec2& p : prism.outline) {
        CHECK(std::isfinite(p.x));
        CHECK(std::isfinite(p.y));
    }
    CHECK(std::fabs(signedArea(prism.outline)) > 1e-9);
    CHECK(prism.top > prism.bottom);
    const std::size_t n = prism.outline.size();
    for (std::size_t i = 0; i < n; ++i) {
        CHECK(distance(prism.outline[i], prism.outline[(i + 1) % n]) > 1e-9);
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            CHECK_FALSE(segmentsMeet(prism.outline[i], prism.outline[(i + 1) % n], prism.outline[j], prism.outline[(j + 1) % n]));
        }
    }
}

// The middle of a ring.
Vec2 middleOf(const std::vector<Vec2>& ring)
{
    Vec2 c;
    for (const Vec2& p : ring) {
        c.x += p.x;
        c.y += p.y;
    }
    c.x /= static_cast<double>(ring.size());
    c.y /= static_cast<double>(ring.size());
    return c;
}

// The posts of a posts_rails part: the only prisms the whole railing height tall.
std::vector<Vec2> postsOf(const BuildPart& part, double foot, double height)
{
    std::vector<Vec2> out;
    for (const PartPrism& prism : part.prisms) {
        if (prism.bottom == Approx(foot).margin(1e-9) && prism.top == Approx(foot + height).margin(1e-9)) {
            out.push_back(middleOf(prism.outline));
        }
    }
    return out;
}

// How many places in a list are at this world point.
int countAt(const std::vector<Vec2>& places, Vec2 at)
{
    return static_cast<int>(std::count_if(places.begin(), places.end(), [&](Vec2 p) { return distance(p, at) < 1e-6; }));
}

// Says whether a prism has a corner at this world point.
bool hasCorner(const PartPrism& prism, Vec2 at)
{
    return std::any_of(prism.outline.begin(), prism.outline.end(), [&](Vec2 p) { return distance(p, at) < 1e-6; });
}

// The problems with this code.
std::vector<BuildProblem> withCode(const std::vector<BuildProblem>& problems, const std::string& code)
{
    std::vector<BuildProblem> out;
    for (const BuildProblem& p : problems) {
        if (p.code == code) out.push_back(p);
    }
    return out;
}

}

TEST_CASE("the built-in railing styles are registered in order", "[railings]")
{
    const std::vector<const RailingStyle*> styles = allRailingStyles();
    REQUIRE(styles.size() >= 6);
    const std::vector<std::string> ids{"posts_rails", "spindles", "flat_bars", "glass", "cables", "half_wall"};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(styles[i]->id() == ids[i]);
        CHECK_FALSE(styles[i]->name().empty());
        CHECK(findRailingStyle(ids[i]) == styles[i]);
    }
    CHECK(findRailingStyle("glass")->name() == "Glass panels");
    CHECK(findRailingStyle("spindles")->name() == "Wooden spindles");
    CHECK(findRailingStyle("flat_bars")->name() == "Metal bars");
    CHECK(findRailingStyle("glass")->maxHeight() == Approx(1.5));
    CHECK(findRailingStyle("posts_rails")->maxHeight() == Approx(kMaxDrawnRailingHeight));
    CHECK(findRailingStyle("no_such_style") == nullptr);
}

TEST_CASE("registerRailingStyle refuses a taken or badly formed id", "[railings]")
{
    CHECK_FALSE(registerRailingStyle(nullptr));

    // Same id as a built-in style.
    class Copy final : public RailingStyle {
    public:
        explicit Copy(std::string_view id) : id_(id) {}
        std::string_view id() const override { return id_; }
        std::string_view name() const override { return "Copy"; }
        int sortKey() const override { return 999; }
        std::vector<RailingPiece> run(const RailingRunInput&) const override { return {}; }

    private:
        std::string_view id_;
    };
    CHECK_FALSE(registerRailingStyle(std::make_unique<Copy>("glass")));
    CHECK_FALSE(registerRailingStyle(std::make_unique<Copy>("Glass")));
    CHECK_FALSE(registerRailingStyle(std::make_unique<Copy>("")));
    CHECK_FALSE(registerRailingStyle(std::make_unique<Copy>("two words")));
    CHECK(findRailingStyle("glass")->name() == "Glass panels");
}

TEST_CASE("every style gives closed solids inside its run", "[railings][geometry]")
{
    for (const RailingStyle* style : allRailingStyles()) {
        if (style->id().rfind("test_", 0) == 0) continue;
        for (const double length : {0.1, 0.5, 1.5, 3.0, 4.7}) {
            for (const double height : {style->minHeight(), 1.0, style->maxHeight()}) {
                for (const bool end : {false, true}) {
                    INFO(std::string(style->id()) << " length " << length << " height " << height);
                    RailingRunInput input;
                    input.length = length;
                    input.height = height;
                    input.postAtEnd = end;
                    const std::vector<RailingPiece> pieces = style->run(input);
                    std::size_t prisms = 0;
                    for (const RailingPiece& piece : pieces) {
                        CHECK(piece.sweeps.empty());
                        for (const PartPrism& prism : piece.prisms) {
                            ++prisms;
                            checkSolidPrism(prism);
                            CHECK(prism.slot.empty());
                            CHECK(prism.bottom >= -1e-9);
                            CHECK(prism.top <= height + 1e-9);
                            for (const Vec2& p : prism.outline) {
                                CHECK(p.x >= -0.1);
                                CHECK(p.x <= length + 0.1);
                                CHECK(std::fabs(p.y) <= 0.15);
                            }
                        }
                    }
                    CHECK(prisms > 0);
                }
            }
        }
    }
}

TEST_CASE("styles put posts at the start, at most the spacing apart, and at the end when asked", "[railings][geometry]")
{
    for (const std::string id : {"posts_rails", "spindles", "flat_bars", "cables"}) {
        INFO(id);
        const RailingStyle* style = findRailingStyle(id);
        REQUIRE(style);
        RailingRunInput input;
        input.length = 4.0;
        input.height = 1.0;
        input.postAtEnd = false;
        std::vector<double> posts;
        for (const RailingPiece& piece : style->run(input)) {
            if (piece.role != RailingPiece::Role::Post) continue;
            for (const PartPrism& prism : piece.prisms) posts.push_back(middleOf(prism.outline).x);
        }
        std::sort(posts.begin(), posts.end());
        // 4 m in bays of at most 1.5 m: three bays, the far end left to the next run.
        REQUIRE(posts.size() == 3);
        CHECK(posts[0] == Approx(0.0).margin(1e-9));
        CHECK(posts[1] == Approx(4.0 / 3.0));
        CHECK(posts[2] == Approx(8.0 / 3.0));

        input.postAtEnd = true;
        input.postAtStart = false;
        posts.clear();
        for (const RailingPiece& piece : style->run(input)) {
            if (piece.role != RailingPiece::Role::Post) continue;
            for (const PartPrism& prism : piece.prisms) posts.push_back(middleOf(prism.outline).x);
        }
        std::sort(posts.begin(), posts.end());
        REQUIRE(posts.size() == 3);
        CHECK(posts.back() == Approx(4.0));
    }
}

TEST_CASE("spindles and bars stay at most 0.1 m apart and cables at most 0.08 m", "[railings][geometry]")
{
    RailingRunInput input;
    input.length = 2.9;
    input.height = 1.0;
    for (const std::string id : {"spindles", "flat_bars"}) {
        INFO(id);
        // Each upright by its middle along the run, and how far a post's side is from its middle.
        std::vector<std::pair<double, double>> places;
        int infill = 0;
        for (const RailingPiece& piece : findRailingStyle(id)->run(input)) {
            const bool post = piece.role == RailingPiece::Role::Post;
            if (!post && piece.role != RailingPiece::Role::Infill) continue;
            for (const PartPrism& prism : piece.prisms) {
                double low = prism.outline[0].x;
                double high = low;
                for (const Vec2& q : prism.outline) {
                    low = std::min(low, q.x);
                    high = std::max(high, q.x);
                }
                places.push_back({(low + high) / 2.0, post ? (high - low) / 2.0 : 0.0});
                if (!post) ++infill;
            }
        }
        std::sort(places.begin(), places.end());
        CHECK(infill > 20);
        for (std::size_t i = 1; i < places.size(); ++i) {
            CHECK(places[i].first - places[i].second - places[i - 1].first - places[i - 1].second <= 0.1 + 1e-9);
        }
    }
    std::vector<double> heights;
    for (const RailingPiece& piece : findRailingStyle("cables")->run(input)) {
        if (piece.role != RailingPiece::Role::Infill) continue;
        for (const PartPrism& prism : piece.prisms) heights.push_back((prism.bottom + prism.top) / 2.0);
    }
    std::sort(heights.begin(), heights.end());
    REQUIRE(heights.size() >= 2);
    for (std::size_t i = 1; i < heights.size(); ++i) CHECK(heights[i] - heights[i - 1] <= 0.08 + 1e-9);
}

TEST_CASE("the glass style stands thin panels in a shoe under a top rail", "[railings][geometry]")
{
    RailingRunInput input;
    input.length = 3.0;
    input.height = 1.1;
    bool shoe = false;
    bool rail = false;
    int panes = 0;
    for (const RailingPiece& piece : findRailingStyle("glass")->run(input)) {
        if (piece.role == RailingPiece::Role::Shoe) shoe = true;
        if (piece.role == RailingPiece::Role::Rail) {
            rail = true;
            for (const PartPrism& prism : piece.prisms) CHECK(prism.top == Approx(1.1));
        }
        if (piece.role != RailingPiece::Role::Glass) continue;
        for (const PartPrism& prism : piece.prisms) {
            ++panes;
            double across = 0.0;
            for (const Vec2& p : prism.outline) across = std::max(across, std::fabs(p.y) * 2.0);
            CHECK(across == Approx(kGlassThickness));
        }
    }
    CHECK(shoe);
    CHECK(rail);
    CHECK(panes == 3);
}

TEST_CASE("checkRailing takes good values and refuses the rest", "[railings][checks]")
{
    registerTestStyles();
    CHECK(checkRailing(railingOf({{0.0, 0.0}, {3.0, 0.0}})).ok());
    CHECK(checkRailing(railingOf({{0.0, 0.0}, {3.0, 0.0}, {3.0, 2.0}}, "glass", 1.5)).ok());
    CHECK(checkRailing(railingOf({{0.0, 0.0}, {3.0, 0.0}, {3.0, 2.0}}, "spindles", 0.3, true)).ok());
    CHECK(checkRailing(railingOf({{0.0, 0.0}, {0.1, 0.0}}, "half_wall", 1.5)).ok());
    CHECK(checkRailing(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "test_glass_on_posts")).ok());
    // A line that ends where it starts counts as closed, so its last piece is not a zero length piece.
    CHECK(checkRailing(railingOf({{0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 0.0}})).ok());

    const auto refused = [](const Railing& railing, const std::string& message) {
        const Status s = checkRailing(railing);
        CHECK(s.code == ErrorCode::InvalidValue);
        CHECK(s.message == message);
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    refused(railingOf({{0.0, 0.0}}), "A railing line needs at least two points.");
    refused(railingOf({{0.0, 0.0}, {nan, 0.0}}), "The railing line points must be finite numbers.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "posts_rails", 1.0, true), "A closed railing line needs at least three points.");
    refused(railingOf({{0.0, 0.0}, {0.05, 0.0}}), "Each piece of a railing line must be at least 0.1 m long.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}, {3.0, 0.0}, {3.0, 2.0}}), "Each piece of a railing line must be at least 0.1 m long.");
    refused(railingOf({{0.0, 0.0}, {600.0, 0.0}, {0.0, 0.5}}), "A railing line must be at most 1000 m long.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "posts_rails", 0.2), "The railing height must be from 0.3 to 3 metres.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "posts_rails", 3.5), "The railing height must be from 0.3 to 3 metres.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "glass", 1.6), "A railing of Glass panels must be from 0.3 to 1.5 m tall.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "half_wall", 2.0), "A railing of Solid half wall must be from 0.3 to 1.5 m tall.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "no_such_style"), "There is no railing style \"no_such_style\".");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "two words"), "The railing style is not a valid name.");
    refused(railingOf({{0.0, 0.0}, {3.0, 0.0}}, "test_loose_glass"),
            "The railing style \"test_loose_glass\" has glass with no posts or base shoe to hold it.");

    Railing low = railingOf({{0.0, 0.0}, {3.0, 0.0}});
    low.base = -0.1;
    refused(low, "The railing base must be from 0 to 20 metres.");
    low.base = 21.0;
    refused(low, "The railing base must be from 0 to 20 metres.");
    Railing badMaterial = railingOf({{0.0, 0.0}, {3.0, 0.0}});
    badMaterial.material = "brushed steel";
    refused(badMaterial, "The railing material \"brushed steel\" is not a valid name.");
    Railing badGlass = railingOf({{0.0, 0.0}, {3.0, 0.0}}, "glass");
    badGlass.glassMaterial = "";
    refused(badGlass, "The glass material \"\" is not a valid name.");
}

TEST_CASE("railingRuns gives one run per piece of the line at the railing's foot", "[railings]")
{
    Project p = railingProject({}, {});
    Deck deck;
    deck.id = "dk1";
    deck.outline = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    deck.top = 0.6;
    p.plan.floors[0].decks.push_back(deck);
    const Floor& floor = p.plan.floors[0];
    const std::vector<double> levels = floorElevations(p.plan);

    Railing open = railingOf({{4.1, 0.0}, {7.0, 0.0}, {7.0, 0.0}, {7.0, 2.0}});
    open.base = 0.25;
    std::vector<RailingRun> runs = railingRuns(p.plan, floor, open, levels);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].start == Vec3{4.1, 0.0, 0.25});
    CHECK(runs[0].end == Vec3{7.0, 0.0, 0.25});
    CHECK(runs[1].end == Vec3{7.0, -2.0, 0.25});

    open.host = "dk1";
    runs = railingRuns(p.plan, floor, open, levels);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].start.z == Approx(0.6));

    Railing closed = railingOf({{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}}, "posts_rails", 1.0, true);
    runs = railingRuns(p.plan, floor, closed, levels);
    REQUIRE(runs.size() == 3);
    CHECK(runs[2].end == runs[0].start);

    Railing looped = railingOf({{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 0.0}});
    CHECK(railingRuns(p.plan, floor, looped, levels).size() == 3);

    CHECK(railingRuns(p.plan, floor, railingOf({{1.0, 1.0}, {1.0, 1.0}}), levels).empty());
}

TEST_CASE("railings stand on the level of their own floor", "[railings]")
{
    Project p = squareProjectWithWalls();
    Floor upper = squareFloorWithWalls();
    upper.id = "f2";
    upper.name = "Floor 2";
    for (Point& point : upper.points) point.id += "u";
    for (Wall& wall : upper.walls) {
        wall.id += "u";
        wall.from += "u";
        wall.to += "u";
    }
    Railing balcony = railingOf({{4.0, 0.0}, {6.0, 0.0}});
    balcony.id = "rl2";
    upper.railings.push_back(balcony);
    p.plan.floors.push_back(upper);
    requirePlanValid(p.plan);
    const std::vector<double> levels = floorElevations(p.plan);
    const std::vector<RailingRun> runs = railingRuns(p.plan, p.plan.floors[1], balcony, levels);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].start.z == Approx(levels[1]));
    CHECK(levels[1] > 1.0);
}

TEST_CASE("the railing maker is registered for the surfaces group", "[railings][parts]")
{
    const PartMaker* railings = maker("railings");
    REQUIRE(railings != nullptr);
    CHECK(railings->order() == 30);
    CHECK(railings->kinds() == std::vector<std::string>{"railing"});
    CHECK(railings->exportGroup() == "surfaces");

    const Project p = railingProject({railingOf({{5.0, 0.0}, {7.0, 0.0}})});
    const std::vector<ExportGroup> groups = automaticExportGroups(p);
    const auto surfaces = std::find_if(groups.begin(), groups.end(), [](const ExportGroup& g) { return g.id == "surfaces"; });
    REQUIRE(surfaces != groups.end());
    const ExportMember every{"railing", Id()};
    CHECK(std::find(surfaces->members.begin(), surfaces->members.end(), every) != surfaces->members.end());
    CHECK(exportMemberLabel(p.plan, {"railing", "rl1"}) == "Railing rl1");
    CHECK(exportMemberLabel(p.plan, {"railing", "s1"}) == "Railing of stair s1");
}

TEST_CASE("an open L shaped railing has one post at its corner and posts at both ends", "[railings][parts]")
{
    const Project p = railingProject({railingOf({{5.0, 0.0}, {8.0, 0.0}, {8.0, 2.0}})});
    const PartOutput out = railingParts(p);
    CHECK(out.problems.empty());
    REQUIRE(out.parts.size() == 1);
    const BuildPart& part = out.parts[0];
    CHECK(part.object == "f1_rl1_Railing");
    CHECK(part.kind == "railing");
    CHECK(part.id == "rl1");

    const std::vector<Vec2> posts = postsOf(part, 0.0, 1.0);
    // The 3 m run in two bays, the 2 m run in two bays, the corner shared.
    CHECK(posts.size() == 5);
    CHECK(countAt(posts, {5.0, 0.0}) == 1);
    CHECK(countAt(posts, {6.5, 0.0}) == 1);
    CHECK(countAt(posts, {8.0, 0.0}) == 1);
    CHECK(countAt(posts, {8.0, -1.0}) == 1);
    CHECK(countAt(posts, {8.0, -2.0}) == 1);

    for (const PartPrism& prism : part.prisms) {
        checkSolidPrism(prism);
        CHECK(signedArea(prism.outline) > 0.0);
        CHECK_FALSE(prism.slot.empty());
    }
}

TEST_CASE("a closed railing has a post at every corner once and mitred rails", "[railings][parts]")
{
    const Project p = railingProject({railingOf({{5.0, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {5.0, 2.0}}, "posts_rails", 1.0, true)});
    const PartOutput out = railingParts(p);
    REQUIRE(out.parts.size() == 1);
    const BuildPart& part = out.parts[0];
    const std::vector<Vec2> posts = postsOf(part, 0.0, 1.0);
    CHECK(posts.size() == 8);
    for (const Vec2 corner : {Vec2{5.0, 0.0}, Vec2{7.0, 0.0}, Vec2{7.0, -2.0}, Vec2{5.0, -2.0}}) {
        CHECK(countAt(posts, corner) == 1);
    }

    // The top rails of both runs at the corner (7, 0) end on its mitre: the outer corner 0.04 m out on both sides.
    int meeting = 0;
    for (const PartPrism& prism : part.prisms) {
        if (prism.top != Approx(1.0) || prism.bottom != Approx(0.95)) continue;
        if (hasCorner(prism, {7.04, 0.04}) && hasCorner(prism, {6.96, -0.04})) ++meeting;
    }
    CHECK(meeting == 2);
    CHECK(part.collision.size() == 4);
}

TEST_CASE("an open railing's ends stay square", "[railings][parts]")
{
    const Project p = railingProject({railingOf({{5.0, 0.0}, {7.0, 0.0}, {7.0, 2.0}}, "half_wall", 1.0)});
    const PartOutput out = railingParts(p);
    REQUIRE(out.parts.size() == 1);
    bool squareStart = false;
    for (const PartPrism& prism : out.parts[0].prisms) {
        if (hasCorner(prism, {5.0, 0.075}) && hasCorner(prism, {5.0, -0.075})) squareStart = true;
    }
    CHECK(squareStart);
}

TEST_CASE("a railing's collision is one thin box per run, the railing's height tall", "[railings][parts]")
{
    Deck deck;
    deck.id = "dk1";
    deck.outline = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    deck.top = 0.6;
    Railing railing = railingOf({{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}}, "glass", 1.1);
    railing.host = "dk1";
    const Project p = railingProject({railing}, {deck});
    const PartOutput out = railingParts(p);
    REQUIRE(out.parts.size() == 1);
    const BuildPart& part = out.parts[0];
    REQUIRE(part.collision.size() == 3);
    for (const CollisionPiece& piece : part.collision) {
        CHECK(isConvexRing(piece.outline));
        CHECK(piece.bottom == Approx(0.6));
        CHECK(piece.top == Approx(1.7));
    }
    for (const PartPrism& prism : part.prisms) {
        CHECK(prism.bottom >= 0.6 - 1e-9);
        CHECK(prism.top <= 1.7 + 1e-9);
    }
    CHECK(part.hulls.empty());
    CHECK_FALSE(part.bevel);
}

TEST_CASE("glass goes in the glass slot and the rest in the railing slot", "[railings][parts]")
{
    Railing railing = railingOf({{5.0, 0.0}, {8.0, 0.0}}, "glass", 1.0);
    railing.material = "brushed_steel";
    const Project p = railingProject({railing});
    const PartOutput out = railingParts(p);
    REQUIRE(out.parts.size() == 1);
    const std::string railSlot = partSlot(p, Surface::Railing, railing.material).slot;
    const std::string glassSlot = partSlot(p, Surface::Glass, std::nullopt).slot;
    int glass = 0;
    int metal = 0;
    for (const PartPrism& prism : out.parts[0].prisms) {
        double across = 0.0;
        for (const Vec2& p2 : prism.outline) across = std::max(across, std::fabs(p2.y) * 2.0);
        if (across == Approx(kGlassThickness).margin(1e-9)) {
            CHECK(prism.slot == glassSlot);
            ++glass;
        } else {
            CHECK(prism.slot == railSlot);
            ++metal;
        }
    }
    CHECK(glass == 3);
    CHECK(metal == 2);
    CHECK(railSlot != glassSlot);
}

TEST_CASE("the railing maker warns about an unknown style and a missing deck", "[railings][parts]")
{
    Railing unknown = railingOf({{5.0, 0.0}, {7.0, 0.0}}, "rope_bridge");
    Railing orphan = railingOf({{5.0, 3.0}, {7.0, 3.0}});
    orphan.id = "rl2";
    orphan.host = "dk9";
    orphan.base = 0.4;
    const Project p = railingProject({unknown, orphan});
    const PartOutput out = railingParts(p);
    const auto style = withCode(out.problems, "railing_style_unknown");
    REQUIRE(style.size() == 1);
    CHECK(style[0].severity == BuildSeverity::Warning);
    CHECK(style[0].object == "rl1");
    CHECK(style[0].floor == "f1");
    const auto host = withCode(out.problems, "railing_host_missing");
    REQUIRE(host.size() == 1);
    CHECK(host[0].object == "rl2");
    REQUIRE(out.parts.size() == 2);
    CHECK(postsOf(out.parts[0], 0.0, 1.0).size() == 3);
    CHECK(out.parts[1].collision[0].bottom == Approx(0.4));
}

TEST_CASE("every style builds along a closed line through the maker", "[railings][parts][geometry]")
{
    for (const RailingStyle* style : allRailingStyles()) {
        if (style->id().rfind("test_", 0) == 0) continue;
        INFO(std::string(style->id()));
        const Project p = railingProject({railingOf({{5.0, 0.0}, {8.0, 0.0}, {9.0, 2.0}, {5.0, 3.0}}, std::string(style->id()),
                                                    1.0, true)});
        const PartOutput out = railingParts(p);
        CHECK(out.problems.empty());
        REQUIRE(out.parts.size() == 1);
        CHECK_FALSE(out.parts[0].prisms.empty());
        for (const PartPrism& prism : out.parts[0].prisms) {
            checkSolidPrism(prism);
            CHECK(signedArea(prism.outline) > 0.0);
        }
        CHECK(out.parts[0].collision.size() == 4);
        for (const CollisionPiece& piece : out.parts[0].collision) CHECK(isConvexRing(piece.outline));
    }
}

TEST_CASE("the build takes every style's railing part as it is", "[railings][parts][build]")
{
    for (const RailingStyle* style : allRailingStyles()) {
        if (style->id().rfind("test_", 0) == 0) continue;
        INFO(std::string(style->id()));
        Project project = buildProject();
        Railing railing = railingOf({{5.0, 0.0}, {8.0, 0.0}, {8.0, 2.0}}, std::string(style->id()), 1.0);
        project.plan.floors[0].railings.push_back(railing);
        const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
        CHECK(withCode(plan.problems, "part_invalid").empty());
        REQUIRE(plan.floorParts.size() == 1);
        const auto part = std::find_if(plan.floorParts[0].begin(), plan.floorParts[0].end(),
                                       [](const BuildPart& p) { return p.object == "f1_rl1_Railing"; });
        REQUIRE(part != plan.floorParts[0].end());
        for (const PartPrism& prism : part->prisms) {
            const bool glass = prism.slot.rfind("Glass", 0) == 0;
            const bool metal = prism.slot.rfind("Railing", 0) == 0;
            CHECK((glass || metal));
        }
    }
}

TEST_CASE("openDeckEdges leaves out the edges along the walls", "[railings][decks]")
{
    const Floor floor = squareFloorWithWalls();
    Deck porch;
    porch.id = "dk1";
    porch.outline = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    std::vector<std::vector<PlanXY>> lines = openDeckEdges(floor, porch);
    REQUIRE(lines.size() == 1);
    const std::vector<PlanXY> expected{{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    REQUIRE(lines[0].size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(lines[0][i].x == Approx(expected[i].x).margin(1e-6));
        CHECK(lines[0][i].y == Approx(expected[i].y).margin(1e-6));
    }

    // Only the part of the left edge beside the wall is left out.
    Deck corner;
    corner.id = "dk2";
    corner.outline = {{4.1, 3.0}, {7.0, 3.0}, {7.0, 6.0}, {4.1, 6.0}};
    lines = openDeckEdges(floor, corner);
    REQUIRE(lines.size() == 1);
    const std::vector<PlanXY> past{{4.1, 3.0}, {7.0, 3.0}, {7.0, 6.0}, {4.1, 6.0}, {4.1, 4.1}};
    REQUIRE(lines[0].size() == past.size());
    for (std::size_t i = 0; i < past.size(); ++i) {
        CHECK(lines[0][i].x == Approx(past[i].x).margin(1e-6));
        CHECK(lines[0][i].y == Approx(past[i].y).margin(1e-6));
    }

    // A deck away from every wall is open all round: one line back to its first point, which counts as closed.
    Deck free;
    free.id = "dk3";
    free.outline = {{10.0, 0.0}, {12.0, 0.0}, {12.0, 2.0}, {10.0, 2.0}};
    lines = openDeckEdges(floor, free);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].size() == 5);
    CHECK(lines[0].front().x == Approx(lines[0].back().x));
    CHECK(lines[0].front().y == Approx(lines[0].back().y));
    Railing round = railingOf(lines[0]);
    CHECK(checkRailing(round).ok());
    Project p = railingProject({round});
    CHECK(railingRuns(p.plan, p.plan.floors[0], round, floorElevations(p.plan)).size() == 4);

    Deck inside;
    inside.outline = {{1.0, 1.0}, {3.0, 1.0}, {3.0, 3.0}, {1.0, 3.0}};
    CHECK(openDeckEdges(floor, inside).size() == 1);
    Deck onWall;
    onWall.outline = {{3.95, 1.0}, {4.05, 1.0}, {4.05, 3.0}, {3.95, 3.0}};
    CHECK(openDeckEdges(floor, onWall).empty());
}

TEST_CASE("a drawn railing's materials count as used", "[railings][materials]")
{
    Railing steel = railingOf({{5.0, 0.0}, {7.0, 0.0}}, "posts_rails");
    steel.material = "teak";
    Railing glass = railingOf({{5.0, 3.0}, {7.0, 3.0}}, "glass");
    glass.id = "rl2";
    glass.glassMaterial = "frosted_glass";
    const Project p = railingProject({steel, glass});
    const std::vector<std::string> used = materialsUsed(p);
    CHECK(std::find(used.begin(), used.end(), "teak") != used.end());
    CHECK(std::find(used.begin(), used.end(), "frosted_glass") != used.end());
}

TEST_CASE("railingProblems warns about an unknown style and a missing deck, and nothing else", "[railings]")
{
    Floor floor = squareFloorWithWalls();
    Deck deck;
    deck.id = "dk1";
    deck.outline = {{4.1, 0.0}, {7.0, 0.0}, {7.0, 2.0}, {4.1, 2.0}};
    floor.decks.push_back(deck);

    Railing fine = railingOf({{4.1, 0.0}, {7.0, 0.0}}, "glass");
    fine.host = "dk1";
    CHECK(railingProblems(floor, fine).empty());

    Railing both = railingOf({{4.1, 0.0}, {7.0, 0.0}}, "rope_bridge");
    both.host = "dk9";
    const std::vector<BuildProblem> found = railingProblems(floor, both);
    REQUIRE(found.size() == 2);
    CHECK(found[0].code == "railing_style_unknown");
    CHECK(found[1].code == "railing_host_missing");
    for (const BuildProblem& problem : found) {
        CHECK(problem.severity == BuildSeverity::Warning);
        CHECK(problem.floor == "f1");
        CHECK(problem.object == "rl1");
        CHECK(problem.step == std::string(kCheckStep));
        CHECK_FALSE(problem.message.empty());
    }

    // The maker reports the same warnings.
    const Project p = railingProject({both});
    CHECK(railingParts(p).problems == found);
}
