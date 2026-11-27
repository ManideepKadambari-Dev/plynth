#include "TestSupport.h"

#include <plynth/core/AgentFiles.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Resources.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

using namespace plynth;
using plynth::test::ScopedEnv;
using plynth::test::TempDir;
namespace fs = std::filesystem;

namespace {

// Writes a file with the given bytes, creating its folder.
void writeFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

// The whole file as text, or empty when it cannot be read.
std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A docs folder of its own with both templates, found through PLYNTH_DOCS while the object lives.
struct DocsFixture {
    explicit DocsFixture(const std::string& tag) : tmp(tag)
    {
        docs = tmp.path() / "docs";
        writeFile(docs / pathFromUtf8(resourceMarker(ResourceKind::Docs)), "start\n");
        writeFile(docs / pathFromUtf8(kAgentGuideTemplate), "rules\n");
        writeFile(docs / pathFromUtf8(kSkillTemplate), "skill\n");
        variable = std::make_unique<ScopedEnv>(std::string(kDocsVariable), pathToUtf8(docs));
    }

    TempDir tmp;
    fs::path docs;
    std::unique_ptr<ScopedEnv> variable;
};

// The text of a file of the source tree's docs/templates folder.
std::string sourceTemplate(std::string_view name)
{
    return readFile(fs::path(PLYNTH_SOURCE_DIR) / "docs" / pathFromUtf8(name));
}

}

TEST_CASE("writeAgentGuide copies the template into the project folder", "[core][agentfiles]")
{
    DocsFixture f("agents-write");
    const fs::path project = f.tmp.path() / "Cottage";
    fs::create_directories(project);

    const AgentFileWrite first = writeAgentGuide(project, false);
    plynth::test::requireOk(first.status);
    CHECK(first.result == AgentFileResult::Written);
    CHECK(first.file == fs::absolute(project / "AGENTS.md").lexically_normal());
    CHECK(first.file.is_absolute());
    CHECK(readFile(project / "AGENTS.md") == "rules\n");
    CHECK_FALSE(fs::exists(project / "AGENTS.md.tmp"));
}

TEST_CASE("writeAgentGuide keeps an AGENTS.md that is there", "[core][agentfiles]")
{
    DocsFixture f("agents-keep");
    const fs::path project = f.tmp.path() / "Cottage";

    SECTION("one the user wrote")
    {
        writeFile(project / "AGENTS.md", "mine\n");
        const AgentFileWrite kept = writeAgentGuide(project, false);
        plynth::test::requireOk(kept.status);
        CHECK(kept.result == AgentFileResult::Kept);
        CHECK(kept.file.is_absolute());
        CHECK(readFile(project / "AGENTS.md") == "mine\n");
    }
    SECTION("a second write after the template changed")
    {
        fs::create_directories(project);
        plynth::test::requireOk(writeAgentGuide(project, false).status);
        writeFile(f.docs / pathFromUtf8(kAgentGuideTemplate), "newer rules\n");
        const AgentFileWrite second = writeAgentGuide(project, false);
        plynth::test::requireOk(second.status);
        CHECK(second.result == AgentFileResult::Kept);
        CHECK(readFile(project / "AGENTS.md") == "rules\n");
    }
    SECTION("kept even when the template is missing")
    {
        writeFile(project / "AGENTS.md", "mine\n");
        fs::remove(f.docs / pathFromUtf8(kAgentGuideTemplate));
        const AgentFileWrite kept = writeAgentGuide(project, false);
        plynth::test::requireOk(kept.status);
        CHECK(kept.result == AgentFileResult::Kept);
    }
}

TEST_CASE("writeAgentGuide with overwrite replaces an AGENTS.md", "[core][agentfiles]")
{
    DocsFixture f("agents-force");
    const fs::path project = f.tmp.path() / "Cottage";
    writeFile(project / "AGENTS.md", "mine\n");

    const AgentFileWrite replaced = writeAgentGuide(project, true);
    plynth::test::requireOk(replaced.status);
    CHECK(replaced.result == AgentFileResult::Written);
    CHECK(readFile(project / "AGENTS.md") == "rules\n");
}

