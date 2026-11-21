// plynth.com: a small console program that starts plynth.exe with the same command line, so a
// terminal waits for the answer and sees the exit code.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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
