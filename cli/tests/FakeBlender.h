#pragma once

#include "TestSupport.h"

#include <plynth/core/ProjectFiles.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <cstdlib>

namespace plynth::cli::testing {

// Copies the fake Blender into folder as blender.exe, with a version file next to it. Returns its path.
inline std::filesystem::path installFakeBlender(const std::filesystem::path& folder, const std::string& version,
                                                const std::string& mode = "") {
    std::filesystem::create_directories(folder);
    const std::filesystem::path exe = folder / "blender.exe";
    std::filesystem::copy_file(PLYNTH_FAKE_BLENDER, exe, std::filesystem::copy_options::overwrite_existing);
    std::ofstream(folder / "fake_version.txt") << version;
    if (!mode.empty()) {
        std::ofstream(folder / "fake_mode.txt") << mode;
    }
    return exe;
}

// Sets an environment variable for the life of this object, then puts the old value back. The Win32 copy
// and the C runtime copy of the environment are separate, and a program may have set either one, so both
// are saved, set and restored on their own. The Win32 call goes last because
// clearing the C runtime copy also clears the Win32 one. Names and values are UTF-8.
class ScopedEnvironment {
public:
    ScopedEnvironment(const std::string& name, const std::string& value) : name_(widen(name)) {
        saveWin32();
        saveRuntime();
        const std::wstring wide = widen(value);
        _wputenv_s(name_.c_str(), wide.c_str());
        SetEnvironmentVariableW(name_.c_str(), wide.c_str());
    }
    ~ScopedEnvironment() {
        _wputenv_s(name_.c_str(), runtimeHad_ ? runtimeOld_.c_str() : L"");
        SetEnvironmentVariableW(name_.c_str(), win32Had_ ? win32Old_.c_str() : nullptr);
    }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    // Turns UTF-8 text into UTF-16.
    static std::wstring widen(const std::string& text) {
        if (text.empty()) {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }

    // Remembers the value in the Win32 environment, if there is one.
    void saveWin32() {
        const DWORD needed = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (needed == 0) {
            win32Had_ = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
            return;
        }
        win32Old_.assign(needed, L'\0');
        const DWORD length = GetEnvironmentVariableW(name_.c_str(), win32Old_.data(), needed);
        win32Old_.resize(length);
        win32Had_ = true;
    }

    // Remembers the value in the C runtime environment, if there is one.
    void saveRuntime() {
        wchar_t* old = nullptr;
        std::size_t size = 0;
        if (_wdupenv_s(&old, &size, name_.c_str()) == 0 && old != nullptr) {
            runtimeOld_ = old;
            runtimeHad_ = true;
        }
        std::free(old);
    }

    std::wstring name_;
    std::wstring win32Old_;
    std::wstring runtimeOld_;
    bool win32Had_ = false;
    bool runtimeHad_ = false;
};

// Points every folder Blender or Plynth would use at temporary folders, so no test touches the user's own.
struct PrivateUser {
    explicit PrivateUser(const TempFolder& temp)
        : appdata("APPDATA", plynth::pathToUtf8(temp.path() / "user" / "appdata")),
          profile("USERPROFILE", plynth::pathToUtf8(temp.path() / "user" / "profile")),
          privateFolder("PLYNTH_BLENDER_PRIVATE", plynth::pathToUtf8(temp.path() / "user" / "private")),
          resources("BLENDER_USER_RESOURCES", plynth::pathToUtf8(temp.path() / "user" / "resources")),
          config("BLENDER_USER_CONFIG", plynth::pathToUtf8(temp.path() / "user" / "config")),
          scripts("BLENDER_USER_SCRIPTS", plynth::pathToUtf8(temp.path() / "user" / "scripts")),
          extensions("BLENDER_USER_EXTENSIONS", plynth::pathToUtf8(temp.path() / "user" / "extensions")),
          datafiles("BLENDER_USER_DATAFILES", plynth::pathToUtf8(temp.path() / "user" / "datafiles")),
          xdgData("XDG_DATA_HOME", plynth::pathToUtf8(temp.path() / "user" / "xdg")) {
        for (const char* name : {"appdata", "profile", "private", "resources", "config", "scripts", "extensions",
                                 "datafiles", "xdg"}) {
            std::filesystem::create_directories(temp.path() / "user" / name);
        }
    }
    ScopedEnvironment appdata, profile, privateFolder, resources, config, scripts, extensions, datafiles, xdgData;
};

}
