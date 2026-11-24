#include "BuildFixtures.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/GeneratorBuild.h>
#include <plynth/core/Materials.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

using ValueChanges = std::vector<std::pair<std::string, GeneratorValue>>;

// A made-up user and a folder of generators, all in one temporary folder.
struct JobScratch {
    explicit JobScratch(const std::string& tag) : user(tag), folder(user.tmp.path() / "generators")
    {
        const fs::path root = user.tmp.path();
        fs::create_directories(folder);
        for (const char* name : {"local", "home", "xdg", "settings"}) fs::create_directories(root / name);
        set("LOCALAPPDATA", root / "local");
        set("HOME", root / "home");
        set("XDG_DATA_HOME", root / "xdg");
        set("PLYNTH_CONFIG_DIR", root / "settings");
        set("PLYNTH_BLENDER", root / "no-blender.exe");
        for (const char* name : {"column.py", "archimesh_column.py"}) {
            fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "generators" / name, folder / name);
        }
    }

    // Points one variable at a path of this test.
    void set(const char* name, const fs::path& value)
    {
        envs.push_back(std::make_unique<ScopedEnv>(name, pathToUtf8(value)));
    }

    // Writes a generator with the given parameters that needs no add-on.
    void add(const std::string& id, const std::string& params) const
    {
        std::ofstream(folder / (id + ".py"), std::ios::binary)
            << "\"\"\"plynth-generator\n"
            << R"({"format": "plynth-generator", "version": 1, "name": "Test part", "description": "A part for tests.", )"
            << R"("addons": [], "size": {"width": 0.4, "depth": 0.4, "height": 1.0}, "params": [)" << params << "]}\n"
            << "\"\"\"\n\ndef generate(gen):\n    return []\n";
    }

    // Reads the folder as a generator library.
    GeneratorLibrary load() const { return loadGeneratorFolder(folder); }

    IsolatedUser user;
    fs::path folder;
    std::vector<std::unique_ptr<ScopedEnv>> envs;
};

// The declaration of a material parameter that starts empty.
std::string materialParam(const std::string& id)
{
    return R"({"id": ")" + id + R"(", "label": "Material", "type": "material", "default": null})";
}

// The declaration of a number parameter.
std::string numberParam()
{
    return R"({"id": "amount", "label": "Amount", "type": "number", "default": 1.0})";
}

// A generator item with the default values and the given changes.
Item placedItem(const GeneratorLibrary& library, const std::string& id, const std::string& generator,
                const ValueChanges& changes = {}, double x = 1.0, double y = 1.0)
{
    const GeneratorInfo* info = library.find(generator);
    INFO(generator);
    REQUIRE(info != nullptr);
    std::vector<GeneratorParamValue> values = defaultGeneratorParams(*info, 3.0);
    for (const auto& [param, value] : changes) requireOk(setGeneratorParam(*info, values, param, value));
    const GeneratorPlacement placed = makeGeneratorUse(*info, values);
    requireOk(placed.status);
    Item item = buildItem(id, "", x, y, placed.size);
    item.generator = placed.use;
    return item;
}

// A copy of a generator item that names another generator.
Item renamedItem(Item item, const std::string& id, const std::string& generator)
{
    item.id = id;
    item.generator->name = generator;
    return item;
}

// The square room with no openings, no materials and the given items.
Project roomProject(std::vector<Item> items)
{
    Project project = buildProject();
    Floor& floor = project.plan.floors[0];
    floor.rooms.resize(1);
    floor.openings.clear();
    floor.items = std::move(items);
    return project;
}

// A second square floor with ids of its own and one bedroom.
Floor upperFloor(std::vector<Item> items)
{
    Floor floor = squareFloorWithWalls();
    floor.id = "f2";
    floor.name = "Floor 2";
    for (Point& point : floor.points) point.id = "p2" + point.id.substr(1);
    for (Wall& wall : floor.walls) {
        wall.id = "w2" + wall.id.substr(1);
        wall.from = "p2" + wall.from.substr(1);
        wall.to = "p2" + wall.to.substr(1);
    }
    Room room;
    room.id = "r21";
    room.type = "bedroom";
    room.walls = {"w21", "w22", "w23", "w24"};
    floor.rooms = {room};
    for (Item& item : items) item.room = Id("r21");
    floor.items = std::move(items);
    return floor;
}

// An assets source of a library that holds no assets.
BuildAssetSource emptyAssets()
{
    return [](std::string_view) -> std::optional<BuildAssetState> { return std::nullopt; };
}

// The build plan with a library of generators and the sample materials.
BuildPlan planWith(const Project& project, const GeneratorLibrary& library,
                   const std::optional<AddonCache>& cache = std::nullopt)
{
    return makeBuildPlan(project, emptyAssets(), sampleMaterialSource(), &library, cache);
}

// The job of a build plan, in a form the schema check reads.
nlohmann::json jobOf(const Project& project, const BuildPlan& plan)
{
    const Json job = makeBuildJob(project, plan, BuildSettings{}, "C:/plynth-test/Project", "build-1a2b3c4d",
                                  "C:/plynth-test/Project/build/result.json");
    return nlohmann::json::parse(job.dump());
}

// How many problems have this code.
std::size_t countCode(const std::vector<BuildProblem>& problems, const std::string& code)
{
    return static_cast<std::size_t>(
        std::count_if(problems.begin(), problems.end(), [&](const BuildProblem& p) { return p.code == code; }));
}

// The built generator item with this id.
GeneratorItemBuild builtItem(const BuildPlan& plan, const std::string& id)
{
    for (const GeneratorFloorBuild& floor : plan.generators.floors) {
        for (const GeneratorItemBuild& item : floor.items) {
            if (item.id == id) return item;
        }
    }
    INFO("No built item " << id);
    REQUIRE(false);
    return {};
}

// How many generator items the plan builds.
std::size_t builtCount(const BuildPlan& plan)
{
    std::size_t total = 0;
    for (const GeneratorFloorBuild& floor : plan.generators.floors) total += floor.items.size();
    return total;
}

// The slot of an item's only material parameter.
std::string onlySlot(const BuildPlan& plan, const std::string& id)
{
    const GeneratorItemBuild item = builtItem(plan, id);
    REQUIRE(item.materialSlots.size() == 1);
    return item.materialSlots[0].slot;
}

