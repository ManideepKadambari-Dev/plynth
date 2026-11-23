// What the fake Blender records and does in a store fetch, for the fetch folder, integrity and helper tests.
// recordFetchRun appends one JSON line to fake_fetch.log next to the program: the arguments, the job file,
// private_folder, the integrity level, start and end times, and the variables the tests check. It also keeps
// the arguments in fake_args.txt next to the program, because the job's own folder is emptied afterwards.
// fetchSideEffects acts by FAKE_FETCH_DO, a list of words joined by "+":
//   helper     writes private_folder/client/system_id when missing, client/bin/helper.exe, and the logs
//              client/default.log and client/client-50000.log holding made-up secrets
//   linger     starts the fake again with --linger, holding private_folder/client/helper.lock open for a minute
//   unzip      starts the fake helper FAKE_STORE_HELPER_EXE on private_folder and leaves it running; its
//              scenario (FAKE_STORE_SCENARIO) can write unzip entries anywhere
//   probe      tries to write each file of FAKE_PROBE_WRITES (joined by ";") and the key FAKE_PROBE_REG under
//              HKEY_CURRENT_USER, and records each result
//   delay      waits FAKE_FETCH_DELAY_MS
//   crash      ends at once with exit code 9 and no result
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

// A path from UTF-8 text.
fs::path fromUtf8(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

// UTF-8 text of a path.
std::string toUtf8(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The value of a variable, or null when it is not set.
Json variable(const wchar_t* name)
{
    wchar_t value[8192];
    const DWORD got = GetEnvironmentVariableW(name, value, 8192);
    if (got == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return nullptr;
    return toUtf8(fs::path(std::wstring(value, got)));
}

// The folder that holds this program.
fs::path here()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path();
}

// The integrity level of this process as its relative id.
unsigned long integrity()
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

// Milliseconds of the system clock, to compare runs of separate processes.
long long clockMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Says whether FAKE_FETCH_DO holds a word.
bool asked(const std::string& word)
{
    const Json all = variable(L"FAKE_FETCH_DO");
    std::stringstream list(all.is_string() ? all.get<std::string>() : "");
    for (std::string part; std::getline(list, part, '+');) {
        if (part == word) return true;
    }
    return false;
}

// Writes text into a file, making its folder. False when it could not.
bool put(const fs::path& file, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

Json entry = Json::object();

// Appends the entry to fake_fetch.log.
void flush()
{
    std::ofstream(here() / "fake_fetch.log", std::ios::binary | std::ios::app) << entry.dump() << "\n";
}

}

// Notes the run of one fetch job.
void recordFetchRun(const Json& job, const std::string& jobPath, const std::string& line)
{
    std::ofstream(here() / "fake_args.txt", std::ios::binary | std::ios::trunc) << line << "\n";
    entry = Json::object();
    entry["args"] = line;
    entry["job"] = toUtf8(fromUtf8(jobPath));
    entry["private_folder"] = job["params"].value("private_folder", Json(nullptr));
    entry["output_folder"] = job["params"].value("output_folder", Json(nullptr));
    entry["integrity"] = integrity();
    entry["start_ms"] = clockMs();
    Json vars = Json::object();
    for (const wchar_t* name : {L"PLYNTH_FETCH_PRIVATE", L"TEMP", L"TMP", L"PYTHONDONTWRITEBYTECODE", L"BLENDER_SYSTEM_SCRIPTS",
                                L"BLENDKIT_CLIENT_SETTINGS", L"BLENDKIT_SERVER", L"BLENDERKIT_SERVER", L"BLENDKIT_DEBUG",
                                L"BLENDKIT_TOOLS_DIR", L"BLENDER_USER_CONFIG", L"BLENDER_USER_SCRIPTS"}) {
        vars[toUtf8(fs::path(name))] = variable(name);
    }
    entry["vars"] = vars;
}

// Starts a child that sleeps for a minute and writes its id to fake_child.pid next to the program, when
// FAKE_LEAVE_CHILD is set, for any job. Then waits FAKE_LEAVE_CHILD milliseconds, so a test can reach the child.
void leaveChildIfAsked()
{
    const Json wait = variable(L"FAKE_LEAVE_CHILD");
    if (!wait.is_string()) return;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const fs::path lockFile = here() / ("leave-" + std::to_string(GetCurrentProcessId()) + ".lock");
    std::wstring command = L"\"" + std::wstring(self) + L"\" --linger \"" + lockFile.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
        std::ofstream(here() / "fake_child.pid", std::ios::binary | std::ios::trunc) << info.dwProcessId;
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
    Sleep(static_cast<DWORD>(std::atoi(wait.get<std::string>().c_str())));
}

// The --linger child: holds a file open in the fetch folder for a minute.
int lingerChild(const std::wstring& lockFile)
{
    const HANDLE file = CreateFileW(lockFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Sleep(60000);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return 0;
}

// Does what FAKE_FETCH_DO asks. Returns -1 to go on, or an exit code to end with.
int fetchSideEffects(const Json& job)
{
    const fs::path folder = fromUtf8(job["params"].value("private_folder", std::string()));
    if (asked("helper") && !folder.empty()) {
        const fs::path id = folder / "client" / "system_id";
        std::error_code ec;
        if (!fs::exists(id, ec)) put(id, std::to_string(clockMs()));
        std::ifstream in(id, std::ios::binary);
        entry["system_id"] = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        put(folder / "client" / "bin" / "helper.exe", "MZ");
        put(folder / "client" / "default.log", "key=KEY-MARKER-7f31 refresh=TOKEN-MARKER-92ac");
        put(folder / "client" / "client-50000.log", "key=KEY-MARKER-7f31");
    }
    if (asked("linger") && !folder.empty()) {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        const fs::path lockFile = folder / "client" / "helper.lock";
        std::error_code ec;
        fs::create_directories(lockFile.parent_path(), ec);
        std::wstring command = L"\"" + std::wstring(self) + L"\" --linger \"" + lockFile.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
            entry["linger_pid"] = info.dwProcessId;
            std::ofstream(here() / "fake_child.pid", std::ios::binary | std::ios::trunc) << info.dwProcessId;
            CloseHandle(info.hThread);
            CloseHandle(info.hProcess);
            Sleep(300);
        }
    }
    if (asked("unzip") && !folder.empty()) {
        const Json helper = variable(L"FAKE_STORE_HELPER_EXE");
        if (helper.is_string()) {
            const std::wstring exe = fromUtf8(helper.get<std::string>()).wstring();
            std::wstring command = L"\"" + exe + L"\" --port 0 --global_dir \"" + folder.wstring() + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION info{};
            if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) {
                entry["helper_pid"] = info.dwProcessId;
                WaitForSingleObject(info.hProcess, 1500);
                CloseHandle(info.hThread);
                CloseHandle(info.hProcess);
            }
        }
    }
    if (asked("probe")) {
        Json probes = Json::array();
        const Json writes = variable(L"FAKE_PROBE_WRITES");
        std::stringstream list(writes.is_string() ? writes.get<std::string>() : "");
        for (std::string path; std::getline(list, path, ';');) {
            if (!path.empty()) probes.push_back({{"path", path}, {"ok", put(fromUtf8(path), "probe")}});
        }
        const Json reg = variable(L"FAKE_PROBE_REG");
        if (reg.is_string()) {
            HKEY key = nullptr;
            const std::wstring subkey = fromUtf8(reg.get<std::string>()).wstring();
            const bool ok = RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS;
            if (ok) RegCloseKey(key);
            probes.push_back({{"registry", reg}, {"ok", ok}});
        }
        entry["probes"] = probes;
    }
    if (asked("delay")) {
        const Json ms = variable(L"FAKE_FETCH_DELAY_MS");
        Sleep(static_cast<DWORD>(ms.is_string() ? std::atoi(ms.get<std::string>().c_str()) : 1000));
    }
    entry["end_ms"] = clockMs();
    flush();
    if (asked("crash")) return 9;
    return -1;
}

// Keeps a copy of the last fetch result next to the program, because core removes the result file.
void keepResultCopy(const Json& result)
{
    std::ofstream(here() / "fake_result.json", std::ios::binary | std::ios::trunc) << result.dump(2);
}

// Appends one line per start to fake_starts.log: the integrity level and the fetch variables, so a test can
// see that a job other than a fetch got none of the fetch rules.
void noteStart()
{
    Json line{{"integrity", integrity()}, {"private", variable(L"PLYNTH_FETCH_PRIVATE")}, {"system_scripts", variable(L"BLENDER_SYSTEM_SCRIPTS")}};
    std::ofstream(here() / "fake_starts.log", std::ios::binary | std::ios::app) << line.dump() << "\n";
}
