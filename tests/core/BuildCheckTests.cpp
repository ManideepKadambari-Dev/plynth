#include "BuildFixtures.h"
#include "MaterialCatalogSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/BuildCheck.h>
#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildResolve.h>
#include <plynth/core/Generators.h>
#include <plynth/core/catalog/BuildChoices.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A library with the asset sofa, the material brick and the column generator, and the build project with wall w1
// in brick, wall w2 in a material the library lacks, a column item g1 and an item g2 of a generator it lacks.
struct CheckFixture {
    explicit CheckFixture(const std::string& tag) : lib(tag)
    {
        lib.add("sofa", "Sofa", {"sofa"});
        Status status;
        materials = MaterialCatalog::open(lib.paths, status);
        plynth::test::requireOk(status);
        plynth::test::requireOk(materials->indexMaterial(plynth::test::makeMaterialOnDisk(lib.paths, "brick", "Brick")));
        fs::create_directories(lib.paths.generators);
        fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / "generators" / "column.py", lib.paths.generators / "column.py",
                      fs::copy_options::overwrite_existing);
        const GeneratorLibrary generators = loadGeneratorFolder(lib.paths.generators);
        const GeneratorInfo* column = generators.find("column");
        REQUIRE(column != nullptr);

        project = plynth::test::buildProject();
        Floor& floor = project.plan.floors[0];
        floor.walls[0].materialInside = "brick";
        floor.walls[1].materialInside = "ghost_paint";
        const GeneratorPlacement placement = makeGeneratorUse(*column, defaultGeneratorParams(*column, 2.7));
        plynth::test::requireOk(placement.status);
        Item g1;
        g1.id = "g1";
        g1.room = Id("r1");
        g1.x = 3.5;
        g1.y = 3.5;
        g1.generator = placement.use;
        g1.size = placement.size;
        Item g2 = g1;
        g2.id = "g2";
        g2.x = 0.5;
        g2.generator->name = "ghost_column";
        floor.items.push_back(g1);
        floor.items.push_back(g2);
    }

    plynth::test::TestLibrary lib;
    std::unique_ptr<MaterialCatalog> materials;
    Project project;
};

// The first problem with this code about this object, or null.
const BuildProblem* problemFor(const BuildCheck& check, const std::string& code, const std::string& object)
{
    for (const BuildProblem& problem : check.problems) {
        if (problem.code == code && problem.object == object) return &problem;
    }
    return nullptr;
}

// Counts the problems with this code.
long long countOf(const BuildCheck& check, const std::string& code)
{
    return std::ranges::count_if(check.problems, [&](const BuildProblem& p) { return p.code == code; });
}

}

TEST_CASE("a plan with no walls gives the checkBuildable refusal and no problems", "[core][buildcheck]")
{
    const Project empty = makeNewProject();
    const BuildCheck check = checkBuild(empty, std::nullopt);
    REQUIRE(check.status.code == ErrorCode::InvalidValue);
    REQUIRE(check.status.message == checkBuildable(empty.plan).message);
    REQUIRE(check.problems.empty());
    REQUIRE(check.errors == 0);
    REQUIRE(check.warnings == 0);
    REQUIRE(check.planFingerprint == planFingerprint(empty));
    REQUIRE_FALSE(check.libraryOpened);
}

TEST_CASE("without a library the items are reported as no_library", "[core][buildcheck]")
{
    const Project project = plynth::test::buildProject();
    const BuildCheck check = checkBuild(project, std::nullopt);
    plynth::test::requireOk(check.status);
    REQUIRE_FALSE(check.libraryOpened);
    REQUIRE(countOf(check, "no_library") >= 1);
    REQUIRE(countOf(check, "missing_asset") == 0);
    REQUIRE(check.errors + check.warnings == static_cast<int>(check.problems.size()));
    REQUIRE(check.planFingerprint == planFingerprint(project));
}

TEST_CASE("a library folder that does not open counts as no library", "[core][buildcheck]")
{
    plynth::test::TempDir tmp("buildcheck-notlib");
    fs::create_directories(tmp.path() / "NotALibrary");
    const Project project = plynth::test::buildProject();
    const BuildCheck none = checkBuild(project, std::nullopt);
    const BuildCheck broken = checkBuild(project, libraryPaths(tmp.path() / "NotALibrary"));
    plynth::test::requireOk(broken.status);
    REQUIRE_FALSE(broken.libraryOpened);
    REQUIRE(broken.problems == none.problems);
}