// Says whether the slot table has this name.
bool hasSlot(const BuildPlan& plan, const std::string& name)
{
    return std::any_of(plan.materialSlots.begin(), plan.materialSlots.end(),
                       [&](const BuildSlot& slot) { return slot.name == name; });
}

// Checks that the slot table has this entry exactly once.
void requireSlot(const BuildPlan& plan, const std::string& name, const std::string& material,
                 const std::string& surface = "Generated")
{
    INFO(name);
    std::vector<BuildSlot> found;
    for (const BuildSlot& slot : plan.materialSlots) {
        if (slot.name == name) found.push_back(slot);
    }
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].material == material);
    REQUIRE(found[0].surface == surface);
}

// Says whether a list holds this text.
bool holdsText(const std::vector<std::string>& list, const std::string& text)
{
    return std::find(list.begin(), list.end(), text) != list.end();
}

// Puts a placeholder in place of every copy of a folder in a text.
std::string withPlaceholder(std::string text, const std::string& folder, const std::string& placeholder)
{
    for (std::size_t at = text.find(folder); at != std::string::npos; at = text.find(folder, at + placeholder.size())) {
        text.replace(at, folder.size(), placeholder);
    }
    return text;
}

// The job the Blender tests build: two floors with three columns and a railing, with folders as placeholders.
std::string generatorFixtureText()
{
    GeneratorLibrary library = loadGeneratorFolder(fs::path(PLYNTH_SAMPLES_DIR) / "generators");
    std::erase_if(library.generators,
                  [](const GeneratorInfo& info) { return info.id != "column" && info.id != "railing"; });
    for (GeneratorInfo& info : library.generators) info.file = fs::path("C:/plynth-fixture/generators") / (info.id + ".py");

    const Item round = placedItem(library, "g1", "column");
    Item square = placedItem(library, "g2", "column",
                             {{"material", std::string("oak_planks")}, {"shape", std::string("square")}}, 3.0, 2.5);
    square.rotation = 30.0;
    Item railing = placedItem(library, "g3", "railing", {}, 2.0, 0.125);
    railing.wall = Id("w1");

    Project project = roomProject({round, square, railing});
    project.settings.name = "Fixture";
    project.plan.floors[0].rooms[0].type = "living";
    project.plan.floors.push_back(upperFloor({placedItem(library, "g4", "column", {}, 3.0, 3.0)}));

    const BuildMaterialSource materials = [](std::string_view id) -> std::optional<BuildMaterialState> {
        const std::string name(id);
        if (name != "white_paint" && name != "oak_planks" && name != "brushed_steel") return std::nullopt;
        BuildMaterialState state;
        const std::string maps = "C:/plynth-fixture/library/" + name + "/";
        state.material.id = name;
        state.material.tileSize = {2.0, 1.0};
        state.material.baseColor = maps + "base_color.png";
        state.material.normal = maps + "normal.png";
        state.material.roughness = maps + "roughness.png";
        state.material.metallic = maps + "metallic.png";
        return state;
    };
    const BuildPlan plan = makeBuildPlan(project, emptyAssets(), materials, &library, std::nullopt);
    REQUIRE(plan.problems.empty());
    REQUIRE(builtCount(plan) == 4);
    const Json job =
        makeBuildJob(project, plan, BuildSettings{}, "@PROJECT@", "build-fixture", "@PROJECT@/build/result.json");
    std::string text = withPlaceholder(job.dump(2) + "\n", "C:/plynth-fixture/generators", "@GENERATORS@");
    return withPlaceholder(std::move(text), "C:/plynth-fixture/library", "@LIBRARY@");
}

// Where the generator job for the Blender tests is kept.
fs::path generatorFixtureFile()
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "buildjob" / "from_core_generators.json";
}

}

TEST_CASE("a generator item is never looked up or built as an asset", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-asset");
    scratch.add("sofa", materialParam("material"));
    const GeneratorLibrary library = scratch.load();
    const Project project = roomProject({buildItem("i1", "sofa", 2.0, 2.5, {1.8, 0.9, 0.8}),
                                         placedItem(library, "g1", "sofa"), placedItem(library, "g2", "column")});
    std::vector<std::string> asked;
    const BuildAssetSource anyAsset = [&asked](std::string_view id) -> std::optional<BuildAssetState> {
        asked.emplace_back(id);
        return BuildAssetState{"D:/Library/assets/items/any/asset.blend", false, ""};
    };
    const BuildPlan plan = makeBuildPlan(project, anyAsset, sampleMaterialSource(), &library, std::nullopt);

    REQUIRE(asked == std::vector<std::string>{"sofa"});
    REQUIRE(countCode(plan.problems, "missing_asset") == 0);
    REQUIRE(countCode(plan.problems, "missing_generator") == 0);
    REQUIRE(plan.assets.size() == 1);
    REQUIRE(plan.floors[0].items.size() == 1);
    REQUIRE(plan.floors[0].items[0].id == "i1");
    REQUIRE(builtCount(plan) == 2);
    REQUIRE(builtItem(plan, "g1").generator == "sofa");
    REQUIRE(plan.generators.generators.size() == 2);
    REQUIRE(plan.generators.generators[0].id == "column");
    REQUIRE(plan.generators.generators[1].id == "sofa");
    REQUIRE(plan.generators.generators[1].file == library.find("sofa")->file);
}

TEST_CASE("a generator item that is built keeps its plain item warnings", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-warnings");
    const GeneratorLibrary library = scratch.load();
    const BuildPlan plan = planWith(roomProject({placedItem(library, "g1", "column", {}, 9.0, 9.0)}), library);

    REQUIRE(builtCount(plan) == 1);
    REQUIRE_FALSE(plan.problems.empty());
    for (const BuildProblem& problem : plan.problems) {
        REQUIRE(problem.object == "g1");
        REQUIRE(problem.severity == BuildSeverity::Warning);
    }
}

