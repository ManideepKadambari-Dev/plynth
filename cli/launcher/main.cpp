// plynth.com: a small console program that starts plynth.exe with the same command line. For a CLI
// command it waits and returns the exit code, so a terminal sees the answer. To open the window it
// returns at once. For anything else it prints the CLI's unknown_command answer without loading core.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <plynth/cli/Words.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kExitFailed = 1;
constexpr int kExitBadArguments = 2;

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

// What the first argument asks for.
enum class Mode {
    Command, // a CLI command word: run plynth.exe and wait
    Window,  // nothing, an existing file or folder, or an app option: open the window
    Unknown, // anything else: answer with the CLI's unknown_command error
};

// Decides what the arguments ask for. --pretty does not count as the first word, and --help or -h anywhere
// goes to plynth.exe, which answers with the help list.
Mode modeOf(const std::vector<std::string>& args) {
    if (args.empty()) {
        return Mode::Window;
    }
    std::string first;
    bool hasFirst = false;
    for (const std::string& arg : args) {
        if (arg != "--pretty") {
            first = arg;
            hasFirst = true;
            break;
        }
    }
    if (!hasFirst) {
        return Mode::Unknown;
    }
    if (plynth::cli::isCommandWord(first)) {
        return Mode::Command;
    }
    for (const char* option : {"--image", "--screenshot", "--size", "--terminal"}) {
        if (first == option) {
            return Mode::Window;
        }
    }
    for (const std::string& arg : args) {
        if (arg == "--help" || arg == "-h") {
            return Mode::Command;
        }
    }
    if (first.empty() || first.front() == '-') {
        return Mode::Unknown;
    }
    std::error_code ignored;
    const bool exists = std::filesystem::exists(std::filesystem::path(std::u8string(first.begin(), first.end())), ignored);
    return exists ? Mode::Window : Mode::Unknown;
}

// The arguments after the program name, as UTF-8.
std::vector<std::string> readArguments() {
    std::vector<std::string> result;
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) {
        return result;
    }
    for (int i = 1; i < count; ++i) {
        result.push_back(toUtf8(args[i]));
    }
    LocalFree(args);
    return result;
}

// Swallows Ctrl+C and Ctrl+Break so they only reach plynth.exe, and plynth.com keeps waiting for it.
// A handler function is used, not the "ignore" setting, because that setting would pass to the child.
BOOL WINAPI ignoreInterrupt(DWORD type) {
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
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
    const std::vector<std::string> arguments = readArguments();
    const Mode mode = modeOf(arguments);
    if (mode == Mode::Unknown) {
        writeOut(plynth::cli::unknownCommandAnswer(arguments));
        return kExitBadArguments;
    }
    const std::filesystem::path child = ownFolder() / L"plynth.exe";
    std::error_code ignored;
    if (!std::filesystem::exists(child, ignored)) {
        writeOut("{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"not_found\","
                 "\"message\":\"plynth.exe was not found next to plynth.com.\"}}\n");
        return kExitFailed;
    }

    std::wstring commandLine = L"\"" + child.wstring() + L"\"";
    const std::wstring tail = argumentsOf(GetCommandLineW());
    if (!tail.empty()) {
        commandLine += L" " + tail;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    // Opening the window: start it on its own and return at once, so the terminal gets its prompt back.
    if (mode == Mode::Window) {
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

    // Show non ASCII text correctly in the console while the child writes, then put the old page back.
    const UINT oldCodePage = GetConsoleOutputCP();
    if (oldCodePage != 0) {
        SetConsoleOutputCP(CP_UTF8);
    }
    SetConsoleCtrlHandler(ignoreInterrupt, TRUE);

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(child.c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                        &process)) {
        if (oldCodePage != 0) {
            SetConsoleOutputCP(oldCodePage);
        }
        writeOut("{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"cannot_read\","
                 "\"message\":\"plynth.exe could not be started.\"}}\n");
        return kExitFailed;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = kExitFailed;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (oldCodePage != 0) {
        SetConsoleOutputCP(oldCodePage);
    }
    return static_cast<int>(code);
}
