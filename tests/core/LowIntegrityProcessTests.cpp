#include "FetchTestSupport.h"

#include <plynth/core/Process.h>

#include <catch2/catch_test_macros.hpp>

#include <sddl.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Runs the probe program and returns its lines.
std::vector<std::string> probe(std::vector<std::string> arguments, bool low, bool endChildren = false,
                               std::vector<std::string> removed = {})
{
    ProcessSpec spec;
    spec.program = PLYNTH_INTEGRITY_PROBE;
    spec.arguments = std::move(arguments);
    spec.lowIntegrity = low;
    spec.endChildren = endChildren;
    spec.removeEnvironment = std::move(removed);
    spec.timeout = std::chrono::seconds(60);
    std::vector<std::string> lines;
    const ProcessResult result = runProcess(spec, [&](std::string_view line) { lines.emplace_back(line); });
    INFO(result.status.message);
    REQUIRE(result.status.ok());
    return lines;
}

// The line that starts with a word, or empty.
std::string lineOf(const std::vector<std::string>& lines, const std::string& start)
{
    for (const std::string& line : lines) {
        if (line.rfind(start, 0) == 0) return line;
    }
    return {};
}

// The integrity level of another process as its relative id, or 0.
unsigned long levelOf(unsigned long pid)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return 0;
    HANDLE token = nullptr;
    unsigned long rid = 0;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
        std::vector<char> buffer(size);
        if (GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) {
            const auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
            rid = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
        }
        CloseHandle(token);
    }
    CloseHandle(process);
    return rid;
}

}

TEST_CASE("a low integrity program writes only to a folder labelled for it", "[core][process][low_integrity]")
{
    plynth::test::IsolatedUser user("li");
    const fs::path labelled = user.tmp.path() / "low";
    plynth::test::requireOk(makeLowIntegrityFolder(labelled));
    const fs::path normal = user.tmp.path() / "profile" / "probe.txt";
    const std::string key = "Software\\PlynthLowProbe" + std::to_string(GetCurrentProcessId());
    const auto lines = probe({"--level", "--write", pathToUtf8(labelled / "probe.txt"), "--write", pathToUtf8(normal), "--reg", key}, true);
    RegDeleteKeyW(HKEY_CURRENT_USER, plynth::test::widenUtf8(key).c_str());
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "level:1000");
    CHECK(lines[1] == "write:ok");
    CHECK(lines[2] == "write:denied");
    CHECK(lines[3] == "reg:denied");
    CHECK_FALSE(fs::exists(normal));
    CHECK(probe({"--level"}, false).at(0) != "level:1000");
}

