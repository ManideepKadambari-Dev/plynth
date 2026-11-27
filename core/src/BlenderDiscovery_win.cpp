#include "BlenderDiscovery.h"

#include "plynth/core/BlenderDownload.h"
#include "plynth/core/ProjectFiles.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

namespace plynth::discovery {

namespace fs = std::filesystem;

namespace {

// Closes a registry key when it goes out of scope.
class Key {
public:
    Key(HKEY root, const std::wstring& subkey, REGSAM view)
    {
        if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | view, &key_) != ERROR_SUCCESS) key_ = nullptr;
    }
    ~Key()
    {
        if (key_) RegCloseKey(key_);
    }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    explicit operator bool() const { return key_ != nullptr; }
    HKEY get() const { return key_; }

private:
    HKEY key_ = nullptr;
};

// Reads a text value of an open key. An empty name reads the default value.
std::optional<std::wstring> readValue(const Key& key, const wchar_t* name)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return std::nullopt;
    if (type != REG_SZ && type != REG_EXPAND_SZ) return std::nullopt;
    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
    DWORD got = size;
    if (RegQueryValueExW(key.get(), name, nullptr, nullptr, reinterpret_cast<LPBYTE>(value.data()), &got) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    if (type == REG_EXPAND_SZ) {
        const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (needed > 0) {
            std::wstring expanded(needed, L'\0');
            ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
            expanded.resize(wcsnlen(expanded.c_str(), expanded.size()));
            value = expanded;
        }
    }
    return value;
}

// Lists the names of the subkeys of an open key.
std::vector<std::wstring> subKeyNames(const Key& key)
{
    std::vector<std::wstring> names;
    for (DWORD i = 0;; ++i) {
        wchar_t buffer[512];
        DWORD size = static_cast<DWORD>(std::size(buffer));
        if (RegEnumKeyExW(key.get(), i, buffer, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        names.emplace_back(buffer, size);
    }
    return names;
}

// Lower cases text for comparing.
std::wstring lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

// The program path inside a command line or icon value: the quoted part, or the text up to .exe, without quotes.
std::wstring programIn(const std::wstring& command)
{
    std::wstring text = command;
    while (!text.empty() && text.front() == L' ') text.erase(text.begin());
    if (!text.empty() && text.front() == L'"') {
        const std::size_t end = text.find(L'"', 1);
        return text.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
    }
    const std::size_t end = lower(text).find(L".exe");
    if (end != std::wstring::npos) return text.substr(0, end + 4);
    return text.substr(0, text.find_first_of(L", "));
}

// blender.exe next to a program: the program itself when it is blender.exe, otherwise its sibling.
fs::path blenderBeside(const fs::path& program)
{
    if (lower(program.filename().wstring()) == L"blender.exe") return program;
    return program.parent_path() / L"blender.exe";
}

}

// Uninstall entries, App Paths and the .blend file open command.
std::vector<fs::path> registryBlenders()
{
    std::vector<fs::path> out;
    const std::wstring uninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            const Key parent(root, uninstall, view);
            if (!parent) continue;
            for (const std::wstring& name : subKeyNames(parent)) {
                const Key entry(root, uninstall + L"\\" + name, view);
                if (!entry) continue;
                const auto display = readValue(entry, L"DisplayName");
                if (!display || lower(*display).rfind(L"blender", 0) != 0) continue;
                if (const auto location = readValue(entry, L"InstallLocation"); location && !location->empty()) {
                    out.push_back(fs::path(*location) / L"blender.exe");
                }
                if (const auto icon = readValue(entry, L"DisplayIcon"); icon && !icon->empty()) {
                    out.push_back(blenderBeside(fs::path(programIn(*icon))));
                }
            }
        }
    }

    const std::wstring appPaths = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\blender.exe";
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            const Key key(root, appPaths, view);
            if (!key) continue;
            if (const auto program = readValue(key, L""); program && !program->empty()) {
                out.push_back(blenderBeside(fs::path(programIn(*program))));
            }
            if (const auto folder = readValue(key, L"Path"); folder && !folder->empty()) {
                out.push_back(fs::path(*folder) / L"blender.exe");
            }
        }
    }

    const Key openCommand(HKEY_CLASSES_ROOT, L"blendfile\\shell\\open\\command", 0);
    if (openCommand) {
        if (const auto command = readValue(openCommand, L""); command && !command->empty()) {
            out.push_back(blenderBeside(fs::path(programIn(*command))));
        }
    }
    return out;
}