TEST_CASE("writeAgentGuide refuses without a template or a folder", "[core][agentfiles]")
{
    DocsFixture f("agents-refuse");
    const fs::path project = f.tmp.path() / "Cottage";

    SECTION("no template")
    {
        fs::create_directories(project);
        fs::remove(f.docs / pathFromUtf8(kAgentGuideTemplate));
        const AgentFileWrite missing = writeAgentGuide(project, false);
        CHECK(missing.status.code == ErrorCode::NotFound);
        CHECK(missing.result == AgentFileResult::Kept);
        CHECK_FALSE(fs::exists(project / "AGENTS.md"));
    }
    SECTION("a project folder that is not there")
    {
        const AgentFileWrite failed = writeAgentGuide(project, false);
        CHECK(failed.status.code == ErrorCode::CannotWrite);
        CHECK(failed.result == AgentFileResult::Kept);
        CHECK_FALSE(fs::exists(project));
    }
    SECTION("no folder at all")
    {
        const AgentFileWrite failed = writeAgentGuide({}, false);
        CHECK(failed.status.code == ErrorCode::CannotWrite);
        CHECK_FALSE(fs::exists(fs::current_path() / "AGENTS.md.tmp"));
    }
}

TEST_CASE("writeAgentGuide writes the shipped template unchanged", "[core][agentfiles]")
{
    const ScopedEnv variable(std::string(kDocsVariable), pathToUtf8(fs::path(PLYNTH_SOURCE_DIR) / "docs"));
    TempDir tmp("agents-shipped");
    const AgentFileWrite written = writeAgentGuide(tmp.path(), false);
    plynth::test::requireOk(written.status);
    CHECK(written.result == AgentFileResult::Written);
    const std::string text = sourceTemplate(kAgentGuideTemplate);
    REQUIRE_FALSE(text.empty());
    CHECK(readFile(tmp.path() / "AGENTS.md") == text);
}

TEST_CASE("installSkill writes plynth/SKILL.md into the skills folder", "[core][agentfiles]")
{
    DocsFixture f("skill-write");
    const fs::path skills = f.tmp.path() / "skills";
    fs::create_directories(skills);
    const fs::path file = skills / "plynth" / "SKILL.md";

    const AgentFileWrite first = installSkill(skills, false);
    plynth::test::requireOk(first.status);
    CHECK(first.result == AgentFileResult::Written);
    CHECK(first.file == fs::absolute(file).lexically_normal());
    CHECK(readFile(file) == "skill\n");

    SECTION("a second install keeps it")
    {
        writeFile(file, "edited\n");
        const AgentFileWrite second = installSkill(skills, false);
        plynth::test::requireOk(second.status);
        CHECK(second.result == AgentFileResult::Kept);
        CHECK(readFile(file) == "edited\n");
    }
    SECTION("force refreshes it")
    {
        writeFile(f.docs / pathFromUtf8(kSkillTemplate), "skill 2\n");
        const AgentFileWrite refreshed = installSkill(skills, true);
        plynth::test::requireOk(refreshed.status);
        CHECK(refreshed.result == AgentFileResult::Written);
        CHECK(readFile(file) == "skill 2\n");
    }
}

