// A stand-in for plynth.exe in the launcher tests. Prints each argument on its own line and
// exits with the number in an argument of the form exit=N. An argument sleep=N waits N milliseconds first.
// An argument wait-cancel waits up to 20 seconds for the event named in PLYNTH_CANCEL_EVENT, as plynth.exe does
// for Ctrl+C, then prints "cancelled" and exits 5, or prints "not cancelled" and exits 6 (7 without the event).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

// Turns UTF-16 text into UTF-8.
static std::string toUtf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    }
    return result;
}

// Ignores Ctrl+C and Ctrl+Break, as plynth.exe does, which has no console of its own.
static BOOL WINAPI ignore(DWORD) {
    return TRUE;
}

// Waits for the launcher's cancel event and says how it went.
static int waitForCancel(std::string& text) {
    wchar_t name[256] = {};
    if (GetEnvironmentVariableW(L"PLYNTH_CANCEL_EVENT", name, 256) == 0) {
        text += "no event\n";
        return 7;
    }
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (event == nullptr) {
        text += "no event\n";
        return 7;
    }
    const DWORD waited = WaitForSingleObject(event, 20000);
    CloseHandle(event);
    if (waited == WAIT_OBJECT_0) {
        text += "cancelled\n";
        return 5;
    }
    text += "not cancelled\n";
    return 6;
}

int main() {
    SetConsoleCtrlHandler(ignore, TRUE);
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    int code = 0;
    std::string text;
    for (int i = 1; i < count; ++i) {
        const std::wstring arg = args[i];
        if (arg.rfind(L"exit=", 0) == 0) {
            code = std::stoi(arg.substr(5));
        }
        if (arg.rfind(L"sleep=", 0) == 0) {
            Sleep(static_cast<DWORD>(std::stoi(arg.substr(6))));
        }
        text += toUtf8(args[i]) + "\n";
    }
    for (int i = 1; i < count; ++i) {
        if (std::wstring(args[i]) == L"wait-cancel") {
            code = waitForCancel(text);
        }
    }
    LocalFree(args);
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    return code;
}
