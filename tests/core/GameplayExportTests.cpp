#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// The square with room r1, a sofa and a ceiling light.
Project litSquare()
{
    Project p = squareProjectWithWalls();
    p.settings.name = "Square";
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    p.plan.floors[0].rooms = {r};
    Item sofa;
    sofa.id = "i1";
    sofa.asset = "sofa";
    sofa.room = "r1";
    sofa.x = 2.0;
    sofa.y = 3.0;
    sofa.size = {1.8, 0.9, 0.8};
    p.plan.floors[0].items = {sofa};
    LightMarker light;
    light.id = "l1";
    light.room = "r1";
    light.x = 2.0;
    light.y = 2.0;
    light.z = 2.9;
    p.plan.floors[0].lights = {light};
    return p;
}

// A built project folder, a fake Blender and a request to export it.
struct GameplayFixture {
    explicit GameplayFixture(const std::string& tag) : tmp(tag)
    {
        projectFolder = tmp.path() / "Project";
        fs::create_directories(projectFolder / "build");
        std::ofstream(projectFolder / "build" / "building.blend", std::ios::binary) << "FAKEBLEND";
        blenderFolder = tmp.path() / "blender";
        const fs::path exe = makeFakeBlender(blenderFolder, "5.2.0", "");
        request.projectFolder = projectFolder;
        request.project = litSquare();
        request.blender = inspectBlender(exe);
        request.run.scriptsFolder = makeFakeScripts(tmp.path() / "scripts");
    }

    // Switches the gameplay data on or off in the project's export options.
    void gameplay(bool on) const
    {
        ExportOptions options;
        options.gameplay.enabled = on;
        requireOk(writeExportOptions(projectFolder, options));
    }

    fs::path gameplayFile() const { return projectFolder / "export" / "Square_gameplay.json"; }
    void setMode(const std::string& mode) const { std::ofstream(blenderFolder / "fake_mode.txt") << mode; }

    TempDir tmp;
    fs::path projectFolder;
    fs::path blenderFolder;
    ExportRequest request;
};

// Says whether a group holds a member.
bool holds(const ExportGroup& group, const std::string& kind, const std::string& id)
{
    return std::any_of(group.members.begin(), group.members.end(),
                       [&](const ExportMember& m) { return m.kind == kind && m.id == id; });
}

// The automatic group with an id.
const ExportGroup* groupOf(const std::vector<ExportGroup>& groups, const std::string& id)
{
    for (const ExportGroup& g : groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

}

TEST_CASE("an export with gameplay data on writes the file and reports it", "[core][export][gameplay]")
{
    GameplayFixture f("gameplay-on");
    f.gameplay(true);
    const ExportReport report = runExport(f.request);
    requireOk(report.status);
    CHECK(report.gameplay.enabled);
    CHECK(report.gameplay.file == f.gameplayFile());
    CHECK(report.gameplay.rooms == 1);
    CHECK(report.gameplay.doors == 0);
    CHECK(report.gameplay.lights == 1);
    REQUIRE(fs::is_regular_file(f.gameplayFile()));
    const nlohmann::json written = nlohmann::json::parse(readTextFile(f.gameplayFile()));
    CHECK(schemaProblem("gameplay.schema.json", written) == "");
    CHECK(written["project"] == "Square");
    CHECK(written["lights"][0]["position"][2] == 2.9);
    const fs::path reportFile = f.projectFolder / "export" / "export_report.json";
    const nlohmann::json reportJson = nlohmann::json::parse(readTextFile(reportFile));
    CHECK(schemaProblem("export-report.schema.json", reportJson) == "");
    CHECK(reportJson["gameplay"]["lights"] == 1);
    const ExportReportRead read = readExportReport(f.projectFolder);
    requireOk(read.status);
    CHECK(read.report.gameplay.enabled);
    CHECK(read.report.gameplay.rooms == 1);

    // A blocked export leaves the file of the last good one.
    f.setMode("quality_fail");
    CHECK(runExport(f.request).status.code == ErrorCode::QualityFailed);
    CHECK(fs::is_regular_file(f.gameplayFile()));

    // Switched off, the next export removes it.
    f.setMode("");
    f.gameplay(false);
    const ExportReport off = runExport(f.request);
    requireOk(off.status);
    CHECK_FALSE(off.gameplay.enabled);
    CHECK(off.gameplay.file.empty());
    CHECK_FALSE(fs::exists(f.gameplayFile()));
}

TEST_CASE("the grouped export also writes gameplay data", "[core][export][gameplay]")
{
    GameplayFixture f("gameplay-grouped");
    f.request.project.settings.exportMode = ExportMode::Grouped;
    f.gameplay(true);
    const ExportReport report = runExport(f.request);
    requireOk(report.status);
    CHECK(report.gameplay.enabled);
    CHECK(fs::is_regular_file(f.gameplayFile()));
}

TEST_CASE("trim, railings, guards and set dressing have their export groups", "[core][exportgroups][finish]")
{
    Project p = litSquare();
    p.settings.exportMode = ExportMode::Grouped;
    std::vector<ExportGroup> groups = automaticExportGroups(p);
    const ExportGroup* structure = groupOf(groups, "structure");
    const ExportGroup* surfaces = groupOf(groups, "surfaces");
    const ExportGroup* furniture = groupOf(groups, "furniture_r1");
    REQUIRE(structure != nullptr);
    REQUIRE(surfaces != nullptr);
    REQUIRE(furniture != nullptr);
    CHECK(holds(*structure, "trim", "f1"));
    CHECK(holds(*surfaces, "railing", ""));
    CHECK(holds(*surfaces, "guard", ""));
    CHECK(holds(*surfaces, "light", ""));
    CHECK(holds(*furniture, "dressing", "r1"));

    p.plan.floors[0].rooms[0].dressing = false;
    for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill}) {
        trimStyle(p.settings.trim, kind).enabled = false;
    }
    groups = automaticExportGroups(p);
    CHECK_FALSE(holds(*groupOf(groups, "structure"), "trim", "f1"));
    CHECK_FALSE(holds(*groupOf(groups, "furniture_r1"), "dressing", "r1"));
}
