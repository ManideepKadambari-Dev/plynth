#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;
using plynth::test::TempDir;
using plynth::test::readTextFile;
using plynth::test::requireOk;
using plynth::test::schemaProblemForText;

namespace {

const SessionTime t0 = *parseSessionTime("2026-11-21T10:00:00Z");

// A time some seconds after t0.
SessionTime at(int seconds)
{
    return t0 + std::chrono::seconds(seconds);
}

// Makes a project folder with the files a project needs to count as one.
fs::path makeProject(const TempDir& tmp, const std::string& name = "p")
{
    Project project = makeNewProject();
    const fs::path folder = tmp.path() / name;
    requireOk(saveProject(project, folder, {}));
    return folder;
}

// Writes a text file.
void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// Copies a test data file into a folder under another name.
void copyData(const std::string& dataFile, const fs::path& target)
{
    fs::copy_file(fs::path(PLYNTH_TEST_DATA_DIR) / dataFile, target, fs::copy_options::overwrite_existing);
}

// Plays the app: writes app-state.json for this process.
void pretendAppOpen(const fs::path& folder, bool modified)
{
    requireOk(writeAppState(folder, AppState{currentProcess(), modified, t0}));
}

// Writes a session.lock with the given last activity.
void writeLock(const fs::path& folder, SessionTime lastActivity)
{
    requireOk(writeSession(folder, SessionLock{1, "plynth", t0, lastActivity}));
}

}

TEST_CASE("session times round trip and bad text is refused", "[core][session]") {
    REQUIRE(formatSessionTime(t0) == "2026-11-21T10:00:00Z");
    REQUIRE(formatSessionTime(SessionTime{}) == "1970-01-01T00:00:00Z");
    for (int seconds : {0, 1, 59, 3600, 86399, 86400, 123456789}) {
        const auto text = formatSessionTime(at(seconds));
        REQUIRE(parseSessionTime(text) == at(seconds));
    }
    REQUIRE(parseSessionTime("2024-02-29T23:59:59Z").has_value());
    for (const char* bad : {"", "2026-11-21T10:00:00", "2026-11-21 10:00:00Z", "2026-13-21T10:00:00Z",
                            "2026-02-30T10:00:00Z", "2026-11-21T24:00:00Z", "2026-11-21T10:60:00Z",
                            "2026-11-21T10:00:60Z", "2026-11-21T10:00:00+0Z", "20x6-11-21T10:00:00Z",
                            "2026-11-21T10:00:00Zz"}) {
        INFO(bad);
        REQUIRE_FALSE(parseSessionTime(bad).has_value());
    }
}

TEST_CASE("sessionNow is in whole seconds and near the real clock", "[core][session]") {
    const auto before = std::chrono::floor<std::chrono::seconds>(SessionClock::now());
    const SessionTime now = sessionNow();
    const auto after = std::chrono::floor<std::chrono::seconds>(SessionClock::now());
    REQUIRE(now >= before);
    REQUIRE(now <= after);
    REQUIRE(parseSessionTime(formatSessionTime(now)) == now);
}

TEST_CASE("the current process is running and a dead one is not", "[core][session]") {
    const ProcessIdentity me = currentProcess();
    REQUIRE(me.pid > 0);
    REQUIRE(me.startKey > 0);
    REQUIRE(isProcessRunning(me));
    REQUIRE(isProcessRunning({me.pid, 0}));
    REQUIRE_FALSE(isProcessRunning({me.pid, me.startKey + 1}));
    REQUIRE_FALSE(isProcessRunning({0, 0}));
    REQUIRE_FALSE(isProcessRunning({-5, 0}));
    REQUIRE_FALSE(isProcessRunning({2000000000, 0}));
}

TEST_CASE("state names are stable", "[core][session]") {
    REQUIRE(sessionStateName(SessionState::None) == "none");
    REQUIRE(sessionStateName(SessionState::Active) == "active");
    REQUIRE(sessionStateName(SessionState::Expired) == "expired");
    REQUIRE(errorCodeName(ErrorCode::Locked) == "locked");
    REQUIRE(errorCodeName(ErrorCode::UnsavedChanges) == "unsaved_changes");
    REQUIRE(errorCodeName(ErrorCode::NoLibrary) == "no_library");
    REQUIRE(errorCodeName(ErrorCode::BlenderNotFound) == "blender_not_found");
    REQUIRE(errorCodeName(ErrorCode::BlenderFailed) == "blender_failed");
    REQUIRE(errorCodeName(ErrorCode::Cancelled) == "cancelled");
    REQUIRE(errorCodeName(ErrorCode::DatabaseError) == "database_error");
    REQUIRE(errorCodeName(ErrorCode::QualityFailed) == "quality_failed");
}

