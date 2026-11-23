#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#include <process.h>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <initializer_list>
#include <system_error>

namespace {

// Turns off every dialog a failing test program could show, so a failure prints and exits.
struct NoDialogs {
    NoDialogs() {
        // Settings never come from the real %APPDATA%: a folder of this process under the temp folder is used, so
        // test programs that run at the same time never share a settings file.
        char* temp = nullptr;
        std::size_t size = 0;
        _dupenv_s(&temp, &size, "TEMP");
        const std::string base = std::string(temp ? temp : ".") + "\\";
        free(temp);
        config = base + "plynth-tests-config-" + std::to_string(_getpid());
        _putenv_s("PLYNTH_CONFIG_DIR", config.c_str());
        // A store fetch never uses the real %LOCALAPPDATA%: it gets a short folder of this run, unless a test that
        // started this program named one.
        const std::string fetch = base + "plynth-fetch-" + std::to_string(_getpid());
        char* named = nullptr;
        _dupenv_s(&named, &size, "PLYNTH_FETCH_FOLDER");
        if (!named) _putenv_s("PLYNTH_FETCH_FOLDER", fetch.c_str());
        free(named);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
        for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
#endif
    }
    std::string config;
} noDialogs;

// Makes the settings folder of this process before the first test, and removes it at the end.
struct TestConfigFolder {
    TestConfigFolder() : path(noDialogs.config) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path, ignored);
    }
    ~TestConfigFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
} testConfigFolderSetup;

// Gives this test program its own private Blender folder under the temp folder, and removes it at the end.
// Without it a run of the fake Blender would use the shared folder under %LOCALAPPDATA%. The variable is set in
// both the Win32 and the C runtime environment, since code and tests read one or the other.
struct TestPrivateFolder {
    TestPrivateFolder() {
        path = std::filesystem::temp_directory_path() / ("plynth-tests-private-" + std::to_string(_getpid()));
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
