#include "FakeBlender.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/LicenseReport.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// A built project folder, a fake Blender and a request to export it, with no library.
struct LicenseFixture {
    explicit LicenseFixture(const std::string& tag) : tmp(tag)
    {
        projectFolder = tmp.path() / "Project";
        fs::create_directories(projectFolder / "build");
        std::ofstream(projectFolder / "build" / "building.blend", std::ios::binary) << "FAKEBLEND";
        blenderFolder = tmp.path() / "blender";
        const fs::path exe = makeFakeBlender(blenderFolder, "5.2.0", "");
        request.projectFolder = projectFolder;
        request.project = squareProjectWithWalls();
        request.project.settings.name = "Square";
        request.blender = inspectBlender(exe);
        request.run.scriptsFolder = makeFakeScripts(tmp.path() / "scripts");
    }

    // Switches the license report on or off in the project's export options.
    void licenses(bool on) const
    {
        ExportOptions options;
        options.licenses.enabled = on;
        requireOk(writeExportOptions(projectFolder, options));
    }

    // Writes build/used-items.json as a build would.
    void used(const UsedItems& items) const { requireOk(writeUsedItems(projectFolder / "build" / "used-items.json", items)); }

    fs::path jsonFile() const { return projectFolder / "export" / "Square_licenses.json"; }
    fs::path textFile() const { return projectFolder / "export" / "Square_licenses.txt"; }
    void setMode(const std::string& mode) const { std::ofstream(blenderFolder / "fake_mode.txt") << mode; }

    TempDir tmp;
    fs::path projectFolder;
    fs::path blenderFolder;
    ExportRequest request;
};

}

TEST_CASE("an export with the license report on writes both files", "[core][export][licenses]")
{
    LicenseFixture f("licenses-on");
    f.licenses(true);
    f.used({"abc", {"sofa"}, {"oak_planks"}});
    const ExportReport report = runExport(f.request);
    requireOk(report.status);
    CHECK(report.licenses.enabled);
    CHECK(report.licenses.written);
    CHECK(report.licenses.jsonFile == f.jsonFile());
    CHECK(report.licenses.items == 2);
    // There is no library, so both items are missing and say so.
    CHECK(report.licenses.warnings == 2);
    const nlohmann::json written = nlohmann::json::parse(readTextFile(f.jsonFile()));
    CHECK(schemaProblem("license-report.schema.json", written) == "");
    CHECK(written["plan_fingerprint"] == "abc");
    CHECK(readTextFile(f.textFile()).find("not legal advice") != std::string::npos);
    const nlohmann::json reportJson = nlohmann::json::parse(readTextFile(f.projectFolder / "export" / "export_report.json"));
    CHECK(schemaProblem("export-report.schema.json", reportJson) == "");
    const ExportReportRead read = readExportReport(f.projectFolder);
    requireOk(read.status);
    CHECK(read.report.licenses.written);
    CHECK(read.report.licenses.items == 2);

    // A blocked export leaves the files of the last good one.
    f.setMode("quality_fail");
    CHECK(runExport(f.request).status.code == ErrorCode::QualityFailed);
    CHECK(fs::is_regular_file(f.jsonFile()));

    // Switched off, the next export removes them.
    f.setMode("");
    f.licenses(false);
    const ExportReport off = runExport(f.request);
    requireOk(off.status);
    CHECK_FALSE(off.licenses.enabled);
    CHECK_FALSE(fs::exists(f.jsonFile()));
    CHECK_FALSE(fs::exists(f.textFile()));
}

TEST_CASE("without used items the license report is skipped and the export still works", "[core][export][licenses]")
{
    LicenseFixture f("licenses-no-build");
    f.licenses(true);
    const ExportReport report = runExport(f.request);
    requireOk(report.status);
    CHECK(report.licenses.enabled);
    CHECK_FALSE(report.licenses.written);
    CHECK(report.licenses.reason == "Build the project first.");
    CHECK_FALSE(fs::exists(f.jsonFile()));
}
