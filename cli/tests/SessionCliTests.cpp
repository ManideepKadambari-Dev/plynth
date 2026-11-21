#include "TestSupport.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;

namespace {

// The whole content of a file.
std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A path as UTF-8 text.
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The code of a failed answer.
std::string errorCode(const Outcome& outcome) {
    return outcome.answer.at("error").at("code").get<std::string>();
}

// Makes a project with the sample image and returns its folder.
std::filesystem::path makeProject(const TempFolder& temp) {
    const auto folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--image", PLYNTH_SAMPLE_PLAN, "--project", utf8(folder)});
    REQUIRE(outcome.exitCode == kExitOk);
    return folder;
}

// Plays the app: says this process has the project open, with or without unsaved changes.
void appOpens(const std::filesystem::path& folder, bool modified) {
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = modified;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(folder, state).ok());
}

// Writes a session whose last command was this many seconds ago.
void sessionFrom(const std::filesystem::path& folder, int secondsAgo) {
    plynth::SessionLock lock;
    lock.pid = 1;
    lock.tool = "test";
    lock.started = plynth::sessionNow() - std::chrono::seconds(secondsAgo + 10);
    lock.lastActivity = plynth::sessionNow() - std::chrono::seconds(secondsAgo);
    REQUIRE(plynth::writeSession(folder, lock).ok());
}

// Runs point add on a project folder.
Outcome addPoint(const std::filesystem::path& folder) {
    return runChecked({"point", "add", "--x", "1", "--y", "1", "--project", utf8(folder)});
}

}

TEST_CASE("session status before begin, during and after end", "[cli][session]") {
    TempFolder temp("status");
    const auto folder = makeProject(temp);
    const std::string project = utf8(folder);

    const auto none = runChecked({"session", "status", "--project", project});
    CHECK(none.exitCode == kExitOk);
    CHECK(none.answer.at("result").at("state") == "none");
    CHECK(none.answer.at("result").at("tool").is_null());
    CHECK(none.answer.at("result").at("app_open") == false);

    const auto begun = runChecked({"session", "begin", "--tool", "helper", "--project", project});
    CHECK(begun.exitCode == kExitOk);
    CHECK(begun.answer.at("result").at("state") == "active");
    CHECK(begun.answer.at("result").at("tool") == "helper");
    CHECK(begun.answer.at("result").at("already_active") == false);
    CHECK(std::filesystem::exists(folder / "session.lock"));

    const auto during = runChecked({"session", "status", "--project", project});
    CHECK(during.answer.at("result").at("state") == "active");
    CHECK(during.answer.at("result").at("tool") == "helper");
    CHECK(during.answer.at("result").at("started").is_string());
    CHECK(during.answer.at("result").at("expires_at").is_string());

    const auto ended = runChecked({"session", "end", "--project", project});
    CHECK(ended.exitCode == kExitOk);
    CHECK(ended.answer.at("result").at("ended") == true);
    CHECK_FALSE(std::filesystem::exists(folder / "session.lock"));

    const auto again = runChecked({"session", "end", "--project", project});
    CHECK(again.exitCode == kExitOk);
    CHECK(again.answer.at("result").at("ended") == false);
}

TEST_CASE("session begin twice gives already_active", "[cli][session]") {
    TempFolder temp("twice");
    const auto folder = makeProject(temp);
    const std::string project = utf8(folder);
    runChecked({"session", "begin", "--project", project});
    const auto second = runChecked({"session", "begin", "--project", project});
    CHECK(second.exitCode == kExitOk);
    CHECK(second.answer.at("result").at("already_active") == true);
    CHECK(second.answer.at("result").at("tool") == "plynth");
}

TEST_CASE("session begin refuses a bad tool name and a folder without a project", "[cli][session]") {
    TempFolder temp("bad");
    const auto none = runChecked({"session", "begin"}, temp.path());
    CHECK(none.exitCode == kExitFailed);
    CHECK(errorCode(none) == "not_a_project");

    const auto folder = makeProject(temp);
    const auto longName = runChecked({"session", "begin", "--tool", std::string(65, 'x'), "--project", utf8(folder)});
    CHECK(longName.exitCode == kExitFailed);
    CHECK(errorCode(longName) == "invalid_value");
    CHECK_FALSE(std::filesystem::exists(folder / "session.lock"));
}

TEST_CASE("an edit runs when there is no session and the app is not open", "[cli][session]") {
    TempFolder temp("free");
    const auto folder = makeProject(temp);
    const auto outcome = addPoint(folder);
    CHECK(outcome.exitCode == kExitOk);
    CHECK_FALSE(std::filesystem::exists(folder / "session.lock"));
}

TEST_CASE("an edit is refused with locked when the app has the project open and there is no session", "[cli][session]") {
    TempFolder temp("locked");
    const auto folder = makeProject(temp);
    appOpens(folder, false);
    const auto before = readFile(folder / "plan.json");

    const auto outcome = addPoint(folder);
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "locked");
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("session begin") != std::string::npos);
    CHECK(readFile(folder / "plan.json") == before);

    for (const auto& args : std::vector<std::vector<std::string>>{
             {"scale", "set", "--pixels-per-metre", "10"}, {"wall", "add", "--from", "p1", "--to", "p2"}}) {
        auto full = args;
        full.push_back("--project");
        full.push_back(utf8(folder));
        const auto refused = runChecked(full);
        CHECK(errorCode(refused) == "locked");
    }
    CHECK(readFile(folder / "plan.json") == before);

    const auto status = runChecked({"session", "status", "--project", utf8(folder)});
    CHECK(status.answer.at("result").at("app_open") == true);
    CHECK(status.answer.at("result").at("app_modified") == false);
}

