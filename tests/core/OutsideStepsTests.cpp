#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/Decks.h>
#include <plynth/core/StairRailings.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/Storeys.h>
#include <plynth/core/commands/StairCommands.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// Outside steps of rise metres whose top step ends at plan y = endY, centred on x, climbing toward +y.
Stair stepsUpTo(double x, double endY, double rise, const std::string& id = "s1")
{
    Stair s;
    s.id = id;
    s.x = x;
    const int risers = std::max(2, static_cast<int>(std::ceil(rise / kMaxRiser - 1e-9)));
    s.y = endY - (risers - 1) * kStairGoing;
    s.width = 1.0;
    s.rise = rise;
    return s;
}

// The square room project of the build fixtures with its door o1 (on w1, the wall along y = 0) raised to a sill.
Project raisedDoorProject(double sill)
{
    Project project = buildProject();
    project.plan.floors[0].openings[0].sill = sill;
    return project;
}

// The problems of one stair with this code.
std::vector<StairProblem> problemsOf(const Plan& plan, const Floor& floor, const std::string& code)
{
    std::vector<StairProblem> out;
    for (const StairProblem& p : stairProblems(plan, floor)) {
        if (p.code == code) out.push_back(p);
    }
    return out;
}

// The part maker with this id.
const PartMaker* maker(std::string_view id)
{
    for (const PartMaker* m : allPartMakers()) {
        if (m->id() == id) return m;
    }
    return nullptr;
}

// What the cheek maker gives for the first floor of a project.
PartOutput cheekParts(const Project& project)
{
    const PartMaker* cheeks = maker("stair_cheeks");
    REQUIRE(cheeks != nullptr);
    const std::vector<double> levels = floorElevations(project.plan);
    const std::vector<FloorBuild> floors;
    PartInput input;
    input.project = &project;
    input.floor = &project.plan.floors[0];
    input.levels = &levels;
    input.floors = &floors;
    return cheeks->floorParts(input);
}

// The middle of a list of points.
Vec3 middleOf(const std::vector<Vec3>& points)
{
    Vec3 sum;
    for (const Vec3& p : points) {
        sum.x += p.x;
        sum.y += p.y;
        sum.z += p.z;
    }
    const double n = static_cast<double>(points.size());
    return {sum.x / n, sum.y / n, sum.z / n};
}

// The normal of a planar ring by Newell's rule, not made unit length.
Vec3 normalOf(const std::vector<Vec3>& ring)
{
    Vec3 n;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

}

TEST_CASE("outside steps work out their steps from their own rise", "[stairs][outside]")
{
    const Plan plan = makeEmptyPlan();
    const Stair steps = stepsUpTo(2.0, -0.1, 0.6);
    const StairLayout layout = stairLayout(plan, plan.floors[0], steps);
    requireOk(layout.status);
    CHECK(layout.toFloor.empty());
    CHECK(layout.rise == Approx(0.6));
    CHECK(layout.riserCount == 4);
    CHECK(layout.riser == Approx(0.15));
    CHECK(layout.riser <= kMaxRiser);
    REQUIRE(layout.steps.size() == 3);
    CHECK(layout.steps.back().top == Approx(0.45));
    CHECK(layout.arrival.x == Approx(2.0));
    CHECK(layout.arrival.y == Approx(-0.1));

    Stair low = stepsUpTo(0.0, 0.0, 0.1);
    const StairLayout one = stairLayout(plan, plan.floors[0], low);
    requireOk(one.status);
    CHECK(one.riserCount == 2);
    CHECK(one.riser == Approx(0.05));
    CHECK(one.steps.size() == 1);

    Stair tall = stepsUpTo(0.0, 0.0, 1.9);
    const StairLayout many = stairLayout(plan, plan.floors[0], tall);
    CHECK(many.riserCount == 10);
    CHECK(many.riser == Approx(0.19));
}

