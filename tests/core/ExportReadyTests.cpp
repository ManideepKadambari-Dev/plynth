#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Export.h>
#include <plynth/core/ExportReady.h>
#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Settings.h>
#include <plynth/core/SetupCheck.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Writes a file and gives it a write time this many seconds from now.
void writeAt(const fs::path& file, int seconds)
{
    plynth::test::writeBytes(file, "x");
    fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(seconds));
}

// A project folder with build/ and export/, and settings that pass the quick setup check.
struct ReadyFixture {
    explicit ReadyFixture(const std::string& tag) : tmp(tag), noVariable("PLYNTH_BLENDER", "")
    {
        project = tmp.path() / "Project";
        fs::create_directories(project / "build");
        fs::create_directories(project / "export");
        settings.library = tmp.path() / "Library";
        settings.projects = tmp.path() / "Projects";
        plynth::test::requireOk(initLibrary(*settings.library));
        fs::create_directories(*settings.projects);
        settings.blender = plynth::test::makeFakeBlender(tmp.path() / "Blender 5.2", "5.2.0");
    }

    fs::path blendFile() const { return project / "build" / "building.blend"; }
    fs::path buildFile() const { return project / "build" / "build.json"; }
    fs::path reportFile() const { return project / "export" / "export_report.json"; }

    // Writes build/build.json of a build with this fingerprint, failed or not.
    void writeBuild(const std::string& fingerprint, bool failed = false) const
    {
        BuildReport build;
        build.jobId = "build-00000001";
        build.startedAt = sessionNow();
        build.planFingerprint = fingerprint;
        build.blenderVersion = "5.2.0";
        if (failed) {
            build.status = fail(ErrorCode::BlenderFailed, "The build failed.");
            build.errorCode = "save_failed";
        }
        std::ofstream(buildFile(), std::ios::binary) << buildReportToJson(build).dump(2);
    }

    // Writes build/built-plan.json, the record of the last successful build.
    void writeBuiltPlan(const std::string& fingerprint) const
    {
        const Plan plan = plynth::test::squareProjectWithWalls().plan;
        std::ofstream(project / "build" / "built-plan.json", std::ios::binary)
            << Json{{"plan_fingerprint", fingerprint}, {"plan", planToJson(plan)}}.dump();
    }

    // An export report made from the build with this fingerprint, with its file written at a time.
    ExportReport exportedFrom(const std::string& fingerprint, int seconds = 0) const
    {
        ExportReport report;
        report.buildFingerprint = fingerprint;
        writeAt(reportFile(), seconds);
        return report;
    }

    // Writes build/build.lock as a running job of this process holds it.
    void takeLock() const
    {
        const ProcessIdentity me = currentProcess();
        const long long now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream(project / "build" / "build.lock", std::ios::binary)
            << me.pid << " " << me.startKey << " " << now << "\n";
    }

    plynth::test::TempDir tmp;
    plynth::test::ScopedEnv noVariable;
    fs::path project;
    MachineSettings settings;
};

// The codes of a list of blockers.
std::vector<std::string> codes(const std::vector<ExportBlocker>& blockers)
{
    std::vector<std::string> out;
    for (const ExportBlocker& b : blockers) out.push_back(b.code);
    return out;
}

}

TEST_CASE("an export report from the last build is up to date", "[core][exportready]")
{
    ReadyFixture f("ready-current");
    writeAt(f.blendFile(), -60);
    f.writeBuild("aaa");
    REQUIRE_FALSE(exportReportOutOfDate(f.exportedFrom("aaa"), f.project));
}

TEST_CASE("an export report from another build fingerprint is out of date", "[core][exportready]")
{
    ReadyFixture f("ready-fingerprint");
    writeAt(f.blendFile(), -60);
    f.writeBuild("bbb");
    REQUIRE(exportReportOutOfDate(f.exportedFrom("aaa"), f.project));
    // A report written when there was no build at all is older than any build since.
    REQUIRE(exportReportOutOfDate(f.exportedFrom(""), f.project));
}

TEST_CASE("an export report older than building.blend is out of date with the same fingerprint", "[core][exportready]")
{
    ReadyFixture f("ready-time");
    f.writeBuild("aaa");
    const ExportReport report = f.exportedFrom("aaa", -120);
    writeAt(f.blendFile(), -10);
    REQUIRE(exportReportOutOfDate(report, f.project));

    SECTION("without building.blend only the fingerprint counts") {
        fs::remove(f.blendFile());
        REQUIRE_FALSE(exportReportOutOfDate(report, f.project));
    }
}

