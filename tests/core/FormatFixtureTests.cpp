#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;
using plynth::test::readTextFile;
using plynth::test::requireOk;
using plynth::test::schemaProblemForText;
using plynth::test::TempDir;

namespace {

// The folder that keeps a saved file of every format version.
fs::path formatsDir()
{
    return fs::path(PLYNTH_FORMATS_DIR);
}

// The name of the fixture of one version, for example plan.v3.json.
std::string fixtureName(const std::string& kind, int version)
{
    return kind + ".v" + std::to_string(version) + ".json";
}

// Copies a fixture to a file, creating its folder.
void copyFixture(const std::string& fixture, const fs::path& target)
{
    fs::create_directories(target.parent_path());
    fs::copy_file(formatsDir() / fixture, target, fs::copy_options::overwrite_existing);
}

// Writes text to a file, creating its folder.
void writeFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// The schema file for a version: the current schema, or the one kept for an older version.
std::string schemaFor(const std::string& kind, int version, int current)
{
    if (version == current) return kind + ".schema.json";
    return kind + ".v" + std::to_string(version) + ".schema.json";
}

// The version written in a JSON file.
int versionOf(const fs::path& file)
{
    return nlohmann::json::parse(readTextFile(file)).at("version").get<int>();
}

// A fixture with its version changed, as text.
std::string withVersion(const std::string& fixture, int version)
{
    nlohmann::json json = nlohmann::json::parse(readTextFile(formatsDir() / fixture));
    json["version"] = version;
    return json.dump(2);
}

// A folder holding a plan and a project fixture of the given versions.
fs::path olderProject(const fs::path& root, const std::string& name, int planVersion, int projectVersion)
{
    const fs::path folder = root / name;
    copyFixture(fixtureName("plan", planVersion), folder / "plan.json");
    copyFixture(fixtureName("project", projectVersion), folder / "project.json");
    return folder;
}

// Checks that plan.json and project.json in a folder are the current versions and match the current schemas.
void requireCurrentFiles(const fs::path& folder)
{
    const std::string plan = readTextFile(folder / "plan.json");
    const std::string project = readTextFile(folder / "project.json");
    REQUIRE(versionOf(folder / "plan.json") == kPlanFormatVersion);
    REQUIRE(versionOf(folder / "project.json") == kProjectFormatVersion);
    REQUIRE(schemaProblemForText("plan.schema.json", plan).empty());
    REQUIRE(schemaProblemForText("project.schema.json", project).empty());
}

}

TEST_CASE("every format fixture is a true file of its version", "[core][files][formats]") {
    for (int v = 1; v <= kPlanFormatVersion; ++v) {
        INFO("plan version " << v);
        const std::string text = readTextFile(formatsDir() / fixtureName("plan", v));
        REQUIRE_FALSE(text.empty());
        REQUIRE(versionOf(formatsDir() / fixtureName("plan", v)) == v);
        REQUIRE(schemaProblemForText(schemaFor("plan", v, kPlanFormatVersion), text).empty());
    }
    for (int v = 1; v <= kProjectFormatVersion; ++v) {
        INFO("project version " << v);
        const std::string text = readTextFile(formatsDir() / fixtureName("project", v));
        REQUIRE_FALSE(text.empty());
        REQUIRE(versionOf(formatsDir() / fixtureName("project", v)) == v);
        REQUIRE(schemaProblemForText(schemaFor("project", v, kProjectFormatVersion), text).empty());
    }
    for (int v = 1; v <= 3; ++v) {
        INFO("export options version " << v);
        const std::string text = readTextFile(formatsDir() / fixtureName("export-options", v));
        REQUIRE_FALSE(text.empty());
        REQUIRE(versionOf(formatsDir() / fixtureName("export-options", v)) == v);
        REQUIRE(schemaProblemForText(schemaFor("export-options", v, kExportOptionsVersion), text).empty());
    }
}