TEST_CASE("outside steps arrive on no floor and cut no hole", "[stairs][outside]")
{
    Plan plan = buildProject().plan;
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    plan.floors.push_back(upper);
    const SlabBuild plain = slabBuild(plan, 0, floorElevations(plan));
    // Steps up to a platform in the room.
    plan.floors[0].stairs.push_back(stepsUpTo(2.0, 3.0, 0.6, "s9"));
    CHECK(stairsArrivingAt(plan, "f2").empty());
    CHECK(slabBuild(plan, 0, floorElevations(plan)) == plain);

    // The same stair without a rise climbs to the floor above and cuts its well.
    plan.floors[0].stairs[0].rise.reset();
    CHECK(stairsArrivingAt(plan, "f2").size() == 1);
    CHECK(slabBuild(plan, 0, floorElevations(plan)) != plain);
}

TEST_CASE("outside steps cut no ceiling hole and send no floor above in the job", "[stairs][outside]")
{
    plynth::test::TempDir tmp("outside-steps-job");
    Project plain = buildProject();
    Project project = plain;
    // Steps up to a platform inside the room: without the rise rule they would hole the ceiling.
    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, 3.0, 0.6));
    const BuildPlan without = makeBuildPlan(plain, sampleAssetSource(), sampleMaterialSource());
    const BuildPlan with = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE(with.floors[0].stairs.size() == 1);
    CHECK(with.floors[0].stairs[0].toFloor.empty());
    CHECK(with.floors[0].stairs[0].riserCount == 4);
    CHECK(with.floors[0].rooms == without.floors[0].rooms);
    CHECK(std::none_of(with.problems.begin(), with.problems.end(),
                       [](const BuildProblem& p) { return p.code == "stair_no_floor_above"; }));

    const Json job = makeBuildJob(project, with, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    CHECK(job["params"]["floors"][0]["stairs"][0]["to_floor"].is_null());
    const std::string problem = schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
    INFO(problem);
    CHECK(problem.empty());
}

TEST_CASE("checkStair refuses a rise outside the range", "[stairs][outside]")
{
    Stair s = stepsUpTo(0.0, 0.0, 0.6);
    CHECK(checkStair(s).ok());
    s.rise = 0.1;
    CHECK(checkStair(s).ok());
    s.rise = 6.0;
    CHECK(checkStair(s).ok());
    for (double bad : {0.05, 6.5, std::numeric_limits<double>::quiet_NaN()}) {
        s.rise = bad;
        const Status status = checkStair(s);
        CHECK(status.code == ErrorCode::InvalidValue);
        CHECK(status.message == "The stair rise must be from 0.1 to 6 metres.");
    }
}

TEST_CASE("outside steps up to a door sill at their height have no problem", "[stairs][outside]")
{
    const Project project = [] {
        Project p = raisedDoorProject(0.6);
        p.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -0.1, 0.6));
        return p;
    }();
    const Floor& floor = project.plan.floors[0];
    CHECK(stairProblems(project.plan, floor).empty());
}

TEST_CASE("outside steps that miss their door sill get stair_rise_mismatch", "[stairs][outside]")
{
    Project project = raisedDoorProject(0.6);
    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -0.1, 0.5));
    const Floor& floor = project.plan.floors[0];
    const std::vector<StairProblem> found = problemsOf(project.plan, floor, "stair_rise_mismatch");
    REQUIRE(found.size() == 1);
    CHECK_FALSE(found[0].error);
    CHECK(found[0].stair == "s1");
    CHECK(found[0].message == "Stair s1 climbs to 0.50 m, but the sill of door o1 is at 0.60 m.");

    // Within 0.02 m is close enough.
    project.plan.floors[0].openings[0].sill = 0.515;
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_rise_mismatch").empty());
}

TEST_CASE("outside steps that end 0.3 m from a door are checked, further ones are not", "[stairs][outside]")
{
    Project project = raisedDoorProject(0.6);
    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -0.35, 0.45));
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_rise_mismatch").size() == 1);
    project.plan.floors[0].stairs[0] = stepsUpTo(2.0, -0.5, 0.45);
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_rise_mismatch").empty());
}