TEST_CASE("a program started by a low integrity program is low too, and endChildren ends it", "[core][process][low_integrity]")
{
    const auto ended = probe({"--spawn", "30"}, true, true);
    const std::string child = lineOf(ended, "child:");
    REQUIRE_FALSE(child.empty());
    const unsigned long pid = std::stoul(child.substr(6));
    CHECK_FALSE(plynth::test::processRunning(pid));
    plynth::test::endProcess(pid);

    // Without endChildren runProcess does not wait for the child, and the child still ends soon after. The
    // child is held for five seconds so that waiting would show.
    ProcessSpec spec;
    spec.program = PLYNTH_INTEGRITY_PROBE;
    spec.arguments = {"--spawn", "30", "--level", "--sleep", "1"};
    spec.lowIntegrity = true;
    spec.timeout = std::chrono::seconds(60);
    std::unique_ptr<plynth::test::HoldExit> hold;
    unsigned long second = 0;
    unsigned long secondLevel = 0;
    const auto began = std::chrono::steady_clock::now();
    const ProcessResult result = runProcess(spec, [&](std::string_view line) {
        if (line.rfind("child:", 0) == 0 && !hold) {
            second = std::stoul(std::string(line.substr(6)));
            secondLevel = levelOf(second);
            hold = std::make_unique<plynth::test::HoldExit>(second, std::chrono::seconds(5));
        }
    });
    const auto took = std::chrono::steady_clock::now() - began;
    plynth::test::requireOk(result.status);
    REQUIRE(hold);
    REQUIRE(hold->attached());
    CHECK(secondLevel == 0x1000);
    CHECK(took < std::chrono::seconds(4));
    hold.reset();
    bool gone = false;
    for (int i = 0; i < 100 && !gone; ++i) {
        gone = !plynth::test::processRunning(second);
        if (!gone) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(gone);
    plynth::test::endProcess(second);
}

TEST_CASE("the test's hold keeps an ended process from going away until it lets go", "[core][process][test_tool]")
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::wstring line = plynth::test::widenUtf8(std::string("\"") + PLYNTH_INTEGRITY_PROBE + "\" --sleep 30");
    REQUIRE(CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
    CloseHandle(info.hThread);
    {
        plynth::test::HoldExit hold(info.dwProcessId, std::chrono::seconds(3));
        REQUIRE(hold.attached());
        TerminateProcess(info.hProcess, 1);
        CHECK(WaitForSingleObject(info.hProcess, 1000) == WAIT_TIMEOUT);
    }
    CHECK(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0);
    CloseHandle(info.hProcess);
}

TEST_CASE("with endChildren a child that cannot be ended for a minute gives CannotRead", "[core][process][end_children]")
{
    ProcessSpec spec;
    spec.program = PLYNTH_INTEGRITY_PROBE;
    spec.arguments = {"--spawn", "120", "--sleep", "1"};
    spec.endChildren = true;
    spec.timeout = std::chrono::seconds(300);
    std::unique_ptr<plynth::test::HoldExit> hold;
    unsigned long child = 0;
    const auto began = std::chrono::steady_clock::now();
    const ProcessResult result = runProcess(spec, [&](std::string_view line) {
        if (line.rfind("child:", 0) == 0 && !hold) {
            child = std::stoul(std::string(line.substr(6)));
            hold = std::make_unique<plynth::test::HoldExit>(child, std::chrono::seconds(75));
        }
    });
    const auto took = std::chrono::steady_clock::now() - began;
    REQUIRE(hold);
    REQUIRE(hold->attached());
    CHECK(result.status.code == ErrorCode::CannotRead);
    CHECK(result.status.message == "Programs that the program started could not all be ended.");
    CHECK(took >= std::chrono::seconds(55));
    CHECK(took < std::chrono::seconds(75));
    hold.reset();
    plynth::test::endProcess(child);
}

TEST_CASE("removeEnvironment takes variables out by name, ignoring case", "[core][process]")
{
    const plynth::test::ScopedEnv a("PlynthRemoveMe", "secret");
    const plynth::test::ScopedEnv b("PLYNTH_KEEP_ME", "kept");
    const auto lines = probe({"--env", "PLYNTHREMOVEME", "--env", "PLYNTH_KEEP_ME"}, false, false, {"plynthremoveme"});
    CHECK(lines.at(0) == "env:PLYNTHREMOVEME=<unset>");
    CHECK(lines.at(1) == "env:PLYNTH_KEEP_ME=kept");
}

namespace {

const char* const kNotSetUp = "Plynth's fetch folder is not set up as expected. Delete it and try again.";

// Gives a folder an explicit medium integrity label, which is not the low one a fetch folder needs.
bool labelMedium(const fs::path& folder)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;OICI;NW;;;ME)", SDDL_REVISION_1, &descriptor, nullptr)) {
        return false;
    }
    const BOOL ok = SetFileSecurityW(folder.c_str(), LABEL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    return ok != FALSE;
}

// Whether a low integrity program can write a file in a folder.
bool lowCanWrite(const fs::path& folder)
{
    const auto lines = probe({"--write", pathToUtf8(folder / "probe.txt")}, true);
    return lines.at(0) == "write:ok";
}

}

TEST_CASE("checkLowIntegrityFolder accepts a kept folder whose client folder low programs wrote", "[core][process][check_folder]")
{
    plynth::test::TempDir tmp("ck");
    const fs::path folder = tmp.path() / "fetch";
    plynth::test::requireOk(makeLowIntegrityFolder(folder));
    fs::create_directories(folder / "client" / "bin");
    REQUIRE(lowCanWrite(folder / "client" / "bin"));
    plynth::test::requireOk(checkLowIntegrityFolder(folder));
    CHECK(lowCanWrite(folder / "client"));
}

