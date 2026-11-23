#include "FetchTestSupport.h"

#include <plynth/core/Process.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
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
    const std::string key = "Software\PlynthLowProbe" + std::to_string(GetCurrentProcessId());
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

    const auto kept = probe({"--spawn", "30"}, true, false);
    const std::string other = lineOf(kept, "child:");
    REQUIRE_FALSE(other.empty());
    const unsigned long second = std::stoul(other.substr(6));
    CHECK(levelOf(second) == 0x1000);
    plynth::test::endProcess(second);
}

TEST_CASE("removeEnvironment takes variables out by name, ignoring case", "[core][process]")
{
    const plynth::test::ScopedEnv a("PlynthRemoveMe", "secret");
    const plynth::test::ScopedEnv b("PLYNTH_KEEP_ME", "kept");
    const auto lines = probe({"--env", "PLYNTHREMOVEME", "--env", "PLYNTH_KEEP_ME"}, false, false, {"plynthremoveme"});
    CHECK(lines.at(0) == "env:PLYNTHREMOVEME=<unset>");
    CHECK(lines.at(1) == "env:PLYNTH_KEEP_ME=kept");
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
