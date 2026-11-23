// A stand-in for BlenderKit's helper program (bk_client) in the fetch tests. It listens on 127.0.0.1 and takes
// one JSON line for each connection, answering with one JSON line:
//   {"op": "report"}                       the tasks to hand out, each finished one only once
//   {"op": "asset_download", "data": {}}   starts a download, answers {"task_id": "..."}
//   {"op": "search", "asset_base_id": ""}  answers {"results": [...]} from the scenario, after search_delay_ms
// A download copies the scenario's download.source to a partial file, renames it to the item's .blend, and
// writes download.unzip entries without a check when asset_data's files hold a zip.
//   {"op": "refresh_token" | "oauth2_logout" | "settings" | "cancel" | "shutdown" | "unsubscribe"}
// Arguments as the add-on gives them: --port <port> --global_dir <folder>. It acts by the scenario file that
// FAKE_STORE_SCENARIO names (see blender/tests/blenderkit_edge_helpers.py) and appends one JSON line for every
// connection and request to the file that FAKE_STORE_LOG names, so a test can count what left. As the real
// one it writes <global_dir>/client/system_id when that is missing, its settings file beside itself, and its
// own output to whatever it was given. A download before the first report ends it with exit code 9. The
// scenario's unzip_at_start entries are written at start, joined to global_dir without a check, as an unzip of
// a hostile zip would. die_after_reports ends it after that many reports; download.fake_path is reported as
// the file instead of the one written. tray shows a tray icon and notes whether that worked.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <nlohmann/json.hpp>

#include <crtdbg.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

std::mutex lock;
Json scenario = Json::object();
Json queued = Json::array();
Json running = Json::object();
std::atomic<bool> reported{false};
std::atomic<long long> lastReport{0};
std::string port;
fs::path globalDir;

// A path from UTF-8 text.
fs::path pathFrom(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

// UTF-8 text of a path.
std::string textOf(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The value of a variable, or empty.
std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, name);
    std::string out = value ? value : "";
    std::free(value);
    return out;
}

// Milliseconds since the program started.
long long now()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// Appends one line to the request log.
void note(Json entry)
{
    const std::string file = environment("FAKE_STORE_LOG");
    if (file.empty()) return;
    entry["port"] = port;
    std::lock_guard guard(lock);
    std::ofstream(pathFrom(file), std::ios::binary | std::ios::app) << entry.dump() << "\n";
}

// Random hex digits.
std::string randomHex(int digits)
{
    static std::mt19937_64 engine{std::random_device{}()};
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < digits; ++i) out += hex[engine() % 16];
    return out;
}

// The add-on's slug of a name: lower case, other characters one hyphen, at most the given length, no hyphen at
// either end after the cut.
std::string slug(const std::string& name, std::size_t length)
{
    std::string out;
    for (const unsigned char c : name) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (out.empty() || out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(0, 1);
    if (out.size() > length) out.resize(length);
    return out;
}

// Writes text into a file without any check of the path, as the real helper's unzip does.
void writeAnywhere(const fs::path& file, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    note({{"op", "wrote"}, {"path", textOf(file)}, {"ok", static_cast<bool>(out)}});
}

// Runs one download as the scenario says and queues its finished or failed task.
void download(const std::string& taskId, const Json& data)
{
    const Json plan = scenario.value("download", Json::object());
    const Json asset = data.value("asset_data", Json::object());
    Json task{{"task_type", "asset_download"}, {"task_id", taskId}, {"status", "running"}, {"progress", 0},
              {"data", {{"asset_data", {{"assetBaseId", asset.value("assetBaseId", "")}, {"id", asset.value("id", "")}}}}}};
    {
        std::lock_guard guard(lock);
        running[taskId] = task;
    }
    const auto finish = [&](Json done) {
        std::lock_guard guard(lock);
        running.erase(taskId);
        queued.push_back(std::move(done));
    };
    if (plan.value("never", false)) return;
    const Json dirs = data.value("download_dirs", Json::array());
    const fs::path dir = pathFrom(dirs.is_array() && !dirs.empty() && dirs[0].is_string() ? dirs[0].get<std::string>() : std::string());
    const std::string name = asset.value("name", std::string("item"));
    const fs::path item = dir / (slug(name, 16) + "_" + asset.value("id", std::string()));
    bool zipped = false;
    for (const Json& file : asset.value("files", Json::array())) {
        if (file.value("fileType", std::string()).find("zip") != std::string::npos) zipped = true;
    }
    for (const Json& entry : zipped ? plan.value("unzip", Json::array()) : Json::array()) {
        writeAnywhere(item / pathFrom(entry.value("name", std::string())), entry.value("text", std::string("x")));
    }
    std::error_code ec;
    fs::create_directories(item, ec);
    const fs::path partial = item / (".blendkit-download-" + randomHex(8));
    {
        std::ofstream(partial, std::ios::binary) << "PART";
    }
    const int stall = plan.value("stall_s", 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(plan.value("partial_ms", 300)));
    if (stall >= 30) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        fs::remove(partial, ec);
        Json failed = task;
        failed["status"] = "error";
        failed["message"] = "Download stalled.";
        finish(failed);
        return;
    }
    if (stall > 0) std::this_thread::sleep_for(std::chrono::seconds(stall));
    if (plan.contains("error")) {
        fs::remove(partial, ec);
        Json failed = task;
        failed["status"] = "error";
        failed["message"] = plan["error"];
        finish(failed);
        return;
    }
    const fs::path final = item / (slug(name, 50) + "_2K_" + randomHex(8) + "-" + randomHex(4) + ".blend");
    fs::copy_file(pathFrom(plan.value("source", std::string())), partial, fs::copy_options::overwrite_existing, ec);
    fs::rename(partial, final, ec);
    note({{"op", "delivered"}, {"path", textOf(final)}, {"ok", !ec}});
    if (plan.value("silent", false)) {
        std::lock_guard guard(lock);
        running.erase(taskId);
        return;
    }
    Json done = task;
    done["status"] = "finished";
    done["progress"] = 100;
    const std::string reported = plan.contains("fake_path") ? plan["fake_path"].get<std::string>() : textOf(final);
    done["result"] = Json::object({{"file_paths", Json::array({reported})}});
    finish(done);
}

