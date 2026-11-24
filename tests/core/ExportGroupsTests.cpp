#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Collision.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/Exporter.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::TempDir;

namespace {

namespace fs = std::filesystem;

// An item in a room (or in none).
Item itemIn(const std::string& id, const std::string& asset, const std::string& room)
{
    Item item;
    item.id = id;
    item.asset = asset;
    if (!room.empty()) item.room = room;
    item.x = 1.0;
    item.y = 1.0;
    item.size = {1.0, 1.0, 1.0};
    return item;
}

// The plan of milestone 9: living room r1 and bedroom r2 split by the inner wall w7 with door o1, window o2 in
// an outside wall, items i1 in r1, i2 in r2 and i3 in no room. The project is called Flat.
Project flatProject()
{
    Project project = plynth::test::splitHouseProject();
    project.settings.name = "Flat";
    Floor& floor = project.plan.floors[0];
    Room r1;
    r1.id = "r1";
    r1.type = "living";
    r1.walls = {"w1", "w7", "w5", "w6"};
    Room r2;
    r2.id = "r2";
    r2.type = "bedroom";
    r2.walls = {"w2", "w3", "w4", "w7"};
    floor.rooms = {r1, r2};

    Opening door;
    door.id = "o1";
    door.wall = "w7";
    door.offset = 2.0;
    Opening window;
    window.id = "o2";
    window.wall = "w1";
    window.offset = 1.5;
    window.type = OpeningType::Window;
    window.sill = 0.9;
    window.height = 1.2;
    floor.openings = {door, window};

    floor.items = {itemIn("i1", "sofa", "r1"), itemIn("i2", "bed", "r2"), itemIn("i3", "lamp", "")};
    return project;
}

// A group by id. Null when missing.
const ExportGroup* group(const std::vector<ExportGroup>& groups, const std::string& id)
{
    for (const ExportGroup& g : groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

// The ids of the groups, in order.
std::vector<std::string> groupIds(const std::vector<ExportGroup>& groups)
{
    std::vector<std::string> ids;
    for (const ExportGroup& g : groups) ids.push_back(g.id);
    return ids;
}

// Says whether a group holds the exact member.
bool holds(const ExportGroup& g, const std::string& kind, const std::string& id)
{
    return std::find(g.members.begin(), g.members.end(), ExportMember{kind, id}) != g.members.end();
}

// The automatic groups of a project with their file names, as the exporter gives them.
std::vector<ExportGroup> automatic(const Project& project)
{
    std::vector<ExportGroup> groups = automaticExportGroups(project);
    nameExportGroupFiles(groups, project.settings.name);
    return groups;
}

// The groups after no edits, which is the list the edit functions take as current.
std::vector<ExportGroup> currentOf(const Project& project, const ExportGroupEdits& edits)
{
    return applyExportGroupEdits(automatic(project), edits, project.settings.name).groups;
}

// Writes build/built-plan.json for a plan.
void writeBuiltPlan(const fs::path& folder, const Plan& plan)
{
    fs::create_directories(folder / "build");
    Json built = Json::object();
    built["plan_fingerprint"] = "abc";
    built["plan"] = planToJson(plan);
    std::ofstream out(folder / "build" / "built-plan.json", std::ios::binary);
    out << built.dump(2);
}

// A rule for the registry tests: it only does anything for the project called "Rule Test", so the groups of
// every other test stay as they are.
class TestRule final : public ExportGroupRule {
public:
    std::string_view id() const override { return "test_rule"; }
    int order() const override { return 1000; }
    void addGroups(const Project& project, std::vector<ExportGroup>& out) const override
    {
        if (project.settings.name != "Rule Test") return;
        ExportGroup stolen;
        stolen.id = "stolen";
        stolen.name = "Stolen";
        stolen.members = {{"walls", "f1"}};
        ExportGroup extra;
        extra.id = "extra";
        extra.name = "Extra";
        extra.members = {{"walls", "f1"}, {"custom", "x1"}};
        out.push_back(stolen);
        out.push_back(extra);
    }
};

}

TEST_CASE("the automatic groups of a project", "[core][exportgroups]") {
    const Project project = flatProject();
    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    REQUIRE(groupIds(groups) == std::vector<std::string>{"structure", "surfaces", "furniture_r1", "furniture_r2", "furniture"});

    const ExportGroup& structure = groups[0];
    CHECK(structure.name == "Walls");
    CHECK(structure.members == std::vector<ExportMember>{{"walls", "f1"}, {"trim", "f1"}, {"door", "o1"}, {"window", "o2"}});

    const ExportGroup& surfaces = groups[1];
    CHECK(surfaces.name == "Floors");
    CHECK(surfaces.members == std::vector<ExportMember>{{"floor", "r1"}, {"ceiling", "r1"}, {"floor", "r2"}, {"ceiling", "r2"},
                                                        {"thresholds", "f1"}, {"slab", ""}, {"stair", ""}, {"roof", ""}, {"light", ""},
                                                        {"railing", ""}, {"guard", ""}});

    CHECK(groups[2].name == "Furniture Living");
    CHECK(groups[2].members == std::vector<ExportMember>{{"item", "i1"}, {"dressing", "r1"}});
    CHECK(groups[3].name == "Furniture Bedroom");
    CHECK(groups[3].members == std::vector<ExportMember>{{"item", "i2"}, {"dressing", "r2"}});
    CHECK(groups[4].name == "Furniture");
    CHECK(groups[4].members == std::vector<ExportMember>{{"item", "i3"}});

    std::set<ExportMember> seen;
    int rests = 0;
    for (const ExportGroup& g : groups) {
        for (const ExportMember& m : g.members) CHECK(seen.insert(m).second);
        if (g.rest) ++rests;
    }
    CHECK(rests == 1);
    CHECK(structure.rest);
}

TEST_CASE("furniture groups follow the rooms that hold items", "[core][exportgroups]") {
    Project project = flatProject();
    Floor& floor = project.plan.floors[0];
    Room r3;
    r3.id = "r3";
    r3.type = "bedroom";
    Room r4;
    r4.id = "r4";
    r4.type = "kitchen";
    floor.rooms.push_back(r3);
    floor.rooms.push_back(r4);
    floor.items.push_back(itemIn("i4", "bed", "r3"));

    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    REQUIRE(group(groups, "furniture_r2"));
    CHECK(group(groups, "furniture_r2")->name == "Furniture Bedroom r2");
    REQUIRE(group(groups, "furniture_r3"));
    CHECK(group(groups, "furniture_r3")->name == "Furniture Bedroom r3");
    CHECK(group(groups, "furniture_r4") == nullptr);
    CHECK(group(groups, "furniture_r1")->name == "Furniture Living");
}

TEST_CASE("a floor without a door at sill zero has no thresholds member", "[core][exportgroups]") {
    Project project = flatProject();
    project.plan.floors[0].openings[0].sill = 0.5;
    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    CHECK_FALSE(holds(*group(groups, "surfaces"), "thresholds", "f1"));

    project.plan.floors[0].openings[0].sill = 0.0;
    project.plan.floors[0].openings[0].type = OpeningType::Plain;
    CHECK(holds(*group(automaticExportGroups(project), "surfaces"), "thresholds", "f1"));
}

TEST_CASE("a plan without walls has no walls member but keeps the rest group", "[core][exportgroups]") {
    Project project = makeNewProject();
    project.settings.name = "Empty";
    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    REQUIRE(group(groups, "structure"));
    CHECK(group(groups, "structure")->members.empty());
    CHECK(group(groups, "structure")->rest);
    CHECK(group(groups, "furniture") == nullptr);
}

TEST_CASE("group files are named from the project and the group", "[core][exportgroups]") {
    std::vector<ExportGroup> groups = automatic(flatProject());
    CHECK(group(groups, "structure")->file == "Flat_Walls.fbx");
    CHECK(group(groups, "surfaces")->file == "Flat_Floors.fbx");
    CHECK(group(groups, "furniture_r1")->file == "Flat_Furniture_Living.fbx");

    std::vector<ExportGroup> props(2);
    props[0].id = "a";
    props[0].name = "Props";
    props[1].id = "b";
    props[1].name = "props";
    nameExportGroupFiles(props, "Flat");
    CHECK(props[0].file == "Flat_Props.fbx");
    CHECK(props[1].file == "Flat_props_2.fbx");
}

TEST_CASE("group rules register once and give their groups after the built in ones", "[core][exportgroups]") {
    static const bool registered = registerExportGroupRule(std::make_unique<TestRule>());
    REQUIRE(registered);
    CHECK_FALSE(registerExportGroupRule(std::make_unique<TestRule>()));

    class BadIdRule final : public ExportGroupRule {
    public:
        std::string_view id() const override { return "Bad Id"; }
        int order() const override { return 5; }
        void addGroups(const Project&, std::vector<ExportGroup>&) const override {}
    };
    CHECK_FALSE(registerExportGroupRule(std::make_unique<BadIdRule>()));
    CHECK_FALSE(registerExportGroupRule(nullptr));

    std::vector<std::string> ids;
    for (const ExportGroupRule* rule : allExportGroupRules()) ids.emplace_back(rule->id());
    REQUIRE(ids.size() >= 4);
    CHECK(ids[0] == "structure");
    CHECK(ids[1] == "surfaces");
    CHECK(ids[2] == "furniture");
    CHECK(ids.back() == "test_rule");

    Project project = flatProject();
    project.settings.name = "Rule Test";
    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    CHECK(group(groups, "stolen") == nullptr);
    REQUIRE(group(groups, "extra"));
    CHECK(group(groups, "extra")->members == std::vector<ExportMember>{{"custom", "x1"}});
    CHECK(holds(*group(groups, "structure"), "walls", "f1"));
}

TEST_CASE("edits rename groups and the file follows", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    edits.renames = {{"furniture_r1", "Lounge props"}};
    const ExportGroupsApplied applied = applyExportGroupEdits(automatic(project), edits, "Flat");
    CHECK(applied.notes.empty());
    CHECK(group(applied.groups, "furniture_r1")->name == "Lounge props");
    CHECK(group(applied.groups, "furniture_r1")->file == "Flat_Lounge_props.fbx");
}

TEST_CASE("edits merge groups", "[core][exportgroups]") {
    const Project project = flatProject();
    const std::vector<ExportGroup> base = automatic(project);

    ExportGroupEdits edits;
    edits.merges = {{"furniture_r2", "furniture_r1"}};
    ExportGroupsApplied applied = applyExportGroupEdits(base, edits, "Flat");
    CHECK(group(applied.groups, "furniture_r2") == nullptr);
    CHECK(holds(*group(applied.groups, "furniture_r1"), "item", "i1"));
    CHECK(holds(*group(applied.groups, "furniture_r1"), "item", "i2"));
    CHECK(applied.notes.empty());

    edits.merges = {{"furniture_r1", "furniture_r2"}, {"furniture_r2", "furniture"}};
    applied = applyExportGroupEdits(base, edits, "Flat");
    CHECK(group(applied.groups, "furniture_r1") == nullptr);
    CHECK(group(applied.groups, "furniture_r2") == nullptr);
    CHECK(holds(*group(applied.groups, "furniture"), "item", "i1"));
    CHECK(holds(*group(applied.groups, "furniture"), "item", "i2"));
    CHECK(holds(*group(applied.groups, "furniture"), "item", "i3"));

    edits.merges = {{"structure", "surfaces"}};
    applied = applyExportGroupEdits(base, edits, "Flat");
    CHECK(group(applied.groups, "structure") == nullptr);
    CHECK(group(applied.groups, "surfaces")->rest);
    CHECK(holds(*group(applied.groups, "surfaces"), "walls", "f1"));
    CHECK(std::count_if(applied.groups.begin(), applied.groups.end(), [](const ExportGroup& g) { return g.rest; }) == 1);
}

TEST_CASE("edits move members between groups", "[core][exportgroups]") {
    const Project project = flatProject();
    const std::vector<ExportGroup> base = automatic(project);

    ExportGroupEdits edits;
    edits.moves = {{{"item", "i1"}, "structure"}};
    ExportGroupsApplied applied = applyExportGroupEdits(base, edits, "Flat");
    CHECK(holds(*group(applied.groups, "structure"), "item", "i1"));
    CHECK_FALSE(holds(*group(applied.groups, "furniture_r1"), "item", "i1"));
    // The room's set dressing stays with the room.
    CHECK(group(applied.groups, "furniture_r1")->members == std::vector<ExportMember>{{"dressing", "r1"}});

    edits = {};
    edits.added = {{"group_1", "Hero"}};
    edits.moves = {{{"item", "i1"}, "group_1"}};
    applied = applyExportGroupEdits(base, edits, "Flat");
    REQUIRE(group(applied.groups, "group_1"));
    CHECK(group(applied.groups, "group_1")->members == std::vector<ExportMember>{{"item", "i1"}});
    CHECK(group(applied.groups, "group_1")->file == "Flat_Hero.fbx");
    CHECK(groupIds(applied.groups).back() == "group_1");

    edits = {};
    edits.moves = {{{"slab", ""}, "structure"}};
    applied = applyExportGroupEdits(base, edits, "Flat");
    CHECK(holds(*group(applied.groups, "structure"), "slab", ""));
    CHECK_FALSE(holds(*group(applied.groups, "surfaces"), "slab", ""));
}

TEST_CASE("merges apply before moves whatever order the edits were made in", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    edits.moves = {{{"item", "i1"}, "furniture_r2"}};
    edits.merges = {{"furniture_r2", "furniture"}};
    const ExportGroupsApplied applied = applyExportGroupEdits(automatic(project), edits, "Flat");
    CHECK(applied.notes.empty());
    CHECK(group(applied.groups, "furniture_r2") == nullptr);
    CHECK(holds(*group(applied.groups, "furniture"), "item", "i1"));
    CHECK_FALSE(holds(*group(applied.groups, "furniture_r1"), "item", "i1"));
}

TEST_CASE("edits that no longer apply are skipped with one note each", "[core][exportgroups]") {
    const Project project = flatProject();
    const std::vector<ExportGroup> base = automatic(project);
    const std::vector<ExportGroup> untouched = applyExportGroupEdits(base, {}, "Flat").groups;

    ExportGroupEdits edits;
    edits.renames = {{"furniture_r9", "Nothing"}};
    edits.merges = {{"furniture_r8", "structure"}};
    edits.moves = {{{"item", "i9"}, "structure"}};
    const ExportGroupsApplied applied = applyExportGroupEdits(base, edits, "Flat");
    CHECK(applied.notes.size() == 3);
    CHECK(applied.groups == untouched);

    ExportGroupEdits toGone;
    toGone.moves = {{{"item", "i1"}, "group_7"}};
    CHECK(applyExportGroupEdits(base, toGone, "Flat").notes.size() == 1);
}

TEST_CASE("a group emptied by moves stays in the list", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    edits.moves = {{{"item", "i1"}, "structure"}};
    const ExportGroupsApplied applied = applyExportGroupEdits(automatic(project), edits, "Flat");
    REQUIRE(group(applied.groups, "furniture_r1"));
    CHECK(group(applied.groups, "furniture_r1")->members == std::vector<ExportMember>{{"dressing", "r1"}});
}

TEST_CASE("renaming refuses bad names and names in use", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    REQUIRE(renameExportGroup(edits, currentOf(project, edits), "furniture_r2", "Living Furniture").ok());
    const ExportGroupEdits before = edits;
    const std::vector<ExportGroup> current = currentOf(project, edits);

    CHECK(renameExportGroup(edits, current, "furniture_r1", "living furniture").code == ErrorCode::AlreadyExists);
    CHECK(renameExportGroup(edits, current, "furniture_r1", "").code == ErrorCode::InvalidValue);
    CHECK(renameExportGroup(edits, current, "furniture_r1", "   ").code == ErrorCode::InvalidValue);
    CHECK(renameExportGroup(edits, current, "furniture_r1", "---").code == ErrorCode::InvalidValue);
    CHECK(renameExportGroup(edits, current, "furniture_r1", std::string(65, 'a')).code == ErrorCode::InvalidValue);
    CHECK(renameExportGroup(edits, current, "furniture_r9", "Fine").code == ErrorCode::NotFound);
    CHECK(edits == before);

    CHECK(renameExportGroup(edits, current, "furniture_r1", std::string(64, 'a')).ok());
    REQUIRE(renameExportGroup(edits, current, "furniture_r1", "  Hero  ").ok());
    REQUIRE(edits.renames.size() == 2);
    CHECK(edits.renames[1].name == "Hero");

    // A group may change the case of its own name.
    CHECK(renameExportGroup(edits, current, "furniture_r2", "living furniture").ok());
}

TEST_CASE("merging refuses a group into itself and unknown groups", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    const std::vector<ExportGroup> current = currentOf(project, edits);
    CHECK(mergeExportGroups(edits, current, "furniture_r1", "furniture_r1").code == ErrorCode::InvalidValue);
    CHECK(mergeExportGroups(edits, current, "nothing", "structure").code == ErrorCode::NotFound);
    CHECK(mergeExportGroups(edits, current, "structure", "nothing").code == ErrorCode::NotFound);
    CHECK(edits.empty());

    REQUIRE(renameExportGroup(edits, current, "furniture_r2", "Bedroom things").ok());
    REQUIRE(mergeExportGroups(edits, current, "furniture_r2", "furniture_r1").ok());
    CHECK(edits.merges.size() == 1);
    CHECK(edits.renames.empty());
}

TEST_CASE("moving refuses unknown members and groups and replaces an earlier move", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    std::vector<ExportGroup> current = currentOf(project, edits);
    CHECK(moveExportMember(edits, current, {"item", "i9"}, "structure").code == ErrorCode::NotFound);
    CHECK(moveExportMember(edits, current, {"item", "i1"}, "nothing").code == ErrorCode::NotFound);
    CHECK(edits.empty());

    CHECK(moveExportMember(edits, current, {"item", "i1"}, "furniture_r1").ok());
    CHECK(edits.empty());

    REQUIRE(moveExportMember(edits, current, {"item", "i1"}, "structure").ok());
    current = currentOf(project, edits);
    REQUIRE(moveExportMember(edits, current, {"item", "i1"}, "surfaces").ok());
    REQUIRE(edits.moves.size() == 1);
    CHECK(edits.moves[0].to == "surfaces");

    REQUIRE(moveExportMember(edits, current, {"slab", ""}, "structure").ok());
    CHECK(edits.moves.size() == 2);
}

TEST_CASE("added groups get the next free id", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    std::string id;
    REQUIRE(addExportGroup(edits, currentOf(project, edits), "Hero", id).ok());
    CHECK(id == "group_1");
    REQUIRE(addExportGroup(edits, currentOf(project, edits), "Props", id).ok());
    CHECK(id == "group_2");

    const ExportGroupEdits before = edits;
    CHECK(addExportGroup(edits, currentOf(project, edits), "hero", id).code == ErrorCode::AlreadyExists);
    CHECK(addExportGroup(edits, currentOf(project, edits), "***", id).code == ErrorCode::InvalidValue);
    CHECK(edits == before);
}

TEST_CASE("members have plain labels", "[core][exportgroups]") {
    const Project project = flatProject();
    const Plan& plan = project.plan;
    CHECK(exportMemberLabel(plan, {"walls", "f1"}) == "Walls, Floor 1");
    CHECK(exportMemberLabel(plan, {"thresholds", "f1"}) == "Thresholds, Floor 1");
    CHECK(exportMemberLabel(plan, {"floor", "r1"}) == "Floor of Living r1");
    CHECK(exportMemberLabel(plan, {"ceiling", "r2"}) == "Ceiling of Bedroom r2");
    CHECK(exportMemberLabel(plan, {"door", "o1"}) == "Door o1");
    CHECK(exportMemberLabel(plan, {"window", "o2"}) == "Window o2");
    CHECK(exportMemberLabel(plan, {"item", "i1"}) == "sofa i1");
    CHECK(exportMemberLabel(plan, {"generator", "i2"}) == "bed i2");
    CHECK(exportMemberLabel(plan, {"slab", ""}) == "Every slab");
    CHECK(exportMemberLabel(plan, {"stair", "s1"}) == "stair s1");
    CHECK(exportMemberLabel(plan, {"floor", "r9"}) == "floor r9");
    CHECK(exportMemberLabel(plan, {"trim", "f1"}) == "Trim, Floor 1");
    CHECK(exportMemberLabel(plan, {"railing", "s1"}) == "Railing of stair s1");
    CHECK(exportMemberLabel(plan, {"guard", "s1"}) == "Guard of stair s1");
    CHECK(exportMemberLabel(plan, {"dressing", "r1"}) == "Set dressing of Living r1");
    CHECK(exportMemberLabel(plan, {"dressing", "r9"}) == "dressing r9");
    CHECK(exportMemberLabel(plan, {"trim", ""}) == "Every trim");
}

TEST_CASE("a generator item goes to its room's furniture group, labelled by its generator", "[core][exportgroups][generator]") {
    Project project = flatProject();
    project.settings.exportMode = ExportMode::Grouped;
    Item column = itemIn("i4", "", "r1");
    column.generator = GeneratorUse{"column", {{"diameter", GeneratorValue{0.3}}}};
    project.plan.floors[0].items.push_back(column);

    const std::vector<ExportGroup> groups = automaticExportGroups(project);
    REQUIRE(group(groups, "furniture_r1"));
    CHECK(group(groups, "furniture_r1")->members ==
          std::vector<ExportMember>{{"item", "i1"}, {"generator", "i4"}, {"dressing", "r1"}});
    for (const ExportGroup& g : groups) CHECK_FALSE(holds(g, "item", "i4"));
    CHECK(exportMemberLabel(project.plan, {"generator", "i4"}) == "column i4");
    CHECK(exportMemberLabel(project.plan, {"item", "i1"}) == "sofa i1");
}

TEST_CASE("the grouped exporter is reviewable and gives named files", "[core][exportgroups]") {
    const Exporter* exporter = findExporter("grouped");
    REQUIRE(exporter);
    CHECK(exporter->reviewable());
    CHECK(exporter->name() == "Grouped FBX");
    CHECK_FALSE(findExporter("single")->reviewable());

    std::vector<ExportGroup> groups;
    REQUIRE(exporter->groups(flatProject(), groups).ok());
    REQUIRE(groups.size() == 5);
    CHECK(groups[0].file == "Flat_Walls.fbx");
}

TEST_CASE("the review of a single project has the one building group", "[core][exportgroups]") {
    const TempDir dir("review-single");
    Project project = flatProject();
    project.settings.exportMode = ExportMode::Single;
    const ExportGroupReview review = reviewExportGroups(dir.path(), project);
    REQUIRE(review.status.ok());
    CHECK(review.exporter == "single");
    CHECK_FALSE(review.reviewable);
    CHECK_FALSE(review.fromBuild);
    REQUIRE(review.groups.size() == 1);
    CHECK(review.groups[0].id == "building");
    CHECK(review.groups[0].file == "Flat.fbx");
}

TEST_CASE("the review uses the built plan when there is one", "[core][exportgroups]") {
    const TempDir dir("review-built");
    Project built = flatProject();
    built.settings.exportMode = ExportMode::Grouped;
    writeBuiltPlan(dir.path(), built.plan);

    Project project = built;
    Room r3;
    r3.id = "r3";
    r3.type = "kitchen";
    project.plan.floors[0].rooms.push_back(r3);
    project.plan.floors[0].items.push_back(itemIn("i4", "table", "r3"));

    ExportGroupReview review = reviewExportGroups(dir.path(), project);
    REQUIRE(review.status.ok());
    CHECK(review.exporter == "grouped");
    CHECK(review.reviewable);
    CHECK(review.fromBuild);
    CHECK(group(review.groups, "furniture_r3") == nullptr);
    CHECK(group(review.groups, "furniture_r1") != nullptr);
    CHECK(review.groups == review.automatic);

    const TempDir noBuild("review-nobuild");
    review = reviewExportGroups(noBuild.path(), project);
    REQUIRE(review.status.ok());
    CHECK_FALSE(review.fromBuild);
    CHECK(group(review.groups, "furniture_r3") != nullptr);
}

TEST_CASE("the review applies the saved edits and reports the skipped ones", "[core][exportgroups]") {
    const TempDir dir("review-edits");
    Project project = flatProject();
    project.settings.exportMode = ExportMode::Grouped;
    ExportOptions options;
    options.groups.renames = {{"furniture_r1", "Lounge props"}, {"furniture_r7", "Gone"}};
    options.kit.enabled = true;
    plynth::test::requireOk(writeExportOptions(dir.path(), options));

    const ExportGroupReview review = reviewExportGroups(dir.path(), project);
    REQUIRE(review.status.ok());
    CHECK(group(review.groups, "furniture_r1")->name == "Lounge props");
    CHECK(group(review.automatic, "furniture_r1")->name == "Furniture Living");
    CHECK(review.notes.size() == 1);
    CHECK(review.options == options);
}

TEST_CASE("the review refuses a bad options file", "[core][exportgroups]") {
    const TempDir dir("review-bad");
    {
        std::ofstream out(dir.path() / "export-options.json", std::ios::binary);
        out << "{\"format\": \"plynth-export-options\", \"version\": 1, \"extra\": 1}";
    }
    Project project = flatProject();
    project.settings.exportMode = ExportMode::Grouped;
    const ExportGroupReview review = reviewExportGroups(dir.path(), project);
    CHECK(review.status.code == ErrorCode::BadFile);
    CHECK(review.groups.empty());
}

TEST_CASE("group names are English only", "[core][exportgroups]") {
    const Project project = flatProject();
    ExportGroupEdits edits;
    const std::vector<ExportGroup> current = currentOf(project, edits);
    CHECK(renameExportGroup(edits, current, "furniture_r1", "Kitchen-2_a b").ok());
    CHECK(renameExportGroup(edits, current, "furniture_r2", std::string(64, 'a')).ok());
    for (const std::string bad : {std::string("K\xC3\xBC" "che"), std::string("\xE0\xA4\xB0\xE0\xA4\xB8\xE0\xA5\x8B\xE0\xA4\x88 2"),
                                  std::string("Tab\there"), std::string("a.b"), std::string("a/b"), std::string(65, 'a')}) {
        const ExportGroupEdits before = edits;
        const Status status = renameExportGroup(edits, current, "furniture_r1", bad);
        CHECK(status.code == ErrorCode::InvalidValue);
        CHECK(status.message.find("A to Z") != std::string::npos);
        CHECK(status.message.find("hyphens") != std::string::npos);
        CHECK(edits == before);
    }
    std::string id;
    CHECK(addExportGroup(edits, current, "K\xC3\xBC" "che", id).code == ErrorCode::InvalidValue);
}

TEST_CASE("automatic group names stay valid and unique for non English room types", "[core][exportgroups]") {
    Project project = flatProject();
    Floor& floor = project.plan.floors[0];
    floor.rooms[0].type = "k\xC3\xBC" "che";
    floor.rooms[1].type = "\xE0\xA4\xB0\xE0\xA4\xB8\xE0\xA5\x8B\xE0\xA4\x88";
    Room r3;
    r3.id = "r3";
    r3.type = "\xE0\xA4\xB0\xE0\xA4\xB8\xE0\xA5\x8B\xE0\xA4\x88";
    floor.rooms.push_back(r3);
    floor.items.push_back(itemIn("i4", "bed", "r3"));
    Room r4;
    r4.id = "r4";
    r4.type = std::string(100, 'x');
    floor.rooms.push_back(r4);
    floor.items.push_back(itemIn("i5", "bed", "r4"));

    const std::vector<ExportGroup> groups = automatic(project);
    std::set<std::string> names;
    std::set<std::string> files;
    for (const ExportGroup& g : groups) {
        ExportGroupEdits edits;
        std::string newId;
        // A name is valid when the edit functions take it as a rename to itself.
        CHECK(renameExportGroup(edits, groups, g.id, g.name).ok());
        CHECK(g.name.size() <= kMaxExportGroupName);
        CHECK(names.insert(g.name).second);
        CHECK(files.insert(g.file).second);
    }
    CHECK(group(groups, "furniture_r1")->name == "Furniture Kche");
    CHECK(group(groups, "furniture_r2")->name == "Furniture Room r2");
    CHECK(group(groups, "furniture_r3")->name == "Furniture Room r3");
}

TEST_CASE("an options file saved with a non English name is refused and says why", "[core][exportgroups]") {
    const TempDir dir("options-old-name");
    {
        std::ofstream out(dir.path() / "export-options.json", std::ios::binary);
        out << "{\"format\": \"plynth-export-options\", \"version\": 1, \"groups\": {\"renames\": "
               "[{\"group\": \"furniture_r1\", \"name\": \"K\xC3\xBC" "che\"}]}, \"kit\": {\"enabled\": true}}";
    }
    const ExportOptionsRead read = readExportOptions(dir.path());
    CHECK(read.status.code == ErrorCode::BadFile);
    CHECK(read.status.where == "/groups/renames/0/name");
    CHECK(read.status.message.find("A to Z") != std::string::npos);
    CHECK(read.status.message.find("Fix this name") != std::string::npos);

    // The export and the review refuse too, so nothing is dropped without the user knowing.
    Project project = flatProject();
    project.settings.exportMode = ExportMode::Grouped;
    CHECK(reviewExportGroups(dir.path(), project).status.code == ErrorCode::BadFile);
}

TEST_CASE("an item is one member, never an asset and a generator", "[core][exportgroups]") {
    const std::vector<ExportGroup> groups = automaticExportGroups(flatProject());
    for (const ExportGroup& g : groups) {
        for (const ExportMember& m : g.members) CHECK(m.kind != "generator");
    }
    CHECK(group(groups, "furniture_r1")->members.size() == 2);
}

TEST_CASE("the kit file name is never given to a group", "[core][exportgroups]") {
    std::vector<ExportGroup> groups(3);
    groups[0].id = "a";
    groups[0].name = "Kit";
    groups[1].id = "b";
    groups[1].name = "kit";
    groups[2].id = "c";
    groups[2].name = "Walls";
    nameExportGroupFiles(groups, "Flat");
    CHECK(groups[0].file == "Flat_Kit_2.fbx");
    CHECK(groups[1].file == "Flat_kit_3.fbx");
    CHECK(groups[2].file == "Flat_Walls.fbx");
}

TEST_CASE("the review carries the plan its member labels come from", "[core][exportgroups]") {
    const TempDir dir("review-labels");
    Project built = flatProject();
    built.settings.exportMode = ExportMode::Grouped;
    writeBuiltPlan(dir.path(), built.plan);

    Project project = built;
    project.plan.floors[0].rooms[0].type = "kitchen";
    project.plan.floors[0].name = "Ground";

    const ExportMember floorOfR1{"floor", "r1"};
    const ExportMember walls{"walls", "f1"};
    ExportGroupReview review = reviewExportGroups(dir.path(), project);
    REQUIRE(review.status.ok());
    CHECK(review.fromBuild);
    CHECK(exportMemberLabel(review.plan, floorOfR1) == "Floor of Living r1");
    CHECK(exportMemberLabel(review.plan, walls) == "Walls, Floor 1");

    const TempDir noBuild("review-labels-nobuild");
    review = reviewExportGroups(noBuild.path(), project);
    REQUIRE(review.status.ok());
    CHECK_FALSE(review.fromBuild);
    CHECK(exportMemberLabel(review.plan, floorOfR1) == "Floor of Kitchen r1");
    CHECK(exportMemberLabel(review.plan, walls) == "Walls, Ground");
}

namespace {

// A house of two floors with a room on each and a stair between them. The upper floor is smaller, so both floors
// have a roof.
Project houseProject()
{
    Project project = makeNewProject();
    Floor& ground = project.plan.floors[0];
    plynth::test::addRectangle(ground, 1, 0.0, 0.0, 8.0, 6.0, 0.2);
    Room lower;
    lower.id = "r1";
    lower.walls = {"w11", "w12", "w13", "w14"};
    ground.rooms.push_back(lower);
    Stair stair;
    stair.id = "s1";
    stair.type = "u_shape";
    stair.x = 2.0;
    stair.y = 1.0;
    ground.stairs.push_back(stair);

    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    plynth::test::addRectangle(upper, 2, 0.0, 0.0, 5.0, 6.0, 0.2);
    Room above;
    above.id = "r2";
    above.walls = {"w21", "w22", "w23", "w24"};
    upper.rooms.push_back(above);
    project.plan.floors.push_back(upper);
    return project;
}

// The groups that list an object, by its exact member or by the member of its whole kind.
std::vector<std::string> groupsListing(const std::vector<ExportGroup>& groups, const std::string& kind, const std::string& id)
{
    std::vector<std::string> found;
    for (const ExportGroup& g : groups) {
        if (holds(g, kind, id) || holds(g, kind, "")) found.push_back(g.id);
    }
    return found;
}

}

TEST_CASE("every slab, stair, roof and collision target of a house is in exactly one group", "[core][exportgroups]") {
    const Project house = houseProject();
    const BuildPlan plan = makeBuildPlan(house, BuildAssetSource{});
    const std::vector<ExportGroup> groups = automaticExportGroups(house);
    const std::vector<std::string> surfaces{"surfaces"};

    int slabs = 0;
    int stairs = 0;
    int roofs = 0;
    for (const FloorBuild& floor : plan.floors) {
        if (!floor.slab.polygons.empty()) {
            ++slabs;
            CHECK(groupsListing(groups, "slab", floor.id) == surfaces);
        }
        for (const StairBuild& stair : floor.stairs) {
            ++stairs;
            CHECK(groupsListing(groups, "stair", stair.id) == surfaces);
        }
        if (!floor.roofs.empty()) {
            ++roofs;
            CHECK(groupsListing(groups, "roof", floor.id) == surfaces);
        }
    }
    CHECK(slabs == 2);
    CHECK(stairs == 1);
    CHECK(roofs == 2);

    // A UCX_ mesh follows its target, so each target needs its one group.
    std::set<std::string> kinds;
    for (const std::vector<CollisionShape>& shapes : plan.collision) {
        for (const CollisionShape& shape : shapes) {
            const std::string kind(collisionTargetKindName(shape.kind));
            INFO(kind << " " << shape.id);
            kinds.insert(kind);
            CHECK(groupsListing(groups, kind, shape.id).size() == 1);
        }
    }
    for (const char* kind : {"slab", "stair", "roof"}) CHECK(kinds.count(kind) == 1);
}