TEST_CASE("generator problems stand where their items stand", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-order");
    std::ofstream(scratch.folder / "broken.py") << "print('no declaration')\n";
    const GeneratorLibrary library = scratch.load();
    const Item column = placedItem(library, "g2", "column", {}, 9.0, 9.0);
    Item changed = column;
    changed.generator->params.push_back({"zzz_old", 1.0});

    Project project = buildProject();
    project.plan.floors[0].items = {renamedItem(column, "g1", "nothing"),
                                    buildItem("a1", "gone", 9.0, 9.0, {0.5, 0.5, 0.5}),
                                    changed,
                                    buildItem("a2", "gone", 9.0, 9.0, {0.5, 0.5, 0.5}),
                                    placedItem(library, "g3", "archimesh_column", {}, 9.0, 9.0),
                                    renamedItem(column, "g4", "broken")};
    Floor upstairs = upperFloor({renamedItem(column, "g5", "nothing")});
    Stair stair;
    stair.id = "s1";
    stair.x = 1.0;
    stair.y = 1.0;
    upstairs.stairs = {stair};
    project.plan.floors.push_back(upstairs);

    const BuildPlan plan = planWith(project, library, AddonCache{});

    std::vector<std::string> order;
    for (const BuildProblem& problem : plan.problems) {
        if (order.empty() || order.back() != problem.object) order.push_back(problem.object);
    }
    const std::vector<std::string> wanted{"o2", "r2", "g1", "a1", "g2", "a2", "g3", "g4", "g5", "s1"};
    REQUIRE(order == wanted);

    const auto of = [&](const std::string& id) {
        std::vector<BuildProblem> found;
        for (const BuildProblem& problem : plan.problems) {
            if (problem.object == id) found.push_back(problem);
        }
        return found;
    };
    REQUIRE(of("g1").size() == 1);
    REQUIRE(of("g1")[0].code == "missing_generator");
    REQUIRE(of("g1")[0].severity == BuildSeverity::Error);
    REQUIRE(of("g1")[0].floor == "f1");
    REQUIRE(of("g1")[0].step == kCheckStep);
    // Its own problem comes before the other warnings of the item.
    REQUIRE(of("g2").size() >= 2);
    REQUIRE(of("g2")[0].code == "generator_changed");
    REQUIRE(of("g2")[0].severity == BuildSeverity::Warning);
    REQUIRE(of("g3").size() == 1);
    REQUIRE(of("g3")[0].code == "generator_unavailable");
    REQUIRE(of("g3")[0].severity == BuildSeverity::Warning);
    REQUIRE(of("g4").size() == 1);
    REQUIRE(of("g4")[0].code == "generator_broken");
    REQUIRE(of("g4")[0].severity == BuildSeverity::Error);
    REQUIRE(of("g5").size() == 1);
    REQUIRE(of("g5")[0].floor == "f2");

    REQUIRE(builtCount(plan) == 1);
    REQUIRE(plan.generators.generators.size() == 1);
    REQUIRE(plan.generators.problems.size() == 5);
    for (const BuildProblem& problem : plan.generators.problems) {
        REQUIRE(std::count(plan.problems.begin(), plan.problems.end(), problem) == 1);
    }
}

TEST_CASE("without generators the library counts as empty and no item is dropped silently", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-null");
    const GeneratorLibrary library = scratch.load();
    const Project project =
        roomProject({placedItem(library, "g1", "column"), placedItem(library, "g2", "archimesh_column")});

    const BuildPlan none = makeBuildPlan(project, emptyAssets(), sampleMaterialSource(), nullptr, std::nullopt);
    REQUIRE(countCode(none.problems, "missing_generator") == 2);
    REQUIRE(none.problems.size() == 2);
    REQUIRE(builtCount(none) == 0);
    for (const BuildProblem& problem : none.problems) {
        REQUIRE(problem.severity == BuildSeverity::Error);
        REQUIRE(problem.floor == "f1");
        REQUIRE(problem.step == kCheckStep);
    }

    // The form without generators is the same as a null library and no cache.
    REQUIRE(makeBuildPlan(project, emptyAssets(), sampleMaterialSource()) == none);
    REQUIRE(planWith(project, GeneratorLibrary{}) == none);
}

TEST_CASE("without an assets source there is no library and no items of either kind", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-nolibrary");
    const GeneratorLibrary library = scratch.load();
    for (const bool withAsset : {false, true}) {
        INFO("with an asset item: " << withAsset);
        std::vector<Item> items{placedItem(library, "g1", "column")};
        if (withAsset) items.push_back(buildItem("i1", "sofa", 2.0, 2.5, {1.8, 0.9, 0.8}));
        const Project project = roomProject(items);
        const BuildPlan plan = makeBuildPlan(project, {}, {}, &library, std::nullopt);

        REQUIRE(plan.problems.size() == 1);
        REQUIRE(plan.problems[0].code == "no_library");
        REQUIRE(plan.problems[0].severity == BuildSeverity::Error);
        REQUIRE(plan.floors[0].items.empty());
        REQUIRE(builtCount(plan) == 0);
        REQUIRE(plan.generators.generators.empty());
        REQUIRE_FALSE(hasSlot(plan, "Column_Material"));
        const nlohmann::json job = jobOf(project, plan);
        REQUIRE(job.at("params").contains("generators"));
        REQUIRE(job.at("params").at("generators").empty());
    }
}

TEST_CASE("the build job always writes both generator lists", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-lists");
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({});
    project.plan.floors.push_back(upperFloor({}));

    for (const BuildPlan& plan : {makeBuildPlan(project, {}), planWith(project, library)}) {
        const nlohmann::json job = jobOf(project, plan);
        REQUIRE(job.at("params").contains("generators"));
        REQUIRE(job.at("params").at("generators") == nlohmann::json::array());
        for (const auto& floor : job.at("params").at("floors")) {
            REQUIRE(floor.contains("generator_items"));
            REQUIRE(floor.at("generator_items") == nlohmann::json::array());
        }
        REQUIRE(schemaProblem("blender-job.schema.json", job).empty());
    }
}

