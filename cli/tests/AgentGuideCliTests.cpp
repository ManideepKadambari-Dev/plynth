#include "FakeBlender.h"
#include "SampleProject.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;
namespace fs = std::filesystem;

TEST_CASE("project new writes AGENTS.md from the master copy", "[cli][project][agent]") {
    TempFolder temp("agent-new");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("agent_guide") == "written");
    REQUIRE(fs::is_regular_file(folder / "AGENTS.md"));
    CHECK(readFile(folder / "AGENTS.md") == readFile(fs::path(PLYNTH_DOCS_DIR) / "templates" / "project-agents.md"));
}

TEST_CASE("project new --no-agent-guide writes no AGENTS.md", "[cli][project][agent]") {
    TempFolder temp("agent-none");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder), "--no-agent-guide"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("agent_guide") == "skipped");
    CHECK_FALSE(fs::exists(folder / "AGENTS.md"));
}

TEST_CASE("project new keeps an AGENTS.md that is already there", "[cli][project][agent]") {
    TempFolder temp("agent-keep");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    fs::create_directories(folder);
    std::ofstream(folder / "AGENTS.md", std::ios::binary) << "my own rules\n";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("agent_guide") == "kept");
    CHECK(readFile(folder / "AGENTS.md") == "my own rules\n");
}

TEST_CASE("project new still works when the master copy is missing", "[cli][project][agent]") {
    TempFolder temp("agent-missing");
    const fs::path empty = temp.path() / "no-docs";
    fs::create_directories(empty);
    const ScopedEnvironment docs("PLYNTH_DOCS", utf8(empty));
    const fs::path folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(fs::is_regular_file(folder / "project.json"));
    CHECK_FALSE(fs::exists(folder / "AGENTS.md"));
}

TEST_CASE("agent-guide writes, refuses to overwrite and replaces with --force", "[cli][project][agent]") {
    TempFolder temp("agent-guide");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    REQUIRE(runChecked({"project", "new", "--project", utf8(folder), "--no-agent-guide"}).exitCode == kExitOk);

    const auto first = runChecked({"agent-guide", "--project", utf8(folder)});
    REQUIRE(first.exitCode == kExitOk);
    CHECK(first.answer.at("result").at("agent_guide") == "written");
    CHECK(fs::is_regular_file(folder / "AGENTS.md"));

    std::ofstream(folder / "AGENTS.md", std::ios::binary) << "edited\n";
    const auto refused = runChecked({"agent-guide", "--project", utf8(folder)});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "already_exists");
    CHECK(readFile(folder / "AGENTS.md") == "edited\n");

    const auto forced = runChecked({"agent-guide", "--project", utf8(folder), "--force"});
    REQUIRE(forced.exitCode == kExitOk);
    CHECK(forced.answer.at("result").at("agent_guide") == "written");
    CHECK(readFile(folder / "AGENTS.md") == readFile(fs::path(PLYNTH_DOCS_DIR) / "templates" / "project-agents.md"));
}

TEST_CASE("agent-guide refuses a folder that is not a project", "[cli][project][agent]") {
    TempFolder temp("agent-guide-none");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const auto outcome = runChecked({"agent-guide", "--project", utf8(temp.path())});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_a_project");
    CHECK_FALSE(fs::exists(temp.path() / "AGENTS.md"));
}
