#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>
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

TEST_CASE("an export may start with a build, a Blender and no job running", "[core][exportready]")
{
    ReadyFixture f("ready-none");
    writeAt(f.blendFile(), 0);
    REQUIRE(exportBlockers(f.project, f.settings).empty());
}

TEST_CASE("an export needs no library or projects folder unless it makes library items", "[core][exportready]")
{
    ReadyFixture f("ready-nofolders");
    writeAt(f.blendFile(), 0);
    MachineSettings settings = f.settings;
    settings.library.reset();
    settings.projects = f.tmp.path() / "No projects";
    REQUIRE(exportBlockers(f.project, settings).empty());
    settings.projects.reset();
    REQUIRE(exportBlockers(f.project, settings).empty());
}

TEST_CASE("an export without building.blend asks for a build first", "[core][exportready]")
{
    ReadyFixture f("ready-nobuild");
    const std::vector<ExportBlocker> blockers = exportBlockers(f.project, f.settings);
    REQUIRE(codes(blockers) == std::vector<std::string>{"no_build"});
    REQUIRE(blockers[0].message == "Build the project first.");
    REQUIRE(blockers[0].error == ErrorCode::NotFound);

    SECTION("a folder named building.blend is not a build") {
        fs::create_directories(f.blendFile());
        REQUIRE(codes(exportBlockers(f.project, f.settings)) == std::vector<std::string>{"no_build"});
    }
}

TEST_CASE("an export is blocked while a build or an export holds the lock", "[core][exportready]")
{
    ReadyFixture f("ready-lock");
    writeAt(f.blendFile(), 0);
    f.takeLock();
    const std::vector<ExportBlocker> blockers = exportBlockers(f.project, f.settings);
    REQUIRE(codes(blockers) == std::vector<std::string>{"in_use"});
    REQUIRE(blockers[0].message == "A build or an export of this project is running.");
    REQUIRE(blockers[0].error == ErrorCode::InUse);

    SECTION("a lock left by a process that ended does not block") {
        std::ofstream(f.project / "build" / "build.lock", std::ios::binary | std::ios::trunc) << "0 0 0\n";
        REQUIRE(exportBlockers(f.project, f.settings).empty());
    }
}

TEST_CASE("an export needs a Blender, without starting one to find out", "[core][exportready]")
{
    ReadyFixture f("ready-blender");
    writeAt(f.blendFile(), 0);

    SECTION("a Blender option must be a file") {
        ExportNeeds needs;
        needs.blender = f.tmp.path() / "none" / "blender.exe";
        const std::vector<ExportBlocker> blockers = exportBlockers(f.project, f.settings, needs);
        REQUIRE(codes(blockers) == std::vector<std::string>{"no_blender"});
        REQUIRE(blockers[0].message == "The Blender " + pathToUtf8(*needs.blender) + " does not exist.");
        REQUIRE(blockers[0].error == ErrorCode::BlenderNotFound);
        needs.blender = *f.settings.blender;
        REQUIRE(exportBlockers(f.project, f.settings, needs).empty());
    }
    SECTION("an option wins over the settings") {
        MachineSettings settings = f.settings;
        settings.blender = f.tmp.path() / "gone" / "blender.exe";
        ExportNeeds needs;
        needs.blender = *f.settings.blender;
        REQUIRE(exportBlockers(f.project, settings, needs).empty());
    }
    SECTION("without an option the quick setup check's Blender item decides") {
        plynth::test::ScopedEnv missing("PLYNTH_BLENDER", pathToUtf8(f.tmp.path() / "missing" / "blender.exe"));
        const std::vector<ExportBlocker> blockers = exportBlockers(f.project, f.settings);
        REQUIRE(codes(blockers) == std::vector<std::string>{"no_blender"});
        const SetupItem* item = quickSetupCheck(f.settings, true).find(kCheckBlender);
        REQUIRE(item != nullptr);
        REQUIRE(blockers[0].message == item->detail + " " + item->action);
        REQUIRE(blockers[0].error == ErrorCode::BlenderNotFound);
    }
}

TEST_CASE("an export that makes the license report or the kit needs a library", "[core][exportready]")
{
    ReadyFixture f("ready-library");
    writeAt(f.blendFile(), 0);
    MachineSettings settings = f.settings;
    settings.library.reset();
    REQUIRE(exportBlockers(f.project, settings).empty());

    ExportOptions options;
    options.licenses.enabled = true;
    std::ofstream(f.project / "export-options.json", std::ios::binary) << exportOptionsToJson(options).dump(2);

    const std::vector<ExportBlocker> none = exportBlockers(f.project, settings);
    REQUIRE(codes(none) == std::vector<std::string>{"no_library"});
    REQUIRE(none[0].message == "The export makes the license report, which needs a library, and no library folder is chosen.");
    REQUIRE(none[0].error == ErrorCode::NoLibrary);

    settings.library = f.tmp.path() / "Projects";
    const std::vector<ExportBlocker> wrong = exportBlockers(f.project, settings);
    REQUIRE(codes(wrong) == std::vector<std::string>{"no_library"});
    REQUIRE(wrong[0].message == "The export makes the license report, which needs a library, and " +
                                    pathToUtf8(*settings.library) + " is not a Plynth library.");

    REQUIRE(exportBlockers(f.project, f.settings).empty());
    ExportNeeds needs;
    needs.library = *f.settings.library;
    REQUIRE(exportBlockers(f.project, settings, needs).empty());
    needs.library = f.tmp.path() / "Projects";
    REQUIRE(codes(exportBlockers(f.project, f.settings, needs)) == std::vector<std::string>{"no_library"});

    SECTION("the kit asks for a library the same way") {
        fs::remove(f.project / "export-options.json");
        ExportNeeds kit;
        kit.kit = true;
        MachineSettings bare = f.settings;
        bare.library.reset();
        const std::vector<ExportBlocker> blockers = exportBlockers(f.project, bare, kit);
        REQUIRE(codes(blockers) == std::vector<std::string>{"no_library"});
        REQUIRE(blockers[0].message.rfind("The export makes the modular kit, which needs a library, ", 0) == 0);
    }
    SECTION("an export options file that cannot be read is left to the export") {
        plynth::test::writeBytes(f.project / "export-options.json", "not json");
        REQUIRE(exportBlockers(f.project, settings).empty());
    }
}

TEST_CASE("the export blockers come in the order build, lock, Blender, library", "[core][exportready]")
{
    ReadyFixture f("ready-order");
    f.takeLock();
    MachineSettings settings = f.settings;
    settings.library.reset();
    ExportOptions options;
    options.licenses.enabled = true;
    std::ofstream(f.project / "export-options.json", std::ios::binary) << exportOptionsToJson(options).dump(2);
    ExportNeeds needs;
    needs.blender = f.tmp.path() / "none" / "blender.exe";
    REQUIRE(codes(exportBlockers(f.project, settings, needs)) ==
            std::vector<std::string>{"no_build", "in_use", "no_blender", "no_library"});
}
