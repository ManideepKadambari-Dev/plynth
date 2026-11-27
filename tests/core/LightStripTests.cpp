#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildParts.h>
#include <plynth/core/GameplayData.h>
#include <plynth/core/LightStrips.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/commands/DeleteCommands.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/LightStripCommands.h>
#include <plynth/core/commands/StairCommands.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// A strip of a kind along a line of plan points, at a height.
LightStrip stripAlong(LightStripKind kind, std::vector<PlanXY> points, double z, double power = 10.0)
{
    LightStrip l;
    l.kind = kind;
    l.points = std::move(points);
    l.z = z;
    l.power = power;
    return l;
}

// A step strip on a stair.
LightStrip stepStrip(const std::string& stair)
{
    LightStrip l;
    l.kind = LightStripKind::Step;
    l.stair = stair;
    l.z = 0.0;
    return l;
}

// A project with two floors, f1 below f2, and a stair s1 of a type on f1 at (2, 1) climbing toward +y.
Project stairProject(const std::string& type = "straight")
{
    Project p = makeNewProject();
    Floor f2;
    f2.id = "f2";
    f2.name = "Floor 2";
    p.plan.floors.push_back(f2);
    Stair s;
    s.id = "s1";
    s.type = type;
    s.x = 2.0;
    s.y = 1.0;
    p.plan.floors[0].stairs.push_back(s);
    return p;
}

// The light strip part maker.
const PartMaker& stripMaker()
{
    const PartMaker* found = nullptr;
    for (const PartMaker* m : allPartMakers()) {
        if (m->id() == "light_strips") found = m;
    }
    REQUIRE(found != nullptr);
    return *found;
}

// What the light strip maker gives for one floor of a project.
PartOutput stripsOf(const Project& p, std::size_t floorIndex = 0)
{
    const std::vector<double> levels = floorElevations(p.plan);
    const std::vector<FloorBuild> none;
    PartInput input;
    input.project = &p;
    input.floor = &p.plan.floors[floorIndex];
    input.floorIndex = floorIndex;
    input.levels = &levels;
    input.floors = &none;
    return stripMaker().floorParts(input);
}

// The length of a strip light.
double lengthOf(const StripLight& l)
{
    return std::sqrt((l.end.x - l.start.x) * (l.end.x - l.start.x) + (l.end.y - l.start.y) * (l.end.y - l.start.y) +
                     (l.end.z - l.start.z) * (l.end.z - l.start.z));
}

// The number of steps of a stair that are steps, not landings or a column.
std::size_t stepCount(const Project& p)
{
    const StairLayout layout = stairLayout(p.plan, p.plan.floors[0], p.plan.floors[0].stairs[0]);
    REQUIRE(layout.status.ok());
    return static_cast<std::size_t>(
        std::count_if(layout.steps.begin(), layout.steps.end(), [](const StairStep& s) { return s.part == StairPart::Step; }));
}

// Applies a command, checks the plan, undoes it and checks the project is as before, then applies it again.
void applyUndoRedo(Project& p, Command& c)
{
    const Project before = p;
    test::requireOk(c.apply(p));
    test::requirePlanValid(p.plan);
    const Project after = p;
    c.undo(p);
    CHECK(p == before);
    test::requireOk(c.apply(p));
    CHECK(p == after);
}

// Says that a command is refused with a code and leaves the project as it was.
void refused(Project& p, Command& c, ErrorCode code)
{
    const Project before = p;
    CHECK(c.apply(p).code == code);
    CHECK(p == before);
}

}