TEST_CASE("outside steps are checked against the deck they reach", "[stairs][outside][decks]")
{
    Project project = buildProject();
    Deck deck;
    deck.id = "dk1";
    deck.outline = {{0.0, -3.0}, {4.0, -3.0}, {4.0, -0.1}, {0.0, -0.1}};
    deck.top = 0.6;
    project.plan.floors[0].decks.push_back(deck);
    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -3.0, 0.6));
    CHECK(stairProblems(project.plan, project.plan.floors[0]).empty());

    project.plan.floors[0].stairs[0] = stepsUpTo(2.0, -3.0, 0.45);
    const std::vector<StairProblem> found = problemsOf(project.plan, project.plan.floors[0], "stair_rise_mismatch");
    REQUIRE(found.size() == 1);
    CHECK(found[0].message == "Stair s1 climbs to 0.45 m, but the top of deck dk1 is at 0.60 m.");
}

TEST_CASE("outside steps that lead to nothing are not checked for height", "[stairs][outside]")
{
    Project project = buildProject();
    project.plan.floors[0].stairs.push_back(stepsUpTo(10.0, 10.0, 0.45));
    CHECK(stairProblems(project.plan, project.plan.floors[0]).empty());
}

TEST_CASE("outside steps through a wall get stair_overlaps_wall", "[stairs][outside]")
{
    Project project = buildProject();
    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, 0.3, 0.6));
    const std::vector<StairProblem> found = problemsOf(project.plan, project.plan.floors[0], "stair_overlaps_wall");
    REQUIRE(found.size() == 1);
    CHECK(found[0].wall == "w1");
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_outside_floor").empty());
}

TEST_CASE("outside steps on the top floor need no floor above", "[stairs][outside]")
{
    Project project = buildProject();
    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -0.1, 0.6));
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_no_floor_above").empty());
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_arrival_outside_room").empty());

    Stair inside = stepsUpTo(1.0, 2.0, 0.6, "s2");
    inside.rise.reset();
    project.plan.floors[0].stairs.push_back(inside);
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_no_floor_above").size() == 1);
}

TEST_CASE("outside steps outside an upper floor do not reach the ground", "[stairs][outside]")
{
    Project project = buildProject();
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    upper.stairs.push_back(stepsUpTo(2.0, -0.1, 0.6, "s2"));
    project.plan.floors.push_back(upper);
    const std::vector<StairProblem> found = problemsOf(project.plan, project.plan.floors[1], "stair_off_ground");
    REQUIRE(found.size() == 1);
    CHECK_FALSE(found[0].error);
    CHECK(found[0].message == "Stair s2 stands outside the walls of Floor 2, which is not at ground level, so it does not reach the ground.");

    project.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -0.1, 0.6, "s3"));
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_off_ground").empty());

    project.plan.groundFloor = "f2";
    CHECK(problemsOf(project.plan, project.plan.floors[1], "stair_off_ground").empty());
    CHECK(problemsOf(project.plan, project.plan.floors[0], "stair_off_ground").size() == 1);
}

TEST_CASE("railing false leaves the railings of steps out", "[stairs][outside]")
{
    Plan plan = makeEmptyPlan();
    plan.floors[0].stairs.push_back(stepsUpTo(2.0, 2.0, 0.9));
    const std::vector<double> levels = floorElevations(plan);
    CHECK_FALSE(stairRailings(plan, TrimSettings{}, levels).empty());
    plan.floors[0].stairs[0].railing = false;
    CHECK(stairRailings(plan, TrimSettings{}, levels).empty());
}

