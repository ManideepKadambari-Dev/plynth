// plynth.com: a small console program that starts plynth.exe with the same command line. For a CLI
// command it waits and returns the exit code, so a terminal sees the answer. To open the window it
// returns at once.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <plynth/cli/Cli.h>

#include <filesystem>
#include <string>

namespace {

constexpr int kExitFailed = 1;

// Writes a short text to standard output when there is somewhere to write it.
void writeOut(const std::string& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// The part of the command line after the program name, with its leading spaces removed.
std::wstring argumentsOf(const std::wstring& commandLine) {
    std::size_t i = 0;
    if (i < commandLine.size() && commandLine[i] == L'"') {
        i = commandLine.find(L'"', 1);
        i = (i == std::wstring::npos) ? commandLine.size() : i + 1;
    } else {
        while (i < commandLine.size() && commandLine[i] != L' ' && commandLine[i] != L'\t') {
            ++i;
        }
    }
    while (i < commandLine.size() && (commandLine[i] == L' ' || commandLine[i] == L'\t')) {
        ++i;
    }
    return commandLine.substr(i);
}

// Lets the child use a standard handle, when there is one.
void makeInheritable(HANDLE handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
}

// Turns UTF-16 text into UTF-8.
std::string toUtf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

// Says whether the first argument is a CLI command word. False with no arguments.
bool firstArgumentIsCommand() {
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) {
        return false;
    }
    const bool isCommand = count > 1 && plynth::cli::isCommandWord(toUtf8(args[1]));
    LocalFree(args);
    return isCommand;
}

// The folder this program lives in.
std::filesystem::path ownFolder() {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(size);
    return std::filesystem::path(path).parent_path();
}

}

// Starts plynth.exe from this folder, waits for it and returns its exit code.
int main() {
    const std::filesystem::path child = ownFolder() / L"plynth.exe";
    std::error_code ignored;
    if (!std::filesystem::exists(child, ignored)) {
        writeOut("{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"not_found\","
                 "\"message\":\"plynth.exe was not found next to plynth.com.\"}}\n");
        return kExitFailed;
    }

    std::wstring commandLine = L"\"" + child.wstring() + L"\"";
    const std::wstring arguments = argumentsOf(GetCommandLineW());
    if (!arguments.empty()) {
        commandLine += L" " + arguments;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    // Opening the window: start it on its own and return at once, so the terminal gets its prompt back.
    if (!firstArgumentIsCommand()) {
        PROCESS_INFORMATION window{};
        if (!CreateProcessW(child.c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                            nullptr, &startup, &window)) {
            writeOut("{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"cannot_read\","
                     "\"message\":\"plynth.exe could not be started.\"}}\n");
            return kExitFailed;
        }
        CloseHandle(window.hThread);
        CloseHandle(window.hProcess);
        return 0;
    }

    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    makeInheritable(startup.hStdInput);
    makeInheritable(startup.hStdOutput);
    makeInheritable(startup.hStdError);

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(child.c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                        &process)) {
        writeOut("{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"cannot_read\","
                 "\"message\":\"plynth.exe could not be started.\"}}\n");
        return kExitFailed;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = kExitFailed;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