TEST_CASE("checkLightStrip takes a good strip and refuses bad lines, heights and power", "[lightstrips]")
{
    const Project p = stairProject();
    const Floor& f = p.plan.floors[0];
    const LightStrip cove = stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}}, 2.95);
    test::requireOk(checkLightStrip(cove, f));
    test::requireOk(checkLightStrip(stepStrip("s1"), f));

    LightStrip bad = cove;
    bad.points = {{0.5, 0.5}};
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);
    bad.points = {{0.5, 0.5}, {0.55, 0.5}};
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);
    bad.points = {{0.5, 0.5}, {0.505, 0.5}, {1.5, 0.5}};
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);
    bad.points = {{0.5, std::numeric_limits<double>::quiet_NaN()}, {1.5, 0.5}};
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);

    bad = cove;
    bad.z = f.ceilingHeight + 0.01;
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);
    bad.z = -0.01;
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);
    bad.z = f.ceilingHeight;
    test::requireOk(checkLightStrip(bad, f));

    bad = cove;
    bad.power = 201.0;
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);
    bad.power = -1.0;
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);

    bad = cove;
    bad.stair = "s1";
    CHECK(checkLightStrip(bad, f).code == ErrorCode::InvalidValue);

    LightStrip step = stepStrip("s9");
    CHECK(checkLightStrip(step, f).code == ErrorCode::NotFound);
    step.stair.reset();
    CHECK(checkLightStrip(step, f).code == ErrorCode::InvalidValue);
    step = stepStrip("s1");
    step.points = {{0.0, 0.0}, {1.0, 0.0}};
    CHECK(checkLightStrip(step, f).code == ErrorCode::InvalidValue);
}

TEST_CASE("each kind of strip starts at its own height", "[lightstrips]")
{
    CHECK_THAT(defaultStripHeight(LightStripKind::Cove, 2.8), WithinAbs(2.75, 1e-12));
    CHECK_THAT(defaultStripHeight(LightStripKind::Soffit, 2.8), WithinAbs(2.8, 1e-12));
    CHECK(defaultStripHeight(LightStripKind::Edge, 2.8) == 0.0);
    CHECK(defaultStripHeight(LightStripKind::Step, 2.8) == 0.0);
    CHECK(defaultStripHeight(LightStripKind::Cove, 0.02) == 0.0);
}

TEST_CASE("adding, changing and removing a light strip undo and redo", "[lightstrips][commands]")
{
    Project p = stairProject();
    AddLightStripCommand add("f1", stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}}, 2.95));
    CHECK(add.newStripId().empty());
    applyUndoRedo(p, add);
    const Id id = add.newStripId();
    CHECK(id == "ls1");
    CHECK(add.changes() == Change::Lights);
    CHECK(add.floorId() == "f1");
    REQUIRE(findLightStrip(p.plan.floors[0], id) != nullptr);

    AddLightStripCommand nowhere("f9", stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}}, 2.95));
    refused(p, nowhere, ErrorCode::NotFound);
    AddLightStripCommand tooShort("f1", stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {0.52, 0.5}}, 2.95));
    refused(p, tooShort, ErrorCode::InvalidValue);
    AddLightStripCommand tooHigh("f1", stripAlong(LightStripKind::Soffit, {{0.5, 0.5}, {3.5, 0.5}}, 9.0));
    refused(p, tooHigh, ErrorCode::InvalidValue);
    AddLightStripCommand noStair("f1", stepStrip("s9"));
    refused(p, noStair, ErrorCode::NotFound);

    LightStrip values = stripAlong(LightStripKind::Edge, {{0.5, 0.5}, {3.5, 0.5}, {3.5, 3.5}}, 1.2, 20.0);
    values.id = "other";
    SetLightStripCommand set(id, values, 7);
    applyUndoRedo(p, set);
    CHECK(set.changes() == Change::Lights);
    CHECK(set.floorId() == "f1");
    const LightStrip* changed = findLightStrip(p.plan.floors[0], id);
    REQUIRE(changed != nullptr);
    CHECK(changed->kind == LightStripKind::Edge);
    CHECK(changed->points.size() == 3);
    CHECK(changed->power == 20.0);

    LightStrip later = values;
    later.z = 1.5;
    SetLightStripCommand drag(id, later, 7);
    test::requireOk(drag.apply(p));
    CHECK(set.mergeWith(drag));
    CHECK_FALSE(set.mergeWith(SetLightStripCommand(id, later, 8)));
    CHECK_FALSE(SetLightStripCommand(id, later).mergeWith(drag));
    set.undo(p);
    CHECK(findLightStrip(p.plan.floors[0], id)->kind == LightStripKind::Cove);

    SetLightStripCommand missing("ls9", values);
    refused(p, missing, ErrorCode::NotFound);
    LightStrip badPower = values;
    badPower.power = 500.0;
    SetLightStripCommand tooBright(id, badPower);
    refused(p, tooBright, ErrorCode::InvalidValue);

    AddLightStripCommand second("f1", stepStrip("s1"));
    test::requireOk(second.apply(p));
    RemoveLightStripCommand remove(id);
    applyUndoRedo(p, remove);
    CHECK(remove.changes() == Change::Lights);
    CHECK(findLightStrip(p.plan.floors[0], id) == nullptr);
    remove.undo(p);
    REQUIRE(p.plan.floors[0].lightStrips.size() == 2);
    CHECK(p.plan.floors[0].lightStrips[0].id == id);
    RemoveLightStripCommand gone("ls9");
    refused(p, gone, ErrorCode::NotFound);
}

