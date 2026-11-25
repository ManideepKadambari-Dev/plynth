#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

// A new empty folder under the temp folder, removed at the end.
class TempFolder {
public:
    TempFolder() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string tail = std::to_string(stamp);
        path_ = fs::temp_directory_path() / (std::u8string(u8"plynth-launcher-ü-") + std::u8string(tail.begin(), tail.end()));
        fs::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

struct Outcome {
    DWORD exitCode = 0;
    std::string output;
};

// Runs the launcher in a folder with a command line, with its output going to a file.
Outcome runLauncher(const fs::path& launcher, const std::wstring& arguments, const fs::path& folder) {
    const fs::path outFile = folder / "out.txt";
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE out = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(out != INVALID_HANDLE_VALUE);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = out;
    startup.hStdError = out;

    std::wstring commandLine = L"\"" + launcher.wstring() + L"\" " + arguments;
    PROCESS_INFORMATION process{};
    REQUIRE(CreateProcessW(launcher.c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                           &startup, &process));
    WaitForSingleObject(process.hProcess, 30000);
    Outcome outcome;
    GetExitCodeProcess(process.hProcess, &outcome.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(out);

    std::ifstream in(outFile, std::ios::binary);
    outcome.output.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return outcome;
}

}

TEST_CASE("the launcher forwards the arguments and the exit code", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");

    const auto outcome = runLauncher(temp.path() / "plynth.com", L"version exit=3 \"two words\" ü", temp.path());
    CHECK(outcome.exitCode == 3);
    CHECK(outcome.output == "version\nexit=3\ntwo words\n\xC3\xBC\n");
}

TEST_CASE("the launcher passes on exit code zero", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");

    const auto outcome = runLauncher(temp.path() / "plynth.com", L"help", temp.path());
    CHECK(outcome.exitCode == 0);
    CHECK(outcome.output == "help\n");
}

TEST_CASE("the launcher answers in JSON and exits 1 when plynth.exe is missing", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");

    const auto outcome = runLauncher(temp.path() / "plynth.com", L"version", temp.path());
    CHECK(outcome.exitCode == 1);
    REQUIRE(outcome.output.back() == '\n');
    const auto answer = nlohmann::json::parse(outcome.output);
    CHECK(answer.at("ok") == false);
    CHECK(answer.at("command") == "");
    CHECK(answer.at("error").at("code") == "not_found");
    CHECK_FALSE(answer.at("error").at("message").get<std::string>().empty());
}

TEST_CASE("the launcher returns at once when the window is opened", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");
    const std::wstring existing = temp.path().wstring();

    for (const std::wstring& arguments :
         {existing + L" exit=3 sleep=1500", std::wstring(L"--image plan.png exit=3 sleep=1500"),
          std::wstring(L"--terminal exit=3 sleep=1500"), std::wstring()}) {
        const auto start = std::chrono::steady_clock::now();
        const auto outcome = runLauncher(temp.path() / "plynth.com", arguments, temp.path());
        const auto elapsed = std::chrono::steady_clock::now() - start;
        CHECK(outcome.exitCode == 0);
        CHECK(elapsed < std::chrono::milliseconds(1000));
    }
    // Let the stand-in windows end so the folder can be removed.
    Sleep(2000);
}

TEST_CASE("the launcher answers unknown_command with exit 2 for a mistyped word", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");

    for (const std::wstring& arguments : {std::wstring(L"bogus"), std::wstring(L"--bogus"), std::wstring(L"nothing/here exit=3")}) {
        const auto outcome = runLauncher(temp.path() / "plynth.com", arguments, temp.path());
        CHECK(outcome.exitCode == 2);
        REQUIRE_FALSE(outcome.output.empty());
        const auto answer = nlohmann::json::parse(outcome.output);
        CHECK(answer.at("ok") == false);
        CHECK(answer.at("error").at("code") == "unknown_command");
    }
}

TEST_CASE("the launcher prints the exact unknown_command answer without plynth.exe", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");

    const auto word = runLauncher(temp.path() / "plynth.com", L"bogus", temp.path());
    CHECK(word.exitCode == 2);
    CHECK(word.output ==
          "{\"ok\":false,\"command\":\"\",\"error\":{\"code\":\"unknown_command\",\"message\":"
          "\"Unknown command 'bogus'. Run 'plynth help' to list the commands.\"}}\n");
    const auto pretty = runLauncher(temp.path() / "plynth.com", L"--pretty --bogus", temp.path());
    CHECK(pretty.exitCode == 2);
    CHECK(pretty.output ==
          "{\n  \"ok\": false,\n  \"command\": \"\",\n  \"error\": {\n    \"code\": \"unknown_command\",\n"
          "    \"message\": \"Unknown command '--bogus'. Run 'plynth help' to list the commands.\"\n  }\n}\n");
}


TEST_CASE("the launcher in a folder named cli starts the plynth.exe one folder up", "[launcher]") {
    TempFolder temp;
    const fs::path cli = temp.path() / "app" / "CLI";
    fs::create_directories(cli);
    fs::copy_file(PLYNTH_LAUNCHER_FILE, cli / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "app" / "plynth.exe");

    const auto outcome = runLauncher(cli / "plynth.com", L"version exit=4", temp.path());
    CHECK(outcome.exitCode == 4);
    CHECK(outcome.output == "version\nexit=4\n");
}

TEST_CASE("the launcher in another folder does not look one folder up", "[launcher]") {
    TempFolder temp;
    const fs::path tools = temp.path() / "app" / "tools";
    fs::create_directories(tools);
    fs::copy_file(PLYNTH_LAUNCHER_FILE, tools / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "app" / "plynth.exe");

    const auto outcome = runLauncher(tools / "plynth.com", L"version", temp.path());
    CHECK(outcome.exitCode == 1);
    const auto answer = nlohmann::json::parse(outcome.output);
    CHECK(answer.at("error").at("code") == "not_found");
}

TEST_CASE("Ctrl+Break in the terminal reaches plynth.exe as a stop request and the launcher waits for it", "[launcher]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");

