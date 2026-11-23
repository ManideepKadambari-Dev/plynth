#include "FetchTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
using plynth::test::FetchRig;
using plynth::test::ScopedEnv;
namespace fs = std::filesystem;

namespace {

constexpr unsigned long kLowIntegrity = 0x1000;

// What is left in the fetch folder: nothing, or only the helper's kept folder without logs.
bool onlyKeptParts(const fs::path& folder)
{
    for (const std::string& name : plynth::test::listing(folder)) {
        if (name.rfind("client", 0) != 0 || name.ends_with(".log") || name.ends_with(".lock")) return false;
    }
    return true;
}

// Says whether a path is inside a folder, by its parts.
bool inside(const fs::path& path, const fs::path& folder)
{
    const std::string a = pathToUtf8(fs::weakly_canonical(path));
    const std::string b = pathToUtf8(fs::weakly_canonical(folder));
    return a.size() > b.size() && a.compare(0, b.size(), b) == 0;
}

// The ways a fetch can end, with the code it must end with.
struct Ending {
    const char* name;
    const char* kit;
    const char* doing;
    bool stop;
    int timeoutSeconds;
    ErrorCode code;
};

const std::vector<Ending> kEndings{
    {"ok", "", "", false, 0, ErrorCode::None},
    {"failed", "download_failed", "", false, 0, ErrorCode::BlenderFailed},
    {"crash", "", "crash", false, 0, ErrorCode::BlenderFailed},
    {"timeout", "", "delay", false, 2, ErrorCode::BlenderFailed},
    {"cancel", "", "delay", true, 0, ErrorCode::Cancelled}};

// Runs a fetch that ends one way, stopping it after a moment when asked.
FetchRun endOneWay(const FetchRig& rig, const Ending& ending)
{
    const ScopedEnv kit("FAKE_BLENDERKIT", ending.kit);
    const ScopedEnv doing("FAKE_FETCH_DO", std::string(ending.doing) + (*ending.doing ? "+" : "") + "helper+linger");
    const ScopedEnv delay("FAKE_FETCH_DELAY_MS", "10000");
    std::stop_source stop;
    std::jthread stopper([&stop, wanted = ending.stop](std::stop_token done) {
        for (int i = 0; wanted && i < 15 && !done.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (wanted) stop.request_stop();
    });
    return rig.fetch(stop.get_token(), std::chrono::seconds(ending.timeoutSeconds));
}

}

TEST_CASE("a fetch runs at low integrity in its fetch folder with the guard and private variables", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fa");
    const FetchRun run = rig.fetch();
    plynth::test::requireOk(run.status);
    const auto runs = rig.runs();
    REQUIRE(runs.size() == 1);
    const nlohmann::json& seen = runs[0];
    INFO(seen.dump());
    CHECK(seen.at("integrity") == kLowIntegrity);
    CHECK(fs::equivalent(pathFromUtf8(seen.at("private_folder").get<std::string>()), rig.fetchFolder));
    CHECK(inside(pathFromUtf8(seen.at("job").get<std::string>()), rig.fetchFolder));
    CHECK(inside(pathFromUtf8(seen.at("output_folder").get<std::string>()), rig.fetchFolder));
    const nlohmann::json& vars = seen.at("vars");
    CHECK(fs::equivalent(pathFromUtf8(vars.at("PLYNTH_FETCH_PRIVATE").get<std::string>()), rig.fetchFolder));
    CHECK(fs::path(pathFromUtf8(vars.at("TEMP").get<std::string>())) == pathFromUtf8(vars.at("TMP").get<std::string>()));
    CHECK(inside(pathFromUtf8(vars.at("TEMP").get<std::string>()), rig.fetchFolder));
    CHECK_FALSE(vars.at("PYTHONDONTWRITEBYTECODE").is_null());
    const std::string system = vars.at("BLENDER_SYSTEM_SCRIPTS").is_string() ? vars.at("BLENDER_SYSTEM_SCRIPTS").get<std::string>() : "";
    CHECK(fs::equivalent(pathFromUtf8(system.substr(0, system.find(';'))), rig.base.scriptsFolder / "guard"));
    CHECK_FALSE(inside(rig.fetchFolder, rig.work));
    CHECK(run.file == rig.work / "fetched" / "fake_item.blend");
    CHECK(fs::is_regular_file(run.file));
    CHECK(onlyKeptParts(rig.fetchFolder));
}

TEST_CASE("the five helper variables never reach the fetch", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fv");
    const ScopedEnv a("BLENDKIT_CLIENT_SETTINGS", pathToUtf8(rig.tmp.path() / "elsewhere.json"));
    const ScopedEnv b("BLENDKIT_SERVER", "https://store.invalid");
    const ScopedEnv c("BLENDERKIT_SERVER", "https://store.invalid");
    const ScopedEnv d("BLENDKIT_DEBUG", "1");
    const ScopedEnv e("BlendKit_Tools_Dir", pathToUtf8(rig.tmp.path() / "tools"));
    plynth::test::requireOk(rig.fetch().status);
    const nlohmann::json vars = rig.runs().at(0).at("vars");
    for (const char* name : {"BLENDKIT_CLIENT_SETTINGS", "BLENDKIT_SERVER", "BLENDERKIT_SERVER", "BLENDKIT_DEBUG", "BLENDKIT_TOOLS_DIR"}) {
        INFO(name);
        CHECK(vars.at(name).is_null());
    }
}

TEST_CASE("every ending leaves the fetch folder empty but the helper's program and id, and no helper running", "[core][fetch][fetch_folder]")
{
    for (const auto& ending : kEndings) {
        INFO(ending.name);
        FetchRig rig("fe");
        const FetchRun run = endOneWay(rig, ending);
        CHECK(run.status.code == ending.code);
        CHECK(run.status.message.find(pathToUtf8(rig.fetchFolder)) == std::string::npos);
        CHECK(onlyKeptParts(rig.fetchFolder));
        CHECK_FALSE(plynth::test::holdsSecret(rig.fetchFolder));
        CHECK_FALSE(plynth::test::holdsSecret(rig.work));
        const auto runs = rig.runs();
        if (!runs.empty() && runs.back().contains("linger_pid")) {
            const unsigned long pid = runs.back().at("linger_pid");
            CHECK_FALSE(plynth::test::processRunning(pid));
            plynth::test::endProcess(pid);
        }
        if (ending.code != ErrorCode::None) {
            CHECK_FALSE(fs::exists(rig.work / "fetched" / "fake_item.blend"));
        }
    }
}

TEST_CASE("the helper keeps one program and one machine id across fetches", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fk");
    const ScopedEnv doing("FAKE_FETCH_DO", "helper");
    plynth::test::requireOk(rig.fetch().status);
    plynth::test::requireOk(rig.fetch().status);
    const auto runs = rig.runs();
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].at("system_id") == runs[1].at("system_id"));
    CHECK(fs::is_regular_file(rig.fetchFolder / "client" / "bin" / "helper.exe"));
    CHECK(fs::is_regular_file(rig.fetchFolder / "client" / "system_id"));
    CHECK_FALSE(fs::exists(rig.fetchFolder / "client" / "default.log"));
    CHECK_FALSE(fs::exists(rig.fetchFolder / "client" / "client-50000.log"));
}