TEST_CASE("removing a stair takes its step strips and clears other strips that name it", "[lightstrips][commands][stairs]")
{
    Project p = stairProject();
    Floor& f = p.plan.floors[0];
    LightStrip step = stepStrip("s1");
    step.id = "ls1";
    LightStrip cove = stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}}, 2.95);
    cove.id = "ls2";
    LightStrip namesIt = stripAlong(LightStripKind::Edge, {{0.5, 3.5}, {3.5, 3.5}}, 1.0);
    namesIt.id = "ls3";
    namesIt.stair = "s1";
    f.lightStrips = {step, cove, namesIt};
    test::requirePlanValid(p.plan);

    RemoveStairCommand remove("s1");
    applyUndoRedo(p, remove);
    CHECK(remove.changes() == (Change::Stairs | Change::Lights));
    REQUIRE(p.plan.floors[0].lightStrips.size() == 2);
    CHECK(p.plan.floors[0].lightStrips[0].id == "ls2");
    CHECK(p.plan.floors[0].lightStrips[1].id == "ls3");
    CHECK_FALSE(p.plan.floors[0].lightStrips[1].stair.has_value());
    remove.undo(p);
    CHECK(p.plan.floors[0].lightStrips == std::vector<LightStrip>{step, cove, namesIt});

    p.plan.floors[0].lightStrips = {cove};
    RemoveStairCommand plain("s1");
    applyUndoRedo(p, plain);
    CHECK(plain.changes() == Change::Stairs);
    CHECK(p.plan.floors[0].lightStrips == std::vector<LightStrip>{cove});
}

TEST_CASE("a delete removes named strips, and a stair with its step strip in one step", "[lightstrips][commands]")
{
    Project p = stairProject();
    LightStrip step = stepStrip("s1");
    step.id = "ls1";
    LightStrip cove = stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}}, 2.95);
    cove.id = "ls2";
    p.plan.floors[0].lightStrips = {step, cove};

    const Id one[] = {Id("ls2")};
    DeleteBuild del = makeDeleteCommand(p.plan, one);
    test::requireOk(del.status);
    CHECK(del.removes.lightStrips == std::vector<Id>{"ls2"});
    applyUndoRedo(p, *del.command);
    CHECK(findLightStrip(p.plan.floors[0], "ls2") == nullptr);
    del.command->undo(p);

    const Id both[] = {Id("s1"), Id("ls1")};
    DeleteBuild all = makeDeleteCommand(p.plan, both);
    test::requireOk(all.status);
    CHECK(all.removes.lightStrips == std::vector<Id>{"ls1"});
    CHECK(all.removes.stairs == std::vector<Id>{"s1"});
    applyUndoRedo(p, *all.command);
    CHECK(p.plan.floors[0].stairs.empty());
    CHECK(p.plan.floors[0].lightStrips == std::vector<LightStrip>{cove});
}

