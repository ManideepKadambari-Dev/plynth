#pragma once

#include <process.h>

#include <plynth/core/Settings.h>

#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace plynth::cli::testing {

// The folder this test process uses for machine settings, under the temp folder.
inline std::filesystem::path testConfigFolder() {
    return std::filesystem::temp_directory_path() / ("plynth-cli-config-" + std::to_string(_getpid()));
}

// Lower case, forward slash form of a path with a trailing slash, for comparing without touching any file.
inline std::wstring comparableFolder(const std::filesystem::path& path) {
    std::wstring text = path.lexically_normal().generic_wstring();
    for (wchar_t& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    if (text.empty() || text.back() != L'/') {
        text.push_back(L'/');
    }
    return text;
}

// True when path is strictly inside folder. Compares text only.
inline bool isInsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder) {
    if (path.empty() || folder.empty() || !path.is_absolute() || !folder.is_absolute()) {
        return false;
    }
    const std::wstring inner = comparableFolder(path);
    const std::wstring outer = comparableFolder(folder);
    return inner.size() > outer.size() && inner.compare(0, outer.size(), outer) == 0;
}

// True when settingsFile() sits inside the temporary folder of this process.
inline bool settingsAreInTempFolder() {
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    return !ec && isInsideFolder(plynth::settingsFile(), temp);
}

// Stops the whole test program before anything is touched when settings would not be in the temp folder.
inline void requireSafeSettings(const char* action) {
    if (!settingsAreInTempFolder()) {
        std::fprintf(stderr,
                     "REFUSED: %s, because the settings file is not in the temporary folder: %s\n"
                     "The test program stops here and nothing was changed.\n",
                     action, plynth::settingsFile().string().c_str());
        std::fflush(stderr);
        std::abort();
    }
}

// Deletes the settings file, but only after the guard agrees it is a temporary one.
inline void removeSettingsFileSafely() {
    requireSafeSettings("deleting the settings file");
    std::error_code ignored;
    std::filesystem::remove(plynth::settingsFile(), ignored);
}

// Clears the settings file when a test starts and when it ends, guarded.
struct GuardedSettingsReset {
    GuardedSettingsReset() { clear(); }
    ~GuardedSettingsReset() { clear(); }
    static void clear() { removeSettingsFileSafely(); }
};

}