TEST_CASE("AddStairCommand places outside steps on the top floor", "[stairs][outside][commands]")
{
    Project p = makeNewProject();
    const Project before = p;
    Stair steps = stepsUpTo(2.0, -0.1, 0.6);
    steps.cheeks = true;
    AddStairCommand add("f1", steps);
    requireOk(add.apply(p));
    requirePlanValid(p.plan);
    REQUIRE(p.plan.floors[0].stairs.size() == 1);
    CHECK(p.plan.floors[0].stairs[0].rise == std::optional<double>(0.6));
    CHECK(p.plan.floors[0].stairs[0].cheeks);
    const Id id = add.newStairId();
    add.undo(p);
    CHECK(p.plan == before.plan);
    requireOk(add.apply(p));
    CHECK(p.plan.floors[0].stairs[0].id == id);

    Stair bad = stepsUpTo(2.0, -0.1, 0.6);
    bad.rise = 7.0;
    AddStairCommand tooHigh("f1", bad);
    CHECK(tooHigh.apply(p).message == "The stair rise must be from 0.1 to 6 metres.");
    CHECK(p.plan.floors[0].stairs.size() == 1);
}

TEST_CASE("SetStairCommand changes the rise, cheeks and railing of outside steps", "[stairs][outside][commands]")
{
    Project p = makeNewProject();
    p.plan.floors[0].stairs.push_back(stepsUpTo(2.0, -0.1, 0.6));
    const Project before = p;
    Stair values = p.plan.floors[0].stairs[0];
    values.rise = 0.9;
    values.cheeks = true;
    values.railing = false;
    SetStairCommand set("s1", values);
    requireOk(set.apply(p));
    requirePlanValid(p.plan);
    CHECK(p.plan.floors[0].stairs[0].rise == std::optional<double>(0.9));
    CHECK_FALSE(p.plan.floors[0].stairs[0].railing);
    set.undo(p);
    CHECK(p.plan == before.plan);

    values.rise = 0.0;
    SetStairCommand bad("s1", values);
    CHECK(bad.apply(p).code == ErrorCode::InvalidValue);
    CHECK(p.plan == before.plan);

    RemoveStairCommand remove("s1");
    requireOk(remove.apply(p));
    CHECK(p.plan.floors[0].stairs.empty());
    remove.undo(p);
    CHECK(p.plan == before.plan);
}

TEST_CASE("the cheek maker is registered for the surfaces group", "[stairs][outside][parts]")
{
    const PartMaker* cheeks = maker("stair_cheeks");
    REQUIRE(cheeks != nullptr);
    CHECK(cheeks->order() == 25);
    CHECK(cheeks->kinds() == std::vector<std::string>{"stair"});
    CHECK(cheeks->exportGroup() == "surfaces");
}