TEST_CASE("scale, image offset and ceiling height carry the strips", "[lightstrips][commands]")
{
    Project p = test::squareProjectWithWalls();
    p.plan.scale.pixelsPerMetre = 100.0;
    LightStrip cove = stripAlong(LightStripKind::Cove, {{1.0, 1.0}, {3.0, 1.0}}, 2.9);
    cove.id = "ls1";
    p.plan.floors[0].lightStrips = {cove};

    SetScaleCommand scale(50.0);
    applyUndoRedo(p, scale);
    CHECK((scale.changes() & Change::Lights) != 0);
    CHECK_THAT(p.plan.floors[0].lightStrips[0].points[1].x, WithinAbs(6.0, 1e-9));
    scale.undo(p);

    SetFloorImageOffsetCommand offset("f1", {1.0, 0.5}, true);
    applyUndoRedo(p, offset);
    CHECK_THAT(p.plan.floors[0].lightStrips[0].points[0].x, WithinAbs(2.0, 1e-9));
    CHECK_THAT(p.plan.floors[0].lightStrips[0].points[0].y, WithinAbs(1.5, 1e-9));
    offset.undo(p);

    SetCeilingHeightCommand lower("f1", 2.5);
    refused(p, lower, ErrorCode::InvalidValue);
    SetCeilingHeightCommand keep("f1", 2.95);
    applyUndoRedo(p, keep);
}

TEST_CASE("a cove strip gives an upward light per piece and one housing", "[lightstrips][parts][schema]")
{
    test::TempDir tmp("strips-cove");
    Project project = test::buildProject();
    LightStrip cove = stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}, {3.5, 3.5}}, 2.9);
    cove.id = "ls1";
    project.plan.floors[0].lightStrips = {cove};
    test::requirePlanValid(project.plan);
    const BuildPlan plan = makeBuildPlan(project, test::sampleAssetSource(), test::sampleMaterialSource());

    REQUIRE(plan.stripLights.size() == 1);
    REQUIRE(plan.stripLights[0].size() == 2);
    const StripLight& first = plan.stripLights[0][0];
    CHECK(first.id == "ls1");
    CHECK(first.facing == Vec3{0.0, 0.0, 1.0});
    CHECK(first.width == kStripWidth);
    CHECK_THAT(first.start.x, WithinAbs(0.5, 1e-9));
    CHECK_THAT(first.start.y, WithinAbs(-0.5, 1e-9));
    CHECK_THAT(first.start.z, WithinAbs(2.9 + kStripGap, 1e-9));
    CHECK_THAT(first.end.x, WithinAbs(3.5, 1e-9));
    CHECK_THAT(first.power, WithinAbs(30.0, 1e-9));
    CHECK_THAT(plan.stripLights[0][1].end.y, WithinAbs(-3.5, 1e-9));

    const auto part = std::find_if(plan.floorParts[0].begin(), plan.floorParts[0].end(),
                                   [](const BuildPart& b) { return b.object == "f1_ls1_Strip"; });
    REQUIRE(part != plan.floorParts[0].end());
    CHECK(part->kind == "light");
    CHECK(part->id == "ls1");
    CHECK_FALSE(part->bevel);
    CHECK(part->collision.empty());
    CHECK(part->hulls.empty());
    REQUIRE(part->prisms.size() == 2);
    for (const PartPrism& prism : part->prisms) {
        CHECK(prism.slot == "Light_Strip");
        CHECK_THAT(prism.bottom, WithinAbs(2.9 - kStripDepth, 1e-9));
        CHECK_THAT(prism.top, WithinAbs(2.9, 1e-9));
        CHECK(signedArea(prism.outline) > 0.0);
    }
    // The first housing runs past the corner by half its width so the two meet.
    double reach = -std::numeric_limits<double>::infinity();
    for (const Vec2& q : part->prisms[0].outline) reach = std::max(reach, q.x);
    CHECK_THAT(reach, WithinAbs(3.5 + kStripWidth / 2.0, 1e-9));
    CHECK(std::none_of(plan.problems.begin(), plan.problems.end(), [](const BuildProblem& b) {
        return b.code == "part_invalid" || b.code == "light_strip_stair_missing";
    }));

    const Json job = makeBuildJob(project, plan, BuildSettings{}, tmp.path(), "build-1a2b3c4d", tmp.path() / "result.json");
    const Json& floor = job["params"]["floors"][0];
    REQUIRE(floor["strip_lights"].size() == 2);
    REQUIRE(floor.contains("parts"));
    Json parts = Json::object();
    parts["parts"] = floor["parts"];
    parts["strip_lights"] = floor["strip_lights"];
    const std::string problem = test::schemaProblem("blender-job-parts.schema.json", nlohmann::json::parse(parts.dump()));
    INFO(problem);
    CHECK(problem.empty());
}

