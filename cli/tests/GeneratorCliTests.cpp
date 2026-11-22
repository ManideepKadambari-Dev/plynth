#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Addons.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::PrivateUser;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::utf8;

namespace {

// Clears the settings file and the add-on cache when a test starts and when it ends.
struct GeneratorReset {
    GeneratorReset() { clear(); }
    ~GeneratorReset() { clear(); }
    static void clear() {
        plynth::cli::testing::removeSettingsFileSafely();
        std::error_code ignored;
        std::filesystem::remove(plynth::addonCacheFile(), ignored);
    }
};

// Writes a text file.
void writeText(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A library made by the CLI and set in the settings, with the two test generators, a broken file and a helper
// whose name starts with an underscore. Returns the library folder.
std::filesystem::path makeLibrary(const Sample& sample) {
    const auto library = sample.temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    const std::filesystem::path source = PLYNTH_TEST_GENERATORS;
    for (const char* name : {"column.py", "archimesh_column.py"}) {
        std::filesystem::copy_file(source / name, library / "generators" / name,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    writeText(library / "generators" / "broken.py", "print('not a generator')\n");
    writeText(library / "generators" / "_helper.py", "x = 1\n");
    return library;
}

// The hall r1 (walls w1 to w4, x 1 to 5, y 3 to 7) with the library ready for generator commands.
std::filesystem::path prepare(const Sample& sample) {
    const auto library = makeLibrary(sample);
    sample.drawRoomSquare();
    return library;
}

// The generator entry with this id from a list result.
nlohmann::json entryOf(const nlohmann::json& result, const std::string& id) {
    for (const auto& entry : result.at("generators")) {
        if (entry.at("id") == id) {
            return entry;
        }
    }
    INFO("No generator " << id);
    REQUIRE(false);
    return {};
}

// Checks the saved plan against its schema.
void checkPlanSchema(const Sample& sample) {
    std::ifstream schemaFile(std::filesystem::path(PLYNTH_SCHEMA_DIR) / "plan.schema.json");
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schemaFile));
    const auto plan = nlohmann::json::parse(readFile(sample.folder / "plan.json"));
    CHECK_NOTHROW(validator.validate(plan));
}

}

TEST_CASE("generator list gives the sorted generators and the refused files", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-list");
    makeLibrary(sample);
    const auto outcome = runChecked({"generator", "list"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "generator list");
    const auto& result = outcome.answer.at("result");
    std::vector<std::string> ids;
    for (const auto& entry : result.at("generators")) {
        ids.push_back(entry.at("id").get<std::string>());
    }
    CHECK(ids == std::vector<std::string>{"archimesh_column", "column"});

    const auto column = entryOf(result, "column");
    CHECK(column.at("name") == "Column");
    CHECK(column.at("availability") == "available");
    CHECK(column.at("addons").empty());
    CHECK(column.at("params").size() == 6);
    CHECK_THAT(column.at("default_size").at("height").get<double>(), WithinAbs(3.0, 1e-9));
    CHECK_THAT(column.at("default_size").at("width").get<double>(), WithinAbs(0.3, 1e-9));
    const auto& diameter = column.at("params").at(0);
    CHECK(diameter.at("id") == "diameter");
    CHECK(diameter.at("type") == "number");
    CHECK(diameter.at("unit") == "m");
    CHECK_THAT(diameter.at("default").get<double>(), WithinAbs(0.3, 1e-9));
    CHECK_THAT(diameter.at("min").get<double>(), WithinAbs(0.05, 1e-9));
    CHECK(column.at("params").at(2).at("choices").size() == 2);

    const auto archimesh = entryOf(result, "archimesh_column");
    CHECK(archimesh.at("availability") == "unknown");
    CHECK(archimesh.at("addons").at(0).at("name") == "archimesh");
    CHECK(archimesh.at("addons").at(0).at("display_name") == "Archimesh");
    CHECK(archimesh.at("addons").at(0).at("enabled").is_null());
    CHECK_FALSE(archimesh.at("message").get<std::string>().empty());

    REQUIRE(result.at("refused").size() == 1);
    CHECK(result.at("refused").at(0).at("file") == "broken.py");
    CHECK(result.at("refused").at(0).at("code") == "bad_file");
    CHECK(result.at("addons_checked_at").is_null());
}

TEST_CASE("generator list with no library is no_library", "[cli][generator]") {
    GeneratorReset reset;
    const auto outcome = runChecked({"generator", "list"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "no_library");
}

TEST_CASE("generator list --probe asks the fake Blender and sets the availability", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-probe");
    const PrivateUser user(sample.temp);
    makeLibrary(sample);
    const auto fake = installFakeBlender(sample.temp.path() / "fb52", "5.2.0");
    {
        const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.archimesh");
        const auto outcome = runChecked({"generator", "list", "--probe", "--blender", utf8(fake)});
        REQUIRE(outcome.exitCode == kExitOk);
        const auto& result = outcome.answer.at("result");
        const auto archimesh = entryOf(result, "archimesh_column");
        CHECK(archimesh.at("availability") == "available");
        CHECK(archimesh.at("addons").at(0).at("enabled") == true);
        CHECK(entryOf(result, "column").at("availability") == "available");
        CHECK_FALSE(result.at("addons_checked_at").is_null());

        const auto again = runChecked({"generator", "list", "--blender", utf8(fake)});
        REQUIRE(again.exitCode == kExitOk);
        CHECK(entryOf(again.answer.at("result"), "archimesh_column").at("availability") == "available");
    }
    {
        const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.bool_tool");
        const auto outcome = runChecked({"generator", "list", "--probe", "--blender", utf8(fake)});
        REQUIRE(outcome.exitCode == kExitOk);
        const auto archimesh = entryOf(outcome.answer.at("result"), "archimesh_column");
        CHECK(archimesh.at("availability") == "needs_addon");
        CHECK(archimesh.at("addons").at(0).at("enabled") == false);
    }
}

TEST_CASE("generator check-addons lists the enabled add-ons and the availability", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-check");
    const PrivateUser user(sample.temp);
    makeLibrary(sample);
    const auto fake = installFakeBlender(sample.temp.path() / "fb52", "5.2.0");
    const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.archimesh");
    const auto outcome = runChecked({"generator", "check-addons", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("blender") == utf8(fake));
    CHECK_FALSE(result.at("checked_at").get<std::string>().empty());
    REQUIRE(result.at("enabled").size() == 1);
    CHECK(result.at("enabled").at(0).at("module") == "bl_ext.blender_org.archimesh");
    REQUIRE(result.at("generators").size() == 2);
    CHECK(result.at("generators").at(0).at("id") == "archimesh_column");
    CHECK(result.at("generators").at(0).at("availability") == "available");
}

TEST_CASE("generator check-addons without a usable Blender is blender_not_found", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-check-none");
    const PrivateUser user(sample.temp);
    makeLibrary(sample);
    const auto outcome =
        runChecked({"generator", "check-addons", "--blender", utf8(sample.temp.path() / "nowhere" / "blender.exe")});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "blender_not_found");
}

TEST_CASE("generator info describes one generator and an unknown one is not_found", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-info");
    makeLibrary(sample);
    const auto outcome = runChecked({"generator", "info", "column"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& generator = outcome.answer.at("result").at("generator");
    CHECK(generator.at("id") == "column");
    CHECK(generator.at("params").size() == 6);
    CHECK(generator.at("file").get<std::string>().find("column.py") != std::string::npos);

    const auto missing = runChecked({"generator", "info", "nope"});
    CHECK(missing.exitCode == kExitFailed);
    CHECK(errorCode(missing) == "not_found");
}

TEST_CASE("place --generator puts a column with its size from the values", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-place");
    prepare(sample);
    const auto outcome =
        sample.run({"place", "--generator", "column", "--room", "r1", "--param", "diameter=0.4"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("item");
    CHECK(item.at("id") == "i1");
    CHECK(item.at("asset").is_null());
    CHECK(item.at("asset_name").is_null());
    CHECK(item.at("generator").at("name") == "column");
    CHECK(item.at("generator").at("display_name") == "Column");
    CHECK(item.at("generator").at("state") == "ok");
    CHECK_THAT(item.at("generator").at("params").at("diameter").get<double>(), WithinAbs(0.4, 1e-9));
    CHECK(item.at("generator").at("params").at("sides") == 24);
    CHECK(item.at("generator").at("params").at("plinth") == true);
    CHECK(item.at("generator").at("params").at("shape") == "round");
    CHECK_THAT(item.at("size").at("width").get<double>(), WithinAbs(0.4, 1e-9));
    CHECK_THAT(item.at("size").at("depth").get<double>(), WithinAbs(0.4, 1e-9));
    CHECK_THAT(item.at("size").at("height").get<double>(), WithinAbs(3.0, 1e-9));
    checkPlanSchema(sample);

    const auto list = sample.run({"item", "list"});
    REQUIRE(list.exitCode == kExitOk);
    const auto& listed = list.answer.at("result").at("items").at(0);
    CHECK(listed.at("asset").is_null());
    CHECK(listed.at("generator").at("name") == "column");
}

TEST_CASE("place --generator uses the floor's ceiling height", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-ceiling");
    prepare(sample);
    REQUIRE(sample.run({"floor", "set", "--floor", "f1", "--ceiling-height", "2.6"}).exitCode == kExitOk);
    const auto outcome = sample.run({"place", "--generator", "column", "--x", "3", "--y", "5"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK_THAT(outcome.answer.at("result").at("item").at("size").at("height").get<double>(), WithinAbs(2.6, 1e-9));
}

TEST_CASE("place --generator refuses bad values and leaves the plan alone", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-refuse");
    prepare(sample);
    const std::string before = readFile(sample.folder / "plan.json");

    const auto fraction = sample.run({"place", "--generator", "column", "--room", "r1", "--param", "sides=2.5"});
    CHECK(fraction.exitCode == kExitFailed);
    CHECK(errorCode(fraction) == "invalid_value");
    CHECK(fraction.answer.at("error").at("message").get<std::string>().find("sides") != std::string::npos);

    const auto unknownParam = sample.run({"place", "--generator", "column", "--room", "r1", "--param", "nope=1"});
    CHECK(errorCode(unknownParam) == "invalid_value");

    const auto noEquals = sample.run({"place", "--generator", "column", "--room", "r1", "--param", "diameter"});
    CHECK(errorCode(noEquals) == "invalid_value");

    const auto range = sample.run({"place", "--generator", "column", "--room", "r1", "--param", "diameter=9"});
    CHECK(errorCode(range) == "invalid_value");

    const auto unknown = sample.run({"place", "--generator", "nothing", "--room", "r1"});
    CHECK(unknown.exitCode == kExitFailed);
    CHECK(errorCode(unknown) == "not_found");

    const auto both = sample.run({"place", "sofa", "--generator", "column", "--room", "r1"});
    CHECK(both.exitCode == kExitBadArguments);
    CHECK(errorCode(both) == "bad_arguments");

    const auto neither = sample.run({"place", "--room", "r1"});
    CHECK(neither.exitCode == kExitBadArguments);

    const auto paramWithAsset = sample.run({"place", "sofa", "--room", "r1", "--param", "a=1"});
    CHECK(paramWithAsset.exitCode == kExitBadArguments);

    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("a generator that needs an add-on is refused when it is off and placed when unknown", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-addon");
    const PrivateUser user(sample.temp);
    // The variable beats the saved path, so it must not name a Blender of this machine.
    const ScopedEnvironment variable("PLYNTH_BLENDER", "");
    prepare(sample);

    const auto unknown = sample.run({"place", "--generator", "archimesh_column", "--room", "r1"});
    REQUIRE(unknown.exitCode == kExitOk);
    CHECK(unknown.answer.at("result").at("item").at("generator").at("state") == "unknown");

    const auto fake = installFakeBlender(sample.temp.path() / "fb52", "5.2.0");
    const ScopedEnvironment addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.bool_tool");
    REQUIRE(runChecked({"generator", "check-addons", "--blender", utf8(fake)}).exitCode == kExitOk);
    REQUIRE(runChecked({"settings", "set", "--blender", utf8(fake)}).exitCode == kExitOk);
    const std::string before = readFile(sample.folder / "plan.json");
    const auto off = sample.run({"place", "--generator", "archimesh_column", "--room", "r1"});
    CHECK(off.exitCode == kExitFailed);
    CHECK(errorCode(off) == "invalid_value");
    CHECK(off.answer.at("error").at("message").get<std::string>().find("Archimesh") != std::string::npos);
    CHECK(readFile(sample.folder / "plan.json") == before);

    const auto list = sample.run({"item", "list"});
    REQUIRE(list.exitCode == kExitOk);
    CHECK(list.answer.at("result").at("items").at(0).at("generator").at("state") == "unavailable");
    const auto& problems = list.answer.at("result").at("items").at(0).at("problems");
    std::vector<std::string> codes;
    for (const auto& problem : problems) {
        codes.push_back(problem.at("code").get<std::string>());
    }
    CHECK(std::ranges::find(codes, "generator_unavailable") != codes.end());
}

TEST_CASE("item params changes, resets and fits the values of a generator item", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-params");
    const auto library = prepare(sample);
    REQUIRE(sample.run({"place", "--generator", "column", "--room", "r1", "--param", "diameter=0.5"}).exitCode ==
            kExitOk);

    const auto square = sample.run({"item", "params", "i1", "--param", "shape=square", "--param", "sides=8"});
    REQUIRE(square.exitCode == kExitOk);
    const auto& params = square.answer.at("result").at("item").at("generator").at("params");
    CHECK(params.at("shape") == "square");
    CHECK(params.at("sides") == 8);
    CHECK_THAT(params.at("diameter").get<double>(), WithinAbs(0.5, 1e-9));
    checkPlanSchema(sample);

    const auto resized = sample.run({"item", "params", "i1", "--param", "diameter=0.8"});
    REQUIRE(resized.exitCode == kExitOk);
    CHECK_THAT(resized.answer.at("result").at("item").at("size").at("width").get<double>(), WithinAbs(0.8, 1e-9));

    const auto resetAll = sample.run({"item", "params", "i1", "--reset"});
    REQUIRE(resetAll.exitCode == kExitOk);
    const auto& defaults = resetAll.answer.at("result").at("item").at("generator").at("params");
    CHECK(defaults.at("shape") == "round");
    CHECK(defaults.at("sides") == 24);
    CHECK_THAT(defaults.at("diameter").get<double>(), WithinAbs(0.3, 1e-9));
    CHECK_THAT(resetAll.answer.at("result").at("item").at("size").at("width").get<double>(), WithinAbs(0.3, 1e-9));

    const auto bad = sample.run({"item", "params", "i1", "--param", "sides=2.5"});
    CHECK(errorCode(bad) == "invalid_value");
    const auto nothing = sample.run({"item", "params", "i1"});
    CHECK(nothing.exitCode == kExitBadArguments);
    const auto both = sample.run({"item", "params", "i1", "--reset", "--fit"});
    CHECK(both.exitCode == kExitBadArguments);
    const auto missing = sample.run({"item", "params", "i9", "--reset"});
    CHECK(errorCode(missing) == "not_found");

    const std::string text = readFile(library / "generators" / "column.py");
    const std::string drop = "  {\"id\": \"plinth\", \"label\": \"Plinth and cap\", \"type\": \"boolean\", \"default\": true},\n";
    const std::size_t at = text.find(drop);
    REQUIRE(at != std::string::npos);
    std::string changed = text;
    changed.erase(at, drop.size());
    writeText(library / "generators" / "column.py", changed);

    const auto stale = sample.run({"item", "list"});
    REQUIRE(stale.exitCode == kExitOk);
    CHECK(stale.answer.at("result").at("items").at(0).at("generator").at("state") == "changed");
    const auto refused = sample.run({"item", "params", "i1", "--param", "sides=8"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");

    const auto fitted = sample.run({"item", "params", "i1", "--fit"});
    REQUIRE(fitted.exitCode == kExitOk);
    const auto& item = fitted.answer.at("result").at("item");
    CHECK(item.at("generator").at("state") == "ok");
    CHECK_FALSE(item.at("generator").at("params").contains("plinth"));
    checkPlanSchema(sample);
}

TEST_CASE("item params on an asset item is invalid_value", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-params-asset");
    const auto library = prepare(sample);
    plynth::Status status;
    auto catalog = plynth::AssetCatalog::open(plynth::libraryPaths(library), status);
    REQUIRE(status.ok());
    const auto folder = catalog->paths().assetItems / "sofa";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "asset.blend", std::ios::binary) << "x";
    plynth::AssetInfo info;
    info.id = "sofa";
    info.name = "Sofa";
    info.categories = {"sofa"};
    info.source.kind = "local_file";
    info.source.reference = "sofa.obj";
    info.source.importedAt = plynth::sessionNow();
    info.size = {2.0, 0.9, 0.85};
    info.triangles = 12;
    info.blendBytes = 1;
    info.blenderVersion = "5.2.0";
    REQUIRE(plynth::writeAssetInfo(folder / "info.json", info).ok());
    REQUIRE(catalog->indexAsset(info).ok());
    catalog.reset();

    REQUIRE(sample.run({"place", "sofa", "--room", "r1"}).exitCode == kExitOk);
    const auto outcome = sample.run({"item", "params", "i1", "--reset"});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "invalid_value");
    const auto list = sample.run({"item", "list"});
    CHECK(list.answer.at("result").at("items").at(0).at("generator").is_null());
    CHECK(list.answer.at("result").at("items").at(0).at("asset") == "sofa");
}

TEST_CASE("library add-sample-generators copies and then skips", "[cli][generator]") {
    GeneratorReset reset;
    const Sample sample("gen-samples");
    const auto library = sample.temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    const std::filesystem::path source = PLYNTH_TEST_GENERATORS;
    const auto first = runChecked({"library", "add-sample-generators", "--from", utf8(source)});
    REQUIRE(first.exitCode == kExitOk);
    const auto& result = first.answer.at("result");
    CHECK(result.at("folder").get<std::string>().find("generators") != std::string::npos);
    CHECK(result.at("copied").size() == 2);
    CHECK(result.at("skipped").empty());
    CHECK(std::filesystem::exists(library / "generators" / "column.py"));

    const auto second = runChecked({"library", "add-sample-generators", "--from", utf8(source)});
    REQUIRE(second.exitCode == kExitOk);
    CHECK(second.answer.at("result").at("copied").empty());
    CHECK(second.answer.at("result").at("skipped").size() == 2);

    const auto missing = runChecked({"library", "add-sample-generators", "--from", utf8(sample.temp.path() / "none")});
    CHECK(missing.exitCode == kExitFailed);
    CHECK(errorCode(missing) == "not_found");
}

TEST_CASE("help lists the generator commands", "[cli][generator]") {
    const auto outcome = runChecked({"help"});
    REQUIRE(outcome.exitCode == kExitOk);
    const std::string text = outcome.answer.dump();
    for (const char* words : {"generator list", "generator info", "generator check-addons", "item params",
                              "library add-sample-generators"}) {
        INFO(words);
        CHECK(text.find(words) != std::string::npos);
    }
}