TEST_CASE("the job carries the values, place and slots of a generator item", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-values");
    scratch.add("plain", numberParam());
    const GeneratorLibrary library = scratch.load();
    Item turned = placedItem(library, "g1", "column", {{"material", std::string("oak_planks")}});
    turned.rotation = 90.0;
    turned.wall = Id("w1");
    const Project project = roomProject({turned, placedItem(library, "g2", "plain", {}, 3.0, 3.0)});
    const nlohmann::json job = jobOf(project, planWith(project, library));

    REQUIRE(schemaProblem("blender-job.schema.json", job).empty());
    const auto& params = job.at("params");
    REQUIRE(params.contains("generators"));
    REQUIRE(params.at("generators").size() == 2);
    REQUIRE(params.at("generators").at(0).at("id") == "column");
    REQUIRE(params.at("generators").at(0).at("file") == pathToUtf8(library.find("column")->file));
    REQUIRE(params.at("generators").at(0).at("addons").empty());
    const auto& items = params.at("floors").at(0).at("generator_items");
    REQUIRE(items.size() == 2);
    REQUIRE(items.at(0).at("id") == "g1");
    REQUIRE(items.at(0).at("generator") == "column");
    REQUIRE(items.at(0).at("params").at("material") == "oak_planks");
    REQUIRE(items.at(0).at("params").at("shape") == "round");
    REQUIRE(items.at(0).at("slots").at("material") == "Column_Material");
    REQUIRE(items.at(0).at("room") == "r1");
    REQUIRE(items.at(0).at("wall") == "w1");
    const ItemPlacement placed = itemPlacement(turned, 0.0);
    REQUIRE(items.at(0).at("location").at(0).get<double>() == placed.location.x);
    REQUIRE(items.at(0).at("location").at(1).get<double>() == placed.location.y);
    REQUIRE(items.at(0).at("rotation_z").get<double>() == placed.rotationZ);
    REQUIRE(items.at(1).at("slots") == nlohmann::json::object());

    bool listed = false;
    for (const auto& material : params.at("materials")) listed = listed || material.at("id") == "oak_planks";
    REQUIRE(listed);
    bool inTable = false;
    for (const auto& slot : params.at("slots")) {
        inTable = inTable || (slot.at("name") == "Column_Material" && slot.at("material") == "oak_planks" &&
                              slot.at("surface") == "Generated");
    }
    REQUIRE(inTable);
    for (const auto& shape : params.at("floors").at(0).at("collision")) {
        REQUIRE(shape.at("target").at("kind") != "generator");
    }
}

TEST_CASE("generatorJobItems writes the slots and an empty object without them", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-items");
    GeneratorItemBuild withSlot;
    withSlot.id = "g1";
    withSlot.generator = "column";
    withSlot.params = {{"material", std::string("oak_planks")}};
    withSlot.materialParams = {"material"};
    withSlot.size = {0.4, 0.4, 1.0};
    withSlot.materialSlots = {{"material", "oak_planks", "Column_Material"}};
    GeneratorItemBuild withoutSlot;
    withoutSlot.id = "g2";
    withoutSlot.generator = "plain";
    withoutSlot.size = {0.4, 0.4, 1.0};
    GeneratorFloorBuild floor;
    floor.floor = "f1";
    floor.items = {withSlot, withoutSlot};

    const Json list = generatorJobItems(floor);
    REQUIRE(list[0].contains("slots"));
    REQUIRE(list[0].at("slots").size() == 1);
    REQUIRE(list[0].at("slots").at("material") == "Column_Material");
    REQUIRE(list[1].contains("slots"));
    REQUIRE(list[1].at("slots").is_object());
    REQUIRE(list[1].at("slots").empty());
}

TEST_CASE("missing and broken generator materials name the item and get the neutral look", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-materials");
    scratch.add("trim", materialParam("body") + "," + materialParam("edge"));
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({});
    project.plan.floors.push_back(
        upperFloor({placedItem(library, "g1", "column", {{"material", std::string("red_brick")}}),
                    placedItem(library, "g2", "trim",
                               {{"body", std::string("plaster")}, {"edge", std::string("plaster")}}, 3.0, 3.0)}));
    const BuildPlan plan = planWith(project, library);

    REQUIRE(plan.problems.size() == 2);
    // The material problems are sorted by material id, then object.
    const BuildProblem& broken = plan.problems[0];
    REQUIRE(broken.code == "material_broken");
    REQUIRE(broken.object == "g2");
    REQUIRE(broken.floor == "f2");
    REQUIRE(broken.step == kCheckStep);
    REQUIRE(broken.severity == BuildSeverity::Warning);
    REQUIRE(broken.message.find("Item g2") != std::string::npos);
    const BuildProblem& missing = plan.problems[1];
    REQUIRE(missing.code == "missing_material");
    REQUIRE(missing.object == "g1");
    REQUIRE(missing.floor == "f2");
    REQUIRE(missing.severity == BuildSeverity::Warning);
    REQUIRE(missing.message.find("Item g1 uses the material red_brick") != std::string::npos);

    REQUIRE(plan.materials.empty());
    REQUIRE(onlySlot(plan, "g1") == "Column_Material");
    REQUIRE(builtItem(plan, "g1").materialSlots.at(0).material.empty());
    requireSlot(plan, "Column_Material", "");
    const nlohmann::json job = jobOf(project, plan);
    const auto& items = job.at("params").at("floors").at(1).at("generator_items");
    REQUIRE(items.size() == 2);
    REQUIRE(items.at(0).at("params").at("material").is_null());
    REQUIRE(items.at(1).at("params").at("body").is_null());
    REQUIRE(items.at(1).at("params").at("edge").is_null());
    REQUIRE(schemaProblem("blender-job.schema.json", job).empty());
}

TEST_CASE("a bad material on a wall and a generator is reported for both", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-both");
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({placedItem(library, "g1", "column", {{"material", std::string("red_brick")}})});
    project.plan.floors[0].walls[0].materialOutside = std::string("red_brick");
    const BuildPlan plan = planWith(project, library);

    REQUIRE(plan.problems.size() == 2);
    REQUIRE(countCode(plan.problems, "missing_material") == 2);
    REQUIRE(plan.problems[0].object == "g1");
    REQUIRE(plan.problems[1].object == "w1");
}

TEST_CASE("without a materials source the material of a generator is the neutral look", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-nomaterials");
    const GeneratorLibrary library = scratch.load();
    const Project project = roomProject({placedItem(library, "g1", "column")});
    const BuildPlan plan = makeBuildPlan(project, emptyAssets(), {}, &library, std::nullopt);

    requireSlot(plan, "Column_Material", "");
    REQUIRE(plan.materials.empty());
    const nlohmann::json job = jobOf(project, plan);
    REQUIRE(job.at("params").at("floors").at(0).at("generator_items").at(0).at("params").at("material").is_null());
}

TEST_CASE("items of one generator share a slot across floors while the material is the same", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-share");
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({placedItem(library, "g1", "column")});
    project.plan.floors.push_back(upperFloor({placedItem(library, "g2", "column")}));
    const BuildPlan plan = planWith(project, library);

    REQUIRE(onlySlot(plan, "g1") == "Column_Material");
    REQUIRE(onlySlot(plan, "g2") == "Column_Material");
    requireSlot(plan, "Column_Material", "white_paint");
    REQUIRE(plan.materials.size() == 1);
    REQUIRE(plan.materials[0].id == "white_paint");
}

