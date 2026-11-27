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
    options.links[std::string(kUnrealLinkId)].kit = true;
    return options;
}

// The Unreal link's options of a set of export options.
EngineLinkOptions unrealOf(const ExportOptions& options)
{
    return engineLinkOptions(options, kUnrealLinkId);
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
    writeOptionsText(dir.path(), readTextFile(dataFile("options_version_5.json")));
    REQUIRE(readExportOptions(dir.path()).status.code == ErrorCode::UnsupportedVersion);
    // Version 4 is the current one.
    writeOptionsText(dir.path(), readTextFile(dataFile("options_version_4.json")));
    REQUIRE(readExportOptions(dir.path()).status.ok());
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
    other.links[std::string(kUnrealLinkId)].gameplay = true;
    REQUIRE_FALSE(writeExportOptions(dir.path(), other).ok());
    REQUIRE(readTextFile(dir.path() / "export-options.json") == before);
}

TEST_CASE("the schema takes English group names only", "[core][exportoptions]") {
    const auto problem = [](const std::string& name) {
        nlohmann::json json = {{"format", "plynth-export-options"}, {"version", 4}, {"groups", {{"renames", {{{"group", "a"}, {"name", name}}}}}}};
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
    REQUIRE(unrealOf(repair.options).kit);
    REQUIRE(repair.options.groups.empty());
    const ExportOptionsRead after = readExportOptions(dir.path());
    REQUIRE(after.status.ok());
    REQUIRE(after.options == repair.options);
    REQUIRE(readTextFile(dir.path() / "export-options.json.bad") == bad);

    const ExportOptionsRepair again = repairExportOptions(dir.path());
    REQUIRE(again.status.ok());
    REQUIRE_FALSE(again.repaired);
    REQUIRE(unrealOf(again.options).kit);
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

    const std::string newer = readTextFile(dataFile("options_version_5.json"));
    writeOptionsText(dir.path(), newer);
    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.code == ErrorCode::UnsupportedVersion);
    REQUIRE_FALSE(repair.repaired);
    REQUIRE(readTextFile(dir.path() / "export-options.json") == newer);
    REQUIRE_FALSE(fs::exists(dir.path() / "export-options.json.bad"));
}

TEST_CASE("version 1 options read with gameplay data off and are written as version 4", "[core][exportoptions]") {
    const ExportOptionsRead old = exportOptionsFromJson(Json::parse(readTextFile(dataFile("options_valid.json"))));
    REQUIRE(old.status.ok());
    REQUIRE(old.options == busyOptions());
    REQUIRE_FALSE(unrealOf(old.options).gameplay);
    const Json written = exportOptionsToJson(old.options);
    REQUIRE(written["version"] == 4);
    REQUIRE_FALSE(written.contains("kit"));
    REQUIRE_FALSE(written.contains("gameplay"));
    REQUIRE(written["links"]["unreal"]["kit"] == true);
    REQUIRE(written["links"]["unreal"]["gameplay"] == false);
    REQUIRE(schemaProblem("export-options.schema.json", plain(written)).empty());
    REQUIRE(exportOptionsFromJson(written).options == old.options);
    // Version 1 had no gameplay switch, so a version 1 file that carries one is bad.
    REQUIRE(exportOptionsFromJson(Json::parse(R"({"format": "plynth-export-options", "version": 1, "gameplay": {"enabled": true}})"))
                .status.code == ErrorCode::BadFile);
}

TEST_CASE("the gameplay switch round trips through a version 2 file", "[core][exportoptions]") {
    const TempDir dir("options-gameplay");
    const nlohmann::json json = nlohmann::json::parse(readTextFile(dataFile("options_gameplay.json")));
    REQUIRE(schemaProblem("export-options.v2.schema.json", json).empty());
    REQUIRE(schemaProblem("export-options.v1.schema.json", json) != "");
    const ExportOptionsRead read = exportOptionsFromJson(Json::parse(json.dump()));
    REQUIRE(read.status.ok());
    REQUIRE(unrealOf(read.options).gameplay);
    REQUIRE(unrealOf(read.options).kit);

    ExportOptions options;
    options.links[std::string(kUnrealLinkId)].gameplay = true;
    plynth::test::requireOk(writeExportOptions(dir.path(), options));
    REQUIRE(readExportOptions(dir.path()).options == options);
    options.links[std::string(kUnrealLinkId)].gameplay = false;
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
    REQUIRE(unrealOf(repair.options).gameplay);
}

TEST_CASE("version 1 and 2 options read with the license report off and are written as version 4", "[core][exportoptions]") {
    const ExportOptionsRead v1 = exportOptionsFromJson(Json::parse(readTextFile(dataFile("options_valid.json"))));
    REQUIRE(v1.status.ok());
    CHECK_FALSE(v1.options.licenses.enabled);
    const ExportOptionsRead v2 = exportOptionsFromJson(Json::parse(readTextFile(dataFile("options_gameplay.json"))));
    REQUIRE(v2.status.ok());
    CHECK_FALSE(v2.options.licenses.enabled);
    CHECK(unrealOf(v2.options).gameplay);
    CHECK(unrealOf(v2.options).kit);
    const Json written = exportOptionsToJson(v2.options);
    CHECK(written["version"].get<int>() == 4);
    CHECK_FALSE(written["licenses"]["enabled"].get<bool>());
    CHECK(schemaProblem("export-options.v2.schema.json", nlohmann::json::parse(readTextFile(dataFile("options_gameplay.json")))).empty());
    CHECK(exportOptionsFromJson(Json::parse(R"({"format": "plynth-export-options", "version": 2, "licenses": {"enabled": true}})"))
              .status.code == ErrorCode::BadFile);
}

