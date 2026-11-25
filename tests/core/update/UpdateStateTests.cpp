#include "UpdateTestSupport.h"

#include "../SchemaSupport.h"

#include <plynth/core/UpdateNames.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

// A unique mutex name for one test.
std::wstring mutexName(const wchar_t* what)
{
    return std::wstring(L"PlynthTest") + what + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(GetTickCount64());
}

// Says whether a mutex of that name exists.
bool mutexExists(const std::wstring& name)
{
    const HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (!handle) return false;
    CloseHandle(handle);
    return true;
}

}

TEST_CASE("the update state round trips and matches its schema", "[update][state]") {
    plynth::test::TempDir tmp("update-state-roundtrip");
    const fs::path file = tmp.path() / "update-state.json";
    UpdateStateFile empty;
    plynth::test::requireOk(readUpdateState(file, empty));
    CHECK_FALSE(empty.highestSigned);
    CHECK_FALSE(empty.last);

    UpdateStateFile state;
    state.highestSigned = v("0.2.0");
    state.last = UpdateLast{"0.1.0", "0.2.0", "2026-11-22T10:00:00Z", tmp.path() / "updates" / "log.txt"};
    plynth::test::requireOk(writeUpdateState(file, state));
    UpdateStateFile read;
    plynth::test::requireOk(readUpdateState(file, read));
    CHECK(read.highestSigned == v("0.2.0"));
    REQUIRE(read.last);
    CHECK(read.last->from == "0.1.0");
    CHECK(read.last->to == "0.2.0");
    CHECK(read.last->started == "2026-11-22T10:00:00Z");
    CHECK(read.last->log == tmp.path() / "updates" / "log.txt");
    CHECK(plynth::test::schemaProblemForText("update-state.schema.json", readBytes(file)).empty());

    plynth::test::requireOk(writeUpdateState(file, UpdateStateFile{}));
    CHECK(plynth::test::schemaProblemForText("update-state.schema.json", readBytes(file)).empty());
}

TEST_CASE("a bad update state file is refused with a pointer", "[update][state]") {
    plynth::test::TempDir tmp("update-state-bad");
    const fs::path file = tmp.path() / "update-state.json";
    const std::vector<std::pair<std::string, std::string>> cases{
        {"not json", ""},
        {R"({"format": "other", "version": 1})", "/format"},
        {R"({"format": "plynth-update-state"})", "/version"},
        {R"({"format": "plynth-update-state", "version": 1, "highest_signed": "0.02.0"})", "/highest_signed"},
        {R"({"format": "plynth-update-state", "version": 1, "colour": 1})", "/colour"},
        {R"({"format": "plynth-update-state", "version": 1, "last": {"from": "0.1.0"}})", "/last/to"},
        {R"({"format": "plynth-update-state", "version": 1, "last": {"from": "a", "to": "b", "started": "c", "log": "d", "x": 1}})", "/last/x"},
    };
    for (const auto& [text, where] : cases) {
        INFO(text);
        writeBytes(file, text);
        UpdateStateFile state;
        const Status status = readUpdateState(file, state);
        CHECK(status.code == ErrorCode::BadFile);
        CHECK(status.where == where);
        if (!where.empty() && text != "not json") {
            CHECK_FALSE(plynth::test::schemaProblemForText("update-state.schema.json", text).empty());
        }
    }
    writeBytes(file, R"({"format": "plynth-update-state", "version": 2})");
    UpdateStateFile state;
    CHECK(readUpdateState(file, state).code == ErrorCode::UnsupportedVersion);
    // A state that cannot be read is never overwritten by remembering a version.
    CHECK(rememberSigned(file, v("0.9.0")).code == ErrorCode::UnsupportedVersion);
    CHECK(readBytes(file) == R"({"format": "plynth-update-state", "version": 2})");
}

TEST_CASE("the highest signed version only goes up", "[update][state]") {
    plynth::test::TempDir tmp("update-state-highest");
    const fs::path file = tmp.path() / "update-state.json";
    plynth::test::requireOk(rememberSigned(file, v("0.3.0")));
    plynth::test::requireOk(rememberSigned(file, v("0.2.0")));
    UpdateStateFile state;
    plynth::test::requireOk(readUpdateState(file, state));
    CHECK(state.highestSigned == v("0.3.0"));
    plynth::test::requireOk(rememberSigned(file, v("0.10.0")));
    plynth::test::requireOk(readUpdateState(file, state));
    CHECK(state.highestSigned == v("0.10.0"));
}

