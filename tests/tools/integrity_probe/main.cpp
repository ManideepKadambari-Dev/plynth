// A small program for the low integrity tests. Each argument is one action, done in order, with one line of
// output each:
//   --level              "level:<integrity rid in hex>"
//   --write <file>       "write:ok" or "write:denied"
//   --reg <subkey>       creates HKEY_CURRENT_USER\<subkey>: "reg:ok" or "reg:denied"
//   --spawn <seconds>    starts itself with --sleep and does not wait: "child:<pid>"
//   --sleep <seconds>    waits
//   --env <name>         "env:<name>=<value>" or "env:<name>=<unset>"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <crtdbg.h>

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// The integrity level of this process as its relative id.
unsigned long integrityLevel()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<char> buffer(size);
    unsigned long rid = 0;
    if (GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) {
        const auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
        rid = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
    }
    CloseHandle(token);
    return rid;
}

// Tries to create a file and write one byte.
bool tryWrite(const std::wstring& path)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, "x", 1, &written, nullptr) && written == 1;
    CloseHandle(file);
    return ok;
}

// Tries to create a key under HKEY_CURRENT_USER.
bool tryRegistry(const std::wstring& subkey)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const DWORD value = 1;
    const bool ok = RegSetValueExW(key, L"probe", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

// Starts this program again with --sleep and returns its id, or 0.
unsigned long spawnSleeper(const std::wstring& seconds)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring line = L"\"" + std::wstring(self) + L"\" --sleep " + seconds;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(self, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) return 0;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return info.dwProcessId;
}

// UTF-16 text as UTF-8.
std::string narrow(const std::wstring& text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

}

// Sends failure reports of the C runtime and Windows to stderr instead of dialogs, so a failing run of this
// tool ends instead of waiting for someone to close a window.
void quietFailures()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
}

int wmain(int argc, wchar_t** argv)
{
    quietFailures();
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const std::wstring next = i + 1 < argc ? argv[i + 1] : L"";
        if (arg == L"--level") {
            std::printf("level:%lx\n", integrityLevel());
        } else if (arg == L"--write") {
            std::printf("write:%s\n", tryWrite(next) ? "ok" : "denied");
            ++i;
        } else if (arg == L"--reg") {
            std::printf("reg:%s\n", tryRegistry(next) ? "ok" : "denied");
            ++i;
        } else if (arg == L"--spawn") {
            std::printf("child:%lu\n", spawnSleeper(next));
            ++i;
        } else if (arg == L"--sleep") {
            Sleep(static_cast<DWORD>(_wtoi(next.c_str())) * 1000);
            ++i;
        } else if (arg == L"--env") {
            wchar_t value[4096];
            const DWORD got = GetEnvironmentVariableW(next.c_str(), value, 4096);
            std::printf("env:%s=%s\n", narrow(next).c_str(), got ? narrow(value).c_str() : "<unset>");
            ++i;
        }
        std::fflush(stdout);
    }
    return 0;
}
