#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>
#include <process.h>
#include <cstdio>
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
        setFolder(L"PLYNTH_CONFIG_DIR", root / "settings");
        setFolder(L"BLENDER_USER_RESOURCES", root / "resources");
        setFolder(L"BLENDER_USER_CONFIG", root / "blender-config");
        setFolder(L"BLENDER_USER_SCRIPTS", root / "scripts");
        setFolder(L"BLENDER_USER_EXTENSIONS", root / "extensions");
        setFolder(L"BLENDER_USER_DATAFILES", root / "datafiles");
        // A file that does not exist. With no value at all Plynth would look for the installed Blender.
        setVariable(L"PLYNTH_BLENDER", root / "no-blender.exe");
        // A store fetch never uses the real %LOCALAPPDATA%: it gets a short folder of this run, made by core,
        // unless a test that started this program named one, so both share the same fetch folder.
        wchar_t* named = nullptr;
        std::size_t size = 0;
        if (_wdupenv_s(&named, &size, L"PLYNTH_FETCH_FOLDER") != 0 || !named || !*named) {
            setVariable(L"PLYNTH_FETCH_FOLDER", root / "fetch");
        }
        free(named);
    }
    ~ScratchUser() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
} scratchUser;

}  // namespace
