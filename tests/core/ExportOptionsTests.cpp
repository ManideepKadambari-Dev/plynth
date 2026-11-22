#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
using plynth::test::readTextFile;
using plynth::test::schemaProblem;
using plynth::test::TempDir;

namespace {

namespace fs = std::filesystem;

// Turns the ordered JSON that core writes into the plain JSON the validator takes.
nlohmann::json plain(const Json& j)
{
    return nlohmann::json::parse(j.dump());
}

// Writes text to export-options.json in a folder.
void writeOptionsText(const fs::path& folder, const std::string& text)
{
    std::ofstream out(folder / "export-options.json", std::ios::binary);
    out << text;
}

// A data file of the export tests.
fs::path dataFile(const std::string& name)
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "export" / name;
}

// Options with something in every list.
ExportOptions busyOptions()
{
    ExportOptions options;
    options.groups.added = {{"group_1", "Hero"}};
    options.groups.renames = {{"furniture_r1", "Lounge props"}};
    options.groups.merges = {{"furniture_r2", "furniture_r1"}};
    options.groups.moves = {{{"item", "i1"}, "group_1"}, {{"slab", ""}, "structure"}};
    options.kit.enabled = true;
    return options;
}

}

TEST_CASE("export options survive a round trip and validate against the schema", "[core][exportoptions]") {
    const ExportOptions options = busyOptions();
    const Json json = exportOptionsToJson(options);
    REQUIRE(schemaProblem("export-options.schema.json", plain(json)).empty());
    const ExportOptionsRead back = exportOptionsFromJson(json);
    REQUIRE(back.status.ok());
    REQUIRE(back.options == options);

    REQUIRE(schemaProblem("export-options.schema.json", plain(exportOptionsToJson({}))).empty());
    const ExportOptionsRead defaults = exportOptionsFromJson(exportOptionsToJson({}));
    REQUIRE(defaults.status.ok());
    REQUIRE(defaults.options == ExportOptions{});
}

TEST_CASE("an options file with only the required fields reads as the defaults", "[core][exportoptions]") {
    const Json json = Json::parse(R"({"format": "plynth-export-options", "version": 1})");
    const ExportOptionsRead read = exportOptionsFromJson(json);
    REQUIRE(read.status.ok());
    REQUIRE(read.options == ExportOptions{});
}

TEST_CASE("the sample options file validates and reads", "[core][exportoptions]") {
    const nlohmann::json json = nlohmann::json::parse(readTextFile(dataFile("options_valid.json")));
    REQUIRE(schemaProblem("export-options.schema.json", json).empty());
    const ExportOptionsRead read = exportOptionsFromJson(Json::parse(json.dump()));
    REQUIRE(read.status.ok());
    REQUIRE(read.options == busyOptions());
}

TEST_CASE("a missing options file is ok with the defaults", "[core][exportoptions]") {
    const TempDir dir("options-missing");
    const ExportOptionsRead read = readExportOptions(dir.path());
    REQUIRE(read.status.ok());
    REQUIRE_FALSE(read.found);
    REQUIRE(read.options == ExportOptions{});
}

TEST_CASE("an options file with an unknown field is a bad file", "[core][exportoptions]") {
    const TempDir dir("options-unknown");
    writeOptionsText(dir.path(), readTextFile(dataFile("options_unknown_field.json")));
    const ExportOptionsRead read = readExportOptions(dir.path());
    REQUIRE(read.status.code == ErrorCode::BadFile);
    REQUIRE(read.status.message.find("export-options.json") != std::string::npos);
    REQUIRE_FALSE(read.found);
}

TEST_CASE("a newer options file is an unsupported version", "[core][exportoptions]") {
    const TempDir dir("options-version");
    writeOptionsText(dir.path(), readTextFile(dataFile("options_version_2.json")));
    REQUIRE(readExportOptions(dir.path()).status.code == ErrorCode::UnsupportedVersion);
}

TEST_CASE("malformed options are bad files", "[core][exportoptions]") {
    const TempDir dir("options-bad");
    writeOptionsText(dir.path(), "not json");
    REQUIRE(readExportOptions(dir.path()).status.code == ErrorCode::BadFile);
    writeOptionsText(dir.path(), readTextFile(dataFile("options_bad_move.json")));
    REQUIRE(readExportOptions(dir.path()).status.code == ErrorCode::BadFile);

    const auto code = [](const char* text) { return exportOptionsFromJson(Json::parse(text)).status.code; };
    REQUIRE(code(R"({"format": "other", "version": 1})") == ErrorCode::BadFile);
    REQUIRE(code(R"({"format": "plynth-export-options"})") == ErrorCode::BadFile);
    REQUIRE(code(R"({"format": "plynth-export-options", "version": 1, "groups": {"added": [{"id": "g1", "name": "A"}]}})") ==
            ErrorCode::BadFile);
    REQUIRE(code(R"({"format": "plynth-export-options", "version": 1, "groups": {"renames": [{"group": "a", "name": "---"}]}})") ==
            ErrorCode::BadFile);
    REQUIRE(code(R"({"format": "plynth-export-options", "version": 1, "groups": {"merges": [{"group": "a"}]}})") == ErrorCode::BadFile);
    REQUIRE(code(R"({"format": "plynth-export-options", "version": 1, "kit": {"enabled": "yes"}})") == ErrorCode::BadFile);
    REQUIRE(code(R"({"format": "plynth-export-options", "version": 1, "groups": []})") == ErrorCode::BadFile);
}

TEST_CASE("options are written, read back and removed when they are the defaults", "[core][exportoptions]") {
    const TempDir dir("options-write");
    const fs::path file = dir.path() / "export-options.json";

    plynth::test::requireOk(writeExportOptions(dir.path(), busyOptions()));
    REQUIRE(fs::exists(file));
    REQUIRE(schemaProblem("export-options.schema.json", nlohmann::json::parse(readTextFile(file))).empty());
    const ExportOptionsRead read = readExportOptions(dir.path());
    REQUIRE(read.status.ok());
    REQUIRE(read.found);
    REQUIRE(read.options == busyOptions());

    plynth::test::requireOk(writeExportOptions(dir.path(), {}));
    REQUIRE_FALSE(fs::exists(file));
    plynth::test::requireOk(writeExportOptions(dir.path(), {}));
}

TEST_CASE("a failed write leaves the old options file", "[core][exportoptions]") {
    const TempDir dir("options-fail");
    plynth::test::requireOk(writeExportOptions(dir.path(), busyOptions()));
    const std::string before = readTextFile(dir.path() / "export-options.json");

    // A folder where the temporary file goes makes the write fail before the rename.
    fs::create_directory(dir.path() / "export-options.json.tmp");
    ExportOptions other;
    other.kit.enabled = true;
    REQUIRE_FALSE(writeExportOptions(dir.path(), other).ok());
    REQUIRE(readTextFile(dir.path() / "export-options.json") == before);
}
