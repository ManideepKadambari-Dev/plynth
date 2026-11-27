#include "StoreFetchFolder.h"

#include "../Fnv.h"

#include <plynth/core/ProjectFiles.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>

#include <cstdio>
#include <system_error>
#include <vector>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The value of an environment variable, or empty when it is not set.
std::wstring variableText(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(got < size ? got : 0);
    return value;
}

// The short 8.3 form of a folder that exists, or the same path when Windows gives none.
fs::path shortPathOf(const fs::path& path)
{
    const std::wstring text = path.wstring();
    const DWORD size = GetShortPathNameW(text.c_str(), nullptr, 0);
    if (size == 0) return path;
    std::wstring out(size, L'\0');
    const DWORD got = GetShortPathNameW(text.c_str(), out.data(), size);
    if (got == 0 || got >= size) return path;
    out.resize(got);
    return fs::path(out);
}

// The full path with Windows separators and no separator at the end.
fs::path tidy(const fs::path& path)
{
    std::error_code ec;
    fs::path out = fs::absolute(path, ec);
    if (ec) out = path;
    out = out.lexically_normal();
    out.make_preferred();
    if (!out.has_filename()) out = out.parent_path();
    return out;
}

// Says whether a fetch folder is short enough for the store's helper.
bool fits(const fs::path& folder)
{
    return pathToUtf8(folder).size() <= kMaxFetchFolderBytes;
}

}

namespace detail {

// The SID of the signed in Windows user, empty when it cannot be read.
std::vector<unsigned char> currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    std::vector<unsigned char> out;
    if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        const PSID sid = reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid;
        if (IsValidSid(sid)) {
            const auto* bytes = static_cast<const unsigned char*>(sid);
            out.assign(bytes, bytes + GetLengthSid(sid));
        }
    }
    CloseHandle(token);
    return out;
}

// The first 8 hex digits of the FNV hash of a user's SID text.
std::string fetchUserId(std::string_view sidText)
{
    char text[17] = {};
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(fnv1a(sidText)));
    return std::string(text, 8);
}

// The values the choice reads from this process and this Windows user.
FetchFolderInputs systemFetchFolderInputs()
{
    FetchFolderInputs in;
    const std::wstring name(kFetchFolderVariable.begin(), kFetchFolderVariable.end());
    in.variable = fs::path(variableText(name.c_str()));
    in.localAppData = fs::path(variableText(L"LOCALAPPDATA"));
    if (const std::wstring drive = variableText(L"SystemDrive"); !drive.empty()) in.shortRoot = fs::path(drive + L"\\");
    std::vector<unsigned char> sid = currentUserSid();
    LPWSTR sidText = nullptr;
    if (!sid.empty() && ConvertSidToStringSidW(sid.data(), &sidText)) {
        std::string text;
        for (const wchar_t* c = sidText; *c; ++c) text.push_back(static_cast<char>(*c));
        in.userId = fetchUserId(text);
        LocalFree(sidText);
    }
    in.shortPath = shortPathOf;
    return in;
}

// The sentence for a fetch when no folder is short enough.
std::string noShortFetchFolderSentence()
{
    return "The folder for store downloads would need a path of at most " + std::to_string(kMaxFetchFolderBytes) +
           " characters. Set " + std::string(kFetchFolderVariable) + " to a short folder, for example C:/PlynthFetch.";
}

// Picks the first fetch folder that is short enough, by the four rules of chooseFetchFolder.
FetchFolderChoice chooseFetchFolder(const FetchFolderInputs& inputs)
{
    FetchFolderChoice choice;
    const auto pick = [&](const fs::path& folder, int used) {
        choice.folder = folder;
        choice.used = used;
        return choice;
    };
    if (!inputs.variable.empty()) {
        const fs::path folder = tidy(inputs.variable);
        if (fits(folder)) return pick(folder, 1);
        const std::string variable(kFetchFolderVariable);
        choice.status = fail(ErrorCode::CannotWrite, "The folder " + variable + " names is longer than " +
                                                         std::to_string(kMaxFetchFolderBytes) + " characters. Set " +
                                                         variable + " to a short folder, for example C:/PlynthFetch.");
        return choice;
    }
    if (!inputs.localAppData.empty()) {
        const fs::path local = tidy(inputs.localAppData);
        if (const fs::path folder = local / L"Plynth" / L"fetch"; fits(folder)) return pick(folder, 2);
        // Plynth and fetch are already valid 8.3 names, so only the part above them can be shorter.
        if (inputs.shortPath) {
            if (const fs::path folder = tidy(inputs.shortPath(local)) / L"Plynth" / L"fetch"; fits(folder)) return pick(folder, 3);
        }
    }
    if (!inputs.shortRoot.empty() && !inputs.userId.empty()) {
        if (const fs::path folder = tidy(inputs.shortRoot) / L"PlynthFetch" / pathFromUtf8(inputs.userId); fits(folder)) {
            return pick(folder, 4);
        }
    }
    choice.status = fail(ErrorCode::CannotWrite, noShortFetchFolderSentence());
    return choice;
}

}

// Picks the fetch folder for this user from the environment.
FetchFolderChoice chooseFetchFolder()
{
    return detail::chooseFetchFolder(detail::systemFetchFolderInputs());
}

}
