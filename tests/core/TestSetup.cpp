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

// Sets one variable in both the Win32 and the C runtime environment, since code and tests read one or the other.
void setVariable(const wchar_t* name, const std::filesystem::path& value) {
    _wputenv_s(name, value.c_str());
    SetEnvironmentVariableW(name, value.c_str());
}

// Gives this test program its own settings folder under the temp folder, and removes it at the end. Settings never
// come from the real %APPDATA%, and test programs that run at the same time never share a settings file.
struct TestConfigFolder {
    TestConfigFolder() {
        path = std::filesystem::temp_directory_path() / ("plynth-tests-config-" + std::to_string(_getpid()));
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path, ignored);
        setVariable(L"PLYNTH_CONFIG_DIR", path);
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
        path = std::filesystem::temp_directory_path() / ("plynth-tests-private-" + std::to_string(_getpid()));
        setVariable(L"PLYNTH_BLENDER_PRIVATE", path);
    }
    ~TestPrivateFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
} testPrivateFolderSetup;

}  // namespace