TEST_CASE("project info is never refused while the app has the project open", "[cli][session]") {
    TempFolder temp("info");
    const auto folder = makeProject(temp);
    appOpens(folder, true);
    const auto outcome = runChecked({"project", "info", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitOk);
}

TEST_CASE("session begin works with the app open, then edits run", "[cli][session]") {
    TempFolder temp("begin");
    const auto folder = makeProject(temp);
    appOpens(folder, false);
    const std::string project = utf8(folder);

    const auto begun = runChecked({"session", "begin", "--project", project});
    CHECK(begun.exitCode == kExitOk);
    CHECK(begun.answer.at("result").at("app_open") == true);
    CHECK(begun.answer.at("result").at("app_modified") == false);

    const auto edit = addPoint(folder);
    CHECK(edit.exitCode == kExitOk);
    CHECK(edit.answer.at("result").at("id") == "p1");
}

TEST_CASE("session begin is refused with unsaved_changes when the app has unsaved changes", "[cli][session]") {
    TempFolder temp("unsaved");
    const auto folder = makeProject(temp);
    appOpens(folder, true);
    const auto outcome = runChecked({"session", "begin", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "unsaved_changes");
    CHECK_FALSE(std::filesystem::exists(folder / "session.lock"));
    CHECK(errorCode(addPoint(folder)) == "locked");
}

TEST_CASE("an expired session no longer lets edits through", "[cli][session]") {
    TempFolder temp("expired");
    const auto folder = makeProject(temp);
    appOpens(folder, false);
    sessionFrom(folder, 400);
    const auto before = readFile(folder / "plan.json");

    const auto status = runChecked({"session", "status", "--project", utf8(folder)});
    CHECK(status.answer.at("result").at("state") == "expired");

    const auto refused = addPoint(folder);
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "locked");
    CHECK(refused.answer.at("error").at("message").get<std::string>().find("5 minutes") != std::string::npos);
    CHECK(readFile(folder / "plan.json") == before);

    const auto begun = runChecked({"session", "begin", "--project", utf8(folder)});
    CHECK(begun.exitCode == kExitOk);
    CHECK(begun.answer.at("result").at("already_active") == false);
    CHECK(addPoint(folder).exitCode == kExitOk);
}

TEST_CASE("every edit and project info refresh the last activity of an active session", "[cli][session]") {
    TempFolder temp("refresh");
    const auto folder = makeProject(temp);
    appOpens(folder, false);

    for (int i = 0; i < 2; ++i) {
        sessionFrom(folder, 200);
        const auto outcome =
            i == 0 ? addPoint(folder) : runChecked({"project", "info", "--project", utf8(folder)});
        CHECK(outcome.exitCode == kExitOk);
        const auto read = plynth::readSession(folder, plynth::sessionNow());
        CHECK(read.state == plynth::SessionState::Active);
        CHECK(read.lock.lastActivity >= plynth::sessionNow() - std::chrono::seconds(10));
    }
}

TEST_CASE("session status does not refresh the session", "[cli][session]") {
    TempFolder temp("poll");
    const auto folder = makeProject(temp);
    sessionFrom(folder, 200);
    runChecked({"session", "status", "--project", utf8(folder)});
    const auto read = plynth::readSession(folder, plynth::sessionNow());
    CHECK(read.lock.lastActivity <= plynth::sessionNow() - std::chrono::seconds(190));
}

TEST_CASE("an app-state file of a process that is gone does not block edits", "[cli][session]") {
    TempFolder temp("stale");
    const auto folder = makeProject(temp);
    plynth::AppState state;
    state.process = {0, 0};
    state.modified = true;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(folder, state).ok());
    CHECK(addPoint(folder).exitCode == kExitOk);

    state.process = {plynth::currentProcess().pid, 1}; // right id, wrong start time: another run of that id
    REQUIRE(plynth::writeAppState(folder, state).ok());
    CHECK(addPoint(folder).exitCode == kExitOk);
}

TEST_CASE("help lists the session commands", "[cli][session]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"session begin", "session end", "session status"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
    CHECK(plynth::cli::isCommandWord("session"));
}

TEST_CASE("session begin counts the tool name in characters and maps empty to the default", "[cli][session]") {
    TempFolder temp("tool");
    const auto folder = makeProject(temp);
    const std::string project = utf8(folder);

    std::string accented;
    for (int i = 0; i < 40; ++i) {
        accented += "\xC3\xBC"; // 40 characters, 80 bytes
    }
    const auto accentedBegin = runChecked({"session", "begin", "--tool", accented, "--project", project});
    CHECK(accentedBegin.exitCode == kExitOk);
    CHECK(accentedBegin.answer.at("result").at("tool") == accented);

    runChecked({"session", "end", "--project", project});
    const auto emptyBegin = runChecked({"session", "begin", "--tool", "", "--project", project});
    CHECK(emptyBegin.exitCode == kExitOk);
    CHECK(emptyBegin.answer.at("result").at("tool") == "plynth");
}