TEST_CASE("a junction in the fetch folder is removed, never followed", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fj");
    const fs::path outside = rig.tmp.path() / "outside";
    plynth::test::writeBytes(outside / "keep.txt", "keep");
    fs::create_directories(rig.fetchFolder);
    REQUIRE(plynth::test::makeJunction(rig.fetchFolder / "link", outside));
    plynth::test::requireOk(rig.fetch().status);
    CHECK(plynth::test::readTextFile(outside / "keep.txt") == "keep");
    CHECK_FALSE(fs::exists(rig.fetchFolder / "link"));
}

TEST_CASE("a fetch folder longer than 95 bytes stops before Blender, 95 runs", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fl");
    const std::string root = pathToUtf8(rig.fetchFolder.parent_path() / "pl");
    REQUIRE(root.size() < 90);
    for (const std::size_t bytes : {std::size_t(96), std::size_t(95)}) {
        INFO(bytes);
        const std::string folder = root + std::string(bytes - root.size(), 'x');
        REQUIRE(folder.size() == bytes);
        const ScopedEnv named("PLYNTH_FETCH_FOLDER", folder);
        const int before = rig.starts();
        const FetchRun run = rig.fetch();
        if (bytes == 96) {
            CHECK(run.status.code == ErrorCode::CannotWrite);
            CHECK(run.status.message.find("PLYNTH_FETCH_FOLDER") != std::string::npos);
            CHECK(run.status.message.find(folder) == std::string::npos);
            CHECK(rig.starts() == before);
        } else {
            plynth::test::requireOk(run.status);
        }
        std::error_code ec;
        fs::remove_all(pathFromUtf8(folder), ec);
    }
}