TEST_CASE("soffit strips shine down and edge strips shine to the right of their line", "[lightstrips][parts]")
{
    Project p = stairProject();
    LightStrip soffit = stripAlong(LightStripKind::Soffit, {{0.5, 0.5}, {3.5, 0.5}}, 2.4, 5.0);
    soffit.id = "ls1";
    LightStrip edge = stripAlong(LightStripKind::Edge, {{0.5, 0.5}, {3.5, 0.5}}, 1.0, 5.0);
    edge.id = "ls2";
    p.plan.floors[0].lightStrips = {soffit, edge};
    const PartOutput out = stripsOf(p);
    REQUIRE(out.lights.size() == 2);
    REQUIRE(out.parts.size() == 2);
    CHECK(out.problems.empty());

    const StripLight& down = out.lights[0];
    CHECK(down.facing == Vec3{0.0, 0.0, -1.0});
    CHECK_THAT(down.start.z, WithinAbs(2.4 - kStripGap, 1e-9));
    CHECK_THAT(down.power, WithinAbs(15.0, 1e-9));
    CHECK_THAT(out.parts[0].prisms[0].bottom, WithinAbs(2.4, 1e-9));
    CHECK_THAT(out.parts[0].prisms[0].top, WithinAbs(2.4 + kStripDepth, 1e-9));

    // The line runs along plan +x, so its right is plan +y, which is world -Y.
    const StripLight& side = out.lights[1];
    CHECK_THAT(side.facing.x, WithinAbs(0.0, 1e-12));
    CHECK_THAT(side.facing.y, WithinAbs(-1.0, 1e-12));
    CHECK(side.facing.z == 0.0);
    CHECK_THAT(side.start.y, WithinAbs(-0.5 - kStripGap, 1e-9));
    CHECK_THAT(side.start.z, WithinAbs(1.0, 1e-9));
    const PartPrism& housing = out.parts[1].prisms[0];
    CHECK_THAT(housing.bottom, WithinAbs(1.0 - kStripWidth / 2.0, 1e-9));
    CHECK_THAT(housing.top, WithinAbs(1.0 + kStripWidth / 2.0, 1e-9));
    for (const Vec2& q : housing.outline) {
        CHECK(q.y >= -0.5 - 1e-9);
        CHECK(q.y <= -0.5 + kStripDepth + 1e-9);
    }
}

TEST_CASE("a step strip lights the nose of every step of a straight stair", "[lightstrips][parts][stairs]")
{
    Project p = stairProject();
    LightStrip step = stepStrip("s1");
    step.id = "ls1";
    p.plan.floors[0].lightStrips = {step};
    test::requirePlanValid(p.plan);
    const StairLayout layout = stairLayout(p.plan, p.plan.floors[0], p.plan.floors[0].stairs[0]);
    REQUIRE(layout.status.ok());

    const PartOutput out = stripsOf(p);
    CHECK(out.problems.empty());
    REQUIRE(out.lights.size() == stepCount(p));
    REQUIRE(out.parts.size() == 1);
    CHECK(out.parts[0].object == "f1_ls1_Strip");
    CHECK(out.parts[0].prisms.size() == out.lights.size());

    const double width = p.plan.floors[0].stairs[0].width;
    const StripLight& first = out.lights[0];
    CHECK_THAT(std::min(first.start.x, first.end.x), WithinAbs(2.0 - width / 2.0 + kStepStripInset, 1e-9));
    CHECK_THAT(std::max(first.start.x, first.end.x), WithinAbs(2.0 + width / 2.0 - kStepStripInset, 1e-9));
    CHECK_THAT(first.start.y, WithinAbs(-1.0 + kStripDepth + kStripGap, 1e-9));
    CHECK_THAT(first.start.z, WithinAbs(layout.steps[0].top - kStepStripDrop, 1e-9));
    CHECK_THAT(first.facing.x, WithinAbs(0.0, 1e-9));
    CHECK_THAT(first.facing.y, WithinAbs(std::sqrt(0.5), 1e-9));
    CHECK_THAT(first.facing.z, WithinAbs(-std::sqrt(0.5), 1e-9));
    CHECK_THAT(first.power, WithinAbs(step.power * (width - 2.0 * kStepStripInset), 1e-9));

    // Each light sits under the nose of the next step up, one going further along the climb.
    for (std::size_t i = 1; i < out.lights.size(); ++i) {
        CHECK_THAT(out.lights[i].start.y, WithinAbs(out.lights[i - 1].start.y - kStairGoing, 1e-9));
        CHECK(out.lights[i].start.z > out.lights[i - 1].start.z);
    }
}

