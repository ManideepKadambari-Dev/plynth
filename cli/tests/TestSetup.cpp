#include <plynth/core/Blender.h>
#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "FakeBlender.h"
#include "SettingsGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <crtdbg.h>
#include <process.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>

namespace {

// Turns off every dialog a failing test program could show, so a failure prints and exits.
struct NoDialogs {
    NoDialogs() {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
        for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
#endif
    }
} noDialogs;

// Ends the test program before any test runs, when its scratch folders could not be set up.
[[noreturn]] void stopUnsafe(const wchar_t* what) {
    std::fwprintf(stderr, L"The tests stop here, %ls could not be set to a scratch folder.\n", what);
    std::fflush(stderr);
    std::_Exit(1);
}

// Sets one variable for this program and the programs it starts. It goes into both the Win32 and the C runtime
// environment, since code and tests read one or the other.
void setVariable(const wchar_t* name, const std::filesystem::path& value) {
    if (_wputenv_s(name, value.c_str()) != 0 || !SetEnvironmentVariableW(name, value.c_str())) {
        stopUnsafe(name);
    }
}

// Makes a folder and points a variable at it. Blender ignores a user variable whose folder is missing.
void setFolder(const wchar_t* name, const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        stopUnsafe(name);
    }
    setVariable(name, folder);
}

// Makes a new empty folder for this run, so nothing left over from an earlier run is used.
std::filesystem::path makeScratchRoot() {
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec) {
        stopUnsafe(L"TEMP");
    }
    const std::wstring name = L"plynth-" + std::to_wstring(_getpid()) + L"-";
    for (int number = 0; number < 1000; ++number) {
        const std::filesystem::path root = temp / (name + std::to_wstring(number));
        if (std::filesystem::create_directory(root, ec)) {
            return root;
        }
    }
    stopUnsafe(L"TEMP");
}

// Points everything that names a user folder into a scratch root before the first test, and removes it at the end.
struct ScratchUser {
    ScratchUser() : root(makeScratchRoot()) {
        // A short name, because tests build long paths below the temp folder.
        setFolder(L"TEMP", root / "t");
        setFolder(L"TMP", root / "t");
        setFolder(L"APPDATA", root / "appdata");
        setFolder(L"LOCALAPPDATA", root / "local");
        setFolder(L"USERPROFILE", root / "profile");
        setFolder(L"HOME", root / "home");
        setFolder(L"XDG_DATA_HOME", root / "xdg");
        setFolder(L"PLYNTH_BLENDER_PRIVATE", root / "private");
        // The settings guard wants this one under the temp folder, which is already the scratch one here.
        setFolder(L"PLYNTH_CONFIG_DIR", plynth::cli::testing::testConfigFolder());
        setFolder(L"BLENDER_USER_RESOURCES", root / "resources");
        setFolder(L"BLENDER_USER_CONFIG", root / "blender-config");
        setFolder(L"BLENDER_USER_SCRIPTS", root / "scripts");
        setFolder(L"BLENDER_USER_EXTENSIONS", root / "extensions");
        setFolder(L"BLENDER_USER_DATAFILES", root / "datafiles");
        // A file that does not exist. With no value at all Plynth would look for the installed Blender.
        setVariable(L"PLYNTH_BLENDER", root / "no-blender.exe");
        // A store fetch never uses the real %LOCALAPPDATA%: it gets a short folder of this run, made by core.
        setVariable(L"PLYNTH_FETCH_FOLDER", root / "fetch");
        // The engine links talk to a group and port no Unreal editor uses, unless a test names its fake editor's.
        setVariable(L"PLYNTH_UNREAL_MULTICAST", L"239.0.0.77:7099");
        plynth::cli::testing::requireSafeSettings("starting the test program");
    }
    ~ScratchUser() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
} scratchUser;