TEST_CASE("cheeks stand along both sides of straight steps", "[stairs][outside][parts]")
{
    for (double rotation : {0.0, 90.0, 217.0}) {
        INFO("rotation " << rotation);
        Project project = makeNewProject();
        Stair steps = stepsUpTo(2.0, -0.1, 0.6);
        steps.rotation = rotation;
        steps.cheeks = true;
        steps.material = "concrete";
        project.plan.floors[0].stairs.push_back(steps);
        const PartOutput out = cheekParts(project);
        CHECK(out.problems.empty());
        REQUIRE(out.parts.size() == 1);
        const BuildPart& part = out.parts[0];
        CHECK(part.object == "f1_s1_Cheeks");
        CHECK(part.kind == "stair");
        CHECK(part.id == "s1");
        REQUIRE(part.faces.size() == 12);
        REQUIRE(part.hulls.size() == 2);
        for (const PartFace& face : part.faces) {
            CHECK(face.slot == partSlot(project, Surface::Stair, std::string("concrete")).slot);
        }

        for (std::size_t i = 0; i < part.faces.size(); ++i) {
            const std::vector<Vec3>& hull = part.hulls[i / 6];
            const Vec3 centre = middleOf(hull);
            const Vec3 middle = middleOf(part.faces[i].outline);
            const Vec3 n = normalOf(part.faces[i].outline);
            const double outward = n.x * (middle.x - centre.x) + n.y * (middle.y - centre.y) + n.z * (middle.z - centre.z);
            CHECK(outward > 0.0);
        }

        double low = 1e9;
        double high = -1e9;
        for (const auto& hull : part.hulls) {
            REQUIRE(hull.size() == 8);
            for (const Vec3& p : hull) {
                low = std::min(low, p.z);
                high = std::max(high, p.z);
            }
        }
        CHECK(low == Approx(0.0));
        CHECK(high == Approx(0.6 + kCheekRise));

        // Each cheek is kCheekThickness wide and starts at the side of the flight.
        const double r = rotation * std::numbers::pi / 180.0;
        const Vec2 climb{-std::sin(r), std::cos(r)};
        const Vec2 right = rightOf(climb);
        for (const auto& hull : part.hulls) {
            double closest = 1e9;
            double farthest = 0.0;
            for (const Vec3& p : hull) {
                const Vec2 plan{p.x, -p.y};
                const double u = std::fabs((plan.x - steps.x) * right.x + (plan.y - steps.y) * right.y);
                closest = std::min(closest, u);
                farthest = std::max(farthest, u);
            }
            CHECK(closest == Approx(0.5));
            CHECK(farthest == Approx(0.5 + kCheekThickness));
        }
    }
}

TEST_CASE("cheeks follow the nose line of the steps", "[stairs][outside][parts]")
{
    Project project = makeNewProject();
    Stair steps = stepsUpTo(0.0, 0.81, 0.6);
    steps.cheeks = true;
    project.plan.floors[0].stairs.push_back(steps);
    const PartOutput out = cheekParts(project);
    REQUIRE(out.parts.size() == 1);
    // The top face runs from the first nose (0.15) to the last riser (0.6), kCheekRise above both.
    const PartFace& top = out.parts[0].faces[1];
    double front = 1e9;
    double back = -1e9;
    for (const Vec3& p : top.outline) {
        front = std::min(front, p.z);
        back = std::max(back, p.z);
    }
    CHECK(front == Approx(0.15 + kCheekRise));
    CHECK(back == Approx(0.6 + kCheekRise));
}

TEST_CASE("only straight steps get cheeks", "[stairs][outside][parts]")
{
    Project project = makeNewProject();
    Stair steps = stepsUpTo(2.0, -0.1, 1.2);
    steps.type = std::string(kLShapeStairId);
    steps.cheeks = true;
    project.plan.floors[0].stairs.push_back(steps);
    Stair plain = stepsUpTo(6.0, -0.1, 0.6, "s2");
    project.plan.floors[0].stairs.push_back(plain);
    const PartOutput out = cheekParts(project);
    CHECK(out.parts.empty());
    REQUIRE(out.problems.size() == 1);
    CHECK(out.problems[0].code == "stair_cheeks_not_straight");
    CHECK(out.problems[0].severity == BuildSeverity::Warning);
    CHECK(out.problems[0].object == "s1");
    CHECK(out.problems[0].floor == "f1");
}

TEST_CASE("cheeks go through the build as a stair part", "[stairs][outside][parts]")
{
    Project project = raisedDoorProject(0.6);
    Stair steps = stepsUpTo(2.0, -0.1, 0.6);
    steps.cheeks = true;
    project.plan.floors[0].stairs.push_back(steps);
    const BuildPlan plan = makeBuildPlan(project, sampleAssetSource(), sampleMaterialSource());
    REQUIRE(plan.floorParts[0].size() == 1);
    CHECK(plan.floorParts[0][0].object == "f1_s1_Cheeks");
    CHECK(plan.floorParts[0][0].faces[0].slot == "Stair");
    CHECK(std::none_of(plan.problems.begin(), plan.problems.end(),
                       [](const BuildProblem& p) { return p.code == "part_invalid" || p.code == "stair_rise_mismatch"; }));
}