TEST_CASE("a project of every older format opens and saves as the current version", "[core][files][formats]") {
    TempDir tmp("formats-open");
    for (int planVersion = 1; planVersion <= kPlanFormatVersion; ++planVersion) {
        const int projectVersion = std::min(planVersion, kProjectFormatVersion);
        INFO("plan version " << planVersion << ", project version " << projectVersion);
        const fs::path folder = olderProject(tmp.path(), "v" + std::to_string(planVersion), planVersion, projectVersion);

        ProjectLoad load = loadProject(folder);
        requireOk(load.status);
        const bool older = planVersion < kPlanFormatVersion || projectVersion < kProjectFormatVersion;
        CHECK(load.migrated == older);

        SaveReport report;
        requireOk(saveProject(load.project, folder, folder, &report));
        CHECK(report.missingImages.empty());
        requireCurrentFiles(folder);

        std::vector<fs::path> expected;
        if (planVersion < kPlanFormatVersion) expected.push_back(folder / "backup" / fixtureName("plan", planVersion));
        if (projectVersion < kProjectFormatVersion) expected.push_back(folder / "backup" / fixtureName("project", projectVersion));
        REQUIRE(report.backup.files == expected);
        for (const fs::path& file : expected) {
            CHECK(readTextFile(file) == readTextFile(formatsDir() / file.filename()));
        }
        CHECK(fs::exists(folder / "backup") == older);

        const ProjectLoad again = loadProject(folder);
        requireOk(again.status);
        CHECK_FALSE(again.migrated);
        CHECK(again.project == load.project);
    }
}

TEST_CASE("the sample projects saved by 0.1.0 and 0.1.1 open and save as the current version", "[core][files][formats]") {
    TempDir tmp("formats-released");
    for (const char* release : {"saved-0.1.0", "saved-0.1.1"}) {
        for (const char* sample : {"house", "apartment"}) {
            INFO(release << " " << sample);
            const fs::path source = formatsDir() / release / sample;
            const fs::path folder = tmp.path() / release / sample;
            fs::create_directories(folder.parent_path());
            fs::copy(source, folder, fs::copy_options::recursive);

            ProjectLoad load = loadProject(folder);
            requireOk(load.status);
            CHECK(load.migrated);
            const ExportOptionsRead options = readExportOptions(folder);
            requireOk(options.status);
            CHECK(options.found);

            SaveReport report;
            requireOk(saveProject(load.project, folder, folder, &report));
            CHECK(report.missingImages.empty());
            requireCurrentFiles(folder);
            for (const Floor& floor : load.project.plan.floors) {
                REQUIRE(floor.image);
                CHECK(floor.image->rfind("plans/", 0) == 0);
                CHECK(fs::is_regular_file(folder / pathFromUtf8(*floor.image)));
            }

            CHECK(readTextFile(folder / "backup" / "plan.v7.json") == readTextFile(source / "plan.json"));
            CHECK(readTextFile(folder / "backup" / "project.v6.json") == readTextFile(source / "project.json"));
            const bool optionsOlder = kExportOptionsVersion > 3;
            CHECK(report.backup.files.size() == (optionsOlder ? 3u : 2u));
            CHECK(fs::exists(folder / "backup" / "export-options.v3.json") == optionsOlder);

            requireOk(writeExportOptions(folder, options.options));
            if (fs::exists(folder / "export-options.json")) {
                CHECK(versionOf(folder / "export-options.json") == kExportOptionsVersion);
                CHECK(schemaProblemForText("export-options.schema.json", readTextFile(folder / "export-options.json")).empty());
            }
            const ExportOptionsRead after = readExportOptions(folder);
            requireOk(after.status);
            CHECK(after.options == options.options);

            const ProjectLoad again = loadProject(folder);
            requireOk(again.status);
            CHECK_FALSE(again.migrated);
            CHECK(again.project == load.project);
        }
    }
}