TEST_CASE("readSession gives none when there is no lock", "[core][session]") {
    TempDir tmp("session-none");
    const SessionRead r = readSession(tmp.path(), t0);
    REQUIRE(r.state == SessionState::None);
    REQUIRE(r.status.ok());
}

TEST_CASE("a written lock reads back and is active until 300 seconds pass", "[core][session]") {
    TempDir tmp("session-active");
    const SessionLock lock{4242, "my tool ü", t0, at(12)};
    requireOk(writeSession(tmp.path(), lock));

    SessionRead r = readSession(tmp.path(), at(12));
    REQUIRE(r.state == SessionState::Active);
    REQUIRE(r.lock == lock);
    REQUIRE(r.expiresAt == at(312));

    REQUIRE(readSession(tmp.path(), at(312)).state == SessionState::Active);
    r = readSession(tmp.path(), at(313));
    REQUIRE(r.state == SessionState::Expired);
    REQUIRE(r.status.ok());
    REQUIRE(r.lock == lock);
    REQUIRE(r.expiresAt == at(312));
}

TEST_CASE("a lock with activity far in the future is expired", "[core][session]") {
    TempDir tmp("session-future");
    writeLock(tmp.path(), at(300));
    REQUIRE(readSession(tmp.path(), t0).state == SessionState::Active);
    writeLock(tmp.path(), at(301));
    REQUIRE(readSession(tmp.path(), t0).state == SessionState::Expired);
}

TEST_CASE("a lock that cannot be used counts as expired with a status", "[core][session]") {
    TempDir tmp("session-bad");
    const fs::path lock = tmp.path() / pathFromUtf8(kSessionFileName);

    writeText(lock, "not json at all");
    SessionRead r = readSession(tmp.path(), t0);
    REQUIRE(r.state == SessionState::Expired);
    REQUIRE(r.status.code == ErrorCode::BadFile);

    for (const char* name : {"session-bad-format.json", "session-bad-extra-key.json", "session-bad-time.json",
                             "session-bad-empty-tool.json", "session-bad-missing.json"}) {
        INFO(name);
        copyData(name, lock);
        r = readSession(tmp.path(), t0);
        REQUIRE(r.state == SessionState::Expired);
        REQUIRE(r.status.code == ErrorCode::BadFile);
        REQUIRE_FALSE(schemaProblemForText("session.schema.json", readTextFile(lock)).empty());
    }

    copyData("session-bad-version.json", lock);
    r = readSession(tmp.path(), t0);
    REQUIRE(r.state == SessionState::Expired);
    REQUIRE(r.status.code == ErrorCode::UnsupportedVersion);

    fs::remove(lock);
    fs::create_directory(lock);
    r = readSession(tmp.path(), t0);
    REQUIRE(r.state == SessionState::Expired);
    REQUIRE(r.status.code == ErrorCode::CannotRead);
}

TEST_CASE("the sample lock file reads and validates", "[core][session]") {
    TempDir tmp("session-sample");
    const fs::path lock = tmp.path() / pathFromUtf8(kSessionFileName);
    copyData("session-valid.json", lock);
    REQUIRE(schemaProblemForText("session.schema.json", readTextFile(lock)).empty());
    const SessionRead r = readSession(tmp.path(), *parseSessionTime("2026-11-21T10:04:00Z"));
    REQUIRE(r.state == SessionState::Active);
    REQUIRE(r.lock.pid == 4242);
    REQUIRE(r.lock.tool == "plynth");
    REQUIRE(formatSessionTime(r.lock.started) == "2026-11-21T10:00:00Z");
    REQUIRE(formatSessionTime(r.lock.lastActivity) == "2026-11-21T10:03:12Z");
}