TEST_CASE("a generator that is left out names no slot and no material", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-left-out");
    const GeneratorLibrary library = scratch.load();
    const Item off = placedItem(library, "g2", "archimesh_column", {{"material", std::string("red_brick")}}, 3.0);
    const Item oak = placedItem(library, "g3", "column", {{"material", std::string("oak_planks")}});
    const Project project = roomProject({placedItem(library, "g1", "column"), off, renamedItem(oak, "g3", "nothing")});
    const BuildPlan plan = planWith(project, library, AddonCache{});

    REQUIRE(builtCount(plan) == 1);
    REQUIRE(onlySlot(plan, "g1") == "Column_Material");
    REQUIRE_FALSE(hasSlot(plan, "ArchimeshColumn_Material"));
    REQUIRE_FALSE(hasSlot(plan, "Generated"));
    REQUIRE(countCode(plan.problems, "missing_material") == 0);
    REQUIRE(plan.materials.size() == 1);
}

TEST_CASE("a generator slot that would show several materials gets the material added", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-clash");
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({placedItem(library, "g1", "column"),
                                   placedItem(library, "g2", "column", {{"material", std::string("oak_planks")}}, 2.0),
                                   placedItem(library, "g3", "column", {{"material", std::string()}}, 3.0)});
    project.plan.floors.push_back(upperFloor({placedItem(library, "g4", "column", {{"material", std::string("plaster")}})}));
    const BuildPlan plan = planWith(project, library);

    REQUIRE(onlySlot(plan, "g1") == "Column_Material_WhitePaint");
    REQUIRE(onlySlot(plan, "g2") == "Column_Material_OakPlanks");
    REQUIRE(onlySlot(plan, "g3") == "Column_Material_Neutral");
    // A broken material counts as the neutral look when the names are compared.
    REQUIRE(onlySlot(plan, "g4") == "Column_Material_Neutral");
    requireSlot(plan, "Column_Material_WhitePaint", "white_paint");
    requireSlot(plan, "Column_Material_OakPlanks", "oak_planks");
    requireSlot(plan, "Column_Material_Neutral", "");
    REQUIRE_FALSE(hasSlot(plan, "Column_Material"));

    const nlohmann::json job = jobOf(project, plan);
    const auto& items = job.at("params").at("floors").at(0).at("generator_items");
    REQUIRE(items.at(1).at("params").at("material") == "oak_planks");
    REQUIRE(items.at(1).at("slots").at("material") == "Column_Material_OakPlanks");
}

TEST_CASE("the slot of a generator never takes the name of a surface", "[core][build][generatorjob][slots]")
{
    struct SlotNames {
        const char* generator;
        const char* param;
        const char* slot;
    };
    const std::array<SlotNames, 25> cases{{
        {"door", "material", "DoorGenerator_Material"},
        {"frame", "material", "FrameGenerator_Material"},
        {"glass", "material", "GlassGenerator_Material"},
        {"handle", "material", "HandleGenerator_Material"},
        {"threshold", "material", "ThresholdGenerator_Material"},
        {"stair", "well", "StairGenerator_Well"},
        {"roof", "material", "RoofGenerator_Material"},
        {"soffit", "material", "SoffitGenerator_Material"},
        {"fascia", "material", "FasciaGenerator_Material"},
        {"wall", "top", "WallGenerator_Top"},
        {"exterior", "wall", "ExteriorGenerator_Wall"},
        {"exterior", "material", "ExteriorGenerator_Material"},
        {"flat", "roof", "FlatGenerator_Roof"},
        {"generated", "material", "GeneratedGenerator_Material"},
        {"door", "frame", "DoorGenerator_Frame"},
        {"door", "floor", "DoorGenerator_Floor"},
        {"room", "wall", "RoomGenerator_Wall"},
        {"room", "floor", "RoomGenerator_Floor"},
        {"room", "ceiling", "RoomGenerator_Ceiling"},
        {"living", "wall", "LivingGenerator_Wall"},
        {"lamp", "material", "Lamp_Material"},
        {"doorway", "material", "Doorway_Material"},
        {"flat_roof", "material", "FlatRoof_Material"},
        {"living", "wall_top", "Living_WallTop"},
        {"guest_room", "seat_back", "GuestRoom_SeatBack"},
    }};
    for (const SlotNames& names : cases) {
        DYNAMIC_SECTION(names.generator << " " << names.param)
        {
            JobScratch scratch("genjob-reserved");
            scratch.add(names.generator, materialParam(names.param));
            const GeneratorLibrary library = scratch.load();
            const Project project =
                roomProject({placedItem(library, "g1", names.generator, {{names.param, std::string("tile")}})});
            const BuildPlan plan = planWith(project, library);

            REQUIRE(onlySlot(plan, "g1") == names.slot);
            requireSlot(plan, names.slot, "tile");
            requireSlot(plan, "Room_Floor", "", "Floor");
            requireSlot(plan, "Wall_Top", "", "Wall_Top");
        }
    }
}

TEST_CASE("long ids are cut to 24 characters in a slot name", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-long");
    const std::string generator(64, 'g');
    const std::string param(64, 'p');
    const std::string worded = "extra_long_column_of_many_marble_drums_and_rings";
    scratch.add(generator, materialParam(param));
    scratch.add(worded, materialParam("finish"));
    const GeneratorLibrary library = scratch.load();
    const Project project = roomProject({placedItem(library, "g1", generator), placedItem(library, "g2", worded, {}, 3.0)});
    const BuildPlan plan = planWith(project, library);

    const std::string slot = onlySlot(plan, "g1");
    REQUIRE(slot == "G" + std::string(23, 'g') + "_P" + std::string(23, 'p'));
    REQUIRE(slot.size() <= 63);
    // The id is put in PascalCase first and cut after that.
    REQUIRE(onlySlot(plan, "g2") == "ExtraLongColumnOfManyMar_Finish");
    REQUIRE(schemaProblem("blender-job.schema.json", jobOf(project, plan)).empty());
}

