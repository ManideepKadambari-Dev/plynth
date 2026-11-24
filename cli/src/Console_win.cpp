#include <plynth/cli/Cli.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

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

// The words after the program name, read from the Windows command line as UTF-8.
std::vector<std::string> readArguments() {
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> args;
    if (wide == nullptr) {
        return args;
    }
    for (int i = 1; i < count; ++i) {
        args.push_back(toUtf8(wide[i]));
    }
    LocalFree(wide);
    return args;
}

// Says whether a handle can be written to.
bool isUsable(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// Finds somewhere to write the answer: the redirected output if there is one, otherwise the parent
// console. Returns null when started from Explorer, where there is nowhere to write.
HANDLE openOutput(bool& ownsHandle) {
    ownsHandle = false;
    HANDLE standard = GetStdHandle(STD_OUTPUT_HANDLE);
    if (isUsable(standard)) {
        return standard;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return nullptr;
    }
    HANDLE console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    if (console == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    SetConsoleOutputCP(CP_UTF8);
    ownsHandle = true;
    return console;
}

// Writes all the bytes to a handle.
void writeAll(HANDLE handle, const std::string& text) {
    std::size_t done = 0;
    while (done < text.size()) {
        DWORD written = 0;
        if (!WriteFile(handle, text.data() + done, static_cast<DWORD>(text.size() - done), &written, nullptr) ||
            written == 0) {
            return;
        }
        done += written;
    }
}

// Writes what it is given straight to a handle, so progress lines appear while the command runs.
class HandleBuffer : public std::streambuf {
public:
    explicit HandleBuffer(HANDLE handle) : handle_(handle) {}

protected:
    std::streamsize xsputn(const char* text, std::streamsize size) override {
        writeAll(handle_, std::string(text, static_cast<std::size_t>(size)));
        return size;
    }
    int_type overflow(int_type c) override {
        if (c != traits_type::eof()) {
            const char byte = static_cast<char>(c);
            writeAll(handle_, std::string(1, byte));
        }
        return traits_type::not_eof(c);
    }

private:
    HANDLE handle_;
};

}

int runFromProcess(int, char*[], const CliHost& host) {
    const std::vector<std::string> args = readArguments();
    std::ostringstream buffer;
    HANDLE errorHandle = GetStdHandle(STD_ERROR_HANDLE);
    HandleBuffer errorBuffer(errorHandle);
    std::ostream progress(&errorBuffer);
    const int code =
        run(args, buffer, std::filesystem::current_path(), isUsable(errorHandle) ? &progress : nullptr, host);

    bool ownsHandle = false;
    HANDLE output = openOutput(ownsHandle);
    if (output != nullptr) {
        writeAll(output, buffer.str());
        if (ownsHandle) {
            CloseHandle(output);
        }
    }
    return code;
}

}