TEST_CASE("the license switch round trips and the defaults still remove the file", "[core][exportoptions]") {
    const TempDir dir("options-licenses");
    ExportOptions options;
    options.licenses.enabled = true;
    plynth::test::requireOk(writeExportOptions(dir.path(), options));
    const ExportOptionsRead read = readExportOptions(dir.path());
    REQUIRE(read.status.ok());
    CHECK(read.options == options);
    CHECK(schemaProblem("export-options.schema.json", nlohmann::json::parse(readTextFile(dir.path() / "export-options.json"))).empty());
    options.licenses.enabled = false;
    plynth::test::requireOk(writeExportOptions(dir.path(), options));
    CHECK_FALSE(fs::exists(dir.path() / "export-options.json"));
    CHECK(exportOptionsFromJson(Json::parse(R"({"format": "plynth-export-options", "version": 3, "licenses": {"enabled": "on"}})"))
              .status.where == "/licenses/enabled");
}

TEST_CASE("repairing a bad options file keeps the license switch", "[core][exportoptions]") {
    const TempDir dir("options-repair-licenses");
    writeOptionsText(dir.path(), R"({"format": "plynth-export-options", "version": 3, "groups": [], "licenses": {"enabled": true}})");
    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.ok());
    CHECK(repair.repaired);
    CHECK(repair.options.licenses.enabled);
}

TEST_CASE("the version 3 schema is a valid schema", "[core][exportoptions][schema]") {
    const std::string problem = schemaProblem("export-options.v3.schema.json", nlohmann::json::object());
    REQUIRE_FALSE(problem.empty());
    REQUIRE(problem.find("could not read") == std::string::npos);
}

TEST_CASE("a version 3 options file reads its kit and gameplay into the Unreal link and is written as version 4", "[core][exportoptions]") {
    const nlohmann::json file = nlohmann::json::parse(readTextFile(dataFile("options_version_3.json")));
    REQUIRE(schemaProblem("export-options.v3.schema.json", file).empty());
    REQUIRE_FALSE(schemaProblem("export-options.schema.json", file).empty());

    const ExportOptionsRead read = exportOptionsFromJson(Json::parse(file.dump()));
    REQUIRE(read.status.ok());
    const EngineLinkOptions unreal = unrealOf(read.options);
    CHECK(unreal.kit);
    CHECK(unreal.gameplay);
    CHECK(unreal.packMaterials);
    CHECK_FALSE(unreal.grouped.has_value());
    CHECK(unreal.syncAfterBuild);
    CHECK_FALSE(unreal.target.has_value());
    CHECK(read.options.licenses.enabled);
    CHECK(read.options.groups.renames.size() == 1);
    CHECK_FALSE(read.options.kit.enabled);
    CHECK_FALSE(read.options.gameplay.enabled);

    const Json written = exportOptionsToJson(read.options);
    CHECK(written["version"] == 4);
    CHECK_FALSE(written.contains("kit"));
    CHECK_FALSE(written.contains("gameplay"));
    CHECK(written["licenses"]["enabled"] == true);
    CHECK(written["links"]["unreal"]["kit"] == true);
    CHECK(written["links"]["unreal"]["gameplay"] == true);
    CHECK(written["links"]["unreal"]["pack_materials"] == true);
    CHECK(written["links"]["unreal"]["grouped"].is_null());
    CHECK(written["links"]["unreal"]["sync_after_build"] == true);
    CHECK(written["links"]["unreal"]["target"].is_null());
    CHECK(schemaProblem("export-options.schema.json", plain(written)).empty());
    CHECK(exportOptionsFromJson(written).options == read.options);

    // Through the project folder: read as version 3, saved as version 4.
    const TempDir dir("options-v3-file");
    writeOptionsText(dir.path(), readTextFile(dataFile("options_version_3.json")));
    const ExportOptionsRead fromFile = readExportOptions(dir.path());
    REQUIRE(fromFile.status.ok());
    REQUIRE(fromFile.options == read.options);
    plynth::test::requireOk(writeExportOptions(dir.path(), fromFile.options));
    const nlohmann::json saved = nlohmann::json::parse(readTextFile(dir.path() / "export-options.json"));
    CHECK(saved["version"] == 4);
    CHECK(schemaProblem("export-options.schema.json", saved).empty());
    CHECK(readExportOptions(dir.path()).options == read.options);
}

