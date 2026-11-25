#include "UpdateInternals.h"

#include "plynth/core/UpdateNames.h"
#include "plynth/core/Version.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>

namespace plynth::update_detail {

namespace fs = std::filesystem;

namespace {

// The folder of the running program.
fs::path programFolder()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD got = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (got == 0) return {};
        if (got < buffer.size()) {
            buffer.resize(got);
            return fs::path(buffer).parent_path();
        }
        if (buffer.size() > 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}

// The InstallLocation the installer wrote. Empty when there is none.
std::optional<std::wstring> installLocation()
{
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size == 0 || size > 65536) {
        return std::nullopt;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, value.data(), &size) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(size / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

// %LOCALAPPDATA%\Programs\Plynth, where the per user installer puts Plynth.
fs::path fixedFolder()
{
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_UserProgramFiles, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        result = fs::path(raw) / "Plynth";
    }
    CoTaskMemFree(raw);
    return result;
}

// Reads a small text file. Empty when it cannot be read or is too big.
std::optional<std::string> readSmall(const fs::path& file)
{
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) > 16 * 1024 * 1024 || ec) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}

// Says whether package.json is that of a release of this version.
bool isReleasePackage(std::string_view text, std::string_view version)
{
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return false;
    const auto is = [&](const char* key, const nlohmann::json& expected) {
        const auto it = json.find(key);
        return it != json.end() && *it == expected;
    };
    return is("format", "plynth-package") && is("version", 1) && is("app_version", std::string(version)) &&
           is("platform", "windows-x64") && is("trial", false);
}

// Decides whether a probe describes an installed release.
Installation detectInstallation(const InstallationProbe& probe)
{
    Installation result;
    result.folder = probe.programFolder;
    result.version = probe.runningVersion;
    if (probe.programFolder.empty() || !probe.installLocation || probe.fixedFolder.empty()) return result;
    const auto package = readSmall(probe.programFolder / kPackageFile);
    if (!package || !isReleasePackage(*package, probe.runningVersion)) return result;
    if (!sameFolder(probe.programFolder, probe.fixedFolder)) return result;
    if (!sameFolder(probe.programFolder, fs::path(*probe.installLocation))) return result;
    result.kind = InstallKind::Installed;
    return result;
}

// The probe of the running program.
InstallationProbe currentProbe()
{
    InstallationProbe probe;
    probe.programFolder = programFolder();
    probe.runningVersion = std::string(version());
    probe.fixedFolder = fixedFolder();
    probe.installLocation = installLocation();
    return probe;
}

}

namespace plynth {

// Says whether this copy is an installed release.
Installation currentInstallation()
{
    return update_detail::detectInstallation(update_detail::currentProbe());
}

}
