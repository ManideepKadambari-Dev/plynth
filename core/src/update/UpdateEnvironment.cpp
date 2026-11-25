#include "UpdateInternals.h"

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace plynth::update_detail {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kRepository = "/ManideepKadambari-Dev/plynth";

const UpdateEnvironment* current = nullptr;

// The environment of the shipped program, made on first use.
const UpdateEnvironment& shipped()
{
    static const UpdateEnvironment environment;
    return environment;
}

// The value of an environment variable as a path. Empty when it is not set.
fs::path variablePath(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), size));
    return fs::path(value);
}

// The key that says two paths name the same folder.
std::wstring folderKey(const fs::path& folder)
{
    std::error_code ec;
    fs::path result = fs::absolute(folder, ec);
    if (ec) result = folder;
    result = result.lexically_normal();
    std::wstring text = result.wstring();
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    for (wchar_t& c : text) {
        if (c == L'/') c = L'\\';
    }
    if (!text.empty()) CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

}

// The environment in use.
const UpdateEnvironment& environment()
{
    return current ? *current : shipped();
}

// Puts a test environment in place.
ScopedUpdateEnvironment::ScopedUpdateEnvironment(UpdateEnvironment environment)
    : previous_(current), own_(std::move(environment))
{
    current = &own_;
}

// Puts the earlier environment back.
ScopedUpdateEnvironment::~ScopedUpdateEnvironment()
{
    current = previous_;
}

// Two folders are the same.
bool sameFolder(const fs::path& a, const fs::path& b)
{
    if (a.empty() || b.empty()) return false;
    return folderKey(a) == folderKey(b);
}

// The state file in use.
fs::path stateFile()
{
    const UpdateEnvironment& env = environment();
    if (!env.stateFile.empty()) return env.stateFile;
    return settingsFolder() / "update-state.json";
}

// The folder that holds the download folders.
fs::path downloadRoot()
{
    const UpdateEnvironment& env = environment();
    if (!env.downloadRoot.empty()) return env.downloadRoot;
    if (fs::path config = variablePath(L"PLYNTH_CONFIG_DIR"); !config.empty()) return config / "updates";
    PWSTR raw = nullptr;
    fs::path local;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) local = fs::path(raw);
    CoTaskMemFree(raw);
    if (local.empty()) local = variablePath(L"LOCALAPPDATA");
    if (local.empty()) return {};
    return local / "Plynth" / "updates";
}

// The address of the release list.
std::string releasesListUrl()
{
    return environment().apiBase + "/repos" + std::string(kRepository) + "/releases?per_page=30";
}

// The address of a release page.
std::string releasePageUrl(const Version& version)
{
    return environment().downloadBase + std::string(kRepository) + "/releases/tag/v" + versionText(version);
}

// The address of a file of a release.
std::string releaseFileUrl(const Version& version, std::string_view file)
{
    return environment().downloadBase + std::string(kRepository) + "/releases/download/v" + versionText(version) + "/" +
           std::string(file);
}

}