TEST_CASE("every link setting round trips and a link at its defaults is left out", "[core][exportoptions]") {
    ExportOptions options;
    EngineLinkOptions& link = options.links[std::string(kUnrealLinkId)];
    link.packMaterials = false;
    link.grouped = true;
    link.kit = true;
    link.gameplay = false;
    link.syncAfterBuild = false;
    link.target = EngineTarget{"MyGame", pathFromUtf8("C:/Unreal/MyGame"), "5.8.0"};
    const Json json = exportOptionsToJson(options);
    REQUIRE(schemaProblem("export-options.schema.json", plain(json)).empty());
    CHECK(json["links"]["unreal"]["pack_materials"] == false);
    CHECK(json["links"]["unreal"]["grouped"] == true);
    CHECK(json["links"]["unreal"]["sync_after_build"] == false);
    CHECK(json["links"]["unreal"]["target"]["project_name"] == "MyGame");
    CHECK(json["links"]["unreal"]["target"]["project_root"] == "C:/Unreal/MyGame");
    CHECK(json["links"]["unreal"]["target"]["engine_version"] == "5.8.0");
    const ExportOptionsRead back = exportOptionsFromJson(json);
    REQUIRE(back.status.ok());
    CHECK(back.options == options);

    options.links[std::string(kUnrealLinkId)].grouped = false;
    CHECK(exportOptionsFromJson(exportOptionsToJson(options)).options == options);

    // A link with only default settings is not written and reads back as no entry.
    ExportOptions defaults;
    defaults.links[std::string(kUnrealLinkId)] = EngineLinkOptions{};
    const Json empty = exportOptionsToJson(defaults);
    CHECK(empty["links"].empty());
    CHECK(exportOptionsFromJson(empty).options == ExportOptions{});
    const TempDir dir("options-default-link");
    plynth::test::requireOk(writeExportOptions(dir.path(), defaults));
    CHECK_FALSE(fs::exists(dir.path() / "export-options.json"));
    const Json explicitDefaults =
        Json::parse(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {}}})");
    CHECK(exportOptionsFromJson(explicitDefaults).options == ExportOptions{});
}

TEST_CASE("a version 4 options file refuses the old switches and bad link settings", "[core][exportoptions]") {
    const auto read = [](const char* text) { return exportOptionsFromJson(Json::parse(text)).status; };
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "kit": {"enabled": true}})").code == ErrorCode::BadFile);
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "gameplay": {"enabled": true}})").code == ErrorCode::BadFile);
    CHECK(read(R"({"format": "plynth-export-options", "version": 3, "links": {}})").code == ErrorCode::BadFile);
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": []})").code == ErrorCode::BadFile);
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": {"other": {}}})").code == ErrorCode::BadFile);
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {"speed": 1}}})").code == ErrorCode::BadFile);
    const Status grouped = read(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {"grouped": "yes"}}})");
    CHECK(grouped.code == ErrorCode::BadFile);
    CHECK(grouped.where == "/links/unreal/grouped");
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {"kit": 1}}})").where == "/links/unreal/kit");
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {"target": {"project_name": "A"}}}})").code ==
          ErrorCode::BadFile);
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {"target": {"project_name": "", "project_root": "C:/A", "engine_version": ""}}}})")
              .where == "/links/unreal/target/project_name");
    CHECK(read(R"({"format": "plynth-export-options", "version": 4, "links": {"unreal": {"grouped": null, "target": null}}})").ok());
}

TEST_CASE("engineLinkOptions gives the defaults for a link with no entry and linkGrouped follows the export mode", "[core][exportoptions]") {
    ExportOptions options;
    CHECK(engineLinkOptions(options, kUnrealLinkId) == EngineLinkOptions{});
    CHECK(engineLinkOptions(options, "unity") == EngineLinkOptions{});
    options.links[std::string(kUnrealLinkId)].kit = true;
    CHECK(engineLinkOptions(options, kUnrealLinkId).kit);
    CHECK_FALSE(engineLinkOptions(options, "unity").kit);

    Project project;
    EngineLinkOptions link;
    project.settings.exportMode = ExportMode::Single;
    CHECK_FALSE(linkGrouped(link, project));
    project.settings.exportMode = ExportMode::Grouped;
    CHECK(linkGrouped(link, project));
    link.grouped = false;
    CHECK_FALSE(linkGrouped(link, project));
    project.settings.exportMode = ExportMode::Single;
    link.grouped = true;
    CHECK(linkGrouped(link, project));
}

TEST_CASE("repairing a bad version 4 file keeps the Unreal link settings", "[core][exportoptions]") {
    const TempDir dir("options-repair-links");
    writeOptionsText(dir.path(), R"({"format": "plynth-export-options", "version": 4, "groups": [], )"
                                 R"("links": {"unreal": {"kit": true, "pack_materials": false}}})");
    REQUIRE(readExportOptions(dir.path()).status.code == ErrorCode::BadFile);
    const ExportOptionsRepair repair = repairExportOptions(dir.path());
    REQUIRE(repair.status.ok());
    CHECK(repair.repaired);
    CHECK(unrealOf(repair.options).kit);
    CHECK_FALSE(unrealOf(repair.options).packMaterials);
    CHECK(readExportOptions(dir.path()).options == repair.options);
}
