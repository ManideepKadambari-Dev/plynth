#include "FakeBlender.h"
#include "SampleProject.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Resources.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;
namespace fs = std::filesystem;

namespace {

// The skill file that skill install writes into a skills folder.
fs::path skillFile(const fs::path& skills) {
    return skills / "plynth" / "SKILL.md";
}

// The skill template in the docs folder.
std::string skillTemplate() {
    return readFile(fs::path(PLYNTH_DOCS_DIR) / "templates" / "SKILL.md");
}

// A docs folder with its marker file but no templates. Core takes it, so it does not fall back to the source tree.
fs::path docsWithoutTemplates(const TempFolder& temp) {
    const fs::path docs = temp.path() / "no-docs";
    fs::create_directories(docs);
    std::ofstream(docs / plynth::pathFromUtf8(plynth::resourceMarker(plynth::ResourceKind::Docs)), std::ios::binary)
        << "start\n";
    return docs;
}

// The path of the file an answer names, read as UTF-8.
fs::path answerFile(const plynth::cli::testing::Outcome& outcome) {
    return plynth::pathFromUtf8(outcome.answer.at("result").at("file").get<std::string>());
}

}

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

TEST_CASE("project new refuses a folder that already has an AGENTS.md", "[cli][project][agent]") {
    TempFolder temp("agent-keep");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    fs::create_directories(folder);
    std::ofstream(folder / "AGENTS.md", std::ios::binary) << "my own rules\n";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "already_exists");
    CHECK(readFile(folder / "AGENTS.md") == "my own rules\n");
    CHECK_FALSE(fs::exists(folder / "project.json"));
}

