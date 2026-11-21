// A stand-in for plynth.exe in the launcher tests. Prints each argument on its own line and
// exits with the number in an argument of the form exit=N. An argument sleep=N waits N milliseconds first.

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

int main() {
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
    LocalFree(args);
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    return code;
}
