#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "SettingsGuard.h"

#include <crtdbg.h>
#include <process.h>
#include <cstdlib>
#include <filesystem>
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

// Points the machine settings at a temporary folder, so no test touches the real settings file. The variable is
// set in both the Win32 and the C runtime environment, since code and tests read one or the other.
struct TestConfigFolder {
    TestConfigFolder() {
        path = plynth::cli::testing::testConfigFolder();
        std::filesystem::create_directories(path);
        _wputenv_s(L"PLYNTH_CONFIG_DIR", path.wstring().c_str());
        SetEnvironmentVariableW(L"PLYNTH_CONFIG_DIR", path.wstring().c_str());
        plynth::cli::testing::requireSafeSettings("starting the test program");
    }
    ~TestConfigFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
} testConfigFolderSetup;

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

}  // namespace
