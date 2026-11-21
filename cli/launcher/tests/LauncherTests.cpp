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