// Gives this test program its own private Blender folder under the temp folder, and removes it at the end.
// Without it a run of the fake Blender would use the shared folder under %LOCALAPPDATA%.
struct TestPrivateFolder {
    TestPrivateFolder() {
        path = std::filesystem::temp_directory_path() / ("plynth-cli-private-" + std::to_string(_getpid()));
        _wputenv_s(L"PLYNTH_BLENDER_PRIVATE", path.c_str());
        SetEnvironmentVariableW(L"PLYNTH_BLENDER_PRIVATE", path.c_str());
    }
    ~TestPrivateFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
} testPrivateFolderSetup;

// The private folder in the Win32 environment, empty when it is not set.
std::wstring win32PrivateFolder() {
    const DWORD size = GetEnvironmentVariableW(L"PLYNTH_BLENDER_PRIVATE", nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(L"PLYNTH_BLENDER_PRIVATE", value.data(), size));
    return value;
}

// The private folder in the C runtime environment, empty when it is not set.
std::wstring runtimePrivateFolder() {
    wchar_t* text = nullptr;
    std::size_t size = 0;
    std::wstring value;
    if (_wdupenv_s(&text, &size, L"PLYNTH_BLENDER_PRIVATE") == 0 && text != nullptr) {
        value = text;
    }
    std::free(text);
    return value;
}

// What a child command prompt lists for the private folder. It writes UTF-16, so any folder name compares exactly.
std::wstring childPrivateFolderLine() {
    wchar_t system[MAX_PATH] = {};
    REQUIRE(GetSystemDirectoryW(system, MAX_PATH) > 0);
    plynth::ProcessSpec spec;
    spec.program = std::filesystem::path(system) / "cmd.exe";
    spec.arguments = {"/u", "/d", "/c", "set", "PLYNTH_BLENDER_PRIVATE"};
    const plynth::ProcessResult result = plynth::runProcess(spec);
    REQUIRE(result.status.ok());
    REQUIRE(result.exitCode == 0);
    std::wstring line(result.outputTail.size() / sizeof(wchar_t), L'\0');
    std::memcpy(line.data(), result.outputTail.data(), line.size() * sizeof(wchar_t));
    return line;
}

}  // namespace

TEST_CASE("the CLI tests keep the private Blender folder under the temp folder", "[cli][environment][privatefolder]") {
    const plynth::cli::testing::TempFolder temp("private-guard");
    // Should the guard ever be wrong, the fallback folder lands here and not in the user's own.
    const plynth::cli::testing::ScopedEnvironment local("LOCALAPPDATA", plynth::pathToUtf8(temp.path() / "local"));

    const std::wstring atStart = win32PrivateFolder();
    REQUIRE_FALSE(atStart.empty());
    REQUIRE(plynth::cli::testing::isInsideFolder(atStart, std::filesystem::temp_directory_path()));
    REQUIRE(runtimePrivateFolder() == atStart);
    const std::wstring childAtStart = L"PLYNTH_BLENDER_PRIVATE=" + atStart + L"\r\n";
    REQUIRE(childPrivateFolderLine() == childAtStart);
    {
        const std::filesystem::path other = temp.path() / "private";
        const plynth::cli::testing::ScopedEnvironment scope("PLYNTH_BLENDER_PRIVATE", plynth::pathToUtf8(other));
        REQUIRE(std::filesystem::path(win32PrivateFolder()) == other);
        REQUIRE(childPrivateFolderLine() == L"PLYNTH_BLENDER_PRIVATE=" + win32PrivateFolder() + L"\r\n");
    }
    REQUIRE(win32PrivateFolder() == atStart);
    REQUIRE(runtimePrivateFolder() == atStart);
    REQUIRE(childPrivateFolderLine() == childAtStart);

    // Only now is Blender's folder resolved, so a failed check above creates nothing.
    const auto exe = plynth::cli::testing::installFakeBlender(temp.path() / "blender", "5.2.0");
    REQUIRE(plynth::inspectBlender(exe).status.ok());
    std::ifstream log(exe.parent_path() / "fake_env.log", std::ios::binary);
    std::string seen;
    REQUIRE(std::getline(log, seen).good());
    const std::string expected = "|BLENDER_USER_RESOURCES=" + plynth::pathToUtf8(std::filesystem::path(atStart)) + "|";
    REQUIRE(seen.find(expected) != std::string::npos);
}