TEST_CASE("checkLowIntegrityFolder refuses a folder with another label and changes nothing", "[core][process][check_folder]")
{
    plynth::test::TempDir tmp("cm");
    const fs::path folder = tmp.path() / "fetch";
    fs::create_directories(folder / "client");
    REQUIRE(labelMedium(folder));
    const Status status = checkLowIntegrityFolder(folder);
    CHECK(status.code == ErrorCode::CannotWrite);
    CHECK(status.message == kNotSetUp);
    CHECK_FALSE(lowCanWrite(folder));
}

TEST_CASE("checkLowIntegrityFolder refuses a junction in the folder's place", "[core][process][check_folder]")
{
    plynth::test::TempDir tmp("cj");
    const fs::path target = tmp.path() / "target";
    plynth::test::requireOk(makeLowIntegrityFolder(target));
    const fs::path folder = tmp.path() / "fetch";
    REQUIRE(plynth::test::makeJunction(folder, target));
    const Status status = checkLowIntegrityFolder(folder);
    CHECK(status.code == ErrorCode::CannotWrite);
    CHECK(status.message == kNotSetUp);
    CHECK(status.message.find(pathToUtf8(folder)) == std::string::npos);
    RemoveDirectoryW(folder.c_str());
}

TEST_CASE("checkLowIntegrityFolder refuses an older empty folder without a label and does not label it", "[core][process][check_folder]")
{
    plynth::test::TempDir tmp("ce");
    const fs::path folder = tmp.path() / "fetch";
    fs::create_directories(folder);
    const Status status = checkLowIntegrityFolder(folder);
    CHECK(status.code == ErrorCode::CannotWrite);
    CHECK(status.message == kNotSetUp);
    CHECK_FALSE(lowCanWrite(folder));
    CHECK(checkLowIntegrityFolder(tmp.path() / "missing").code == ErrorCode::NotFound);
}

// S-R 7, real Blender, hidden. Run alone on the owner's PC with PLYNTH_TEST_BLENDER naming blender.exe:
//   plynth_core_tests "[s_r_7]"
TEST_CASE("Blender in background works at low integrity and writes only its labelled folder", "[.blender][s_r_7]")
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, "PLYNTH_TEST_BLENDER");
    const std::string blender = value ? value : "";
    free(value);
    if (blender.empty()) SKIP("set PLYNTH_TEST_BLENDER to blender.exe");

    plynth::test::IsolatedUser user("sr");
    const fs::path low = user.tmp.path() / "low";
    plynth::test::requireOk(makeLowIntegrityFolder(low));
    const fs::path outside = user.tmp.path() / "profile" / "outside.txt";
    const fs::path script = fs::path(PLYNTH_SOURCE_DIR) / "blender" / "tests" / "scratch" / "sr7_low_integrity.py";

    ProcessSpec spec;
    spec.program = pathFromUtf8(blender);
    spec.arguments = {"-b", "-t", "6", "--offline-mode", "--factory-startup", "-noaudio", "--python-exit-code", "3",
                      "--python", pathToUtf8(script), "--", pathToUtf8(low), pathToUtf8(outside), PLYNTH_FAKE_STORE_HELPER};
    for (const char* name : {"BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS",
                             "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES"}) {
        spec.environment.emplace_back(name, pathToUtf8(low / name));
    }
    spec.environment.emplace_back("TEMP", pathToUtf8(low / "tmp"));
    spec.environment.emplace_back("TMP", pathToUtf8(low / "tmp"));
    spec.environment.emplace_back("HOME", pathToUtf8(low / "home"));
    spec.lowIntegrity = true;
    spec.endChildren = true;
    spec.timeout = std::chrono::seconds(120);
    std::vector<std::string> lines;
    const ProcessResult result = runProcess(spec, [&](std::string_view line) { lines.emplace_back(line); });
    std::string all;
    for (const std::string& line : lines) all += line + "\n";
    INFO(all);
    REQUIRE(result.status.ok());
    CHECK(result.exitCode == 0);
    CHECK(lineOf(lines, "SR7 python:") == "SR7 python: ok");
    CHECK(lineOf(lines, "SR7 inside:") == "SR7 inside: ok");
    CHECK(lineOf(lines, "SR7 outside:") == "SR7 outside: denied");
    CHECK(lineOf(lines, "SR7 socket:") == "SR7 socket: ok");
    CHECK(lineOf(lines, "SR7 helper:") == "SR7 helper: ok");
    CHECK(lineOf(lines, "SR7 tray:") == "SR7 tray: ok");
    CHECK_FALSE(fs::exists(outside));
}
