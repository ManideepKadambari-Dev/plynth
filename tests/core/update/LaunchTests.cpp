#include "UpdateTestSupport.h"

#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

// Downloads the fake installer as version 0.2.0 through the server of the test.
VerifiedInstaller downloadFake(ReleaseServer& server, const TestKey& key)
{
    server.setList(releaseList({{"v0.2.0"}}));
    server.publish(key, "0.2.0", fakeInstaller());
    const UpdateCheck check = checkForUpdate();
    plynth::test::requireOk(check.status);
    REQUIRE(check.offer);
    UpdateDownload download = downloadUpdate(*check.offer);
    plynth::test::requireOk(download.status);
    REQUIRE(download.installer);
    return std::move(*download.installer);
}

// UTF-16 to UTF-8.
std::string narrow(const std::wstring& text)
{
    return pathToUtf8(fs::path(text));
}

// Waits until a file exists, at most the given time. False when it never came.
bool waitForFile(const fs::path& file, std::chrono::milliseconds limit)
{
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        std::error_code ec;
        if (fs::exists(file, ec)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// Waits for a process to end, at most the given time.
bool waitForProcess(unsigned long pid, std::chrono::milliseconds limit)
{
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return true;
    const bool ended = WaitForSingleObject(process, static_cast<DWORD>(limit.count())) == WAIT_OBJECT_0;
    CloseHandle(process);
    return ended;
}

// Says whether a file can be renamed and back right now.
bool canRename(const fs::path& file)
{
    const fs::path moved = file.wstring() + L".moved";
    if (!MoveFileExW(file.c_str(), moved.c_str(), 0)) return false;
    MoveFileExW(moved.c_str(), file.c_str(), 0);
    return true;
}

// Runs a program with nothing inherited and returns its exit code, or -1 when it could not run.
int runAndWait(const std::wstring& commandLine)
{
    std::wstring line = commandLine;
    STARTUPINFOW info{};
    info.cb = sizeof info;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &info, &process)) {
        return -1;
    }
    WaitForSingleObject(process.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

// The handle value as decimal text, for the fake installer.
std::string handleText(void* handle)
{
    return std::to_string(reinterpret_cast<std::uintptr_t>(handle));
}

}

TEST_CASE("the installer is started with the update mode arguments", "[update][launch]") {
    const fs::path program = fs::path(L"C:\\Users\\Ünï Côdé\\AppData\\Local\\Plynth\\updates\\ab\\PlynthSetup.exe");
    const fs::path log = program.parent_path() / "install.log";
    CHECK(installerCommandLine(program, log, false) ==
          L"\"C:\\Users\\Ünï Côdé\\AppData\\Local\\Plynth\\updates\\ab\\PlynthSetup.exe\" /SILENT /SUPPRESSMSGBOXES "
          L"/NORESTART /CURRENTUSER /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /PLYNTHUPDATE=1 "
          L"/LOG=\"C:\\Users\\Ünï Côdé\\AppData\\Local\\Plynth\\updates\\ab\\install.log\"");
    CHECK(installerCommandLine(program, log, true).find(L"/PLYNTHUPDATE=1 /PLYNTHREOPEN=1 /LOG=") != std::wstring::npos);
}

TEST_CASE("a launch records the start, passes the locked file and can be undone", "[update][launch]") {
    plynth::test::TempDir tmp("update-launch-record");
    ReleaseServer server;
    const TestKey key = makeKey();
    UpdateEnvironment base = testEnvironment(server, {key}, tmp.path());
    std::optional<InstallerStart> started;
    Status answer;
    base.startInstaller = [&](const InstallerStart& start) {
        started = start;
        return answer;
    };
    const fs::path stateFile = tmp.path() / "update-state.json";

    SECTION("started") {
        const ScopedUpdateEnvironment env(base);
        VerifiedInstaller installer = downloadFake(server, key);
        const fs::path file = installer.file();
        void* handle = InstallerAccess::handle(installer);
        plynth::test::requireOk(launchUpdate(std::move(installer), {}, true));
        REQUIRE(started);
        CHECK(started->program == file);
        CHECK(started->workingFolder == file.parent_path());
        CHECK(started->commandLine == installerCommandLine(file, file.parent_path() / "install.log", true));
        CHECK(started->inheritedHandle == handle);
        UpdateStateFile state;
        plynth::test::requireOk(readUpdateState(stateFile, state));
        REQUIRE(state.last);
        CHECK(state.last->from == "0.1.0");
        CHECK(state.last->to == "0.2.0");
        CHECK(state.last->log == file.parent_path() / "install.log");
        CHECK(parseSessionTime(state.last->started));
        CHECK(state.highestSigned == v("0.2.0"));

        const UpdateStatus before = updateStatus();
        CHECK(before.last == LastUpdateResult::NotFinished);
        CHECK(before.from == "0.1.0");
        CHECK(before.to == "0.2.0");
        CHECK(before.keyIds == std::vector<std::string>{keyIdText(key.keyId)});
        UpdateEnvironment after = testEnvironment(server, {key}, tmp.path(), "0.2.0");
        const ScopedUpdateEnvironment updated(after);
        CHECK(updateStatus().last == LastUpdateResult::Succeeded);
        CHECK(lastUpdateResultName(LastUpdateResult::NotFinished) == "not_finished");
    }
    SECTION("blocked") {
        base.blockers = [](const Installation&, const fs::path&) {
            return std::vector<UpdateBlocker>{{"edit_session", "An edit session is active in the test project."}};
        };
        const ScopedUpdateEnvironment env(base);
        VerifiedInstaller installer = downloadFake(server, key);
        const Status status = launchUpdate(std::move(installer), {}, false);
        CHECK(status.code == ErrorCode::UpdateBlocked);
        CHECK(status.message == "An edit session is active in the test project.");
        CHECK_FALSE(started);
        UpdateStateFile state;
        plynth::test::requireOk(readUpdateState(stateFile, state));
        CHECK_FALSE(state.last);
    }
    SECTION("the start fails") {
        answer = fail(ErrorCode::CannotRead, "The installer could not be started.");
        const ScopedUpdateEnvironment env(base);
        VerifiedInstaller installer = downloadFake(server, key);
        CHECK(launchUpdate(std::move(installer), {}, false).code == ErrorCode::CannotRead);
        UpdateStateFile state;
        plynth::test::requireOk(readUpdateState(stateFile, state));
        CHECK_FALSE(state.last);
        CHECK(updateStatus().last == LastUpdateResult::None);
    }
    SECTION("not installed") {
        VerifiedInstaller installer = [&] {
            const ScopedUpdateEnvironment env(base);
            return downloadFake(server, key);
        }();
        UpdateEnvironment notInstalled = base;
        const Installation copy{InstallKind::NotInstalled, tmp.path(), "0.1.0"};
        notInstalled.installation = [copy] { return copy; };
        const ScopedUpdateEnvironment env(notInstalled);
        CHECK(launchUpdate(std::move(installer), {}, false).code == ErrorCode::UpdateNotInstalled);
        CHECK_FALSE(started);
    }
}

TEST_CASE("the real start passes only the installer's handle and the lock lasts until it ends", "[update][launch]") {
    const JobPlacement job = currentJobPlacement();
    if (job.inJob && job.killOnClose && !job.breakawayAllowed) SKIP("This test program runs in a job that ends its programs.");
    plynth::test::TempDir tmp("update-launch-real");
    ReleaseServer server;
    const TestKey key = makeKey();
    UpdateEnvironment base = testEnvironment(server, {key}, tmp.path());
    base.downloadRoot = tmp.path() / "Down loads ä";
    const ScopedUpdateEnvironment env(base);
    VerifiedInstaller installer = downloadFake(server, key);
    const fs::path file = installer.file();
    const fs::path folder = file.parent_path();

    const fs::path probeFile = tmp.path() / "probe.txt";
    writeBytes(probeFile, "probe");
    SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE};
    const HANDLE probe = CreateFileW(probeFile.c_str(), GENERIC_READ, FILE_SHARE_READ, &inheritable, OPEN_EXISTING, 0, nullptr);
    REQUIRE(probe != INVALID_HANDLE_VALUE);
    const plynth::test::ScopedEnv probeHandle("FAKE_PROBE_HANDLE", handleText(probe));
    const plynth::test::ScopedEnv probePath("FAKE_PROBE_FILE", pathToUtf8(probeFile));
    const plynth::test::ScopedEnv lockHandle("FAKE_LOCK_HANDLE", handleText(InstallerAccess::handle(installer)));
    const plynth::test::ScopedEnv wait("FAKE_INSTALLER_WAIT_MS", "3000");

    plynth::test::requireOk(launchUpdate(std::move(installer), {}, false));
    CloseHandle(probe);
    REQUIRE(waitForFile(folder / "fake-start.json", std::chrono::seconds(20)));
    const nlohmann::json start = nlohmann::json::parse(readBytes(folder / "fake-start.json"));
    CHECK(start.at("command_line") == narrow(installerCommandLine(file, folder / "install.log", false)));
    CHECK(sameFolder(pathFromUtf8(start.at("working_folder").get<std::string>()), folder));
    CHECK(start.at("stdin_empty") == true);
    CHECK(start.at("stdout_empty") == true);
    CHECK(start.at("stderr_empty") == true);
    CHECK(start.at("probe_inherited") == false);
    CHECK(start.at("lock_inherited") == true);

    // Plynth's own handle is closed now; the installer's inherited one still holds the file.
    CHECK_FALSE(fs::exists(folder / "fake-done.txt"));
    CHECK_FALSE(canRename(file));
    REQUIRE(waitForProcess(start.at("pid").get<unsigned long>(), std::chrono::seconds(30)));
    CHECK(fs::exists(folder / "fake-done.txt"));
    CHECK(readBytes(folder / "install.log") == "finished\n");
    CHECK(canRename(file));
}

