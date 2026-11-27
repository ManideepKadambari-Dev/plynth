#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Decks.h>
#include <plynth/core/ExportGroups.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A deck with this outline, top and thickness, the other values at their defaults.
Deck deckOf(const std::string& id, std::vector<PlanXY> outline, double top = 0.6, double thickness = 0.15)
{
    Deck deck;
    deck.id = id;
    deck.outline = std::move(outline);
    deck.top = top;
    deck.thickness = thickness;
    return deck;
}

// A rectangle on the plan.
std::vector<PlanXY> rectangle(double x0, double y0, double x1, double y1)
{
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// The part maker with this id.
const PartMaker* maker(std::string_view id)
{
    for (const PartMaker* m : allPartMakers()) {
        if (m->id() == id) return m;
    }
    return nullptr;
}

// What the deck maker gives for one floor of a project.
PartOutput deckParts(const Project& project, std::size_t floorIndex = 0)
{
    const PartMaker* decks = maker("decks");
    REQUIRE(decks != nullptr);
    const std::vector<double> levels = floorElevations(project.plan);
    const std::vector<FloorBuild> floors;
    PartInput input;
    input.project = &project;
    input.floor = &project.plan.floors[floorIndex];
    input.floorIndex = floorIndex;
    input.levels = &levels;
    input.floors = &floors;
    return decks->floorParts(input);
}

// The area of a polygon with holes, the holes taken off.
double polygonArea(const std::vector<Vec2>& outline, const std::vector<std::vector<Vec2>>& holes)
{
    double area = signedArea(outline);
    for (const auto& hole : holes) area += signedArea(hole);
    return area;
}

// The square room project with a deck added to its floor.
Project deckProject(const Deck& deck)
{
    Project project = buildProject();
    project.plan.floors[0].decks.push_back(deck);
    return project;
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

TEST_CASE("checkDeck takes the values of the plan format and refuses the rest", "[decks]")
{
    CHECK(checkDeck(deckOf("dk1", rectangle(5.0, 0.0, 8.0, 2.0))).ok());
    CHECK(checkDeck(deckOf("", rectangle(5.0, 0.0, 8.0, 2.0), 0.0, 0.05)).ok());
    CHECK(checkDeck(deckOf("", rectangle(5.0, 0.0, 8.0, 2.0), 10.0, 1.0)).ok());

    const auto refused = [](const Deck& deck, const std::string& message) {
        const Status s = checkDeck(deck);
        CHECK(s.code == ErrorCode::InvalidValue);
        CHECK(s.message == message);
    };
    refused(deckOf("", {{0.0, 0.0}, {1.0, 0.0}}), "A deck outline needs at least three points.");
    refused(deckOf("", {{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}), "The deck outline must enclose more than 0.01 square metres.");
    refused(deckOf("", rectangle(0.0, 0.0, 0.09, 0.09)), "The deck outline must enclose more than 0.01 square metres.");
    refused(deckOf("", {{0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 0.0}, {1.0, 1.0}}),
            "The deck outline points must be finite numbers.");
    refused(deckOf("", rectangle(0.0, 0.0, 1.0, 1.0), -0.1), "The deck top must be from 0 to 10 metres.");
    refused(deckOf("", rectangle(0.0, 0.0, 1.0, 1.0), 10.5), "The deck top must be from 0 to 10 metres.");
    refused(deckOf("", rectangle(0.0, 0.0, 1.0, 1.0), 0.6, 0.04), "The deck thickness must be from 0.05 to 1 metre.");
    refused(deckOf("", rectangle(0.0, 0.0, 1.0, 1.0), 0.6, 1.1), "The deck thickness must be from 0.05 to 1 metre.");

    Deck badMaterial = deckOf("", rectangle(0.0, 0.0, 1.0, 1.0));
    badMaterial.material = "oak planks";
    refused(badMaterial, "The deck material \"oak planks\" is not a valid name.");
    Deck badSkirt = deckOf("", rectangle(0.0, 0.0, 1.0, 1.0));
    badSkirt.skirtMaterial = "";
    refused(badSkirt, "The skirt material \"\" is not a valid name.");
}

TEST_CASE("deckTopAt gives the top of the highest deck under a point", "[decks]")
{
    Floor floor;
    floor.decks.push_back(deckOf("dk1", rectangle(0.0, 0.0, 4.0, 2.0), 0.4));
    floor.decks.push_back(deckOf("dk2", rectangle(3.0, 1.0, 6.0, 3.0), 0.9));
    REQUIRE(deckTopAt(floor, 1.0, 1.0));
    CHECK(*deckTopAt(floor, 1.0, 1.0) == Approx(0.4));
    CHECK(*deckTopAt(floor, 3.5, 1.5) == Approx(0.9));
    CHECK(*deckTopAt(floor, 5.0, 2.5) == Approx(0.9));
    CHECK_FALSE(deckTopAt(floor, 8.0, 8.0));
    CHECK_FALSE(deckTopAt(Floor{}, 1.0, 1.0));
}

TEST_CASE("the deck maker is registered for the surfaces group", "[decks][parts]")
{
    const PartMaker* decks = maker("decks");
    REQUIRE(decks != nullptr);
    CHECK(decks->order() == 20);
    CHECK(decks->kinds() == std::vector<std::string>{"deck"});
    CHECK(decks->exportGroup() == "surfaces");

    const std::vector<ExportGroup> groups = automaticExportGroups(deckProject(deckOf("dk1", rectangle(4.0, 0.0, 7.0, 2.0))));
    const auto surfaces = std::find_if(groups.begin(), groups.end(), [](const ExportGroup& g) { return g.id == "surfaces"; });
    REQUIRE(surfaces != groups.end());
    CHECK(std::find(surfaces->members.begin(), surfaces->members.end(), ExportMember{"deck", ""}) != surfaces->members.end());
}

TEST_CASE("a deck is cut back to the outer wall face and gets its skirt and collision", "[decks][parts]")
{
    // w2 runs down x = 4 with a band from 3.9 to 4.1, so the deck drawn from x = 4 starts at the outer face.
    Deck deck = deckOf("dk1", rectangle(4.0, 0.0, 7.0, 2.0), 0.6, 0.15);
    deck.skirtMaterial = "painted_wood";
    const Project project = deckProject(deck);
    const PartOutput out = deckParts(project);
    CHECK(out.problems.empty());
    REQUIRE(out.parts.size() == 1);
    const BuildPart& part = out.parts[0];
    CHECK(part.object == "f1_dk1_Deck");
    CHECK(part.kind == "deck");
    CHECK(part.id == "dk1");
    CHECK(part.uv == PartUv::World);
    CHECK(part.bevel);

    REQUIRE(part.prisms.size() == 2);
    const PartPrism& top = part.prisms[0];
    CHECK(top.bottom == Approx(0.45));
    CHECK(top.top == Approx(0.6));
    CHECK(top.slot == partSlot(project, Surface::Deck, std::nullopt).slot);
    CHECK(top.sideSlot.empty());
    CHECK(polygonArea(top.outline, top.holes) == Approx(2.9 * 2.0).margin(1e-4));
    for (const Vec2& p : top.outline) {
        CHECK(p.x >= 4.1 - 1e-6);
        CHECK(p.y <= 1e-6); // world y is the plan's y turned round
        CHECK(p.y >= -2.0 - 1e-6);
    }

    const PartPrism& skirt = part.prisms[1];
    CHECK(skirt.bottom == Approx(0.0));
    CHECK(skirt.top == Approx(0.45));
    CHECK(skirt.slot == partSlot(project, Surface::Deck, std::string("painted_wood")).slot);
    CHECK(polygonArea(skirt.outline, skirt.holes) == Approx((2.9 - 2 * kSkirtInset) * (2.0 - 2 * kSkirtInset)).margin(1e-4));

    REQUIRE_FALSE(part.collision.empty());
    double collisionArea = 0.0;
    for (const CollisionPiece& piece : part.collision) {
        CHECK(isConvexRing(piece.outline));
        CHECK(piece.bottom == Approx(0.0));
        CHECK(piece.top == Approx(0.6));
        collisionArea += signedArea(piece.outline);
    }
    CHECK(collisionArea == Approx(2.9 * 2.0).margin(1e-4));
}

TEST_CASE("a deck without a skirt has one prism and collision from its bottom", "[decks][parts]")
{
    Deck deck = deckOf("dk1", rectangle(5.0, 0.0, 8.0, 2.0), 0.6, 0.2);
    deck.skirt = false;
    const PartOutput out = deckParts(deckProject(deck));
    REQUIRE(out.parts.size() == 1);
    REQUIRE(out.parts[0].prisms.size() == 1);
    CHECK(polygonArea(out.parts[0].prisms[0].outline, out.parts[0].prisms[0].holes) == Approx(6.0).margin(1e-6));
    for (const CollisionPiece& piece : out.parts[0].collision) CHECK(piece.bottom == Approx(0.4));
}

TEST_CASE("a deck at ground height has no skirt and stands in the ground", "[decks][parts]")
{
    const PartOutput out = deckParts(deckProject(deckOf("dk1", rectangle(5.0, 0.0, 8.0, 2.0), 0.1, 0.15)));
    REQUIRE(out.parts.size() == 1);
    REQUIRE(out.parts[0].prisms.size() == 1);
    CHECK(out.parts[0].prisms[0].bottom == Approx(-0.05));
    CHECK(out.parts[0].prisms[0].top == Approx(0.1));
    for (const CollisionPiece& piece : out.parts[0].collision) CHECK(piece.bottom == Approx(-0.05));
}

TEST_CASE("a deck on an upper floor stands on that floor's level", "[decks][parts]")
{
    Project project = buildProject();
    Floor upper = squareFloorWithWalls();
    upper.id = "f2";
    upper.name = "Floor 2";
    for (Point& p : upper.points) p.id = "q" + p.id.substr(1);
    for (Wall& w : upper.walls) {
        w.id = "v" + w.id.substr(1);
        w.from = "q" + w.from.substr(1);
        w.to = "q" + w.to.substr(1);
    }
    upper.decks.push_back(deckOf("dk2", rectangle(4.1, 0.0, 6.0, 2.0), 0.0, 0.2));
    project.plan.floors.push_back(upper);
    const double level = floorElevations(project.plan)[1];
    const PartOutput out = deckParts(project, 1);
    REQUIRE(out.parts.size() == 1);
    CHECK(out.parts[0].object == "f2_dk2_Deck");
    CHECK(out.parts[0].prisms[0].top == Approx(level));
    CHECK(out.parts[0].prisms[0].bottom == Approx(level - 0.2));
}

TEST_CASE("a deck that crosses itself is left out with an error", "[decks][parts]")
{
    const PartOutput out = deckParts(deckProject(deckOf("dk1", {{5.0, 0.0}, {8.0, 0.0}, {8.0, 2.0}, {6.0, -1.0}})));
    CHECK(out.parts.empty());
    REQUIRE(out.problems.size() == 1);
    CHECK(out.problems[0].code == "deck_outline_invalid");
    CHECK(out.problems[0].severity == BuildSeverity::Error);
    CHECK(out.problems[0].floor == "f1");
    CHECK(out.problems[0].object == "dk1");
    CHECK(out.problems[0].step == kCheckStep);
    CHECK(out.problems[0].message == "Deck dk1 has an outline that crosses itself or has no area, so it is left out.");

    const PartOutput folded = deckParts(deckProject(deckOf("dk1", {{5.0, 0.0}, {8.0, 0.0}, {6.0, 0.0}, {6.0, 2.0}})));
    CHECK(folded.parts.empty());
    REQUIRE(folded.problems.size() == 1);
    CHECK(folded.problems[0].code == "deck_outline_invalid");
}

TEST_CASE("a deck that lies on a wall only is left out", "[decks][parts]")
{
    const PartOutput out = deckParts(deckProject(deckOf("dk1", rectangle(3.95, 1.0, 4.05, 3.0))));
    CHECK(out.parts.empty());
    REQUIRE(out.problems.size() == 1);
    CHECK(out.problems[0].code == "deck_outline_invalid");
    CHECK(out.problems[0].message == "Deck dk1 lies wholly on walls, so it is left out.");
}

TEST_CASE("a deck mostly inside the building is built with a warning", "[decks][parts]")
{
    const PartOutput inside = deckParts(deckProject(deckOf("dk1", rectangle(1.0, 1.0, 3.0, 3.0))));
    REQUIRE(inside.parts.size() == 1);
    REQUIRE(inside.problems.size() == 1);
    CHECK(inside.problems[0].code == "deck_inside_building");
    CHECK(inside.problems[0].severity == BuildSeverity::Warning);
    CHECK(inside.problems[0].message == "Deck dk1 lies mostly inside the walls of Floor 1.");

    // Less than half inside: a porch that reaches a little into the building is fine.
    const PartOutput porch = deckParts(deckProject(deckOf("dk1", rectangle(3.0, 1.0, 7.0, 3.0))));
    CHECK(withCode(porch.problems, "deck_inside_building").empty());
    REQUIRE(porch.parts.size() == 1);
    // The wall band is taken out, so the deck is two pieces: one in the room and one outside.
    CHECK(polygonArea(porch.parts[0].prisms[0].outline, porch.parts[0].prisms[0].holes) +
              polygonArea(porch.parts[0].prisms[1].outline, porch.parts[0].prisms[1].holes) ==
          Approx(8.0 - 0.4).margin(1e-4));
}

TEST_CASE("a deck goes through the build into the job", "[decks][parts][schema]")
{
    plynth::test::TempDir tmp("deck-job");
    Project project = deckProject(deckOf("dk1", rectangle(4.1, 0.0, 7.0, 2.0)));
    setMaterialDefault(project.settings.materials, Surface::Deck, std::string(kSampleOakPlanks));
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.floorParts.size() == 1);
    REQUIRE(plan.floorParts[0].size() == 1);
    CHECK(plan.floorParts[0][0].prisms[0].slot == "Deck");
    CHECK(withCode(plan.problems, "deck_outline_invalid").empty());

    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    const Json& parts = job["params"]["floors"][0]["parts"];
    REQUIRE(parts.size() == 1);
    CHECK(parts[0]["object"] == "f1_dk1_Deck");
    CHECK(parts[0]["plynth_kind"] == "deck");
    CHECK(parts[0]["plynth_id"] == "dk1");
    const std::string problem = schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
    INFO(problem);
    CHECK(problem.empty());
}

TEST_CASE("a deck that crosses itself shows its error in the build plan", "[decks][parts]")
{
    const Project project = deckProject(deckOf("dk1", {{5.0, 0.0}, {8.0, 0.0}, {8.0, 2.0}, {6.0, -1.0}}));
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    CHECK(plan.floorParts[0].empty());
    const std::vector<BuildProblem> found = withCode(plan.problems, "deck_outline_invalid");
    REQUIRE(found.size() == 1);
    CHECK(found[0].object == "dk1");
    CHECK(found[0].floor == "f1");
}
