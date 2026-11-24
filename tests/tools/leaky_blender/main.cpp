// A stand-in Blender that talks like a careless add-on. During a store fetch it prints a made-up key and a
// made-up token, then acts by leaky_mode.txt next to it: success, failed, crash, bad_result, hang, child or
// forged. Everything else is handed to the fake Blender, copied next to it as fake_inner.exe.
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* const kNoise = "key=KEY-MARKER-7f31 refresh=TOKEN-MARKER-92ac";

// The path of this program.
fs::path self()
{
    std::wstring buffer(32768, L'\0');
    buffer.resize(GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())));
    return buffer;
}

// Starts a program on this program's output and waits for its exit code.
int runChild(const fs::path& exe, std::wstring line)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) return 98;
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 99;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}

// Prints the made-up secrets on both outputs.
void noise()
{
    std::cout << kNoise << std::endl;
    std::cerr << kNoise << std::endl;
}

// Prints a progress line as the worker does.
void progress(const std::string& step, int index, const std::string& message)
{
    const nlohmann::json line{{"progress", {{"step", step}, {"index", index}, {"count", 3}, {"message", message}}}};
    std::cout << "PLYNTH:" << line.dump() << std::endl;
}

}

int wmain(int argc, wchar_t** argv)
{
    const fs::path me = self();
    const fs::path inner = me.parent_path() / L"fake_inner.exe";
    std::wstring jobPath;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--noise-child") {
            noise();
            return 0;
        }
        if (arg == L"--job" && i + 1 < argc) jobPath = argv[i + 1];
    }
    if (jobPath.empty()) return runChild(inner, GetCommandLineW());
    std::ifstream in(fs::path(jobPath), std::ios::binary);
    const nlohmann::json job = nlohmann::json::parse(in, nullptr, false);
    in.close();
    if (job.is_discarded() || job.value("task", "") != "blenderkit_fetch") return runChild(inner, GetCommandLineW());

    std::string mode;
    std::ifstream(me.parent_path() / L"leaky_mode.txt") >> mode;
    if (job.at("params").at("kind") == "material") SetEnvironmentVariableW(L"FAKE_BLENDERKIT", L"material");
    if (mode == "failed") SetEnvironmentVariableW(L"FAKE_BLENDERKIT", L"download_failed");

    if (mode != "child") noise();
    if (mode == "child") {
        runChild(me, L"blender --noise-child");
        const int code = runChild(inner, GetCommandLineW());
        runChild(me, L"blender --noise-child");
        return code;
    }
    if (mode == "crash" || mode == "bad_result" || mode == "hang") {
        progress("check_addon", 1, "Checking the BlenderKit add-on");
        progress("download", 2, "Downloading from BlenderKit");
        if (mode == "bad_result" || mode == "hang") {
            const std::string file = job.at("result_file").get<std::string>();
            std::ofstream(fs::path(std::u8string(file.begin(), file.end())), std::ios::binary) << "{ not json " << kNoise;
        }
        noise();
        if (mode == "hang") Sleep(60000);
        return mode == "bad_result" ? 0 : 9;
    }
    if (mode == "forged") {
        progress("check_addon", 1, "KEY-MARKER-7f31");
        progress("download", 2, "TOKEN-MARKER-92ac");
        progress("KEY-MARKER-7f31", 2, "Downloading from BlenderKit");
        std::cout << "PLYNTH:{not json KEY-MARKER-7f31" << std::endl;
    }
    const int code = runChild(inner, GetCommandLineW());
    noise();
    return code;
}