TEST_CASE("an installer started from a job that ends its programs survives only when the job lets it leave", "[update][launch]") {
    const JobPlacement outer = currentJobPlacement();
    if (outer.inJob && !outer.breakawayAllowed) SKIP("This test program runs in a job that lets no program leave.");
    plynth::test::TempDir tmp("update-launch-job");
    const plynth::test::ScopedEnv wait("FAKE_INSTALLER_WAIT_MS", "2000");
    const std::string bytes = fakeInstaller();

    for (const bool leave : {true, false}) {
        INFO("the job lets programs leave: " << leave);
        const fs::path folder = tmp.path() / (leave ? "leave" : "stay");
        const fs::path installer = folder / "PlynthSetup.exe";
        writeBytes(installer, bytes);
        const std::wstring line = L"\"" + fs::path(PLYNTH_UPDATE_LAUNCHER).wstring() + L"\" \"" + installer.wstring() +
                                  L"\" " + (leave ? L"1" : L"0") + L" \"" + (folder / "state.json").wstring() + L"\"";
        const int code = runAndWait(line);
        if (leave) {
            CHECK(code == 0);
            REQUIRE(waitForFile(folder / "fake-start.json", std::chrono::seconds(20)));
            CHECK(waitForFile(folder / "fake-done.txt", std::chrono::seconds(20)));
            const nlohmann::json start = nlohmann::json::parse(readBytes(folder / "fake-start.json"));
            CHECK(waitForProcess(start.at("pid").get<unsigned long>(), std::chrono::seconds(20)));
        } else {
            CHECK(code == 3);
            CHECK_FALSE(waitForFile(folder / "fake-start.json", std::chrono::seconds(3)));
        }
    }
}