TEST_CASE("export options of every older version read and are written as the current version", "[core][files][formats]") {
    TempDir tmp("formats-options");
    for (int v = 1; v <= 3; ++v) {
        INFO("export options version " << v);
        const fs::path folder = olderProject(tmp.path(), "v" + std::to_string(v), kPlanFormatVersion, kProjectFormatVersion);
        copyFixture(fixtureName("export-options", v), folder / "export-options.json");

        const ExportOptionsRead read = readExportOptions(folder);
        requireOk(read.status);
        CHECK(read.found);
        REQUIRE(read.options.groups.added.size() == 1);

        requireOk(writeExportOptions(folder, read.options));
        const std::string written = readTextFile(folder / "export-options.json");
        CHECK(versionOf(folder / "export-options.json") == kExportOptionsVersion);
        CHECK(schemaProblemForText("export-options.schema.json", written).empty());
        const ExportOptionsRead again = readExportOptions(folder);
        requireOk(again.status);
        CHECK(again.options == read.options);

        const fs::path kept = folder / "backup" / fixtureName("export-options", v);
        if (v < kExportOptionsVersion) {
            CHECK(readTextFile(kept) == readTextFile(formatsDir() / fixtureName("export-options", v)));
        } else {
            CHECK_FALSE(fs::exists(folder / "backup"));
        }
    }
}

TEST_CASE("an upgraded project keeps its older files in backup once", "[core][files][backup]") {
    TempDir tmp("backup-once");
    const fs::path folder = olderProject(tmp.path(), "p", 7, 6);

    ProjectLoad load = loadProject(folder);
    requireOk(load.status);
    SaveReport first;
    requireOk(saveProject(load.project, folder, folder, &first));
    const std::vector<fs::path> expected = {folder / "backup" / "plan.v7.json", folder / "backup" / "project.v6.json"};
    REQUIRE(first.backup.files == expected);
    CHECK(readTextFile(expected[0]) == readTextFile(formatsDir() / "plan.v7.json"));
    CHECK(readTextFile(expected[1]) == readTextFile(formatsDir() / "project.v6.json"));

    SaveReport second;
    requireOk(saveProject(load.project, folder, folder, &second));
    CHECK(second.backup.files.empty());

    // An older plan put back by hand does not replace the backup that is there.
    nlohmann::json changed = nlohmann::json::parse(readTextFile(formatsDir() / "plan.v7.json"));
    changed["scale"]["pixels_per_metre"] = 55.0;
    writeFile(folder / "plan.json", changed.dump(2));
    ProjectLoad reload = loadProject(folder);
    requireOk(reload.status);
    CHECK(reload.migrated);
    SaveReport third;
    requireOk(saveProject(reload.project, folder, folder, &third));
    CHECK(third.backup.files.empty());
    CHECK(readTextFile(expected[0]) == readTextFile(formatsDir() / "plan.v7.json"));
    CHECK(versionOf(folder / "plan.json") == kPlanFormatVersion);

    int count = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(folder / "backup")) ++count;
    CHECK(count == 2);
}

TEST_CASE("a backup already there is never overwritten", "[core][files][backup]") {
    TempDir tmp("backup-kept");
    const fs::path folder = olderProject(tmp.path(), "p", 7, 6);
    writeFile(folder / "backup" / "plan.v7.json", "the first backup");

    ProjectLoad load = loadProject(folder);
    requireOk(load.status);
    SaveReport report;
    requireOk(saveProject(load.project, folder, folder, &report));
    REQUIRE(report.backup.files == std::vector<fs::path>{folder / "backup" / "project.v6.json"});
    CHECK(readTextFile(folder / "backup" / "plan.v7.json") == "the first backup");
    CHECK(readTextFile(folder / "backup" / "project.v6.json") == readTextFile(formatsDir() / "project.v6.json"));
}

TEST_CASE("a current project and a folder without a project keep no backup", "[core][files][backup]") {
    TempDir tmp("backup-none");
    const fs::path folder = tmp.path() / "p";
    Project project = makeNewProject();
    requireOk(createProject(project, folder, {}));
    SaveReport report;
    requireOk(saveProject(project, folder, folder, &report));
    CHECK(report.backup.files.empty());
    CHECK_FALSE(fs::exists(folder / "backup"));

    UpgradeBackup backup;
    backup.files.push_back(folder / "stale");
    requireOk(backUpOlderFiles(folder, &backup));
    CHECK(backup.files.empty());

    const fs::path empty = tmp.path() / "empty";
    copyFixture("plan.v1.json", empty / "plan.json");
    requireOk(backUpOlderFiles(empty, &backup));
    CHECK(backup.files.empty());
    CHECK_FALSE(fs::exists(empty / "backup"));
    requireOk(backUpOlderFiles(tmp.path() / "missing"));
}