TEST_CASE("a fetch without the guard file starts nothing", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fg");
    fs::remove(rig.base.scriptsFolder / "guard" / "startup" / "plynth_fetch_guard.py");
    const FetchRun run = rig.fetch();
    CHECK(run.status.code == ErrorCode::NotFound);
    CHECK(rig.starts() == 0);
}

TEST_CASE("a job with factory startup gets no fetch rules", "[core][fetch][fetch_folder]")
{
    FetchRig rig("fn");
    Json params = Json::object();
    params["source"] = Json{{"file", "D:/in/chair.obj"}, {"format", "obj"}};
    const Json job = makeBlenderJob("cleanup_asset", "chair-1", rig.work / "result.json", params);
    runBlenderJob(rig.blender, job, rig.work, rig.base);
    CHECK(rig.runs().empty());
    const std::string args = plynth::test::readTextFile(rig.blender.parent_path() / "fake_args.log");
    CHECK(args.find("--factory-startup") != std::string::npos);
    std::ifstream in(rig.blender.parent_path() / "fake_starts.log", std::ios::binary);
    std::string line;
    REQUIRE(std::getline(in, line));
    const nlohmann::json start = nlohmann::json::parse(line);
    CHECK(start.at("integrity") != kLowIntegrity);
    CHECK(start.at("private").is_null());
    CHECK_FALSE(fs::exists(rig.fetchFolder));
}

TEST_CASE("at low integrity the fetch cannot write the user's home, the library or the registry", "[core][fetch][low_integrity]")
{
    FetchRig rig("fw");
    const fs::path home = rig.user.tmp.path() / "profile" / "probe.txt";
    const fs::path library = rig.tmp.path() / "Library" / "probe.txt";
    fs::create_directories(library.parent_path());
    const fs::path own = rig.fetchFolder / "probe.txt";
    const std::string key = "Software\\PlynthFetchProbe" + std::to_string(GetCurrentProcessId());
    const ScopedEnv doing("FAKE_FETCH_DO", "probe");
    const ScopedEnv writes("FAKE_PROBE_WRITES", pathToUtf8(home) + ";" + pathToUtf8(library) + ";" + pathToUtf8(own));
    const ScopedEnv reg("FAKE_PROBE_REG", key);
    plynth::test::requireOk(rig.fetch().status);
    RegDeleteTreeW(HKEY_CURRENT_USER, plynth::test::widenUtf8(key).c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, plynth::test::widenUtf8(key).c_str());
    const nlohmann::json probes = rig.runs().at(0).at("probes");
    REQUIRE(probes.size() == 4);
    CHECK(probes[0].at("ok") == false);
    CHECK(probes[1].at("ok") == false);
    CHECK(probes[2].at("ok") == true);
    CHECK(probes[3].at("ok") == false);
    CHECK_FALSE(fs::exists(home));
    CHECK_FALSE(fs::exists(library));
}

TEST_CASE("the user's fake home is unchanged by any ending of a fetch", "[core][fetch][fetch_folder]")
{
    for (const auto& ending : kEndings) {
        INFO(ending.name);
        FetchRig rig("fh");
        plynth::test::writeBytes(rig.user.tmp.path() / "profile" / "blenderkit_data" / "config" / "preferences.json", "{}");
        const auto before = rig.user.fingerprint();
        endOneWay(rig, ending);
        CHECK(rig.user.fingerprint() == before);
    }
}

namespace {

// The start and end of each fetch in milliseconds.
std::vector<std::pair<long long, long long>> spans(const std::vector<nlohmann::json>& runs)
{
    std::vector<std::pair<long long, long long>> out;
    for (const auto& run : runs) out.emplace_back(run.at("start_ms").get<long long>(), run.at("end_ms").get<long long>());
    std::sort(out.begin(), out.end());
    return out;
}

// Says whether two of the spans overlap.
bool overlap(const std::vector<std::pair<long long, long long>>& all)
{
    for (std::size_t i = 1; i < all.size(); ++i) {
        if (all[i].first < all[i - 1].second) return true;
    }
    return false;
}

}