TEST_CASE("old downloads are removed only inside download folders of the updates folder", "[update][state]") {
    plynth::test::TempDir tmp("update-state-cleanup");
    const fs::path root = tmp.path() / "updates";
    const fs::path oldOne = root / "0123456789abcdef0123456789abcdef";
    const fs::path kept = root / "fedcba9876543210fedcba9876543210";
    const fs::path otherName = root / "my-files";
    const fs::path upper = root / "0123456789ABCDEF0123456789ABCDEF";
    writeBytes(oldOne / "PlynthSetup.exe", "old");
    writeBytes(oldOne / "install.log", "log");
    writeBytes(oldOne / "inner" / "keep.txt", "inner folder");
    writeBytes(kept / "install.log", "the last log");
    writeBytes(otherName / "file.txt", "not ours");
    writeBytes(upper / "file.txt", "not ours either");
    const fs::path outside = tmp.path() / "outside";
    writeBytes(outside / "precious.txt", "must stay");
    const fs::path linked = root / "00000000000000000000000000000000";
    const std::wstring command = L"mklink /J \"" + linked.wstring() + L"\" \"" + outside.wstring() + L"\" >nul 2>nul";
    REQUIRE(_wsystem(command.c_str()) == 0);

    removeOldDownloads(root, kept);
    CHECK_FALSE(fs::exists(oldOne / "PlynthSetup.exe"));
    CHECK_FALSE(fs::exists(oldOne / "install.log"));
    CHECK(fs::exists(oldOne / "inner" / "keep.txt"));
    CHECK(fs::exists(kept / "install.log"));
    CHECK(fs::exists(otherName / "file.txt"));
    CHECK(fs::exists(upper / "file.txt"));
    CHECK(fs::exists(outside / "precious.txt"));
    REQUIRE(RemoveDirectoryW(linked.c_str()));

    fs::remove_all(oldOne / "inner");
    removeOldDownloads(root, {});
    CHECK_FALSE(fs::exists(oldOne));
    CHECK_FALSE(fs::exists(kept));
    CHECK(fs::exists(otherName / "file.txt"));
    removeOldDownloads(tmp.path() / "missing", {});
}

TEST_CASE("download folder names are 32 lower case hex digits", "[update][state]") {
    CHECK(isDownloadFolderName(L"0123456789abcdef0123456789abcdef"));
    CHECK_FALSE(isDownloadFolderName(L"0123456789abcdef0123456789abcde"));
    CHECK_FALSE(isDownloadFolderName(L"0123456789abcdef0123456789abcdeg"));
    CHECK_FALSE(isDownloadFolderName(L"0123456789ABCDEF0123456789ABCDEF"));
    CHECK_FALSE(isDownloadFolderName(L""));
}

TEST_CASE("a packaged program marks itself running and stops while setup runs", "[update][state]") {
    plynth::test::TempDir tmp("update-state-packaged");
    const std::wstring running = mutexName(L"Running");
    const std::wstring setup = mutexName(L"Setup");

    const fs::path build = tmp.path() / "build";
    fs::create_directories(build);
    plynth::test::requireOk(beginPackagedRunIn(build, running.c_str(), setup.c_str()));
    CHECK_FALSE(mutexExists(running));

    const fs::path packaged = tmp.path() / "packaged";
    writeBytes(packaged / "package.json", "{}");
    plynth::test::requireOk(beginPackagedRunIn(packaged, running.c_str(), setup.c_str()));
    CHECK(mutexExists(running));

    const HANDLE installer = CreateMutexW(nullptr, FALSE, setup.c_str());
    REQUIRE(installer);
    const Status busy = beginPackagedRunIn(packaged, running.c_str(), setup.c_str());
    CHECK(busy.code == ErrorCode::UpdateInProgress);
    CHECK(beginPackagedRunIn(build, running.c_str(), setup.c_str()).ok());
    CloseHandle(installer);
}

TEST_CASE("the shipped names never change", "[update][state]") {
    CHECK(std::wstring(kRunningMutex) == L"PlynthRunning");
    CHECK(std::wstring(kSetupMutex) == L"PlynthSetup");
    CHECK(std::wstring(kPackageFile) == L"package.json");
    CHECK(std::string(kInstallerName) == "PlynthSetup.exe");
    CHECK(std::string(kSignatureName) == "PlynthSetup.exe.minisig");
    CHECK(std::string(kReleasesPage) == "https://github.com/ManideepKadambari-Dev/plynth/releases");
}

TEST_CASE("the release addresses are built from the constants and the version", "[update][state]") {
    CHECK(releasesListUrl() == "https://api.github.com/repos/ManideepKadambari-Dev/plynth/releases?per_page=30");
    CHECK(releaseFileUrl(v("0.2.0"), kSignatureName) ==
          "https://github.com/ManideepKadambari-Dev/plynth/releases/download/v0.2.0/PlynthSetup.exe.minisig");
    CHECK(releasePageUrl(v("0.2.0")) == "https://github.com/ManideepKadambari-Dev/plynth/releases/tag/v0.2.0");
}

TEST_CASE("a packaged start of a copy that is not installed removes nothing", "[update][state]") {
    plynth::test::TempDir tmp("update-state-start");
    ReleaseServer server;
    UpdateEnvironment notInstalled = testEnvironment(server, {}, tmp.path());
    const Installation copy{InstallKind::NotInstalled, tmp.path(), "0.1.0"};
    notInstalled.installation = [copy] { return copy; };
    const ScopedUpdateEnvironment env(notInstalled);
    const fs::path old = tmp.path() / "updates" / "0123456789abcdef0123456789abcdef" / "PlynthSetup.exe";
    writeBytes(old, "old");
    plynth::test::requireOk(beginPackagedRun());
    CHECK(fs::exists(old));
}
