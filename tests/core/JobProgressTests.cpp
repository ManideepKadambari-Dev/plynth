#include "BuildFixtures.h"
#include "CatalogSupport.h"
#include "FakeBlender.h"

#include <plynth/core/Build.h>
#include <plynth/core/JobProgress.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

TEST_CASE("jobPercent maps stages and worker steps onto the bar", "[job-progress]") {
    CHECK(jobPercent("preparing", {}) == 3);
    CHECK(jobPercent("blender", {}) == 5);
    CHECK(jobPercent("blender", {"raise_walls", 1, 10, ""}) == 5);
    CHECK(jobPercent("blender", {"save", 10, 10, ""}) == 86);
    CHECK(jobPercent("blender", {"odd", 30, 10, ""}) == 95);
    CHECK(jobPercent("finishing", {}) == 97);
    CHECK(jobPercent("done", {}) == 100);
}

TEST_CASE("readJobProgress gives nothing for a missing or broken file", "[job-progress]") {
    TempDir tmp("job-progress-read");
    const fs::path project = tmp.path() / "P";
    fs::create_directories(project / "build");
    CHECK_FALSE(readJobProgress(project).has_value());
    std::ofstream(project / "build" / "progress.json") << "{ not json";
    CHECK_FALSE(readJobProgress(project).has_value());
    std::ofstream(project / "build" / "progress.json", std::ios::trunc)
        << R"({"format": "plynth-build-progress", "version": 1, "kind": "render"})";
    CHECK_FALSE(readJobProgress(project).has_value());
}

TEST_CASE("the progress file is written on each step, throttled within one, and removed at the end", "[job-progress]") {
    TempDir tmp("job-progress-file");
    const fs::path project = tmp.path() / "P";
    fs::create_directories(project / "build");
    const SessionTime started = sessionNow();
    {
        JobProgressFile file(project / "build", "export", "export-0000abcd", started);
        CHECK_FALSE(fs::exists(project / "build" / "progress.json"));

        file.update("preparing", {}, "Preparing the export");
        std::optional<JobProgress> read = readJobProgress(project);
        REQUIRE(read.has_value());
        CHECK(read->kind == "export");
        CHECK(read->jobId == "export-0000abcd");
        CHECK(read->stage == "preparing");
        CHECK(read->message == "Preparing the export");
        CHECK(read->percent == 3);
        CHECK(read->started == started);
        CHECK(read->owner == currentProcess());

        file.update("blender", {"check_scale", 2, 4, "Checking the scale"}, "Starting Blender");
        read = readJobProgress(project);
        REQUIRE(read.has_value());
        CHECK(read->stage == "blender");
        CHECK(read->step == "check_scale");
        CHECK(read->index == 2);
        CHECK(read->count == 4);
        CHECK(read->message == "Checking the scale");
        CHECK(read->percent == 27);

        // A new message within the same step waits for the interval.
        file.update("blender", {"check_scale", 2, 4, "Still checking"}, "Starting Blender");
        CHECK(readJobProgress(project).value().message == "Checking the scale");
        std::this_thread::sleep_for(kJobProgressInterval + std::chrono::milliseconds(50));
        file.update("blender", {"check_scale", 2, 4, "Still checking"}, "Starting Blender");
        CHECK(readJobProgress(project).value().message == "Still checking");
    }
    CHECK_FALSE(fs::exists(project / "build" / "progress.json"));
    CHECK_FALSE(fs::exists(project / "build" / "progress.json.tmp"));
}

TEST_CASE("a running build shows its progress in progress.json and removes it when it ends", "[job-progress]") {
    TestLibrary lib("job-progress-build");
    const fs::path project = lib.tmp.path() / "Project";
    fs::create_directories(project);
    BuildRequest request;
    request.projectFolder = project;
    request.project = buildProject();
    clearMaterialDefaults(request.project);
    request.library = lib.paths;
    request.blender = inspectBlender(makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", "hang"));
    request.run.scriptsFolder = makeFakeScripts(lib.tmp.path() / "scripts");
    lib.add("sofa", "Sofa", {"sofa"});

    // The fake worker prints every step and then hangs, so the file settles on its last step until the cancel.
    std::atomic<bool> done = false;
    std::optional<JobProgress> seen;
    bool runningSeen = false;
    std::thread watcher([&] {
        for (int i = 0; i < 300 && !done; ++i) {
            const std::optional<JobProgress> p = readJobProgress(project);
            if (p && p->stage == "blender" && p->count > 0 && p->index == p->count) {
                seen = p;
                runningSeen = jobRunning(project);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (int i = 0; i < 300 && !done; ++i) {
            if (requestCancel(project)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    const BuildReport report = runBuild(request);
    done = true;
    watcher.join();

    CHECK(report.status.code == ErrorCode::Cancelled);
    REQUIRE(seen.has_value());
    CHECK(runningSeen);
    CHECK(seen->kind == "build");
    CHECK(seen->jobId == report.jobId);
    CHECK(seen->owner == currentProcess());
    CHECK(seen->percent == jobPercent("blender", {seen->step, seen->index, seen->count, ""}));
    CHECK_FALSE(seen->message.empty());
    CHECK_FALSE(fs::exists(project / "build" / "progress.json"));
    CHECK_FALSE(fs::exists(project / "build" / "build.lock"));
    CHECK_FALSE(jobRunning(project));
}