    // A console event needs a console. When the test runs without one, it makes one for this test only.
    const bool madeConsole = GetConsoleWindow() == nullptr && AllocConsole();

    const fs::path outFile = temp.path() / "out.txt";
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE out = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(out != INVALID_HANDLE_VALUE);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = out;
    startup.hStdError = out;
    const fs::path launcher = temp.path() / "plynth.com";
    std::wstring commandLine = L"\"" + launcher.wstring() + L"\" build wait-cancel";
    PROCESS_INFORMATION process{};
    // Its own process group, so the Ctrl+Break below reaches the launcher and its child but not this test.
    REQUIRE(CreateProcessW(launcher.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NEW_PROCESS_GROUP,
                           nullptr, nullptr, &startup, &process));
    Sleep(1500);
    CHECK(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process.dwProcessId));
    const DWORD waited = WaitForSingleObject(process.hProcess, 30000);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(out);
    if (madeConsole) {
        FreeConsole();
    }

    std::ifstream in(outFile, std::ios::binary);
    const std::string output((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    INFO(output);
    CHECK(waited == WAIT_OBJECT_0);
    CHECK(exitCode == 5);
    CHECK(output.find("cancelled\n") != std::string::npos);
    CHECK(output.find("not cancelled") == std::string::npos);
}

namespace {

// Holds a named mutex for the length of a test, as the installer does while it runs.
class HeldMutex {
public:
    explicit HeldMutex(const wchar_t* name) : handle_(CreateMutexW(nullptr, FALSE, name)) { REQUIRE(handle_ != nullptr); }
    ~HeldMutex() { CloseHandle(handle_); }
    HeldMutex(const HeldMutex&) = delete;
    HeldMutex& operator=(const HeldMutex&) = delete;

private:
    HANDLE handle_;
};

// Writes a minimal package.json.
void writePackageFile(const fs::path& folder) {
    std::ofstream(folder / "package.json") << "{}";
}

}

TEST_CASE("a packaged launcher answers update_in_progress while setup runs", "[launcher][update]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");
    writePackageFile(temp.path());

    HeldMutex setup(L"PlynthSetup");
    for (const std::wstring arguments : {std::wstring(L"version"), std::wstring(L"update")}) {
        const auto outcome = runLauncher(temp.path() / "plynth.com", arguments, temp.path());
        CHECK(outcome.exitCode == 1);
        const auto answer = nlohmann::json::parse(outcome.output);
        CHECK(answer.at("ok") == false);
        CHECK(answer.at("error").at("code") == "update_in_progress");
    }
}

TEST_CASE("the launcher in a cli folder is refused when package.json is in the parent", "[launcher][update]") {
    TempFolder temp;
    const fs::path cli = temp.path() / "app" / "cli";
    fs::create_directories(cli);
    fs::copy_file(PLYNTH_LAUNCHER_FILE, cli / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "app" / "plynth.exe");
    writePackageFile(temp.path() / "app");

    HeldMutex setup(L"PlynthSetup");
    const auto outcome = runLauncher(cli / "plynth.com", L"version", temp.path());
    CHECK(outcome.exitCode == 1);
    CHECK(nlohmann::json::parse(outcome.output).at("error").at("code") == "update_in_progress");
}

TEST_CASE("a launcher with no package.json ignores the setup mutex", "[launcher][update]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");

    HeldMutex setup(L"PlynthSetup");
    const auto outcome = runLauncher(temp.path() / "plynth.com", L"version", temp.path());
    CHECK(outcome.exitCode == 0);
    CHECK(outcome.output == "version\n");
}

TEST_CASE("a packaged launcher runs normally when setup is not running", "[launcher][update]") {
    TempFolder temp;
    fs::copy_file(PLYNTH_LAUNCHER_FILE, temp.path() / "plynth.com");
    fs::copy_file(PLYNTH_FAKE_CHILD, temp.path() / "plynth.exe");
    writePackageFile(temp.path());

    const auto outcome = runLauncher(temp.path() / "plynth.com", L"update --status", temp.path());
    CHECK(outcome.exitCode == 0);
    CHECK(outcome.output == "update\n--status\n");
}