TEST_CASE("a generator without a material parameter uses Generated", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-generated");
    scratch.add("plain", numberParam());
    const GeneratorLibrary library = scratch.load();
    const Project project = roomProject({placedItem(library, "g1", "plain"), placedItem(library, "g2", "plain", {}, 2.0),
                                         placedItem(library, "g3", "column", {{"material", std::string()}}, 3.0)});
    const BuildPlan plan = planWith(project, library);

    REQUIRE(builtItem(plan, "g1").materialSlots.empty());
    requireSlot(plan, "Generated", "");
    requireSlot(plan, "Column_Material", "");
    REQUIRE(std::is_sorted(plan.materialSlots.begin(), plan.materialSlots.end(),
                           [](const BuildSlot& a, const BuildSlot& b) { return a.name < b.name; }));
}

TEST_CASE("material slots follow the order of the declaration", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-param-order");
    scratch.add("trim", materialParam("zinc") + "," + numberParam() + "," + materialParam("accent"));
    const GeneratorLibrary library = scratch.load();
    const ValueChanges values{{"accent", std::string("tile")}, {"zinc", std::string("brushed_steel")}};
    const BuildPlan plan = planWith(roomProject({placedItem(library, "g1", "trim", values)}), library);

    const std::vector<GeneratorMaterialSlot> wanted{{"zinc", "brushed_steel", "Trim_Zinc"},
                                                    {"accent", "tile", "Trim_Accent"}};
    REQUIRE(builtItem(plan, "g1").materialSlots == wanted);
}

TEST_CASE("a generator item on floor 2 stands on the level of that floor", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-floor2");
    const GeneratorLibrary library = scratch.load();
    Item raised = placedItem(library, "g2", "column");
    raised.z = 0.1;
    std::erase_if(raised.generator->params, [](const GeneratorParamValue& value) { return value.id == "height"; });
    Project project = roomProject({});
    Floor upstairs = upperFloor({raised});
    upstairs.ceilingHeight = 2.5;
    project.plan.floors.push_back(upstairs);
    const BuildPlan plan = planWith(project, library);

    REQUIRE(plan.generators.floors.size() == 2);
    REQUIRE(plan.generators.floors[1].floor == "f2");
    const GeneratorItemBuild built = builtItem(plan, "g2");
    REQUIRE(built.location.z == Approx(3.3));
    REQUIRE(built.room == std::optional<Id>("r21"));
    // A height the plan leaves out is the ceiling of the floor the item is on.
    REQUIRE(built.size.height == Approx(2.5));
    const nlohmann::json job = jobOf(project, plan);
    REQUIRE(job.at("params").at("floors").at(0).at("generator_items").empty());
    REQUIRE(job.at("params").at("floors").at(1).at("generator_items").at(0).at("location").at(2).get<double>() ==
            Approx(3.3));
}

TEST_CASE("materialsUsed lists the materials of generators and never a choice", "[core][materials][generatorjob]")
{
    JobScratch scratch("genjob-used");
    scratch.add("mixed", materialParam("finish") +
                             R"(, {"id": "grain", "label": "Grain", "type": "choice", "default": "gold_leaf",)" +
                             R"( "choices": [{"id": "gold_leaf", "label": "Gold"}]})");
    const GeneratorLibrary library = scratch.load();
    Item changed = placedItem(library, "g2", "column");
    for (GeneratorParamValue& value : changed.generator->params) {
        if (value.id == "material") value.value = 4.0;
    }
    Project project = roomProject({placedItem(library, "g1", "mixed", {{"finish", std::string("green_marble")}}), changed,
                                   placedItem(library, "g3", "column", {{"material", std::string()}})});
    const Item gone = placedItem(library, "g4", "column", {{"material", std::string("lost_material")}});
    project.plan.floors.push_back(upperFloor({renamedItem(gone, "g4", "nothing")}));
    project.plan.floors[0].rooms[0].floorMaterial = std::string("oak_planks");

    const std::vector<std::string> used = materialsUsed(project, library);
    REQUIRE(holdsText(used, "green_marble"));
    // A value that no longer fits counts as the default the build would use.
    REQUIRE(holdsText(used, "white_paint"));
    REQUIRE(holdsText(used, "oak_planks"));
    REQUIRE_FALSE(holdsText(used, "gold_leaf"));
    REQUIRE_FALSE(holdsText(used, "lost_material"));
    REQUIRE_FALSE(holdsText(used, ""));
    REQUIRE(std::is_sorted(used.begin(), used.end()));
    REQUIRE(std::adjacent_find(used.begin(), used.end()) == used.end());
    REQUIRE_FALSE(holdsText(materialsUsed(project), "green_marble"));
    REQUIRE(materialsUsed(project, GeneratorLibrary{}) == materialsUsed(project));
}

TEST_CASE("a stair without a type reads as straight and fits the plan schemas", "[core][schema][generatorjob]")
{
    JobScratch scratch("genjob-stair");
    Plan plan = makeEmptyPlan();
    Stair stair;
    stair.id = "s1";
    stair.type = "spiral";
    stair.x = 1.0;
    stair.y = 2.0;
    plan.floors[0].stairs = {stair};
    Json json = planToJson(plan);
    json["floors"][0]["stairs"][0].erase("type");

    const PlanRead read = planFromJson(json);
    requireOk(read.status);
    REQUIRE(read.plan.floors[0].stairs.size() == 1);
    REQUIRE(read.plan.floors[0].stairs[0].type == "straight");
    CHECK(schemaProblem("plan.schema.json", nlohmann::json::parse(json.dump())) == "");

    Json older = json;
    older["version"] = 5;
    // Plan 7 fields are not part of version 5.
    for (Json& floor : older["floors"]) {
        floor.erase("lights");
        for (Json& room : floor["rooms"]) room.erase("dressing");
        for (Json& opening : floor["openings"]) opening.erase("auto_frame");
    }
    CHECK(schemaProblem("plan.v5.schema.json", nlohmann::json::parse(older.dump())) == "");
    const PlanRead migrated = planFromJson(older);
    requireOk(migrated.status);
    REQUIRE(migrated.plan.floors[0].stairs[0].type == "straight");

    // Only the type may be left out.
    for (const char* key : {"id", "x", "y"}) {
        INFO(key);
        Json incomplete = json;
        incomplete["floors"][0]["stairs"][0].erase(key);
        CHECK_FALSE(planFromJson(incomplete).status.ok());
        CHECK_FALSE(schemaProblem("plan.schema.json", nlohmann::json::parse(incomplete.dump())).empty());
    }
}

