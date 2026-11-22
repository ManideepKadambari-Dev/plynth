#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/GeneratorBuild.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A library folder with the column, the Archimesh column and a refused file.
struct GeneratorFolder {
    explicit GeneratorFolder(const std::string& tag) : tmp(tag)
    {
        folder = tmp.path() / "generators";
        fs::create_directories(folder);
        for (const char* name : {"column.py", "archimesh_column.py"}) {
            fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "generators" / name, folder / name);
        }
        std::ofstream(folder / "broken.py") << "print('no declaration')\n";
        library = loadGeneratorFolder(folder);
    }

    plynth::test::TempDir tmp;
    fs::path folder;
    GeneratorLibrary library;
};

// A generator item standing in room r1 with the given values.
Item generatorItem(const GeneratorInfo& info, const std::string& id, double x, double y, std::vector<GeneratorParamValue> params)
{
    const GeneratorPlacement placement = makeGeneratorUse(info, params);
    plynth::test::requireOk(placement.status);
    Item item;
    item.id = id;
    item.room = Id("r1");
    item.x = x;
    item.y = y;
    item.generator = placement.use;
    item.size = placement.size;
    return item;
}

// An item that names a generator the library cannot give.
Item namedItem(const GeneratorInfo& like, const std::string& id, const std::string& name)
{
    Item item = generatorItem(like, id, 1.0, 1.0, defaultGeneratorParams(like, 3.0));
    item.generator->name = name;
    return item;
}

// The fixture project with generator items added to its first floor and a second floor with one.
Project projectWith(const GeneratorFolder& f, std::vector<Item> first, std::vector<Item> second)
{
    Project project = plynth::test::buildProject();
    Floor upstairs = project.plan.floors[0];
    upstairs.id = "f2";
    upstairs.openings.clear();
    upstairs.rooms.resize(1);
    upstairs.items = std::move(second);
    for (Item& item : first) project.plan.floors[0].items.push_back(std::move(item));
    project.plan.floors.push_back(upstairs);
    return project;
}

const BuildProblem* findProblem(const GeneratorBuildPlan& plan, const std::string& code)
{
    for (const BuildProblem& p : plan.problems) {
        if (p.code == code) return &p;
    }
    return nullptr;
}

}

TEST_CASE("the generator build plan places each item on its floor", "[core][generatorbuild]")
{
    GeneratorFolder f("genbuild-place");
    const GeneratorInfo& column = *f.library.find("column");
    std::vector<GeneratorParamValue> steel = defaultGeneratorParams(column, 3.0);
    plynth::test::requireOk(setGeneratorParam(column, steel, "material", std::string("brushed_steel")));

    Item g1 = generatorItem(column, "g1", 1.0, 1.0, defaultGeneratorParams(column, 3.0));
    g1.rotation = 90.0;
    Item g5 = generatorItem(column, "g5", 2.0, 2.0, steel);
    g5.z = 0.1;
    const Project project = projectWith(f, {g1}, {g5});

    const GeneratorBuildPlan plan = makeGeneratorBuildPlan(project.plan, &f.library, std::nullopt);
    REQUIRE(plan.problems.empty());
    REQUIRE(plan.floors.size() == 2);
    REQUIRE(plan.floors[0].floor == "f1");
    REQUIRE(plan.floors[0].items.size() == 1);
    REQUIRE(plan.floors[1].items.size() == 1);

    const GeneratorItemBuild& first = plan.floors[0].items[0];
    const ItemPlacement p1 = itemPlacement(g1, 0.0);
    REQUIRE(first.id == "g1");
    REQUIRE(first.generator == "column");
    REQUIRE(first.location == p1.location);
    REQUIRE(first.rotationZ == p1.rotationZ);
    REQUIRE(first.size == g1.size);
    REQUIRE(first.room == std::optional<Id>("r1"));
    REQUIRE_FALSE(first.wall.has_value());
    REQUIRE(first.materialParams == std::vector<std::string>{"material"});

    const GeneratorItemBuild& second = plan.floors[1].items[0];
    const ItemPlacement p5 = itemPlacement(g5, 3.0);
    REQUIRE(second.location == p5.location);
    REQUIRE(second.location.z == 3.1);

    REQUIRE(plan.generators.size() == 1);
    REQUIRE(plan.generators[0].id == "column");
    REQUIRE(plan.generators[0].file == f.library.find("column")->file);
    REQUIRE(plan.materials == std::vector<std::string>{"brushed_steel", "white_paint"});
    REQUIRE_FALSE(plan.needsAddons);
}