// Answers one request line.
Json answer(const Json& request)
{
    const std::string op = request.value("op", "");
    Json entry{{"op", op}};
    if (op == "asset_download") entry["data"] = request.value("data", Json::object());
    if (op == "search") entry["asset_base_id"] = request.value("asset_base_id", "");
    if (op == "settings") entry["values"] = request.value("values", Json::object());
    note(entry);
    if (op == "report") {
        lastReport = now();
        static int reports = 0;
        if (scenario.contains("die_after_reports") && ++reports > scenario["die_after_reports"].get<int>()) {
            note({{"op", "died"}});
            std::exit(1);
        }
        std::lock_guard guard(lock);
        Json tasks = Json::array();
        if (!reported.exchange(true)) {
            for (const Json& first : scenario.value("first_tasks", Json::array())) tasks.push_back(first);
        }
        for (const Json& done : queued) tasks.push_back(done);
        queued = Json::array();
        for (const auto& [id, task] : running.items()) tasks.push_back(task);
        return {{"tasks", tasks}};
    }
    if (op == "asset_download") {
        if (!reported) {
            note({{"op", "crashed"}});
            std::exit(9);
        }
        if (scenario.value("log_secrets", false)) {
            std::cout << "request " << request.dump() << std::endl;
        }
        const std::string id = "t" + randomHex(8);
        std::thread(download, id, request.value("data", Json::object())).detach();
        return {{"task_id", id}};
    }
    if (op == "search") {
        std::this_thread::sleep_for(std::chrono::milliseconds(scenario.value("search_delay_ms", 0)));
        return {{"results", scenario.value("search", Json::array())}};
    }
    if (op == "settings") {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::ofstream(fs::path(self).parent_path() / "bk_client-settings.json", std::ios::binary) << request.dump();
    }
    if (op == "shutdown") std::exit(0);
    return {{"ok", true}};
}

}

// Shows a tray icon as the real helper does, from a hidden window on its own thread, and notes whether it worked.
void showTrayIcon()
{
    std::thread([]() {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"FakeStoreHelperTray";
        RegisterClassW(&type);
        const HWND window = CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, nullptr);
        NOTIFYICONDATAW icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = 1;
        icon.uFlags = NIF_ICON | NIF_TIP;
        // 32512 is IDI_APPLICATION, whose macro is the narrow form in this build.
        icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        wcscpy_s(icon.szTip, L"Fake store helper");
        const bool shown = window != nullptr && Shell_NotifyIconW(NIM_ADD, &icon);
        note({{"op", "tray"}, {"ok", shown}});
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    }).detach();
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

int main(int argc, char** argv)
{
    quietFailures();
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port") port = argv[i + 1];
        if (arg == "--global_dir") globalDir = pathFrom(argv[i + 1]);
    }
    const std::string scenarioFile = environment("FAKE_STORE_SCENARIO");
    if (!scenarioFile.empty()) {
        std::ifstream in(pathFrom(scenarioFile), std::ios::binary);
        scenario = Json::parse(in, nullptr, false);
        if (!scenario.is_object()) scenario = Json::object();
    }
    if (!globalDir.empty()) {
        const fs::path id = globalDir / "client" / "system_id";
        std::error_code ec;
        if (!fs::exists(id, ec)) {
            fs::create_directories(id.parent_path(), ec);
            std::ofstream(id, std::ios::binary) << randomHex(32);
        }
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    const SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(std::stoi(port.empty() ? "0" : port)));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 16) != 0) {
        note({{"op", "port_in_use"}});
        return 43;
    }
    Json env = Json::object();
    for (const char* name : {"USERPROFILE", "HOME", "APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "TEMP", "TMP"}) {
        env[name] = environment(name);
    }
    note({{"op", "started"}, {"global_dir", textOf(globalDir)}, {"env", env}});
    if (scenario.value("tray", false)) showTrayIcon();
    for (const Json& entry : scenario.value("unzip_at_start", Json::array())) {
        writeAnywhere(globalDir / pathFrom(entry.value("name", std::string())), entry.value("text", std::string("x")));
    }
    const int linger = scenario.value("linger_s", 3);
    std::thread([linger]() {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (reported && now() - lastReport > linger * 1000LL) std::exit(0);
        }
    }).detach();

    for (;;) {
        const SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        note({{"op", "connect"}});
        std::string line;
        char buffer[4096];
        int got = 0;
        while (line.find('\n') == std::string::npos && (got = recv(client, buffer, sizeof(buffer), 0)) > 0) line.append(buffer, got);
        const Json request = Json::parse(line.substr(0, line.find('\n')), nullptr, false);
        const std::string reply = (request.is_object() ? answer(request) : Json{{"error", "bad request"}}).dump() + "\n";
        send(client, reply.data(), static_cast<int>(reply.size()), 0);
        closesocket(client);
    }
}