TEST_CASE("installSkill refuses a missing skills folder, a missing template and a file in the way",
          "[core][agentfiles]")
{
    DocsFixture f("skill-refuse");
    const fs::path skills = f.tmp.path() / "skills";

    SECTION("no skills folder")
    {
        const AgentFileWrite missing = installSkill(skills, false);
        CHECK(missing.status.code == ErrorCode::NotFound);
        CHECK(missing.result == AgentFileResult::Kept);
        CHECK_FALSE(fs::exists(skills));
    }
    SECTION("the skills folder is a file")
    {
        writeFile(skills, "not a folder");
        CHECK(installSkill(skills, false).status.code == ErrorCode::NotFound);
    }
    SECTION("no template")
    {
        fs::create_directories(skills);
        fs::remove(f.docs / pathFromUtf8(kSkillTemplate));
        const AgentFileWrite missing = installSkill(skills, true);
        CHECK(missing.status.code == ErrorCode::NotFound);
        CHECK_FALSE(fs::exists(skills / "plynth"));
    }
    SECTION("a file named plynth")
    {
        writeFile(skills / "plynth", "in the way");
        const AgentFileWrite blocked = installSkill(skills, false);
        CHECK(blocked.status.code == ErrorCode::CannotWrite);
        CHECK(readFile(skills / "plynth") == "in the way");
    }
}

TEST_CASE("installSkill writes the shipped skill file unchanged", "[core][agentfiles]")
{
    const ScopedEnv variable(std::string(kDocsVariable), pathToUtf8(fs::path(PLYNTH_SOURCE_DIR) / "docs"));
    TempDir tmp("skill-shipped");
    const AgentFileWrite written = installSkill(tmp.path(), false);
    plynth::test::requireOk(written.status);
    const std::string text = sourceTemplate(kSkillTemplate);
    REQUIRE_FALSE(text.empty());
    CHECK(readFile(tmp.path() / "plynth" / "SKILL.md") == text);
}

TEST_CASE("sharedAgentText takes the lines between the shared lines", "[core][agentfiles]")
{
    const std::string begin(kSharedBeginLine);
    const std::string end(kSharedEndLine);

    CHECK(sharedAgentText("top\n" + begin + "\none\ntwo\n" + end + "\nbottom\n") == "one\ntwo\n");
    CHECK(sharedAgentText("top\r\n" + begin + "\r\none\r\ntwo\r\n" + end + "\r\n") == "one\ntwo\n");
    CHECK(sharedAgentText(begin + "\none\n" + end) == "one\n");
    CHECK(sharedAgentText(begin + "  \none\n" + end + "\t\n") == "one\n");
    CHECK(sharedAgentText(begin + "\n" + end + "\n").empty());
    CHECK(sharedAgentText("").empty());
    CHECK(sharedAgentText("one\n" + end + "\n").empty());
    CHECK(sharedAgentText(begin + "\none\n").empty());
    CHECK(sharedAgentText(end + "\none\n" + begin + "\n").empty());
    CHECK(sharedAgentText(begin + "\none\n" + begin + "\ntwo\n" + end + "\n").empty());
    CHECK(sharedAgentText("say " + begin + "\none\n" + end + "\n").empty());
}

TEST_CASE("AGENTS.md and SKILL.md templates share the same text", "[core][agentfiles]")
{
    const std::string agents = sourceTemplate(kAgentGuideTemplate);
    const std::string skill = sourceTemplate(kSkillTemplate);
    REQUIRE_FALSE(agents.empty());
    REQUIRE_FALSE(skill.empty());

    const std::string sharedAgents = sharedAgentText(agents);
    const std::string sharedSkill = sharedAgentText(skill);
    REQUIRE_FALSE(sharedAgents.empty());
    REQUIRE_FALSE(sharedSkill.empty());
    CHECK(sharedAgents == sharedSkill);
}

TEST_CASE("SKILL.md starts with the Agent Skills front matter named plynth", "[core][agentfiles]")
{
    std::string skill = sourceTemplate(kSkillTemplate);
    std::erase(skill, '\r');
    REQUIRE(skill.starts_with("---\n"));
    const std::size_t close = skill.find("\n---\n", 3);
    REQUIRE(close != std::string::npos);
    const std::string front = skill.substr(0, close + 1);
    CHECK(front.find("\nname: plynth\n") != std::string::npos);
    CHECK(front.find("\ndescription: ") != std::string::npos);
}