TEST_CASE("missing, broken and unavailable generators are left out with problems", "[core][generatorbuild]")
{
    GeneratorFolder f("genbuild-problems");
    const GeneratorInfo& column = *f.library.find("column");
    const GeneratorInfo& archimesh = *f.library.find("archimesh_column");
    const Project project = projectWith(
        f,
        {generatorItem(column, "g1", 1.0, 1.0, defaultGeneratorParams(column, 3.0)),
         namedItem(column, "g2", "nothing"), namedItem(column, "g3", "broken"),
         generatorItem(archimesh, "g4", 3.0, 2.0, defaultGeneratorParams(archimesh, 3.0))},
        {});

    AddonCache off;
    const GeneratorBuildPlan withCache = makeGeneratorBuildPlan(project.plan, &f.library, off);
    REQUIRE(withCache.floors[0].items.size() == 1);
    REQUIRE(withCache.floors[0].items[0].id == "g1");
    REQUIRE(withCache.problems.size() == 3);
    const BuildProblem* missing = findProblem(withCache, "missing_generator");
    REQUIRE(missing);
    REQUIRE(missing->severity == BuildSeverity::Error);
    REQUIRE(missing->object == "g2");
    REQUIRE(missing->floor == "f1");
    REQUIRE(missing->step == kCheckStep);
    const BuildProblem* broken = findProblem(withCache, "generator_broken");
    REQUIRE(broken);
    REQUIRE(broken->severity == BuildSeverity::Error);
    REQUIRE(broken->object == "g3");
    const BuildProblem* unavailable = findProblem(withCache, "generator_unavailable");
    REQUIRE(unavailable);
    REQUIRE(unavailable->severity == BuildSeverity::Warning);
    REQUIRE(unavailable->object == "g4");
    REQUIRE_THAT(unavailable->message, Catch::Matchers::ContainsSubstring("Archimesh"));
    REQUIRE_FALSE(withCache.needsAddons);

    const GeneratorBuildPlan noCache = makeGeneratorBuildPlan(project.plan, &f.library, std::nullopt);
    REQUIRE(noCache.floors[0].items.size() == 2);
    REQUIRE(noCache.floors[0].items[1].id == "g4");
    REQUIRE(noCache.problems.size() == 2);
    REQUIRE(findProblem(noCache, "generator_unavailable") == nullptr);
    REQUIRE(noCache.needsAddons);
    REQUIRE(noCache.generators.size() == 2);
    REQUIRE(noCache.generators[0].id == "archimesh_column");
    REQUIRE(noCache.generators[0].addons == std::vector<std::string>{"archimesh"});
    REQUIRE(noCache.generators[1].id == "column");

    AddonCache on;
    on.enabled.push_back({"bl_ext.blender_org.archimesh", "Archimesh", "1.2.5"});
    const GeneratorBuildPlan enabled = makeGeneratorBuildPlan(project.plan, &f.library, on);
    REQUIRE(enabled.floors[0].items.size() == 2);
    REQUIRE(enabled.needsAddons);
}

TEST_CASE("values that no longer fit are fitted with a warning", "[core][generatorbuild]")
{
    GeneratorFolder f("genbuild-changed");
    const GeneratorInfo& column = *f.library.find("column");
    Item item = generatorItem(column, "g1", 1.0, 1.0, defaultGeneratorParams(column, 3.0));
    item.generator->params.push_back({"zzz_old", 1.0});
    for (GeneratorParamValue& v : item.generator->params) {
        if (v.id == "sides") v.value = 2.5;
    }
    const Project project = projectWith(f, {item}, {});

    const GeneratorBuildPlan plan = makeGeneratorBuildPlan(project.plan, &f.library, std::nullopt);
    REQUIRE(plan.floors[0].items.size() == 1);
    const BuildProblem* changed = findProblem(plan, "generator_changed");
    REQUIRE(changed);
    REQUIRE(changed->severity == BuildSeverity::Warning);
    REQUIRE(changed->object == "g1");
    REQUIRE_THAT(changed->message, Catch::Matchers::ContainsSubstring("sides"));
    const GeneratorItemBuild& built = plan.floors[0].items[0];
    plynth::test::requireOk(checkGeneratorParams(column, built.params));
    REQUIRE(*findGeneratorValue(built.params, "sides") == GeneratorValue(24.0));
    REQUIRE(findGeneratorValue(built.params, "zzz_old") == nullptr);
}