TEST_CASE("a backup that cannot be written stops the save and changes nothing", "[core][files][backup]") {
    TempDir tmp("backup-fails");
    const fs::path folder = olderProject(tmp.path(), "p", 7, 6);
    writeFile(folder / "backup", "a file where the folder should be");
    const std::string planBefore = readTextFile(folder / "plan.json");
    const std::string projectBefore = readTextFile(folder / "project.json");

    ProjectLoad load = loadProject(folder);
    requireOk(load.status);
    SaveReport report;
    CHECK(saveProject(load.project, folder, folder, &report).code == ErrorCode::CannotWrite);
    CHECK(report.backup.files.empty());
    CHECK(readTextFile(folder / "plan.json") == planBefore);
    CHECK(readTextFile(folder / "project.json") == projectBefore);
    CHECK(fs::is_regular_file(folder / "backup"));
}

TEST_CASE("writing the export options keeps an older options file", "[core][files][backup]") {
    TempDir tmp("backup-options");
    const fs::path folder = olderProject(tmp.path(), "p", kPlanFormatVersion, kProjectFormatVersion);
    copyFixture("export-options.v1.json", folder / "export-options.json");

    requireOk(writeExportOptions(folder, ExportOptions{}));
    CHECK_FALSE(fs::exists(folder / "export-options.json"));
    CHECK(readTextFile(folder / "backup" / "export-options.v1.json") == readTextFile(formatsDir() / "export-options.v1.json"));
    CHECK_FALSE(fs::exists(folder / "backup" / fixtureName("plan", kPlanFormatVersion)));
}

TEST_CASE("a file saved by a newer Plynth is refused with one sentence", "[core][files][formats]") {
    TempDir tmp("formats-newer");
    SECTION("plan.json") {
        const fs::path folder = olderProject(tmp.path(), "plan", kPlanFormatVersion, kProjectFormatVersion);
        writeFile(folder / "plan.json", withVersion(fixtureName("plan", kPlanFormatVersion), 99));
        const ProjectLoad load = loadProject(folder);
        REQUIRE(load.status.code == ErrorCode::UnsupportedVersion);
        CHECK(load.status.message == "plan.json was saved by a newer Plynth (format version 99). Update Plynth to open this project.");
        CHECK(load.status.where == "/version");
        CHECK_FALSE(fs::exists(folder / "backup"));

        const PlanRead read = planFromJson(Json::parse(readTextFile(folder / "plan.json")));
        CHECK(read.status.message == load.status.message);
        CHECK(read.fileVersion == 99);
    }
    SECTION("project.json") {
        const fs::path folder = olderProject(tmp.path(), "project", kPlanFormatVersion, kProjectFormatVersion);
        writeFile(folder / "project.json", withVersion(fixtureName("project", kProjectFormatVersion), 99));
        const ProjectLoad load = loadProject(folder);
        REQUIRE(load.status.code == ErrorCode::UnsupportedVersion);
        CHECK(load.status.message == "project.json was saved by a newer Plynth (format version 99). Update Plynth to open this project.");
        CHECK(settingsFromJson(Json::parse(readTextFile(folder / "project.json"))).status.message == load.status.message);
    }
    SECTION("export-options.json") {
        const fs::path folder = olderProject(tmp.path(), "options", kPlanFormatVersion, kProjectFormatVersion);
        writeFile(folder / "export-options.json", withVersion("export-options.v3.json", 99));
        const ExportOptionsRead read = readExportOptions(folder);
        REQUIRE(read.status.code == ErrorCode::UnsupportedVersion);
        CHECK(read.status.message ==
              "export-options.json was saved by a newer Plynth (format version 99). Update Plynth to open this project.");
        CHECK(read.status.where == "/version");
    }
}