TEST_CASE("the build job is refused without each generator field", "[core][schema][generatorjob]")
{
    JobScratch scratch("genjob-schema");
    const GeneratorLibrary library = scratch.load();
    const Project project = roomProject({placedItem(library, "g1", "column")});
    const nlohmann::json job = jobOf(project, planWith(project, library));
    REQUIRE(schemaProblem("blender-job.schema.json", job).empty());
    REQUIRE(job.at("params").contains("generators"));
    REQUIRE(job.at("params").at("floors").at(0).at("generator_items").size() == 1);

    nlohmann::json noList = job;
    noList["params"].erase("generators");
    CHECK_FALSE(schemaProblem("blender-job.schema.json", noList).empty());
    nlohmann::json noItems = job;
    noItems["params"]["floors"][0].erase("generator_items");
    CHECK_FALSE(schemaProblem("blender-job.schema.json", noItems).empty());
    nlohmann::json noSlots = job;
    noSlots["params"]["floors"][0]["generator_items"][0].erase("slots");
    CHECK_FALSE(schemaProblem("blender-job.schema.json", noSlots).empty());
}

TEST_CASE("readBuildResult reads the generators count and 0 without one", "[core][build][generatorjob]")
{
    JobScratch scratch("genjob-count");
    Json result = Json::parse(readTextFile(fs::path(PLYNTH_TEST_DATA_DIR) / "buildjob" / "result-valid.json"));
    const BuildWorkerOutput old = readBuildResult(result, "build-00000001");
    requireOk(old.header.status);
    REQUIRE(old.counts.generators == 0);

    result["output"]["counts"]["generators"] = 2;
    REQUIRE(schemaProblem("blender-result.schema.json", nlohmann::json::parse(result.dump())).empty());
    const BuildWorkerOutput counted = readBuildResult(result, "build-00000001");
    requireOk(counted.header.status);
    REQUIRE(counted.counts.generators == 2);

    for (const Json& bad : {Json(-1), Json(1.5), Json("2"), Json(nullptr), Json(true)}) {
        INFO(bad.dump());
        result["output"]["counts"]["generators"] = bad;
        const BuildWorkerOutput out = readBuildResult(result, "build-00000001");
        CHECK(out.header.status.code == ErrorCode::BadFile);
        CHECK(out.header.status.where == "/output/counts/generators");
    }
}

TEST_CASE("the generator job for the Blender tests matches the saved copy", "[core][build][fixture][generatorjob]")
{
    JobScratch scratch("genjob-fixture");
    const std::string text = generatorFixtureText();
    REQUIRE(schemaProblemForText("blender-job.schema.json", text).empty());
    INFO("Regenerate with: plynth_core_tests \"[.write-fixture]\"");
    REQUIRE(readTextFile(generatorFixtureFile()) == text);
}

TEST_CASE("write the generator job for the Blender tests", "[.write-fixture]")
{
    JobScratch scratch("genjob-fixture-write");
    const std::string text = generatorFixtureText();
    std::ofstream(generatorFixtureFile(), std::ios::binary | std::ios::trunc) << text;
    REQUIRE(fs::exists(generatorFixtureFile()));
}

namespace {

// Checks that no two slots have the same name when case is ignored.
void requireUniqueNames(const BuildPlan& plan)
{
    std::vector<std::string> names;
    for (const BuildSlot& slot : plan.materialSlots) {
        std::string name = slot.name;
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        INFO(slot.name);
        REQUIRE_FALSE(holdsText(names, name));
        REQUIRE(slot.name.size() <= 128);
        names.push_back(std::move(name));
    }
}

// Checks that every surface keeps the slot it has without generators.
void requireSurfacesKept(Project project, const BuildPlan& plan)
{
    for (Floor& floor : project.plan.floors) floor.items.clear();
    const BuildPlan bare = makeBuildPlan(project, emptyAssets(), sampleMaterialSource());
    REQUIRE(plan.floorMaterials == bare.floorMaterials);
    for (const BuildSlot& slot : bare.materialSlots) {
        INFO(slot.name);
        REQUIRE(std::count(plan.materialSlots.begin(), plan.materialSlots.end(), slot) == 1);
    }
}

// The slot of a wall face that looks into room r1.
std::string roomWallSlot(const BuildPlan& plan)
{
    for (const WallFaceMaterial& face : plan.floorMaterials.at(0).wallFaces) {
        if (face.room == "r1") return face.slot;
    }
    return {};
}

}

TEST_CASE("a generator slot moves for a room and the room keeps its name", "[core][build][generatorjob][slots]")
{
    struct Meeting {
        const char* room;
        const char* generatorMaterial;
        const char* roomSlot;
    };
    for (const Meeting& meeting : {Meeting{"living_generator", "oak_planks", "LivingGenerator_Wall"},
                                   Meeting{"living_generator", "tile", "LivingGenerator_Wall"},
                                   Meeting{"living_generator", "", "LivingGenerator_Wall"},
                                   Meeting{"livinggenerator", "tile", "Livinggenerator_Wall"}}) {
        DYNAMIC_SECTION(meeting.room << " [" << meeting.generatorMaterial << "]")
        {
            JobScratch scratch("genjob-meet-room");
            scratch.add("living", materialParam("wall"));
            const GeneratorLibrary library = scratch.load();
            Project project = roomProject({placedItem(library, "g1", "living", {{"wall", std::string(meeting.generatorMaterial)}})});
            project.plan.floors[0].rooms[0].type = meeting.room;
            project.plan.floors[0].rooms[0].wallMaterial = std::string("tile");
            const BuildPlan plan = planWith(project, library);

            REQUIRE(onlySlot(plan, "g1") == "LivingGenerator2_Wall");
            requireSlot(plan, "LivingGenerator2_Wall", meeting.generatorMaterial);
            REQUIRE(roomWallSlot(plan) == meeting.roomSlot);
            requireSlot(plan, meeting.roomSlot, "tile", "Wall_Inside");
            requireSurfacesKept(project, plan);
            requireUniqueNames(plan);
            REQUIRE(schemaProblem("blender-job.schema.json", jobOf(project, plan)).empty());
        }
    }
}