TEST_CASE("a step strip finds a nose on every step of the turning stairs", "[lightstrips][parts][stairs]")
{
    for (const char* type : {"l_shape", "u_shape", "spiral"}) {
        INFO(type);
        Project p = stairProject(type);
        LightStrip step = stepStrip("s1");
        step.id = "ls1";
        p.plan.floors[0].lightStrips = {step};
        const PartOutput out = stripsOf(p);
        CHECK(out.problems.empty());
        CHECK(out.lights.size() == stepCount(p));
        for (const StripLight& l : out.lights) {
            CHECK(l.power > 0.0);
            CHECK(lengthOf(l) > 0.0);
            CHECK(l.facing.z < 0.0);
        }
    }
}

TEST_CASE("a step strip whose stair is gone is left out with a problem", "[lightstrips][parts]")
{
    Project p = stairProject();
    LightStrip step = stepStrip("s9");
    step.id = "ls1";
    p.plan.floors[0].lightStrips = {step};
    const PartOutput out = stripsOf(p);
    CHECK(out.lights.empty());
    CHECK(out.parts.empty());
    REQUIRE(out.problems.size() == 1);
    CHECK(out.problems[0].code == "light_strip_stair_missing");
    CHECK(out.problems[0].severity == BuildSeverity::Error);
    CHECK(out.problems[0].object == "ls1");
    CHECK(out.problems[0].floor == "f1");

    // A stair that cannot be built (no floor above) gives no light and no strip problem.
    p.plan.floors.pop_back();
    p.plan.floors[0].lightStrips[0].stair = "s1";
    const PartOutput none = stripsOf(p);
    CHECK(none.lights.empty());
    CHECK(none.parts.empty());
    CHECK(none.problems.empty());
}

TEST_CASE("strip lights go into the gameplay data with their ends and room", "[lightstrips][gameplay]")
{
    Project project = test::buildProject();
    LightStrip cove = stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}}, 2.9, 10.0);
    cove.id = "ls1";
    project.plan.floors[0].lightStrips = {cove};
    const BuildPlan plan = makeBuildPlan(project, test::sampleAssetSource(), test::sampleMaterialSource());

    const std::vector<GameplayLight> lights = planStripLights(project.plan, plan.stripLights);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].id == "ls1");
    CHECK(lights[0].floor == "f1");
    CHECK(lights[0].room == std::optional<Id>("r1"));
    REQUIRE(lights[0].strip.has_value());
    CHECK_THAT(lights[0].position.x, WithinAbs(2.0, 1e-9));
    CHECK_THAT(lights[0].position.y, WithinAbs(-0.5, 1e-9));
    CHECK_THAT(lights[0].strip->power, WithinAbs(30.0, 1e-9));
    CHECK(lights[0].strip->facing == Vec3{0.0, 0.0, 1.0});

    const GameplayData data = gameplayData("Flat", project.plan, plan.floors, lights);
    const Json json = gameplayToJson(data);
    REQUIRE(json["lights"].size() == 1);
    const Json& light = json["lights"][0];
    CHECK(light["kind"] == "strip");
    CHECK(light["room"] == "r1");
    CHECK(light["start"][0].get<double>() == lights[0].strip->start.x);
    CHECK(light["end"][0].get<double>() == lights[0].strip->end.x);
    CHECK(light["facing"][2].get<double>() == 1.0);
    CHECK(light["width"].get<double>() == kStripWidth);
    CHECK(light["power"].get<double>() == lights[0].strip->power);

    // A light marker keeps its own kind and has no strip fields.
    const Json markers = gameplayToJson(gameplayData("Flat", project.plan, plan.floors, planLights(project.plan)));
    CHECK(markers["lights"].empty());
}

