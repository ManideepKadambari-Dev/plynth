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
    // The sample is a version 1 file, so it matches the frozen version 1 schema.
    REQUIRE(schemaProblem("export-options.v1.schema.json", json).empty());
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
    writeOptionsText(dir.path(), readTextFile(dataFile("options_version_3.json")));
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

TEST_CASE("the schema takes English group names only", "[core][exportoptions]") {
    const auto problem = [](const std::string& name) {
        nlohmann::json json = {{"format", "plynth-export-options"}, {"version", 2}, {"groups", {{"renames", {{{"group", "a"}, {"name", name}}}}}}};
        return schemaProblem("export-options.schema.json", json);
    };
    REQUIRE(problem("Living room-2_a").empty());
    REQUIRE_FALSE(problem("K\xC3\xBC" "che").empty());
    REQUIRE_FALSE(problem("\xE0\xA4\xB0\xE0\xA4\xB8\xE0\xA5\x8B\xE0\xA4\x88 2").empty());
    REQUIRE_FALSE(problem("---").empty());
    REQUIRE_FALSE(problem("a.b").empty());
}

TEST_CASE("repairing a bad options file keeps the kit switch and a copy of the old file", "[core][exportoptions]") {
    const TempDir dir("options-repair-name");
    const std::string bad = "{\"format\": \"plynth-export-options\", \"version\": 1, \"groups\": {\"renames\": "
                            "[{\"group\": \"furniture_r1\", \"name\": \"K\xC3\xBC" "che\"}]}, \"kit\": {\"enabled\": true}}";
    writeOptionsText(dir.path(), bad);
    REQUIRE(readExportOptions(dir.path()).status.code == ErrorCode::BadFile);

    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.ok());
    REQUIRE(repair.repaired);
    REQUIRE(repair.options.kit.enabled);
    REQUIRE(repair.options.groups.empty());
    const ExportOptionsRead after = readExportOptions(dir.path());
    REQUIRE(after.status.ok());
    REQUIRE(after.options == repair.options);
    REQUIRE(readTextFile(dir.path() / "export-options.json.bad") == bad);

    const ExportOptionsRepair again = repairExportOptions(dir.path());
    REQUIRE(again.status.ok());
    REQUIRE_FALSE(again.repaired);
    REQUIRE(again.options.kit.enabled);
}

TEST_CASE("repairing a file that is not JSON starts from the defaults", "[core][exportoptions]") {
    const TempDir dir("options-repair-json");
    writeOptionsText(dir.path(), "{ not json");
    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.ok());
    REQUIRE(repair.repaired);
    REQUIRE(repair.options == ExportOptions{});
    REQUIRE_FALSE(fs::exists(dir.path() / "export-options.json"));
    REQUIRE(readTextFile(dir.path() / "export-options.json.bad") == "{ not json");
    REQUIRE(readExportOptions(dir.path()).status.ok());
}

TEST_CASE("repairing never touches a newer file or a missing one", "[core][exportoptions]") {
    const TempDir dir("options-repair-newer");
    const ExportOptionsRepair none = repairExportOptions(dir.path());
    REQUIRE(none.status.ok());
    REQUIRE_FALSE(none.repaired);
    REQUIRE_FALSE(fs::exists(dir.path() / "export-options.json"));

    const std::string newer = readTextFile(dataFile("options_version_3.json"));
    writeOptionsText(dir.path(), newer);
    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.code == ErrorCode::UnsupportedVersion);
    REQUIRE_FALSE(repair.repaired);
    REQUIRE(readTextFile(dir.path() / "export-options.json") == newer);
    REQUIRE_FALSE(fs::exists(dir.path() / "export-options.json.bad"));
}

TEST_CASE("version 1 options read with gameplay data off and are written as version 2", "[core][exportoptions]") {
    const ExportOptionsRead old = exportOptionsFromJson(Json::parse(readTextFile(dataFile("options_valid.json"))));
    REQUIRE(old.status.ok());
    REQUIRE(old.options == busyOptions());
    REQUIRE_FALSE(old.options.gameplay.enabled);
    const Json written = exportOptionsToJson(old.options);
    REQUIRE(written["version"] == 2);
    REQUIRE(written["gameplay"]["enabled"] == false);
    REQUIRE(schemaProblem("export-options.schema.json", plain(written)).empty());
    REQUIRE(exportOptionsFromJson(written).options == old.options);
    // Version 1 had no gameplay switch, so a version 1 file that carries one is bad.
    REQUIRE(exportOptionsFromJson(Json::parse(R"({"format": "plynth-export-options", "version": 1, "gameplay": {"enabled": true}})"))
                .status.code == ErrorCode::BadFile);
}

TEST_CASE("the gameplay switch round trips through a version 2 file", "[core][exportoptions]") {
    const TempDir dir("options-gameplay");
    const nlohmann::json json = nlohmann::json::parse(readTextFile(dataFile("options_gameplay.json")));
    REQUIRE(schemaProblem("export-options.schema.json", json).empty());
    REQUIRE(schemaProblem("export-options.v1.schema.json", json) != "");
    const ExportOptionsRead read = exportOptionsFromJson(Json::parse(json.dump()));
    REQUIRE(read.status.ok());
    REQUIRE(read.options.gameplay.enabled);
    REQUIRE(read.options.kit.enabled);

    ExportOptions options;
    options.gameplay.enabled = true;
    plynth::test::requireOk(writeExportOptions(dir.path(), options));
    REQUIRE(readExportOptions(dir.path()).options == options);
    options.gameplay.enabled = false;
    plynth::test::requireOk(writeExportOptions(dir.path(), options));
    REQUIRE_FALSE(fs::exists(dir.path() / "export-options.json"));

    REQUIRE(exportOptionsFromJson(Json::parse(R"({"format": "plynth-export-options", "version": 2, "gameplay": {"enabled": 1}})"))
                .status.code == ErrorCode::BadFile);
    REQUIRE(exportOptionsFromJson(Json::parse(R"({"format": "plynth-export-options", "version": 2, "gameplay": {"on": true}})"))
                .status.code == ErrorCode::BadFile);
    REQUIRE(exportOptionsFromJson(Json::parse(readTextFile(dataFile("options_version_2.json")))).status.ok());
}

TEST_CASE("repairing a bad options file keeps the gameplay switch", "[core][exportoptions]") {
    const TempDir dir("options-repair-gameplay");
    writeOptionsText(dir.path(), R"({"format": "plynth-export-options", "version": 2, "groups": [], "gameplay": {"enabled": true}})");
    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.ok());
    REQUIRE(repair.repaired);
    REQUIRE(repair.options.gameplay.enabled);
}