TEST_CASE("a generator slot skips a numbered name that is taken", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-meet-third");
    scratch.add("living", materialParam("wall"));
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({placedItem(library, "g1", "living")});
    project.plan.floors[0].rooms[0].type = "living_generator";
    project.plan.floors.push_back(upperFloor({}));
    project.plan.floors[1].rooms[0].type = "living_generator2";
    const BuildPlan plan = planWith(project, library);

    REQUIRE(onlySlot(plan, "g1") == "LivingGenerator3_Wall");
    requireSurfacesKept(project, plan);
    requireUniqueNames(plan);
}

TEST_CASE("generators that would share a name are numbered in id order", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-meet-pairs");
    for (const char* id : {"door", "door_generator", "door_generator2", "ab_cd", "abcd"}) scratch.add(id, materialParam("material"));
    const GeneratorLibrary library = scratch.load();
    const Project project = roomProject({placedItem(library, "g1", "door"), placedItem(library, "g2", "door_generator"),
                                         placedItem(library, "g3", "door_generator2"), placedItem(library, "g4", "ab_cd"),
                                         placedItem(library, "g5", "abcd")});
    const BuildPlan plan = planWith(project, library);

    REQUIRE(onlySlot(plan, "g1") == "DoorGenerator_Material");
    REQUIRE(onlySlot(plan, "g2") == "DoorGenerator2_Material");
    REQUIRE(onlySlot(plan, "g3") == "DoorGenerator22_Material");
    REQUIRE(onlySlot(plan, "g4") == "AbCd_Material");
    REQUIRE(onlySlot(plan, "g5") == "Abcd2_Material");
    requireSurfacesKept(project, plan);
    requireUniqueNames(plan);
}

TEST_CASE("two and three ids alike in 24 characters get names of their own", "[core][build][generatorjob][slots]")
{
    const std::string head(24, 'g');
    const std::string label = "G" + std::string(23, 'g');
    for (const int count : {2, 3}) {
        DYNAMIC_SECTION(count << " generators")
        {
            JobScratch scratch("genjob-meet-cut");
            const std::vector<std::string> endings{"_first", "_second", "_third"};
            for (int i = 0; i < count; ++i) scratch.add(head + endings[i], materialParam("material"));
            const GeneratorLibrary library = scratch.load();
            std::vector<Item> items;
            for (int i = count - 1; i >= 0; --i) {
                items.push_back(placedItem(library, "g" + std::to_string(i + 1), head + endings[i], {}, 1.0 + i));
            }
            const Project project = roomProject(items);
            const BuildPlan plan = planWith(project, library);

            REQUIRE(onlySlot(plan, "g1") == label + "_Material");
            REQUIRE(onlySlot(plan, "g2") == label + "2_Material");
            if (count == 3) REQUIRE(onlySlot(plan, "g3") == label + "3_Material");
            requireUniqueNames(plan);
            REQUIRE(schemaProblem("blender-job.schema.json", jobOf(project, plan)).empty());
        }
    }
}

TEST_CASE("two parameters alike in 24 characters get slots of their own", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-meet-param");
    const std::string head(24, 'p');
    scratch.add("trim", materialParam(head + "_second") + "," + materialParam(head + "_first"));
    const GeneratorLibrary library = scratch.load();
    const BuildPlan plan = planWith(roomProject({placedItem(library, "g1", "trim")}), library);

    const std::string cut = "P" + std::string(23, 'p');
    const std::vector<GeneratorMaterialSlot> wanted{{head + "_second", "", "Trim2_" + cut}, {head + "_first", "", "Trim_" + cut}};
    REQUIRE(builtItem(plan, "g1").materialSlots == wanted);
    requireUniqueNames(plan);
}

TEST_CASE("a name that meets only with the material added moves too", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-meet-material");
    scratch.add("living", materialParam("floor"));
    const GeneratorLibrary library = scratch.load();
    Project project = roomProject({placedItem(library, "g1", "living", {{"floor", std::string("tile")}}),
                                   placedItem(library, "g2", "living", {{"floor", std::string("painted_wood")}}, 3.0)});
    project.plan.floors[0].rooms[0].type = "living_generator";
    project.plan.floors[0].rooms[0].floorMaterial = std::string("tile");
    project.plan.floors.push_back(upperFloor({}));
    project.plan.floors[1].rooms[0].type = "living_generator";
    project.plan.floors[1].rooms[0].floorMaterial = std::string("oak_planks");
    const BuildPlan plan = planWith(project, library);

    REQUIRE(onlySlot(plan, "g1") == "LivingGenerator2_Floor_Tile");
    REQUIRE(onlySlot(plan, "g2") == "LivingGenerator2_Floor_PaintedWood");
    requireSlot(plan, "LivingGenerator_Floor_Tile", "tile", "Floor");
    requireSlot(plan, "LivingGenerator_Floor_OakPlanks", "oak_planks", "Floor");
    requireSurfacesKept(project, plan);
    requireUniqueNames(plan);
}

TEST_CASE("slot names do not depend on the order of the items", "[core][build][generatorjob][slots]")
{
    JobScratch scratch("genjob-meet-order");
    for (const char* id : {"door", "door_generator", "door_generator2"}) scratch.add(id, materialParam("material"));
    scratch.add("living", materialParam("wall"));
    const GeneratorLibrary library = scratch.load();
    const std::vector<Item> items{placedItem(library, "g1", "door", {{"material", std::string("tile")}}),
                                  placedItem(library, "g2", "door", {{"material", std::string("oak_planks")}}, 2.0),
                                  placedItem(library, "g3", "door_generator", {}, 3.0),
                                  placedItem(library, "g4", "door_generator2", {}, 1.0, 3.0),
                                  placedItem(library, "g5", "living", {}, 2.0, 3.0)};
    Project forward = roomProject(items);
    forward.plan.floors[0].rooms[0].type = "living_generator";
    Project backward = forward;
    std::reverse(backward.plan.floors[0].items.begin(), backward.plan.floors[0].items.end());

    const BuildPlan first = planWith(forward, library);
    const BuildPlan second = planWith(backward, library);
    REQUIRE(first.materialSlots == second.materialSlots);
    for (const Item& item : items) {
        INFO(item.id);
        REQUIRE(onlySlot(first, item.id) == onlySlot(second, item.id));
    }
    REQUIRE(onlySlot(first, "g1") == "DoorGenerator_Material_Tile");
    REQUIRE(onlySlot(first, "g3") == "DoorGenerator2_Material");
    REQUIRE(onlySlot(first, "g5") == "LivingGenerator2_Wall");
    requireUniqueNames(first);
}
