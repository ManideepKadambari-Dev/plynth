#include "UpdateTestSupport.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

// The codes of a list of blockers, in order.
std::vector<std::string> codes(const std::vector<UpdateBlocker>& blockers)
{
    std::vector<std::string> out;
    for (const UpdateBlocker& b : blockers) out.push_back(b.code);
    return out;
}

// Facts for an installed copy in a folder, with this process as the only program.
BlockerFacts factsFor(const fs::path& installFolder)
{
    BlockerFacts facts;
    facts.installation = installedCopy(installFolder);
    facts.selfPid = 100;
    facts.programs.push_back({100, 50, installFolder / "plynth.exe"});
    return facts;
}

}

TEST_CASE("an installed copy alone has no blockers, and a copy that is not installed always has one", "[update][blockers]") {
    plynth::test::TempDir tmp("update-blockers-none");
    const fs::path install = tmp.path() / "Plynth";
    CHECK(blockersFrom(factsFor(install)).empty());
    BlockerFacts notInstalled = factsFor(install);
    notInstalled.installation.kind = InstallKind::NotInstalled;
    const auto blockers = blockersFrom(notInstalled);
    REQUIRE(codes(blockers) == std::vector<std::string>{"not_installed"});
    CHECK(blockers[0].message == std::string(kNotInstalledMessage));
}

TEST_CASE("another Plynth from the install folder blocks, but this one and its launcher do not", "[update][blockers]") {
    plynth::test::TempDir tmp("update-blockers-programs");
    const fs::path install = tmp.path() / "Plynth";
    BlockerFacts facts = factsFor(install);
    facts.programs.push_back({50, 10, install / "plynth.com"});         // the launcher that started this process
    facts.programs.push_back({60, 10, install / "cli" / "PLYNTH.COM"}); // a launcher idle in a terminal
    facts.programs.push_back({70, 10, install / "plynth.exe"});         // another window
    facts.programs.push_back({80, 70, tmp.path() / "Other" / "plynth.exe"}); // a copy somewhere else
    facts.programs.push_back({90, 70, install / "blender.exe"});
    facts.programs.push_back({95, 70, fs::path()});
    const auto blockers = blockersFrom(facts);
    REQUIRE(codes(blockers) == std::vector<std::string>{"plynth_running", "plynth_running"});
    CHECK(blockers[0].message.find("process 60") != std::string::npos);
    CHECK(blockers[1].message.find("process 70") != std::string::npos);

    // A parent that is the app, not the launcher, still counts.
    BlockerFacts underApp = factsFor(install);
    underApp.programs[0].parentPid = 70;
    underApp.programs.push_back({70, 10, install / "plynth.exe"});
    CHECK(codes(blockersFrom(underApp)) == std::vector<std::string>{"plynth_running"});
}

TEST_CASE("an active edit session or a running build in a listed project blocks", "[update][blockers]") {
    plynth::test::TempDir tmp("update-blockers-projects");
    const fs::path install = tmp.path() / "Plynth";
    const fs::path editing = tmp.path() / "Editing project";
    const fs::path building = tmp.path() / "Building project";
    const fs::path idle = tmp.path() / "Idle project";
    const fs::path expired = tmp.path() / "Expired project";
    for (const fs::path& folder : {editing, building, idle, expired}) fs::create_directories(folder);
    const SessionTime now = sessionNow();
    plynth::test::requireOk(writeSession(editing, SessionLock{1, "test", now, now}));
    const SessionTime old = now - std::chrono::hours(24);
    plynth::test::requireOk(writeSession(expired, SessionLock{1, "test", old, old}));
    const ProcessIdentity self = currentProcess();
    const long long seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    writeBytes(building / "build" / "build.lock",
               std::to_string(self.pid) + " " + std::to_string(self.startKey) + " " + std::to_string(seconds) + "\n");

    BlockerFacts facts = factsFor(install);
    facts.projects = {idle, editing, building, expired, editing, fs::path()};
    const auto blockers = blockersFrom(facts);
    REQUIRE(codes(blockers) == std::vector<std::string>{"edit_session", "build_running"});
    CHECK(blockers[0].message.find(pathToUtf8(editing)) != std::string::npos);
    CHECK(blockers[1].message.find(pathToUtf8(building)) != std::string::npos);
}

TEST_CASE("a job that would end the installer blocks unless it lets programs leave", "[update][blockers]") {
    plynth::test::TempDir tmp("update-blockers-job");
    BlockerFacts facts = factsFor(tmp.path() / "Plynth");
    facts.job = JobPlacement{true, true, false};
    CHECK(codes(blockersFrom(facts)) == std::vector<std::string>{"parent_job"});
    facts.job = JobPlacement{true, true, true};
    CHECK(blockersFrom(facts).empty());
    facts.job = JobPlacement{true, false, false};
    CHECK(blockersFrom(facts).empty());
}

TEST_CASE("the blockers of a copy that is not installed look at nothing else", "[update][blockers]") {
    plynth::test::TempDir tmp("update-blockers-real");
    const Installation copy{InstallKind::NotInstalled, tmp.path(), "0.1.0"};
    CHECK(codes(updateBlockers(copy, tmp.path())) == std::vector<std::string>{"not_installed"});
}

TEST_CASE("the running programs and the job of this process can be read", "[update][blockers]") {
    const auto programs = runningPrograms();
    const unsigned long self = GetCurrentProcessId();
    bool found = false;
    for (const RunningProgram& p : programs) found = found || p.pid == self;
    CHECK(found);
    const JobPlacement job = currentJobPlacement();
    if (!job.inJob) {
        CHECK_FALSE(job.killOnClose);
        CHECK_FALSE(job.breakawayAllowed);
    }
}