// Store packages and the WindowsApps aliases.
std::vector<fs::path> storeBlenders()
{
    std::vector<fs::path> out;
    const std::wstring packagesPath =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    const Key packages(HKEY_CURRENT_USER, packagesPath, 0);
    if (packages) {
        for (const std::wstring& name : subKeyNames(packages)) {
            if (name.rfind(L"BlenderFoundation.Blender", 0) != 0) continue;
            const Key package(HKEY_CURRENT_USER, packagesPath + L"\\" + name, 0);
            if (!package) continue;
            const auto root = readValue(package, L"PackageRootFolder");
            if (!root || root->empty()) continue;
            const fs::path folder(*root);
            out.push_back(folder / L"blender.exe");
            out.push_back(folder / L"Blender" / L"blender.exe");
            std::error_code ec;
            for (const auto& sub : fs::directory_iterator(folder, ec)) {
                if (sub.is_directory(ec)) out.push_back(sub.path() / L"blender.exe");
            }
        }
    }

    wchar_t local[MAX_PATH];
    const DWORD got = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    if (got > 0 && got < std::size(local)) {
        std::error_code ec;
        const fs::path apps = fs::path(local) / L"Microsoft" / L"WindowsApps";
        for (const auto& entry : fs::directory_iterator(apps, ec)) {
            const std::wstring file = lower(entry.path().filename().wstring());
            if (file.rfind(L"blender", 0) == 0 && file.size() > 4 && file.compare(file.size() - 4, 4, L".exe") == 0 &&
                file.find(L"launcher") == std::wstring::npos) {
                out.push_back(entry.path());
            }
        }
    }
    return out;
}

// The finished downloads in the download folder. A folder without the marker is not one of them, and a link is
// never followed, so only what Plynth unpacked there is listed.
std::vector<fs::path> downloadedBlenders(const fs::path& root)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        std::error_code entryEc;
        if (entry.is_symlink(entryEc) || !entry.is_directory(entryEc)) continue;
        const DWORD attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (entry.path().filename().wstring().rfind(L".", 0) == 0) continue;
        if (!fs::is_regular_file(entry.path() / pathFromUtf8(kDownloadedBlenderMarker), entryEc)) continue;
        out.push_back(entry.path() / L"blender.exe");
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Blender in the Steam libraries.
std::vector<fs::path> steamBlenders()
{
    std::vector<fs::path> steamFolders;
    {
        const Key key(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0);
        if (key) {
            if (const auto path = readValue(key, L"SteamPath"); path && !path->empty()) steamFolders.emplace_back(*path);
        }
    }
    for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        const Key key(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", view);
        if (key) {
            if (const auto path = readValue(key, L"InstallPath"); path && !path->empty()) steamFolders.emplace_back(*path);
        }
    }

    std::vector<fs::path> libraries = steamFolders;
    for (const fs::path& steam : steamFolders) {
        std::ifstream in(steam / L"steamapps" / L"libraryfolders.vdf", std::ios::binary);
        if (!in) continue;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        static const std::regex pathLine(R"re("path"\s+"([^"]+)")re");
        for (std::sregex_iterator it(text.begin(), text.end(), pathLine), end; it != end; ++it) {
            const std::string value = (*it)[1].str();
            std::string unescaped;
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '\\') ++i;
                unescaped.push_back(value[i]);
            }
            libraries.push_back(pathFromUtf8(unescaped));
        }
    }
    std::vector<fs::path> out;
    for (const fs::path& library : libraries) {
        out.push_back(library / L"steamapps" / L"common" / L"Blender" / L"blender.exe");
    }
    return out;
}

}