TEST_CASE("written files validate against their schemas", "[core][session][schema]") {
    TempDir tmp("session-schema");
    requireOk(writeSession(tmp.path(), SessionLock{0, std::string(64, 'x'), t0, at(5)}));
    REQUIRE(schemaProblemForText("session.schema.json", readTextFile(tmp.path() / "session.lock")).empty());

    requireOk(writeAppState(tmp.path(), AppState{currentProcess(), true, at(7)}));
    REQUIRE(schemaProblemForText("app-state.schema.json", readTextFile(tmp.path() / "app-state.json")).empty());
    requireOk(writeAppState(tmp.path(), AppState{{5678, 0}, false, at(7)}));
    REQUIRE(schemaProblemForText("app-state.schema.json", readTextFile(tmp.path() / "app-state.json")).empty());

    const auto json = nlohmann::json::parse(readTextFile(tmp.path() / "app-state.json"));
    REQUIRE(json["pid"] == 5678);
    REQUIRE(json["modified"] == false);
    REQUIRE(json["updated"] == "2026-11-21T10:00:07Z");
}

TEST_CASE("writing into a missing folder fails and leaves no file", "[core][session]") {
    TempDir tmp("session-nowrite");
    const fs::path missing = tmp.path() / "nope";
    REQUIRE(writeSession(missing, SessionLock{1, "plynth", t0, t0}).code == ErrorCode::CannotWrite);
    REQUIRE(writeAppState(missing, AppState{currentProcess(), false, t0}).code == ErrorCode::CannotWrite);
}

TEST_CASE("writeSession replaces an existing lock and leaves no temporary file", "[core][session]") {
    TempDir tmp("session-replace");
    writeLock(tmp.path(), at(1));
    writeLock(tmp.path(), at(2));
    REQUIRE(readSession(tmp.path(), at(2)).lock.lastActivity == at(2));
    int files = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(tmp.path())) ++files;
    REQUIRE(files == 1);
}

TEST_CASE("removeSession is ok with or without a lock", "[core][session]") {
    TempDir tmp("session-remove");
    requireOk(removeSession(tmp.path()));
    writeLock(tmp.path(), t0);
    requireOk(removeSession(tmp.path()));
    REQUIRE_FALSE(fs::exists(tmp.path() / "session.lock"));
}

TEST_CASE("readAppState follows the process", "[core][session]") {
    TempDir tmp("appstate-read");
    AppStateRead r = readAppState(tmp.path());
    REQUIRE_FALSE(r.open);
    REQUIRE(r.status.ok());

    const AppState mine{currentProcess(), true, at(3)};
    requireOk(writeAppState(tmp.path(), mine));
    r = readAppState(tmp.path());
    REQUIRE(r.open);
    REQUIRE(r.state == mine);

    ProcessIdentity wrongStart = currentProcess();
    wrongStart.startKey += 1;
    requireOk(writeAppState(tmp.path(), AppState{wrongStart, false, at(3)}));
    r = readAppState(tmp.path());
    REQUIRE_FALSE(r.open);
    REQUIRE(r.status.ok());

    requireOk(writeAppState(tmp.path(), AppState{{2000000000, 5}, false, at(3)}));
    REQUIRE_FALSE(readAppState(tmp.path()).open);
}

TEST_CASE("readAppState reports a broken file and is not open", "[core][session]") {
    TempDir tmp("appstate-bad");
    const fs::path file = tmp.path() / "app-state.json";
    writeText(file, "{ nope");
    AppStateRead r = readAppState(tmp.path());
    REQUIRE_FALSE(r.open);
    REQUIRE(r.status.code == ErrorCode::BadFile);

    writeText(file, R"({"format":"plynth-app-state","version":1,"pid":0,"process_start":0,"modified":false,"updated":"2026-11-21T10:00:00Z"})");
    r = readAppState(tmp.path());
    REQUIRE_FALSE(r.open);
    REQUIRE(r.status.code == ErrorCode::BadFile);
}

TEST_CASE("removeAppState only removes the file of its owner", "[core][session]") {
    TempDir tmp("appstate-remove");
    const fs::path file = tmp.path() / "app-state.json";
    requireOk(removeAppState(tmp.path(), currentProcess()));

    requireOk(writeAppState(tmp.path(), AppState{{5678, 9}, false, t0}));
    requireOk(removeAppState(tmp.path(), currentProcess()));
    REQUIRE(fs::exists(file));

    requireOk(writeAppState(tmp.path(), AppState{currentProcess(), false, t0}));
    requireOk(removeAppState(tmp.path(), currentProcess()));
    REQUIRE_FALSE(fs::exists(file));

    writeText(file, "garbage");
    requireOk(removeAppState(tmp.path(), currentProcess()));
    REQUIRE(fs::exists(file));
}