TEST_CASE("two threads never run two fetch Blenders at once", "[core][fetch][fetch_lock]")
{
    FetchRig rig("ft");
    const ScopedEnv doing("FAKE_FETCH_DO", "delay");
    const ScopedEnv delay("FAKE_FETCH_DELAY_MS", "1500");
    std::vector<FetchRun> runs(2);
    {
        std::jthread first([&] { runs[0] = rig.fetch(); });
        std::jthread second([&] { runs[1] = rig.fetch(); });
    }
    plynth::test::requireOk(runs[0].status);
    plynth::test::requireOk(runs[1].status);
    const auto all = spans(rig.runs());
    REQUIRE(all.size() == 2);
    CHECK_FALSE(overlap(all));
}

TEST_CASE("a fetch stopped while it waits for another is cancelled and never starts", "[core][fetch][fetch_lock]")
{
    FetchRig rig("fs");
    const ScopedEnv doing("FAKE_FETCH_DO", "delay");
    const ScopedEnv delay("FAKE_FETCH_DELAY_MS", "3000");
    FetchRun waiting;
    {
        std::jthread first([&] { rig.fetch(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        std::stop_source stop;
        std::jthread second([&] { waiting = rig.fetch(stop.get_token(), std::chrono::seconds(2)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        stop.request_stop();
    }
    CHECK(waiting.status.code == ErrorCode::Cancelled);
    CHECK(rig.runs().size() == 1);
}

namespace {

// Starts this test program running only the hidden child case, with its rig named by variables.
PROCESS_INFORMATION startChild(const FetchRig& rig, const std::string& delayMs)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring line = L"\"" + std::wstring(self) + L"\" \"[.fetch_child]\"";
    const ScopedEnv blender("PLYNTH_TEST_CHILD_BLENDER", pathToUtf8(rig.blender));
    const ScopedEnv scripts("PLYNTH_TEST_CHILD_SCRIPTS", pathToUtf8(rig.base.scriptsFolder));
    const ScopedEnv work("PLYNTH_TEST_CHILD_WORK", pathToUtf8(rig.tmp.path() / ("child" + delayMs + std::to_string(GetTickCount64()))));
    const ScopedEnv doing("FAKE_FETCH_DO", "delay");
    const ScopedEnv delay("FAKE_FETCH_DELAY_MS", delayMs);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    REQUIRE(CreateProcessW(self, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info));
    CloseHandle(info.hThread);
    return info;
}

}

// Runs one fetch for the cases below. Hidden, it runs only when they start it.
TEST_CASE("one fetch for another process", "[.fetch_child]")
{
    char* blender = nullptr;
    std::size_t size = 0;
    _dupenv_s(&blender, &size, "PLYNTH_TEST_CHILD_BLENDER");
    const std::string exe = blender ? blender : "";
    free(blender);
    if (exe.empty()) SKIP("only started by the fetch lock cases");
    char* scripts = nullptr;
    _dupenv_s(&scripts, &size, "PLYNTH_TEST_CHILD_SCRIPTS");
    char* work = nullptr;
    _dupenv_s(&work, &size, "PLYNTH_TEST_CHILD_WORK");
    BlenderRunOptions base;
    base.scriptsFolder = pathFromUtf8(scripts ? scripts : "");
    const fs::path folder = pathFromUtf8(work ? work : "");
    free(scripts);
    free(work);
    const FetchRun run = runImportFetch(makeBlenderKitFetch(plynth::test::kFetchItem, kFetchKindModel, BlenderKitOptions{}),
                                        pathFromUtf8(exe), base, folder);
    REQUIRE(run.status.ok());
}

TEST_CASE("two processes never run two fetch Blenders at once", "[core][fetch][fetch_lock]")
{
    FetchRig rig("fp");
    const PROCESS_INFORMATION a = startChild(rig, "1500");
    const PROCESS_INFORMATION b = startChild(rig, "1500");
    for (const HANDLE process : {a.hProcess, b.hProcess}) {
        CHECK(WaitForSingleObject(process, 60000) == WAIT_OBJECT_0);
        DWORD code = 1;
        GetExitCodeProcess(process, &code);
        CHECK(code == 0);
        CloseHandle(process);
    }
    const auto all = spans(rig.runs());
    REQUIRE(all.size() == 2);
    CHECK_FALSE(overlap(all));
}

TEST_CASE("a process that dies during a fetch frees the next one", "[core][fetch][fetch_lock]")
{
    FetchRig rig("fc");
    const PROCESS_INFORMATION holder = startChild(rig, "60000");
    for (int i = 0; i < 200 && rig.starts() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(rig.starts() > 0);
    TerminateProcess(holder.hProcess, 1);
    WaitForSingleObject(holder.hProcess, 10000);
    CloseHandle(holder.hProcess);
    const auto began = std::chrono::steady_clock::now();
    plynth::test::requireOk(rig.fetch().status);
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(30));
}

TEST_CASE("a helper that unzips entries with .. or a full path writes nothing outside the fetch folder", "[core][fetch][low_integrity]")
{
    FetchRig rig("fz");
    const fs::path home = rig.user.tmp.path() / "profile" / "dropped.txt";
    const fs::path scenario = rig.tmp.path() / "scenario.json";
    const fs::path log = rig.tmp.path() / "helper.log";
    nlohmann::json entries = nlohmann::json::array();
    entries.push_back({{"name", "../../up.txt"}, {"text", "x"}});
    entries.push_back({{"name", "..\\..\\..\\up2.txt"}, {"text", "x"}});
    entries.push_back({{"name", pathToUtf8(home)}, {"text", "x"}});
    entries.push_back({{"name", "client/inside.txt"}, {"text", "x"}});
    plynth::test::writeBytes(scenario, nlohmann::json{{"unzip_at_start", entries}}.dump());
    const ScopedEnv doing("FAKE_FETCH_DO", "unzip");
    const ScopedEnv exe("FAKE_STORE_HELPER_EXE", PLYNTH_FAKE_STORE_HELPER);
    const ScopedEnv named("FAKE_STORE_SCENARIO", pathToUtf8(scenario));
    const ScopedEnv logged("FAKE_STORE_LOG", pathToUtf8(log));
    const fs::path up = rig.fetchFolder.parent_path().parent_path() / "up.txt";
    const fs::path up2 = rig.fetchFolder.parent_path().parent_path().parent_path() / "up2.txt";
    plynth::test::requireOk(rig.fetch().status);
    CHECK_FALSE(fs::exists(home));
    CHECK_FALSE(fs::exists(up));
    CHECK_FALSE(fs::exists(up2));
    std::error_code ec;
    fs::remove(up, ec);
    fs::remove(up2, ec);
    std::ifstream in(log, std::ios::binary);
    int wrote = 0;
    int allowed = 0;
    for (std::string line; std::getline(in, line);) {
        const nlohmann::json seen = nlohmann::json::parse(line);
        if (seen.value("op", "") != "wrote") continue;
        ++wrote;
        if (seen.at("ok") == true) ++allowed;
    }
    CHECK(wrote == 4);
    CHECK(allowed == 1);
    const nlohmann::json run = rig.runs().at(0);
    REQUIRE(run.contains("helper_pid"));
    CHECK_FALSE(plynth::test::processRunning(run.at("helper_pid").get<unsigned long>()));
    plynth::test::endProcess(run.at("helper_pid").get<unsigned long>());
    CHECK(onlyKeptParts(rig.fetchFolder));
}

namespace {

const char* const kHelpersLeft = "Blender's helper programs could not all be ended.";

// A probe job for runBlenderJob, with user folders in the rig's temporary folder, which the fake answers with a
// good result.
Json probeJob(const FetchRig& rig)
{
    BlenderUserFolders folders;
    folders.config = rig.tmp.path() / "user" / "config";
    folders.scripts = rig.tmp.path() / "user" / "scripts";
    folders.extensions = rig.tmp.path() / "user" / "extensions";
    folders.blenderKitData = rig.tmp.path() / "user" / "blenderkit_data";
    return makeProbeJob(folders, "probe-00000001", rig.work / "result.json");
}

}

TEST_CASE("every runBlenderJob job waits until the programs Blender started are gone", "[core][blender][end_children]")
{
    FetchRig rig("fx");
    const ScopedEnv leave("FAKE_LEAVE_CHILD", "1500");
    const fs::path pidFile = rig.blender.parent_path() / "fake_child.pid";
    std::unique_ptr<plynth::test::HoldExit> hold;
    std::jthread watcher([&] {
        const unsigned long pid = plynth::test::waitForPid(pidFile, std::chrono::seconds(30));
        if (pid != 0) hold = std::make_unique<plynth::test::HoldExit>(pid, std::chrono::seconds(5));
    });
    const auto began = std::chrono::steady_clock::now();
    const BlenderJobRun run = runBlenderJob(rig.blender, probeJob(rig), rig.work, rig.base);
    const auto took = std::chrono::steady_clock::now() - began;
    watcher.join();
    REQUIRE(hold);
    REQUIRE(hold->attached());
    plynth::test::requireOk(run.status);
    CHECK(took >= std::chrono::milliseconds(4500));
    const unsigned long pid = plynth::test::waitForPid(pidFile, std::chrono::seconds(1));
    CHECK_FALSE(plynth::test::processRunning(pid));
    hold.reset();
    plynth::test::endProcess(pid);
}

TEST_CASE("a helper Blender left that cannot be ended fails the job and removes its good result", "[core][blender][end_children]")
{
    FetchRig rig("fy");
    const ScopedEnv leave("FAKE_LEAVE_CHILD", "1500");
    const fs::path pidFile = rig.blender.parent_path() / "fake_child.pid";
    std::unique_ptr<plynth::test::HoldExit> hold;
    std::jthread watcher([&] {
        const unsigned long pid = plynth::test::waitForPid(pidFile, std::chrono::seconds(30));
        if (pid != 0) hold = std::make_unique<plynth::test::HoldExit>(pid, std::chrono::seconds(75));
    });
    const BlenderJobRun run = runBlenderJob(rig.blender, probeJob(rig), rig.work, rig.base);
    watcher.join();
    REQUIRE(hold);
    REQUIRE(hold->attached());
    CHECK(run.status.code == ErrorCode::BlenderFailed);
    CHECK(run.status.message == kHelpersLeft);
    CHECK_FALSE(fs::exists(rig.work / "result.json"));
    hold.reset();
    plynth::test::endProcess(plynth::test::waitForPid(pidFile, std::chrono::seconds(1)));
}

TEST_CASE("a fetch whose helper cannot be ended brings nothing, and the next fetch empties the folder first", "[core][fetch][end_children]")
{
    FetchRig rig("fq");
    const fs::path pidFile = rig.blender.parent_path() / "fake_child.pid";
    std::unique_ptr<plynth::test::HoldExit> hold;
    FetchRun first;
    {
        const ScopedEnv doing("FAKE_FETCH_DO", "helper+linger+delay");
        const ScopedEnv delay("FAKE_FETCH_DELAY_MS", "3000");
        std::jthread watcher([&] {
            const unsigned long pid = plynth::test::waitForPid(pidFile, std::chrono::seconds(30));
            if (pid != 0) hold = std::make_unique<plynth::test::HoldExit>(pid, std::chrono::seconds(75));
        });
        first = rig.fetch();
    }
    REQUIRE(hold);
    REQUIRE(hold->attached());
    CHECK(first.status.code == ErrorCode::BlenderFailed);
    CHECK(first.status.message == kHelpersLeft);
    CHECK_FALSE(fs::exists(rig.work / "fetched"));
    const unsigned long pid = plynth::test::waitForPid(pidFile, std::chrono::seconds(1));
    hold.reset();
    plynth::test::endProcess(pid);
    fs::remove(pidFile);

    const ScopedEnv doing("FAKE_FETCH_DO", "probe");
    const ScopedEnv writes("FAKE_PROBE_WRITES", "");
    plynth::test::requireOk(rig.fetch().status);
    CHECK(onlyKeptParts(rig.fetchFolder));
    CHECK_FALSE(fs::exists(rig.fetchFolder / "client" / "helper.lock"));
}

TEST_CASE("an older empty fetch folder without a label is labelled as new and used", "[core][fetch][check_folder]")
{
    FetchRig rig("fo");
    fs::create_directories(rig.fetchFolder);
    const ScopedEnv doing("FAKE_FETCH_DO", "probe");
    const ScopedEnv writes("FAKE_PROBE_WRITES", pathToUtf8(rig.fetchFolder / "client" / "probe.txt"));
    plynth::test::requireOk(rig.fetch().status);
    const nlohmann::json probes = rig.runs().at(0).at("probes");
    REQUIRE(probes.size() == 1);
    CHECK(probes[0].at("ok") == true);
}

TEST_CASE("a fetch folder with files and another label fails the fetch and is not relabelled", "[core][fetch][check_folder]")
{
    FetchRig rig("fr");
    plynth::test::writeBytes(rig.fetchFolder / "client" / "system_id", "kept");
    const FetchRun run = rig.fetch();
    CHECK(run.status.code == ErrorCode::CannotWrite);
    CHECK(run.status.message == "Plynth's fetch folder is not set up as expected. Delete it and try again.");
    CHECK(rig.starts() == 0);
    CHECK(plynth::test::readTextFile(rig.fetchFolder / "client" / "system_id") == "kept");
}