TEST_CASE("a failed build does not make an export out of date", "[core][exportready]")
{
    ReadyFixture f("ready-failed");
    writeAt(f.blendFile(), -600);
    f.writeBuiltPlan("aaa");
    const ExportReport report = f.exportedFrom("aaa", -300);
    // The failed build of a changed plan rewrites build.json but leaves building.blend and built-plan.json.
    f.writeBuild("ccc", true);
    REQUIRE_FALSE(exportReportOutOfDate(report, f.project));

    SECTION("the last successful build is the one built-plan.json names") {
        f.writeBuiltPlan("ddd");
        REQUIRE(exportReportOutOfDate(report, f.project));
    }
    SECTION("without built-plan.json a failed build gives no fingerprint to compare") {
        fs::remove(f.project / "build" / "built-plan.json");
        REQUIRE_FALSE(exportReportOutOfDate(report, f.project));
    }
}

TEST_CASE("an export report is never out of date when a file is missing or unreadable", "[core][exportready]")
{
    ReadyFixture f("ready-missing");
    writeAt(f.blendFile(), 0);
    ExportReport report;
    report.buildFingerprint = "aaa";

    SECTION("no export_report.json") {
        f.writeBuild("bbb");
        REQUIRE_FALSE(exportReportOutOfDate(report, f.project));
    }
    SECTION("no build.json") {
        writeAt(f.reportFile(), -100);
        REQUIRE_FALSE(exportReportOutOfDate(report, f.project));
    }
    SECTION("a build.json that is not a report") {
        writeAt(f.reportFile(), -100);
        plynth::test::writeBytes(f.buildFile(), "not json");
        REQUIRE_FALSE(exportReportOutOfDate(report, f.project));
    }
    SECTION("a project folder that does not exist") {
        REQUIRE_FALSE(exportReportOutOfDate(report, f.tmp.path() / "Nothing"));
    }
}

TEST_CASE("an export may start with a build, a working setup and no job running", "[core][exportready]")
{
    ReadyFixture f("ready-none");
    writeAt(f.blendFile(), 0);
    REQUIRE(exportBlockers(f.project, f.settings).empty());
}

TEST_CASE("an export without building.blend asks for a build first", "[core][exportready]")
{
    ReadyFixture f("ready-nobuild");
    const std::vector<ExportBlocker> blockers = exportBlockers(f.project, f.settings);
    REQUIRE(codes(blockers) == std::vector<std::string>{"no_build"});
    REQUIRE(blockers[0].message == "Build the project first.");

    SECTION("a folder named building.blend is not a build") {
        fs::create_directories(f.blendFile());
        REQUIRE(codes(exportBlockers(f.project, f.settings)) == std::vector<std::string>{"no_build"});
    }
}

TEST_CASE("an export is blocked by each required setup item that blocks", "[core][exportready]")
{
    ReadyFixture f("ready-setup");
    writeAt(f.blendFile(), 0);
    MachineSettings settings = f.settings;
    settings.library.reset();
    settings.projects = f.tmp.path() / "No projects";

    const std::vector<ExportBlocker> blockers = exportBlockers(f.project, settings);
    REQUIRE(codes(blockers) == std::vector<std::string>{"setup", "setup"});
    const SetupReport quick = quickSetupCheck(settings, true);
    std::vector<std::string> expected;
    for (const SetupItem& item : quick.items) {
        if (item.blocks()) expected.push_back(item.name + ": " + (item.action.empty() ? item.detail : item.action));
    }
    REQUIRE(blockers[0].message == expected[0]);
    REQUIRE(blockers[1].message == expected[1]);
    REQUIRE(blockers[0].message.rfind("Library folder: ", 0) == 0);
}

TEST_CASE("an export is blocked while a build or an export holds the lock", "[core][exportready]")
{
    ReadyFixture f("ready-lock");
    writeAt(f.blendFile(), 0);
    f.takeLock();
    const std::vector<ExportBlocker> blockers = exportBlockers(f.project, f.settings);
    REQUIRE(codes(blockers) == std::vector<std::string>{"in_use"});
    REQUIRE(blockers[0].message == "A build or an export of this project is running.");

    SECTION("a lock left by a process that ended does not block") {
        std::ofstream(f.project / "build" / "build.lock", std::ios::binary | std::ios::trunc) << "0 0 0\n";
        REQUIRE(exportBlockers(f.project, f.settings).empty());
    }
}

TEST_CASE("the export blockers come in the order build, setup, lock", "[core][exportready]")
{
    ReadyFixture f("ready-order");
    f.takeLock();
    MachineSettings settings = f.settings;
    settings.library.reset();
    REQUIRE(codes(exportBlockers(f.project, settings)) == std::vector<std::string>{"no_build", "setup", "in_use"});
}
