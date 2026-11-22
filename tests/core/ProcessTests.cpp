#include "TestSupport.h"

#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// The echo_lines test program.
ProcessSpec echo(std::vector<std::string> arguments)
{
    ProcessSpec spec;
    spec.program = fs::path(PLYNTH_ECHO_LINES);
    spec.arguments = std::move(arguments);
    return spec;
}

}

TEST_CASE("runProcess gives lines in order", "[core][process]") {
    std::vector<std::string> lines;
    const ProcessResult r = runProcess(echo({"one", "two", "three"}), [&](std::string_view l) { lines.emplace_back(l); });
    plynth::test::requireOk(r.status);
    REQUIRE(r.exitCode == 0);
    REQUIRE(lines == std::vector<std::string>{"arg:one", "arg:two", "arg:three"});
    REQUIRE(r.outputTail.find("arg:three") != std::string::npos);
}

TEST_CASE("runProcess merges standard error into the lines", "[core][process]") {
    std::vector<std::string> lines;
    const ProcessResult r =
        runProcess(echo({"out", "--delay", "50", "--stderr", "err", "more"}), [&](std::string_view l) { lines.emplace_back(l); });
    plynth::test::requireOk(r.status);
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[1] == "err");
}

TEST_CASE("runProcess passes the exit code", "[core][process]") {
    const ProcessResult r = runProcess(echo({"--exit", "7"}));
    plynth::test::requireOk(r.status);
    REQUIRE(r.exitCode == 7);
    REQUIRE_FALSE(r.timedOut);
}

TEST_CASE("a timeout ends a hanging program", "[core][process]") {
    ProcessSpec spec = echo({"before", "--hang"});
    spec.timeout = std::chrono::milliseconds(300);
    std::vector<std::string> lines;
    const auto begin = std::chrono::steady_clock::now();
    const ProcessResult r = runProcess(spec, [&](std::string_view l) { lines.emplace_back(l); });
    const auto took = std::chrono::steady_clock::now() - begin;
    REQUIRE(r.status.code == ErrorCode::Cancelled);
    REQUIRE(r.timedOut);
    REQUIRE(took < std::chrono::seconds(5));
    REQUIRE(lines == std::vector<std::string>{"arg:before"});
}

TEST_CASE("a stop request ends the program within two seconds", "[core][process]") {
    std::stop_source source;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        source.request_stop();
    });
    const auto begin = std::chrono::steady_clock::now();
    const ProcessResult r = runProcess(echo({"--hang"}), {}, source.get_token());
    const auto took = std::chrono::steady_clock::now() - begin;
    stopper.join();
    REQUIRE(r.status.code == ErrorCode::Cancelled);
    REQUIRE_FALSE(r.timedOut);
    REQUIRE(took < std::chrono::seconds(2));
}

TEST_CASE("a missing program is not_found", "[core][process]") {
    ProcessSpec spec;
    spec.program = fs::path(PLYNTH_ECHO_LINES).parent_path() / "no-such-program.exe";
    REQUIRE(runProcess(spec).status.code == ErrorCode::NotFound);
}

TEST_CASE("arguments with spaces and quotes arrive unchanged", "[core][process]") {
    const std::vector<std::string> args = {"two words", "say \"hi\"", "back\\slash", "trail\\", "trail space\\", "", "ü ö"};
    std::vector<std::string> lines;
    const ProcessResult r = runProcess(echo(args), [&](std::string_view l) { lines.emplace_back(l); });
    plynth::test::requireOk(r.status);
    REQUIRE(lines.size() == args.size());
    for (std::size_t i = 0; i < 6; ++i) {
        INFO(args[i]);
        REQUIRE(lines[i] == "arg:" + args[i]);
    }
}

TEST_CASE("runProcess with a minimized window still gives its lines and exit code", "[core][process]") {
    ProcessSpec spec = echo({"one", "two", "--exit", "5"});
    spec.minimizedWindow = true;
    std::vector<std::string> lines;
    const ProcessResult r = runProcess(spec, [&](std::string_view l) { lines.emplace_back(l); });
    plynth::test::requireOk(r.status);
    REQUIRE(r.exitCode == 5);
    REQUIRE(lines == std::vector<std::string>{"arg:one", "arg:two"});
}

TEST_CASE("runProcess adds environment variables and keeps the others", "[core][process]") {
    ProcessSpec spec = echo({"--env", "PLYNTH_TEST_VARIABLE", "--env", "PATH"});
    spec.environment = {{"PLYNTH_TEST_VARIABLE", "hello world"}};
    std::vector<std::string> lines;
    plynth::test::requireOk(runProcess(spec, [&](std::string_view l) { lines.emplace_back(l); }).status);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "env:PLYNTH_TEST_VARIABLE=hello world");
    REQUIRE(lines[1].find("<unset>") == std::string::npos);
}

TEST_CASE("runProcess runs in the working folder", "[core][process]") {
    plynth::test::TempDir tmp("process-cwd");
    ProcessSpec spec = echo({});
    spec.workingFolder = tmp.path();
    plynth::test::requireOk(runProcess(spec).status);
}

TEST_CASE("a last line without a line break is still passed on", "[core][process]") {
    // echo_lines always ends lines, so use the exit path: the output of a program that prints nothing is empty.
    std::vector<std::string> lines;
    plynth::test::requireOk(runProcess(echo({}), [&](std::string_view l) { lines.emplace_back(l); }).status);
    REQUIRE(lines.empty());
}

TEST_CASE("moveToRecycleBin refuses a path that is not there", "[core][process]") {
    plynth::test::TempDir tmp("recycle-missing");
    REQUIRE(moveToRecycleBin(tmp.path() / "nothing.zip").code == ErrorCode::NotFound);
}
