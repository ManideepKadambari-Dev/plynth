// A stand-in for PlynthSetup.exe in the update tests. It never installs anything. At start it writes what it
// was given to fake-start.json beside itself: its command line, working folder, process id, whether each
// standard handle is empty, whether the handle named by FAKE_PROBE_HANDLE (open on the file FAKE_PROBE_FILE)
// came through, and whether the handle named by FAKE_LOCK_HANDLE is open on this very file. It then waits
// FAKE_INSTALLER_WAIT_MS milliseconds, writes "finished" to the /LOG file and writes fake-done.txt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <nlohmann/json.hpp>

#include <crtdbg.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// UTF-16 to UTF-8.
std::string narrow(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

// The value of an environment variable. Empty when it is not set.
std::wstring variable(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), size));
    return value;
}

// A handle value written as a decimal number in a variable. Null when there is none.
HANDLE handleFrom(const wchar_t* name)
{
    const std::wstring text = variable(name);
    if (text.empty()) return nullptr;
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(text.c_str(), nullptr, 10)));
}

// Says whether a standard handle is empty.
bool emptyStd(DWORD which)
{
    const HANDLE handle = GetStdHandle(which);
    return handle == nullptr || handle == INVALID_HANDLE_VALUE;
}

// Says whether the handle named in a variable is open on the given file.
bool handleOnFile(const wchar_t* name, const std::filesystem::path& file)
{
    const HANDLE value = handleFrom(name);
    if (!value || file.empty()) return false;
    DWORD flags = 0;
    if (!GetHandleInformation(value, &flags) || GetFileType(value) != FILE_TYPE_DISK) return false;
    const HANDLE own = CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
    if (own == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    const bool same = GetFileInformationByHandle(value, &a) && GetFileInformationByHandle(own, &b) &&
                      a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
                      a.nFileIndexLow == b.nFileIndexLow;
    CloseHandle(own);
    return same;
}

// The file named by /LOG="..." on the command line. Empty when there is none.
std::wstring logFile(const std::wstring& line)
{
    const std::wstring key = L"/LOG=\"";
    const std::size_t start = line.find(key);
    if (start == std::wstring::npos) return {};
    const std::size_t from = start + key.size();
    const std::size_t end = line.find(L'"', from);
    if (end == std::wstring::npos) return {};
    return line.substr(from, end - from);
}

// Writes text to a file through a temporary file, so a reader never sees half of it.
void writeWhole(const std::filesystem::path& file, const std::string& text)
{
    const std::filesystem::path temp = file.wstring() + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        out << text;
    }
    MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}

// Records how it was started, waits, then marks itself done.
int wmain()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    std::wstring self(32768, L'\0');
    self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    const std::filesystem::path program(self);
    const std::filesystem::path folder = program.parent_path();
    std::wstring current(32768, L'\0');
    current.resize(GetCurrentDirectoryW(static_cast<DWORD>(current.size()), current.data()));
    const std::wstring line = GetCommandLineW();

    nlohmann::json start = {
        {"command_line", narrow(line)},
        {"working_folder", narrow(current)},
        {"pid", GetCurrentProcessId()},
        {"stdin_empty", emptyStd(STD_INPUT_HANDLE)},
        {"stdout_empty", emptyStd(STD_OUTPUT_HANDLE)},
        {"stderr_empty", emptyStd(STD_ERROR_HANDLE)},
        {"probe_inherited", handleOnFile(L"FAKE_PROBE_HANDLE", std::filesystem::path(variable(L"FAKE_PROBE_FILE")))},
        {"lock_inherited", handleOnFile(L"FAKE_LOCK_HANDLE", program)},
    };
    writeWhole(folder / L"fake-start.json", start.dump());

    const std::wstring wait = variable(L"FAKE_INSTALLER_WAIT_MS");
    if (!wait.empty()) Sleep(static_cast<DWORD>(std::wcstoul(wait.c_str(), nullptr, 10)));

    if (const std::wstring log = logFile(line); !log.empty()) writeWhole(std::filesystem::path(log), "finished\n");
    writeWhole(folder / L"fake-done.txt", "done\n");
    return 0;
}