TEST_CASE("beginSession writes a new lock", "[core][session]") {
    TempDir tmp("begin-new");
    const fs::path folder = makeProject(tmp);
    SessionLock lock;
    bool already = true;
    requireOk(beginSession(folder, "my tool", 4242, t0, &lock, &already));
    REQUIRE_FALSE(already);
    REQUIRE(lock == SessionLock{4242, "my tool", t0, t0});
    REQUIRE(readSession(folder, t0).lock == lock);
    REQUIRE(readSession(folder, t0).state == SessionState::Active);
}

TEST_CASE("beginSession continues an active session and keeps its start", "[core][session]") {
    TempDir tmp("begin-continue");
    const fs::path folder = makeProject(tmp);
    requireOk(beginSession(folder, "first", 1, t0));

    SessionLock lock;
    bool already = false;
    requireOk(beginSession(folder, "second", 2, at(100), &lock, &already));
    REQUIRE(already);
    REQUIRE(lock.started == t0);
    REQUIRE(lock.lastActivity == at(100));
    REQUIRE(lock.tool == "first");
    REQUIRE(readSession(folder, at(100)).lock == lock);
}

TEST_CASE("beginSession over an expired lock starts a new one", "[core][session]") {
    TempDir tmp("begin-expired");
    const fs::path folder = makeProject(tmp);
    requireOk(beginSession(folder, "first", 1, t0));

    SessionLock lock;
    bool already = true;
    requireOk(beginSession(folder, "second", 2, at(1000), &lock, &already));
    REQUIRE_FALSE(already);
    REQUIRE(lock == SessionLock{2, "second", at(1000), at(1000)});
}

TEST_CASE("beginSession over a broken lock starts a new one", "[core][session]") {
    TempDir tmp("begin-broken");
    const fs::path folder = makeProject(tmp);
    writeText(folder / "session.lock", "junk");
    bool already = true;
    requireOk(beginSession(folder, "tool", 7, t0, nullptr, &already));
    REQUIRE_FALSE(already);
    REQUIRE(readSession(folder, t0).state == SessionState::Active);
}

TEST_CASE("beginSession refuses a folder that is not a project", "[core][session]") {
    TempDir tmp("begin-notproject");
    REQUIRE(beginSession(tmp.path(), "plynth", 1, t0).code == ErrorCode::NotAProject);
    REQUIRE_FALSE(fs::exists(tmp.path() / "session.lock"));
}

TEST_CASE("beginSession refuses unsaved changes in the app", "[core][session]") {
    TempDir tmp("begin-unsaved");
    const fs::path folder = makeProject(tmp);
    pretendAppOpen(folder, true);
    const Status s = beginSession(folder, "plynth", 1, t0);
    REQUIRE(s.code == ErrorCode::UnsavedChanges);
    REQUIRE(s.message.find("unsaved changes") != std::string::npos);
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));

    pretendAppOpen(folder, false);
    requireOk(beginSession(folder, "plynth", 1, t0));
}

TEST_CASE("beginSession ignores the state file of a dead app", "[core][session]") {
    TempDir tmp("begin-deadapp");
    const fs::path folder = makeProject(tmp);
    requireOk(writeAppState(folder, AppState{{2000000000, 0}, true, t0}));
    requireOk(beginSession(folder, "plynth", 1, t0));
}

TEST_CASE("beginSession checks the tool name", "[core][session]") {
    TempDir tmp("begin-tool");
    const fs::path folder = makeProject(tmp);

    REQUIRE(beginSession(folder, std::string(65, 'a'), 1, t0).code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));
    REQUIRE(beginSession(folder, "bad\nname", 1, t0).code == ErrorCode::InvalidValue);

    SessionLock lock;
    requireOk(beginSession(folder, std::string(64, 'a'), 1, t0, &lock));
    REQUIRE(lock.tool.size() == 64);
    requireOk(endSession(folder, t0));

    std::string accented;
    for (int i = 0; i < 64; ++i) accented += "\xC3\xBC";
    requireOk(beginSession(folder, accented, 1, t0, &lock));
    REQUIRE(lock.tool == accented);
    REQUIRE(readSession(folder, t0).state == SessionState::Active);
    requireOk(endSession(folder, t0));

    requireOk(beginSession(folder, "", 1, t0, &lock));
    REQUIRE(lock.tool == kDefaultSessionTool);
}