TEST_CASE("a gameplay file with strip lights matches its schema", "[lightstrips][gameplay][schema]")
{
    Project project = test::buildProject();
    LightStrip cove = stripAlong(LightStripKind::Cove, {{0.5, 0.5}, {3.5, 0.5}, {3.5, 3.5}}, 2.9, 10.0);
    cove.id = "ls1";
    project.plan.floors[0].lightStrips = {cove};
    LightMarker marker;
    marker.id = "l1";
    marker.x = 2.0;
    marker.y = 2.0;
    marker.z = 2.9;
    project.plan.floors[0].lights = {marker};
    const BuildPlan plan = makeBuildPlan(project, test::sampleAssetSource(), test::sampleMaterialSource());

    std::vector<GameplayLight> lights = planLights(project.plan);
    for (GameplayLight& strip : planStripLights(project.plan, plan.stripLights)) lights.push_back(std::move(strip));
    REQUIRE(lights.size() == 3);
    const Json json = gameplayToJson(gameplayData("Flat", project.plan, plan.floors, lights));
    CHECK(json["lights"][0]["kind"] == "ceiling");
    CHECK_FALSE(json["lights"][0].contains("start"));
    CHECK(json["lights"][1]["kind"] == "strip");
    CHECK(test::schemaProblem("gameplay.schema.json", nlohmann::json::parse(json.dump())) == "");

    // A strip without its facing, or a marker with strip fields, is refused.
    Json noFacing = json;
    noFacing["lights"][1].erase("facing");
    CHECK(test::schemaProblem("gameplay.schema.json", nlohmann::json::parse(noFacing.dump())) != "");
    Json markerWithEnds = json;
    markerWithEnds["lights"][0]["start"] = Json::array({0.0, 0.0, 0.0});
    CHECK(test::schemaProblem("gameplay.schema.json", nlohmann::json::parse(markerWithEnds.dump())) != "");
}

TEST_CASE("stepNose finds the edge the walk line enters a step by", "[lightstrips][stairs]")
{
    const std::vector<Vec2> square{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    // Climbing toward +y from the front edge: the nose is the edge at y = 0, facing out toward -y.
    const std::optional<StepNose> front = stepNose(square, {{0.5, 0.0}, {0.5, 3.0}});
    REQUIRE(front.has_value());
    CHECK(front->from.y == 0.0);
    CHECK(front->to.y == 0.0);
    CHECK_THAT(front->out.x, WithinAbs(0.0, 1e-12));
    CHECK_THAT(front->out.y, WithinAbs(-1.0, 1e-12));

    // Coming in from the side after a turn: the nose is the side edge at x = 0.
    const std::optional<StepNose> side = stepNose(square, {{-2.0, -1.0}, {-2.0, 0.5}, {3.0, 0.5}});
    REQUIRE(side.has_value());
    CHECK(side->from.x == 0.0);
    CHECK(side->to.x == 0.0);
    CHECK_THAT(side->out.x, WithinAbs(-1.0, 1e-12));

    CHECK_FALSE(stepNose(square, {{2.0, 0.0}, {2.0, 3.0}}).has_value());
    CHECK_FALSE(stepNose({{0.0, 0.0}, {1.0, 0.0}}, {{0.5, -1.0}, {0.5, 1.0}}).has_value());

    // Every step of a straight stair has its front edge as its nose.
    const Project p = stairProject();
    const StairLayout layout = stairLayout(p.plan, p.plan.floors[0], p.plan.floors[0].stairs[0]);
    REQUIRE(layout.status.ok());
    for (std::size_t i = 0; i < layout.steps.size(); ++i) {
        const std::optional<StepNose> nose = stepNose(layout.steps[i].outline, layout.walkLine);
        REQUIRE(nose.has_value());
        CHECK_THAT(nose->from.y, WithinAbs(1.0 + static_cast<double>(i) * kStairGoing, 1e-9));
        CHECK_THAT(nose->to.y, WithinAbs(nose->from.y, 1e-9));
        CHECK_THAT(nose->out.y, WithinAbs(-1.0, 1e-12));
    }
}