TEST_CASE("project new still works when the master copy is missing", "[cli][project][agent]") {
    TempFolder temp("agent-missing");
    const fs::path empty = docsWithoutTemplates(temp);
    const ScopedEnvironment docs("PLYNTH_DOCS", utf8(empty));
    const fs::path folder = temp.path() / "flat";
    const auto outcome = runChecked({"project", "new", "--project", utf8(folder)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(fs::is_regular_file(folder / "project.json"));
    CHECK(outcome.answer.at("result").at("agent_guide") == "skipped");
    REQUIRE(outcome.answer.contains("warnings"));
    CHECK(outcome.answer.at("warnings").at(0).at("code") == "not_found");
    CHECK_FALSE(fs::exists(folder / "AGENTS.md"));
}

TEST_CASE("agent-guide writes, keeps an AGENTS.md that is there and replaces it with --force", "[cli][project][agent]") {
    TempFolder temp("agent-guide");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    REQUIRE(runChecked({"project", "new", "--project", utf8(folder), "--no-agent-guide"}).exitCode == kExitOk);

    const auto first = runChecked({"agent-guide", "--project", utf8(folder)});
    REQUIRE(first.exitCode == kExitOk);
    CHECK(first.answer.at("result").at("agent_guide") == "written");
    CHECK(fs::is_regular_file(folder / "AGENTS.md"));
    CHECK(fs::equivalent(answerFile(first), folder / "AGENTS.md"));

    CHECK_FALSE(first.answer.contains("warnings"));

    std::ofstream(folder / "AGENTS.md", std::ios::binary) << "edited\n";
    const auto kept = runChecked({"agent-guide", "--project", utf8(folder)});
    REQUIRE(kept.exitCode == kExitOk);
    CHECK(kept.answer.at("result").at("agent_guide") == "kept");
    REQUIRE(kept.answer.contains("warnings"));
    CHECK(kept.answer.at("warnings").at(0).at("code") == "already_exists");
    CHECK(readFile(folder / "AGENTS.md") == "edited\n");

    const auto forced = runChecked({"agent-guide", "--project", utf8(folder), "--force"});
    REQUIRE(forced.exitCode == kExitOk);
    CHECK(forced.answer.at("result").at("agent_guide") == "written");
    CHECK_FALSE(forced.answer.contains("warnings"));
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

TEST_CASE("agent-guide refuses when the master copy is missing", "[cli][project][agent]") {
    TempFolder temp("agent-guide-missing");
    const ScopedEnvironment real("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path folder = temp.path() / "flat";
    REQUIRE(runChecked({"project", "new", "--project", utf8(folder), "--no-agent-guide"}).exitCode == kExitOk);
    const fs::path empty = docsWithoutTemplates(temp);
    const ScopedEnvironment docs("PLYNTH_DOCS", utf8(empty));
    const auto outcome = runChecked({"agent-guide", "--project", utf8(folder)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK_FALSE(fs::exists(folder / "AGENTS.md"));
}

TEST_CASE("skill install writes plynth/SKILL.md into the skills folder", "[cli][agent][skill]") {
    TempFolder temp("skill-new");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path skills = temp.path() / "skills";
    fs::create_directories(skills);
    const auto outcome = runChecked({"skill", "install", "--out", "skills"}, temp.path());
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "skill install");
    CHECK(outcome.answer.at("result").at("skill") == "written");
    CHECK_FALSE(outcome.answer.contains("warnings"));
    REQUIRE(fs::is_regular_file(skillFile(skills)));
    CHECK(fs::equivalent(answerFile(outcome), skillFile(skills)));
    CHECK(readFile(skillFile(skills)) == skillTemplate());
}

TEST_CASE("skill install keeps a SKILL.md that is already there", "[cli][agent][skill]") {
    TempFolder temp("skill-keep");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path skills = temp.path() / "skills";
    fs::create_directories(skills / "plynth");
    std::ofstream(skillFile(skills), std::ios::binary) << "my own skill\n";
    const auto outcome = runChecked({"skill", "install", "--out", utf8(skills)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("skill") == "kept");
    REQUIRE(outcome.answer.contains("warnings"));
    CHECK(outcome.answer.at("warnings").at(0).at("code") == "already_exists");
    CHECK(readFile(skillFile(skills)) == "my own skill\n");
}

TEST_CASE("skill install --force replaces a SKILL.md that is already there", "[cli][agent][skill]") {
    TempFolder temp("skill-force");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path skills = temp.path() / "skills";
    fs::create_directories(skills / "plynth");
    std::ofstream(skillFile(skills), std::ios::binary) << "an old skill\n";
    const auto outcome = runChecked({"skill", "install", "--out", utf8(skills), "--force"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("skill") == "written");
    CHECK_FALSE(outcome.answer.contains("warnings"));
    CHECK(readFile(skillFile(skills)) == skillTemplate());
}

TEST_CASE("skill install refuses a skills folder that does not exist", "[cli][agent][skill]") {
    TempFolder temp("skill-missing");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path skills = temp.path() / "skills";
    const auto outcome = runChecked({"skill", "install", "--out", utf8(skills)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK_FALSE(fs::exists(skills));
}

TEST_CASE("skill install refuses a file given as the skills folder", "[cli][agent][skill]") {
    TempFolder temp("skill-file");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const fs::path file = temp.path() / "skills";
    std::ofstream(file, std::ios::binary) << "mine\n";
    const auto outcome = runChecked({"skill", "install", "--out", utf8(file)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(readFile(file) == "mine\n");
}

TEST_CASE("skill install refuses when the skill template is missing", "[cli][agent][skill]") {
    TempFolder temp("skill-template");
    const fs::path empty = docsWithoutTemplates(temp);
    const ScopedEnvironment docs("PLYNTH_DOCS", utf8(empty));
    const fs::path skills = temp.path() / "skills";
    fs::create_directories(skills);
    const auto outcome = runChecked({"skill", "install", "--out", utf8(skills)});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
    CHECK(fs::is_empty(skills));
}

TEST_CASE("skill install needs a folder in --out", "[cli][agent][skill]") {
    TempFolder temp("skill-args");
    const ScopedEnvironment docs("PLYNTH_DOCS", PLYNTH_DOCS_DIR);
    const auto none = runChecked({"skill", "install"}, temp.path());
    CHECK(none.exitCode == kExitBadArguments);
    CHECK(errorCode(none) == "bad_arguments");
    const auto empty = runChecked({"skill", "install", "--out", ""}, temp.path());
    CHECK(empty.exitCode == kExitFailed);
    CHECK(errorCode(empty) == "invalid_value");
    CHECK(fs::is_empty(temp.path()));
}