TEST_CASE("the check lists missing assets, materials and generators and the plan's own problems", "[core][buildcheck]")
{
    CheckFixture f("buildcheck-full");
    const BuildCheck check = checkBuild(f.project, f.lib.paths);
    plynth::test::requireOk(check.status);
    REQUIRE(check.libraryOpened);

    REQUIRE(problemFor(check, "missing_asset", "i1") == nullptr);
    REQUIRE(problemFor(check, "missing_asset", "i2") != nullptr);
    REQUIRE(problemFor(check, "missing_asset", "i3") != nullptr);
    REQUIRE(problemFor(check, "missing_material", "w2") != nullptr);
    REQUIRE(problemFor(check, "missing_material", "w1") == nullptr);
    REQUIRE(problemFor(check, "missing_generator", "g2") != nullptr);
    REQUIRE(problemFor(check, "missing_generator", "g1") == nullptr);
    REQUIRE(problemFor(check, "room_not_closed", "r2") != nullptr);
    // The library's materials are asked, so a plan that uses materials is never told there is no library.
    REQUIRE(countOf(check, "no_library") == 0);

    int errors = 0;
    for (const BuildProblem& p : check.problems) errors += p.severity == BuildSeverity::Error ? 1 : 0;
    REQUIRE(check.errors == errors);
    REQUIRE(check.warnings == static_cast<int>(check.problems.size()) - errors);
}

TEST_CASE("the check gives exactly the problems the build plan of the resolved project has", "[core][buildcheck]")
{
    CheckFixture f("buildcheck-same");
    f.project.settings.variation.seed = 7;
    const BuildCheck check = checkBuild(f.project, f.lib.paths);

    const GeneratorLibrary generators = loadGeneratorFolder(f.lib.paths.generators);
    const BuildChoices choices = buildChoicesFrom(f.lib.catalog.get(), f.materials.get());
    const ResolvedBuild resolved = resolveBuild(f.project, choices);
    const BuildPlan plan = makeBuildPlan(resolved.project, buildAssetSourceFor(*f.lib.catalog),
                                         buildMaterialSourceFor(*f.materials), &generators, std::nullopt, &choices);
    REQUIRE(check.problems == plan.problems);
    // The fingerprint is of the project as given, not of the resolved copy.
    REQUIRE(check.planFingerprint == planFingerprint(f.project));
}

TEST_CASE("the check writes nothing into the library", "[core][buildcheck]")
{
    CheckFixture f("buildcheck-readonly");
    const auto entries = [](const fs::path& folder) {
        std::vector<fs::path> list;
        for (const auto& e : fs::recursive_directory_iterator(folder)) {
            if (e.path().extension() != ".db" && e.path().filename().string().find(".db-") == std::string::npos) {
                list.push_back(e.path());
            }
        }
        std::sort(list.begin(), list.end());
        return list;
    };
    const auto before = entries(f.lib.paths.root);
    (void)checkBuild(f.project, f.lib.paths);
    REQUIRE(entries(f.lib.paths.root) == before);
}

TEST_CASE("the check answer has the problems, the counts and the fingerprint", "[core][buildcheck]")
{
    CheckFixture f("buildcheck-json");
    const BuildCheck check = checkBuild(f.project, f.lib.paths);
    const Json json = buildCheckToJson(check);
    std::vector<std::string> keys;
    for (auto it = json.begin(); it != json.end(); ++it) keys.push_back(it.key());
    REQUIRE(keys == std::vector<std::string>{"problems", "error_count", "warning_count", "plan_fingerprint"});
    REQUIRE(json["problems"].size() == check.problems.size());
    for (std::size_t i = 0; i < check.problems.size(); ++i) {
        REQUIRE(json["problems"][i] == buildProblemToJson(check.problems[i]));
    }
    REQUIRE(json["error_count"] == check.errors);
    REQUIRE(json["warning_count"] == check.warnings);
    REQUIRE(json["plan_fingerprint"] == check.planFingerprint);

    const Json clean = buildCheckToJson(BuildCheck{});
    REQUIRE(clean["problems"].is_array());
    REQUIRE(clean["problems"].empty());
    REQUIRE(clean["error_count"] == 0);
}