TEST_CASE("endSession reports whether a session was active", "[core][session]") {
    TempDir tmp("end");
    const fs::path folder = makeProject(tmp);
    bool was = true;
    requireOk(endSession(folder, t0, &was));
    REQUIRE_FALSE(was);

    requireOk(beginSession(folder, "plynth", 1, t0));
    requireOk(endSession(folder, at(10), &was));
    REQUIRE(was);
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));

    requireOk(beginSession(folder, "plynth", 1, t0));
    requireOk(endSession(folder, at(1000), &was));
    REQUIRE_FALSE(was);
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));

    writeText(folder / "session.lock", "junk");
    requireOk(endSession(folder, t0, &was));
    REQUIRE_FALSE(was);
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));
}

TEST_CASE("allowCliCommand with an active session runs and refreshes the activity", "[core][session]") {
    TempDir tmp("allow-active");
    const fs::path folder = makeProject(tmp);
    requireOk(beginSession(folder, "plynth", 1, t0));
    pretendAppOpen(folder, false);

    requireOk(allowCliCommand(folder, at(100), false));
    REQUIRE(readSession(folder, at(100)).lock.lastActivity == at(100));
    REQUIRE(readSession(folder, at(100)).lock.started == t0);

    requireOk(allowCliCommand(folder, at(200), true));
    REQUIRE(readSession(folder, at(200)).lock.lastActivity == at(200));

    requireOk(allowCliCommand(folder, at(450), false));
    REQUIRE(readSession(folder, at(450)).state == SessionState::Active);
}

TEST_CASE("allowCliCommand without an app runs", "[core][session]") {
    TempDir tmp("allow-noapp");
    const fs::path folder = makeProject(tmp);
    requireOk(allowCliCommand(folder, t0, false));
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));

    requireOk(writeAppState(folder, AppState{{2000000000, 0}, true, t0}));
    requireOk(allowCliCommand(folder, t0, false));

    writeLock(folder, t0);
    requireOk(allowCliCommand(folder, at(1000), false));
}

TEST_CASE("allowCliCommand refuses when the app has the project open and there is no session", "[core][session]") {
    TempDir tmp("allow-locked");
    const fs::path folder = makeProject(tmp);
    for (bool modified : {false, true}) {
        pretendAppOpen(folder, modified);
        const Status s = allowCliCommand(folder, t0, false);
        REQUIRE(s.code == ErrorCode::Locked);
        REQUIRE(s.message.find("plynth session begin") != std::string::npos);
        REQUIRE(s.message.find("ended after 5 minutes") == std::string::npos);
        REQUIRE_FALSE(fs::exists(folder / "session.lock"));
    }
}

TEST_CASE("allowCliCommand says when the session expired", "[core][session]") {
    TempDir tmp("allow-expired");
    const fs::path folder = makeProject(tmp);
    requireOk(beginSession(folder, "plynth", 1, t0));
    pretendAppOpen(folder, false);

    const Status s = allowCliCommand(folder, at(301), false);
    REQUIRE(s.code == ErrorCode::Locked);
    REQUIRE(s.message.find("ended after 5 minutes") != std::string::npos);
    REQUIRE(readSession(folder, at(301)).lock.lastActivity == t0);

    writeText(folder / "session.lock", "junk");
    REQUIRE(allowCliCommand(folder, t0, false).code == ErrorCode::Locked);
}

TEST_CASE("allowCliCommand never refuses a read only command", "[core][session]") {
    TempDir tmp("allow-readonly");
    const fs::path folder = makeProject(tmp);
    pretendAppOpen(folder, true);
    requireOk(allowCliCommand(folder, t0, true));
    REQUIRE_FALSE(fs::exists(folder / "session.lock"));

    writeLock(folder, t0);
    requireOk(allowCliCommand(folder, at(1000), true));
    REQUIRE(readSession(folder, at(1000)).lock.lastActivity == t0);
}

TEST_CASE("the lock files are not part of a project save or load", "[core][session]") {
    TempDir tmp("session-not-project");
    const fs::path folder = makeProject(tmp);
    requireOk(beginSession(folder, "plynth", 1, t0));
    pretendAppOpen(folder, false);

    Project project = makeNewProject();
    requireOk(saveProject(project, folder, folder));
    REQUIRE(loadProject(folder).status.ok());

    Project copy = makeNewProject();
    const fs::path other = tmp.path() / "other";
    requireOk(saveProject(copy, other, folder));
    REQUIRE_FALSE(fs::exists(other / "session.lock"));
    REQUIRE_FALSE(fs::exists(other / "app-state.json"));
}