TEST_CASE("no library builds nothing and says nothing", "[core][generatorbuild]")
{
    GeneratorFolder f("genbuild-nolibrary");
    const GeneratorInfo& column = *f.library.find("column");
    const Project project = projectWith(f, {generatorItem(column, "g1", 1.0, 1.0, defaultGeneratorParams(column, 3.0))}, {});
    const GeneratorBuildPlan plan = makeGeneratorBuildPlan(project.plan, nullptr, std::nullopt);
    REQUIRE(plan.problems.empty());
    REQUIRE(plan.generators.empty());
    REQUIRE(plan.floors.size() == 2);
    REQUIRE(plan.floors[0].items.empty());
}

TEST_CASE("generatorJobItems writes null for an empty or unusable material", "[core][generatorbuild]")
{
    GeneratorFolder f("genbuild-items");
    const GeneratorInfo& column = *f.library.find("column");
    std::vector<GeneratorParamValue> bare = defaultGeneratorParams(column, 3.0);
    plynth::test::requireOk(setGeneratorParam(column, bare, "material", std::string()));
    const Project project = projectWith(
        f, {generatorItem(column, "g1", 1.0, 1.0, defaultGeneratorParams(column, 3.0)), generatorItem(column, "g2", 2.0, 1.0, bare)}, {});
    const GeneratorBuildPlan plan = makeGeneratorBuildPlan(project.plan, &f.library, std::nullopt);

    const Json all = generatorJobItems(plan.floors[0]);
    REQUIRE(all.size() == 2);
    REQUIRE(all[0]["params"]["material"] == "white_paint");
    REQUIRE(all[0]["params"]["sides"] == 24.0);
    REQUIRE(all[0]["params"]["plinth"] == true);
    REQUIRE(all[0]["params"]["shape"] == "round");
    REQUIRE(all[0]["room"] == "r1");
    REQUIRE(all[0]["wall"].is_null());
    REQUIRE(all[1]["params"]["material"].is_null());

    const Json unusable = generatorJobItems(plan.floors[0], [](std::string_view) { return false; });
    REQUIRE(unusable[0]["params"]["material"].is_null());
    const Json usable = generatorJobItems(plan.floors[0], [](std::string_view id) { return id == "white_paint"; });
    REQUIRE(usable[0]["params"]["material"] == "white_paint");

    const Json list = generatorJobList(plan);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0]["id"] == "column");
    REQUIRE(list[0]["file"] == pathToUtf8(f.library.find("column")->file));
    REQUIRE(list[0]["addons"].is_array());
}

TEST_CASE("a build job with generators validates against the job schema", "[core][generatorbuild][schema]")
{
    GeneratorFolder f("genbuild-job");
    const GeneratorInfo& column = *f.library.find("column");
    Project project = projectWith(f, {generatorItem(column, "g1", 1.0, 1.0, defaultGeneratorParams(column, 3.0))},
                                  {generatorItem(column, "g5", 2.0, 2.0, defaultGeneratorParams(column, 3.0))});
    const GeneratorBuildPlan generators = makeGeneratorBuildPlan(project.plan, &f.library, std::nullopt);

    const BuildPlan plan = makeBuildPlan(project, plynth::test::sampleAssetSource());
    Json job = makeBuildJob(project, plan, BuildSettings{}, f.tmp.path(), "build-1a2b3c4d", f.tmp.path() / "result.json");
    job["params"]["generators"] = generatorJobList(generators);
    for (std::size_t i = 0; i < generators.floors.size(); ++i) {
        job["params"]["floors"][i]["generator_items"] = generatorJobItems(generators.floors[i]);
    }
    const std::string problem = plynth::test::schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump()));
    INFO(problem);
    REQUIRE(problem.empty());
    REQUIRE(job["params"]["floors"][1]["generator_items"][0]["location"][2] == 3.0);
}