TEST_CASE("a variation preview resolves with the given seed and keeps the project's otherwise", "[core][buildcheck][variation]")
{
    CheckFixture f("preview-seed");
    f.project.settings.variation.seed = 3;
    const BuildChoices choices = buildChoicesFrom(f.lib.catalog.get(), f.materials.get());

    const VariationPreview own = previewVariation(f.project, std::nullopt, f.lib.paths);
    REQUIRE(own.library.has_value());
    REQUIRE(own.library->root == f.lib.paths.root);
    REQUIRE(own.resolved == resolveBuild(f.project, choices));
    REQUIRE(own.resolved.project.settings.variation.seed == 3);

    const VariationPreview other = previewVariation(f.project, 11u, f.lib.paths);
    Project tried = f.project;
    tried.settings.variation.seed = 11;
    REQUIRE(other.resolved == resolveBuild(tried, choices));
    REQUIRE(other.resolved.project.settings.variation.seed == 11);

    const VariationPreview zero = previewVariation(f.project, 0u, f.lib.paths);
    REQUIRE(zero.resolved.project.settings.variation.seed == 0);
    REQUIRE(zero.resolved.changes.empty());
}

TEST_CASE("a variation preview without a working library has no choices and no library", "[core][buildcheck][variation]")
{
    plynth::test::TempDir tmp("preview-nolib");
    Project project = plynth::test::buildProject();
    project.settings.variation.seed = 5;
    const ResolvedBuild bare = resolveBuild(project, BuildChoices{});

    const VariationPreview none = previewVariation(project, std::nullopt, std::nullopt);
    REQUIRE_FALSE(none.library.has_value());
    REQUIRE(none.resolved == bare);

    const VariationPreview broken = previewVariation(project, std::nullopt, libraryPaths(tmp.path() / "Nothing"));
    REQUIRE_FALSE(broken.library.has_value());
    REQUIRE(broken.resolved == bare);
    REQUIRE(variationPreviewToJson(broken)["library"].is_null());
}

TEST_CASE("the variation preview answer lists settings, changes, frames, rooms and props", "[core][buildcheck][variation]")
{
    VariationPreview preview;
    preview.library = libraryPaths(fs::path("C:/Lib"));
    Project& project = preview.resolved.project;
    project = plynth::test::buildProject();
    project.settings.variation = {9, true, false};
    project.plan.floors[0].rooms[0].dressing = true;
    preview.resolved.changes = {{VariationTarget::Item, "i1", "sofa", "sofa_b"}, {VariationTarget::Outside, "", "brick", "plaster"}};
    preview.resolved.frames = {{"o1", "door_a"}};
    DressingPlacement prop;
    prop.room = "r1";
    prop.host = "i1";
    prop.rule = "books_on_shelves";
    prop.asset = "books";
    prop.position = {1.0, 2.0};
    prop.z = 0.4;
    prop.rotation = 90.0;
    DressingPlacement elsewhere = prop;
    elsewhere.room = "r9";
    preview.resolved.dressing = {{"f1", {prop, elsewhere}}};

    const Json json = variationPreviewToJson(preview);
    std::vector<std::string> keys;
    for (auto it = json.begin(); it != json.end(); ++it) keys.push_back(it.key());
    REQUIRE(keys == std::vector<std::string>{"seed", "furniture", "materials", "library", "changes", "frames", "rooms",
                                             "prop_count"});
    REQUIRE(json["seed"] == 9);
    REQUIRE(json["furniture"] == true);
    REQUIRE(json["materials"] == false);
    REQUIRE(json["library"] == pathToUtf8(preview.library->root));
    REQUIRE(json["changes"].size() == 2);
    REQUIRE(json["changes"][0]["target"] == std::string(variationTargetName(VariationTarget::Item)));
    REQUIRE(json["changes"][0]["object"] == "i1");
    REQUIRE(json["changes"][0]["from"] == "sofa");
    REQUIRE(json["changes"][0]["to"] == "sofa_b");
    REQUIRE(json["changes"][1]["object"].is_null());
    REQUIRE(json["frames"] == Json::array({Json{{"opening", "o1"}, {"asset", "door_a"}}}));
    REQUIRE(json["rooms"].size() == project.plan.floors[0].rooms.size());
    REQUIRE(json["rooms"][0]["floor"] == "f1");
    REQUIRE(json["rooms"][0]["room"] == "r1");
    REQUIRE(json["rooms"][0]["dressing"] == true);
    REQUIRE(json["rooms"][0]["props"].size() == 1);
    const Json& shown = json["rooms"][0]["props"][0];
    REQUIRE(shown["asset"] == "books");
    REQUIRE(shown["rule"] == "books_on_shelves");
    REQUIRE(shown["host"] == "i1");
    REQUIRE(shown["x"] == 1.0);
    REQUIRE(shown["y"] == 2.0);
    REQUIRE(shown["z"] == 0.4);
    REQUIRE(shown["rotation"] == 90.0);
    REQUIRE(json["rooms"][1]["props"].empty());
    // A prop of a room that is not in the plan is not listed or counted.
    REQUIRE(json["prop_count"] == 1);
}
