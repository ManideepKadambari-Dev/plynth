#include "TestSupport.h"
#include "SchemaSupport.h"

#include <plynth/core/Generators.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

fs::path dataFolder()
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "generators";
}

std::string columnSource()
{
    return plynth::test::readTextFile(dataFolder() / "column.py");
}

// The column file with one piece of text replaced.
std::string columnWith(const std::string& from, const std::string& to)
{
    std::string text = columnSource();
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

GeneratorInfo columnInfo()
{
    const GeneratorFile file = parseGeneratorSource(columnSource(), "column");
    plynth::test::requireOk(file.status);
    return file.info;
}

// Changes one value of a set that has it.
void setValue(std::vector<GeneratorParamValue>& values, const std::string& id, const GeneratorValue& value)
{
    for (GeneratorParamValue& v : values) {
        if (v.id == id) v.value = value;
    }
}

void writeText(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

void requireRefused(const std::string& source, const std::string& where = {})
{
    const GeneratorFile file = parseGeneratorSource(source, "column");
    INFO(file.status.message);
    REQUIRE(file.status.code == ErrorCode::BadFile);
    REQUIRE_FALSE(file.status.message.empty());
    if (!where.empty()) REQUIRE(file.status.where == where);
}

}

TEST_CASE("the column declaration parses to the expected info", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    REQUIRE(info.id == "column");
    REQUIRE(info.name == "Column");
    REQUIRE(info.description == "A round or square column from the floor up.");
    REQUIRE(info.addons.empty());
    REQUIRE(info.params.size() == 6);

    const GeneratorParam& diameter = info.params[0];
    REQUIRE(diameter.id == "diameter");
    REQUIRE(diameter.label == "Diameter");
    REQUIRE(diameter.type == GeneratorParamType::Number);
    REQUIRE(diameter.unit == GeneratorUnit::Metres);
    REQUIRE(diameter.defaultValue == GeneratorValue(0.3));
    REQUIRE_FALSE(diameter.defaultCeiling);
    REQUIRE(diameter.min == 0.05);
    REQUIRE(diameter.max == 2.0);

    const GeneratorParam& height = info.params[1];
    REQUIRE(height.defaultCeiling);
    REQUIRE(height.min == 0.1);
    REQUIRE(height.max == 20.0);

    const GeneratorParam& shape = info.params[2];
    REQUIRE(shape.type == GeneratorParamType::Choice);
    REQUIRE(shape.defaultValue == GeneratorValue(std::string("round")));
    REQUIRE(shape.choices == std::vector<GeneratorChoice>{{"round", "Round"}, {"square", "Square"}});

    const GeneratorParam& sides = info.params[3];
    REQUIRE(sides.type == GeneratorParamType::Integer);
    REQUIRE(sides.defaultValue == GeneratorValue(24.0));
    REQUIRE(sides.min == 3.0);
    REQUIRE(sides.max == 64.0);

    REQUIRE(info.params[4].type == GeneratorParamType::Boolean);
    REQUIRE(info.params[4].defaultValue == GeneratorValue(true));
    REQUIRE(info.params[5].type == GeneratorParamType::Material);
    REQUIRE(info.params[5].defaultValue == GeneratorValue(std::string("white_paint")));

    REQUIRE(info.size.width.param == "diameter");
    REQUIRE(info.size.depth.param == "diameter");
    REQUIRE(info.size.height.param == "height");
    REQUIRE(info.size.width.times == 1.0);
    REQUIRE(info.size.width.plus == 0.0);
    REQUIRE(info.findParam("sides") == &info.params[3]);
    REQUIRE(info.findParam("nope") == nullptr);
}

TEST_CASE("comments and blank lines before the docstring are allowed", "[generators]")
{
    const GeneratorFile file = parseGeneratorSource("# a note\n\n  # another note\n\n" + columnSource(), "column");
    plynth::test::requireOk(file.status);
    REQUIRE(file.info.name == "Column");

    const GeneratorFile single = parseGeneratorSource(
        "'''plynth-generator\n"
        "{\"format\": \"plynth-generator\", \"version\": 1, \"name\": \"A\","
        " \"params\": [{\"id\": \"w\", \"label\": \"W\", \"type\": \"boolean\", \"default\": false}],"
        " \"size\": {\"width\": 1, \"depth\": 1, \"height\": 1}}\n'''\n",
        "a");
    plynth::test::requireOk(single.status);
    REQUIRE(single.info.size.width.value == 1.0);
}

TEST_CASE("each refusal of parseGeneratorSource is a bad file", "[generators]")
{
    requireRefused("import bpy\n\ndef generate(gen):\n    return []\n");
    requireRefused(columnWith("\"\"\"plynth-generator", "\"\"\"something else"));
    requireRefused(columnWith("\"addons\": [],", "\"addons\": ["));
    requireRefused(columnWith("\"version\": 1,", "\"version\": 2,"), "/version");
    requireRefused(columnWith("\"addons\": [],", "\"addons\": [], \"colour\": 1,"), "/colour");
    requireRefused(columnWith("{\"id\": \"sides\"", "{\"id\": \"shape\""), "/params/3/id");
    requireRefused(columnWith("\"default\": 0.3, \"min\"", "\"default\": 5.0, \"min\""), "/params/0/default");
    requireRefused(columnWith("\"type\": \"number\", \"unit\": \"m\", \"default\": \"ceiling_height\"",
                              "\"type\": \"number\", \"unit\": \"deg\", \"default\": \"ceiling_height\""),
                   "/params/1/default");
    requireRefused(columnWith("\"width\": {\"param\": \"diameter\"}", "\"width\": {\"param\": \"shape\"}"), "/size/width/param");
    requireRefused(columnWith("\"min\": 3, \"max\": 64", "\"min\": 3, \"max\": 64, \"step\": 0"), "/params/3/step");
    requireRefused(columnWith("\"width\": {\"param\": \"diameter\"}", "\"width\": 0"), "/size/width");
    requireRefused(columnWith("\"default\": \"round\"", "\"default\": \"hex\""), "/params/2/default");
    requireRefused(columnWith("\"min\": 3, \"max\": 64", "\"min\": 70, \"max\": 64"));
    requireRefused(columnWith("\"addons\": [],", "\"addons\": [\"Bad Name\"],"), "/addons/0");
    requireRefused(columnWith("\"name\": \"Column\"", "\"name\": \"\""), "/name");
    requireRefused(columnWith("\"default\": \"white_paint\"", "\"default\": \"Bad Id\""), "/params/5/default");
    requireRefused(columnWith("\"width\": {\"param\": \"diameter\"}", "\"width\": {\"param\": \"nope\"}"), "/size/width/param");

    REQUIRE(parseGeneratorSource(columnSource(), "Bad Name").status.code == ErrorCode::BadFile);
}

TEST_CASE("a generator file is read from disk", "[generators]")
{
    plynth::test::TempDir tmp("gen-file");
    const GeneratorFile file = readGeneratorFile(dataFolder() / "column.py");
    plynth::test::requireOk(file.status);
    REQUIRE(file.info.id == "column");
    REQUIRE(file.info.file.is_absolute());

    REQUIRE(readGeneratorFile(tmp.path() / "none.py").status.code == ErrorCode::CannotRead);
    writeText(tmp.path() / "big.py", std::string(kMaxGeneratorFileBytes + 1, '#'));
    REQUIRE(readGeneratorFile(tmp.path() / "big.py").status.code == ErrorCode::BadFile);
}

TEST_CASE("loadGeneratorFolder sorts, skips helpers and subfolders and lists bad files", "[generators]")
{
    plynth::test::TempDir tmp("gen-folder");
    const fs::path folder = tmp.path() / "generators";
    writeText(folder / "b_second.py", columnSource());
    writeText(folder / "a_first.py", columnSource());
    writeText(folder / "_helpers.py", columnSource());
    writeText(folder / "sub" / "inner.py", columnSource());
    writeText(folder / "bad.py", "print('no declaration')\n");
    writeText(folder / "notes.txt", "not a generator");

    const GeneratorLibrary library = loadGeneratorFolder(folder);
    REQUIRE(library.generators.size() == 2);
    REQUIRE(library.generators[0].id == "a_first");
    REQUIRE(library.generators[1].id == "b_second");
    REQUIRE(library.refused.size() == 1);
    REQUIRE(library.refused[0].first.filename() == "bad.py");
    REQUIRE(library.refused[0].second.code == ErrorCode::BadFile);
    REQUIRE(library.find("a_first") == &library.generators[0]);
    REQUIRE(library.find("column") == nullptr);

    const GeneratorLibrary missing = loadGeneratorFolder(tmp.path() / "nothing");
    REQUIRE(missing.generators.empty());
    REQUIRE(missing.refused.empty());
}

TEST_CASE("defaults put the ceiling height in a ceiling default", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    const std::vector<GeneratorParamValue> values = defaultGeneratorParams(info, 2.7);
    REQUIRE(values.size() == 6);
    REQUIRE(values[0].id == "diameter");
    REQUIRE(values[1].id == "height");
    REQUIRE(values[2].id == "material");
    REQUIRE(values[3].id == "plinth");
    REQUIRE(values[4].id == "shape");
    REQUIRE(values[5].id == "sides");
    REQUIRE(*findGeneratorValue(values, "height") == GeneratorValue(2.7));
    REQUIRE(*findGeneratorValue(values, "plinth") == GeneratorValue(true));
    plynth::test::requireOk(checkGeneratorParams(info, values));
    REQUIRE(*findGeneratorValue(defaultGeneratorParams(info, 50.0), "height") == GeneratorValue(20.0));
}

TEST_CASE("checkGeneratorParams refuses values that do not fit", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    const std::vector<GeneratorParamValue> good = defaultGeneratorParams(info, 2.7);

    const auto refuses = [&](const std::vector<GeneratorParamValue>& values, const std::string& named) {
        const Status status = checkGeneratorParams(info, values);
        INFO(status.message);
        REQUIRE(status.code == ErrorCode::InvalidValue);
        REQUIRE_THAT(status.message, Catch::Matchers::ContainsSubstring(named));
    };

    std::vector<GeneratorParamValue> values = good;
    values.erase(values.begin());
    refuses(values, "diameter");

    values = good;
    values.insert(values.begin() + 2, {"extra", 1.0});
    refuses(values, "extra");

    values = good;
    std::swap(values[0], values[1]);
    refuses(values, "sorted");

    values = good;
    values.insert(values.begin(), values[0]);
    refuses(values, "twice");

    values = good;
    setValue(values, "sides", 2.5);
    refuses(values, "sides");

    values = good;
    setValue(values, "shape", std::string("hex"));
    refuses(values, "shape");

    values = good;
    setValue(values, "material", std::string("Bad Id"));
    refuses(values, "material");

    values = good;
    setValue(values, "diameter", 5.0);
    refuses(values, "diameter");

    values = good;
    setValue(values, "plinth", std::string("yes"));
    refuses(values, "plinth");

    values = good;
    setValue(values, "material", std::string());
    plynth::test::requireOk(checkGeneratorParams(info, values));
}

TEST_CASE("fitGeneratorParams keeps what fits and says what it changed", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    const std::vector<GeneratorParamValue> good = defaultGeneratorParams(info, 2.7);

    const GeneratorParamsFit same = fitGeneratorParams(info, good, 3.0);
    REQUIRE_FALSE(same.changed);
    REQUIRE(same.notes.empty());
    REQUIRE(same.params == good);

    std::vector<GeneratorParamValue> stored = good;
    stored.push_back({"old", 1.0});
    setValue(stored, "sides", std::string("many"));
    setValue(stored, "diameter", 0.5);
    stored.erase(stored.begin() + 1);
    const GeneratorParamsFit fit = fitGeneratorParams(info, stored, 3.0);
    REQUIRE(fit.changed);
    REQUIRE(fit.notes.size() == 3);
    plynth::test::requireOk(checkGeneratorParams(info, fit.params));
    REQUIRE(findGeneratorValue(fit.params, "old") == nullptr);
    REQUIRE(*findGeneratorValue(fit.params, "diameter") == GeneratorValue(0.5));
    REQUIRE(*findGeneratorValue(fit.params, "sides") == GeneratorValue(24.0));
    REQUIRE(*findGeneratorValue(fit.params, "height") == GeneratorValue(3.0));
}

TEST_CASE("parseGeneratorValue reads what a person types", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    const auto read = [&](const char* id, const char* text) { return parseGeneratorValue(*info.findParam(id), text); };

    REQUIRE(read("diameter", "0.25").value == GeneratorValue(0.25));
    REQUIRE(read("plinth", "yes").value == GeneratorValue(true));
    REQUIRE(read("plinth", "0").value == GeneratorValue(false));
    REQUIRE(read("material", "none").value == GeneratorValue(std::string()));
    REQUIRE(read("material", "brushed_steel").value == GeneratorValue(std::string("brushed_steel")));
    REQUIRE(read("shape", "square").value == GeneratorValue(std::string("square")));
    REQUIRE(read("sides", "12").value == GeneratorValue(12.0));
    REQUIRE(read("diameter", "abc").status.code == ErrorCode::InvalidValue);
    REQUIRE(read("diameter", "").status.code == ErrorCode::InvalidValue);
    REQUIRE(read("sides", "2.5").status.code == ErrorCode::InvalidValue);
    REQUIRE(read("shape", "hex").status.code == ErrorCode::InvalidValue);
    REQUIRE(read("plinth", "maybe").status.code == ErrorCode::InvalidValue);
    REQUIRE(read("material", "Bad Id").status.code == ErrorCode::InvalidValue);
    REQUIRE_THAT(read("diameter", "abc").status.message, Catch::Matchers::ContainsSubstring("diameter"));
}

TEST_CASE("generatorValueText writes plain text", "[generators]")
{
    REQUIRE(generatorValueText(0.30000) == "0.3");
    REQUIRE(generatorValueText(24.0) == "24");
    REQUIRE(generatorValueText(2.7) == "2.7");
    REQUIRE(generatorValueText(0.1234567891) == "0.123457");
    REQUIRE(generatorValueText(-0.0000001) == "0");
    REQUIRE(generatorValueText(true) == "true");
    REQUIRE(generatorValueText(false) == "false");
    REQUIRE(generatorValueText(std::string("square")) == "square");
    REQUIRE(generatorValueText(std::string()) == "none");
}

TEST_CASE("setGeneratorParam and findGeneratorValue", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    std::vector<GeneratorParamValue> values = defaultGeneratorParams(info, 2.7);
    plynth::test::requireOk(setGeneratorParam(info, values, "diameter", 0.4));
    REQUIRE(*findGeneratorValue(values, "diameter") == GeneratorValue(0.4));
    REQUIRE(setGeneratorParam(info, values, "nope", 1.0).code == ErrorCode::InvalidValue);
    REQUIRE(setGeneratorParam(info, values, "diameter", 9.0).code == ErrorCode::InvalidValue);
    REQUIRE(setGeneratorParam(info, values, "diameter", std::string("x")).code == ErrorCode::InvalidValue);
    REQUIRE(*findGeneratorValue(values, "diameter") == GeneratorValue(0.4));

    std::vector<GeneratorParamValue> partial;
    plynth::test::requireOk(setGeneratorParam(info, partial, "sides", 8.0));
    plynth::test::requireOk(setGeneratorParam(info, partial, "diameter", 0.2));
    REQUIRE(partial[0].id == "diameter");
    REQUIRE(partial[1].id == "sides");
    REQUIRE(findGeneratorValue(partial, "height") == nullptr);
}

TEST_CASE("generatorSize follows the size rule", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    const std::vector<GeneratorParamValue> values = defaultGeneratorParams(info, 2.7);
    const GeneratorSize size = generatorSize(info, values);
    plynth::test::requireOk(size.status);
    REQUIRE(size.size.width == 0.3);
    REQUIRE(size.size.depth == 0.3);
    REQUIRE(size.size.height == 2.7);

    const GeneratorFile scaled = parseGeneratorSource(
        columnWith("\"width\": {\"param\": \"diameter\"}", "\"width\": {\"param\": \"diameter\", \"times\": 2, \"plus\": 0.1}"),
        "column");
    plynth::test::requireOk(scaled.status);
    const GeneratorSize wide = generatorSize(scaled.info, values);
    plynth::test::requireOk(wide.status);
    REQUIRE(wide.size.width == Catch::Approx(0.7));

    const GeneratorFile flat = parseGeneratorSource(
        columnWith("\"width\": {\"param\": \"diameter\"}", "\"width\": {\"param\": \"diameter\", \"plus\": -0.3}"), "column");
    plynth::test::requireOk(flat.status);
    REQUIRE(generatorSize(flat.info, values).status.code == ErrorCode::InvalidValue);

    std::vector<GeneratorParamValue> bad = values;
    bad.pop_back();
    REQUIRE(generatorSize(info, bad).status.code == ErrorCode::InvalidValue);
}

TEST_CASE("makeGeneratorUse gives the use and the size", "[generators]")
{
    const GeneratorInfo info = columnInfo();
    const std::vector<GeneratorParamValue> values = defaultGeneratorParams(info, 2.5);
    const GeneratorPlacement placement = makeGeneratorUse(info, values);
    plynth::test::requireOk(placement.status);
    REQUIRE(placement.use.name == "column");
    REQUIRE(placement.use.params == values);
    REQUIRE(placement.size.height == 2.5);

    std::vector<GeneratorParamValue> bad = values;
    bad.pop_back();
    REQUIRE(makeGeneratorUse(info, bad).status.code == ErrorCode::InvalidValue);
    REQUIRE(generatorMaterialParams(info) == std::vector<std::string>{"material"});
    REQUIRE(isGeneratorId("column_2"));
    REQUIRE_FALSE(isGeneratorId("2column"));
    REQUIRE_FALSE(isGeneratorId("Column"));
    REQUIRE_FALSE(isGeneratorId(""));
}

TEST_CASE("generatorState follows the add-on cache", "[generators]")
{
    const GeneratorInfo column = columnInfo();
    const GeneratorFile archimeshFile = readGeneratorFile(dataFolder() / "archimesh_column.py");
    plynth::test::requireOk(archimeshFile.status);
    const GeneratorInfo& archimesh = archimeshFile.info;
    REQUIRE(archimesh.addons == std::vector<std::string>{"archimesh"});

    AddonCache off;
    AddonCache on;
    on.enabled.push_back({"bl_ext.blender_org.Archimesh", "Archimesh", "1.2.5"});

    REQUIRE(generatorState(column, std::nullopt).availability == GeneratorAvailability::Available);
    REQUIRE(generatorState(column, off).availability == GeneratorAvailability::Available);
    REQUIRE(generatorState(column, std::nullopt).message.empty());
    REQUIRE(generatorState(archimesh, on).availability == GeneratorAvailability::Available);

    const GeneratorState missing = generatorState(archimesh, off);
    REQUIRE(missing.availability == GeneratorAvailability::NeedsAddon);
    REQUIRE(missing.missingAddons == std::vector<std::string>{"archimesh"});
    REQUIRE_THAT(missing.message, Catch::Matchers::ContainsSubstring("Archimesh"));
    REQUIRE_THAT(missing.message, Catch::Matchers::ContainsSubstring("not enabled"));

    const GeneratorState unknown = generatorState(archimesh, std::nullopt);
    REQUIRE(unknown.availability == GeneratorAvailability::Unknown);
    REQUIRE(unknown.missingAddons.empty());
    REQUIRE_THAT(unknown.message, Catch::Matchers::ContainsSubstring("Archimesh"));

    REQUIRE(generatorAvailabilityName(GeneratorAvailability::Available) == "available");
    REQUIRE(generatorAvailabilityName(GeneratorAvailability::NeedsAddon) == "needs_addon");
    REQUIRE(generatorAvailabilityName(GeneratorAvailability::Unknown) == "unknown");

    GeneratorInfo two = archimesh;
    two.addons = {"archimesh", "home_builder_5"};
    const GeneratorState both = generatorState(two, off);
    REQUIRE(both.missingAddons.size() == 2);
    REQUIRE_THAT(both.message, Catch::Matchers::ContainsSubstring("Archimesh and Home Builder add-ons"));
}

TEST_CASE("generatorItemLookupFor answers for placed generators", "[generators]")
{
    const GeneratorLibrary library = loadGeneratorFolder(dataFolder());
    REQUIRE(library.generators.size() == 2);
    REQUIRE(library.refused.empty());
    const GeneratorInfo& column = *library.find("column");
    const GeneratorInfo& archimesh = *library.find("archimesh_column");

    const AddonCache off;
    const GeneratorItemLookup lookup = generatorItemLookupFor(library, off);

    GeneratorUse missing;
    missing.name = "nothing";
    REQUIRE_FALSE(lookup(missing).has_value());

    GeneratorUse use;
    use.name = "column";
    use.params = defaultGeneratorParams(column, 2.7);
    const auto state = lookup(use);
    REQUIRE(state.has_value());
    REQUIRE(state->available);
    REQUIRE(state->valuesFit);
    REQUIRE(state->size.has_value());
    REQUIRE(state->size->height == 2.7);

    use.params.pop_back();
    const auto broken = lookup(use);
    REQUIRE_FALSE(broken->valuesFit);
    REQUIRE_FALSE(broken->size.has_value());

    GeneratorUse arch;
    arch.name = "archimesh_column";
    arch.params = defaultGeneratorParams(archimesh, 2.7);
    const auto unavailable = lookup(arch);
    REQUIRE_FALSE(unavailable->available);
    REQUIRE_THAT(unavailable->unavailableReason, Catch::Matchers::ContainsSubstring("Archimesh"));

    const auto unchecked = generatorItemLookupFor(library, std::nullopt)(arch);
    REQUIRE(unchecked->available);
}

TEST_CASE("addSampleGenerators copies once and never overwrites", "[generators]")
{
    plynth::test::TempDir tmp("gen-samples");
    const fs::path from = tmp.path() / "samples";
    const fs::path to = tmp.path() / "Library" / "generators";
    writeText(from / "a.py", "alpha");
    writeText(from / "b.py", "beta");
    writeText(from / "readme.md", "not copied");

    const SampleGeneratorsCopy first = addSampleGenerators(to, from);
    plynth::test::requireOk(first.status);
    REQUIRE(first.copied == std::vector<std::string>{"a.py", "b.py"});
    REQUIRE(first.skipped.empty());
    REQUIRE_FALSE(fs::exists(to / "readme.md"));

    const SampleGeneratorsCopy second = addSampleGenerators(to, from);
    plynth::test::requireOk(second.status);
    REQUIRE(second.copied.empty());
    REQUIRE(second.skipped == std::vector<std::string>{"a.py", "b.py"});

    writeText(to / "a.py", "my own edit\r\n");
    const SampleGeneratorsCopy third = addSampleGenerators(to, from);
    REQUIRE(third.skipped.size() == 2);
    REQUIRE(plynth::test::readTextFile(to / "a.py") == "my own edit\r\n");

    REQUIRE(addSampleGenerators(to, tmp.path() / "nowhere").status.code == ErrorCode::NotFound);
}
