#include "TestSupport.h"

#include <plynth/core/NewProject.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Resources.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

// Fails the test unless the status has the code and the exact sentence.
void requireRefused(const Status& s, ErrorCode code, const std::string& message)
{
    INFO(s.message);
    CHECK(s.code == code);
    CHECK(s.message == message);
}

// The names of the entries directly inside a folder.
std::vector<std::string> entriesOf(const fs::path& folder)
{
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(folder)) out.push_back(pathToUtf8(e.path().filename()));
    return out;
}

// A scratch folder with a docs folder of its own, found through PLYNTH_DOCS while the object lives.
struct NewProjectFixture {
    explicit NewProjectFixture(const std::string& tag) : tmp(tag)
    {
        docs = tmp.path() / "docs";
        writeFile(docs / pathFromUtf8(resourceMarker(ResourceKind::Docs)), "start\n");
        writeFile(docs / pathFromUtf8(kAgentGuideTemplate), "rules\n");
        variable = std::make_unique<ScopedEnv>(std::string(kDocsVariable), pathToUtf8(docs));
        houses = tmp.path() / "Houses";
    }

    TempDir tmp;
    fs::path docs;
    fs::path houses;
    std::unique_ptr<ScopedEnv> variable;
};

const std::string kCharacters = "A folder name cannot contain any of these characters: \\ / : * ? \" < > |";

}

TEST_CASE("checkProjectName refuses an empty name", "[core][newproject]")
{
    for (std::string_view name : {"", " ", "   "}) {
        INFO("name '" << name << "'");
        requireRefused(checkProjectName(name), ErrorCode::InvalidValue, "Type a name for the project.");
    }
}

TEST_CASE("checkProjectName refuses the characters Windows does not allow", "[core][newproject]")
{
    for (char c : std::string_view("\\/:*?\"<>|")) {
        const std::string name = std::string("my") + c + "house";
        INFO("name " << name);
        requireRefused(checkProjectName(name), ErrorCode::InvalidValue, kCharacters);
        requireRefused(checkProjectName(std::string(1, c)), ErrorCode::InvalidValue, kCharacters);
    }
}

TEST_CASE("checkProjectName refuses control characters", "[core][newproject]")
{
    for (int code = 1; code < 0x20; ++code) {
        const std::string name = "a" + std::string(1, static_cast<char>(code)) + "b";
        INFO("character " << code);
        requireRefused(checkProjectName(name), ErrorCode::InvalidValue, kCharacters);
    }
    requireRefused(checkProjectName(std::string_view("a\0b", 3)), ErrorCode::InvalidValue, kCharacters);
}

TEST_CASE("checkProjectName refuses a name ending with a dot or a space", "[core][newproject]")
{
    for (std::string_view name : {"house.", ".", "..", "a...", " ."}) {
        INFO("name '" << name << "'");
        requireRefused(checkProjectName(name), ErrorCode::InvalidValue, "A folder name cannot end with a dot.");
    }
    for (std::string_view name : {"house ", "a. ", ". "}) {
        INFO("name '" << name << "'");
        requireRefused(checkProjectName(name), ErrorCode::InvalidValue, "A folder name cannot end with a space.");
    }
}

TEST_CASE("checkProjectName refuses the names Windows keeps for itself", "[core][newproject]")
{
    std::vector<std::string> names = {"CON", "PRN", "AUX", "NUL", "con", "Prn", "aUx", "nul"};
    for (char digit = '1'; digit <= '9'; ++digit) {
        names.push_back(std::string("COM") + digit);
        names.push_back(std::string("com") + digit);
        names.push_back(std::string("LPT") + digit);
        names.push_back(std::string("Lpt") + digit);
    }
    for (const char* ending : {"nul.txt", "CON.tar.gz", "Com1.json", "lpt9.x", "aux.", "nul .txt", "PRN  .md"}) {
        names.push_back(ending);
    }
    for (const std::string& name : names) {
        INFO("name " << name);
        const Status s = checkProjectName(name);
        CHECK(s.code == ErrorCode::InvalidValue);
        if (name.back() != '.') {
            CHECK(s.message == "Windows keeps the name " + name + " for itself. Choose another name.");
        }
    }
}

TEST_CASE("checkProjectName takes names that only look like device names", "[core][newproject]")
{
    for (std::string_view name : {"COM0", "COM10", "LPT0", "LPT", "COM", "CONSOLE", "nullable", "AUXILIARY",
                                  "con-tent", "PRN2", "CON_1", "my CON", "xnul.txt", "CON1"}) {
        INFO("name " << name);
        plynth::test::requireOk(checkProjectName(name));
    }
}

TEST_CASE("checkProjectName takes ordinary names", "[core][newproject]")
{
    for (std::string_view name : {"Cottage", "My house 2", "Plan (2)", "a.b", ".hidden", " leading", "x",
                                  "Haus \xC3\xBC", "\xE5\xAE\xB6"}) {
        INFO("name " << name);
        plynth::test::requireOk(checkProjectName(name));
    }
}

TEST_CASE("checkNewProjectFolder checks the name before the location", "[core][newproject]")
{
    requireRefused(checkNewProjectFolder(fs::path{}, ""), ErrorCode::InvalidValue, "Type a name for the project.");
    requireRefused(checkNewProjectFolder(fs::path{}, "NUL"), ErrorCode::InvalidValue,
                   "Windows keeps the name NUL for itself. Choose another name.");
}

TEST_CASE("checkNewProjectFolder refuses an empty, relative or file location", "[core][newproject]")
{
    NewProjectFixture f("newproject-location");

    requireRefused(checkNewProjectFolder(fs::path{}, "Cottage"), ErrorCode::InvalidValue,
                   "Choose a location for the project.");
    for (const char* relative : {"plynth-no-such-folder", "C:plynth-no-such-folder"}) {
        INFO("location " << relative);
        requireRefused(checkNewProjectFolder(fs::path(relative), "Cottage"), ErrorCode::InvalidValue,
                       "Choose a full location, such as one picked with Browse.");
    }
    const fs::path file = f.tmp.path() / "notes.txt";
    writeFile(file, "text");
    requireRefused(checkNewProjectFolder(file, "Cottage"), ErrorCode::InvalidValue,
                   "The location " + pathToUtf8(file) + " is a file, not a folder.");
}

TEST_CASE("checkNewProjectFolder takes a location that is not there yet and an empty folder", "[core][newproject]")
{
    NewProjectFixture f("newproject-free");

    plynth::test::requireOk(checkNewProjectFolder(f.houses / "deeper", "Cottage"));
    fs::create_directories(f.houses / "Cottage");
    plynth::test::requireOk(checkNewProjectFolder(f.houses, "Cottage"));
}

TEST_CASE("checkNewProjectFolder refuses a folder that is taken", "[core][newproject]")
{
    NewProjectFixture f("newproject-taken");
    const fs::path target = f.houses / "Cottage";
    const std::string withFiles =
        "The location already has a folder named Cottage with files in it. Choose another name or location.";

    SECTION("a file of that name")
    {
        writeFile(target, "text");
        requireRefused(checkNewProjectFolder(f.houses, "Cottage"), ErrorCode::AlreadyExists,
                       "The location already has a file named Cottage. Choose another name.");
    }
    SECTION("a folder that holds a project")
    {
        writeFile(target / "project.json", "{}");
        requireRefused(checkNewProjectFolder(f.houses, "Cottage"), ErrorCode::AlreadyExists,
                       "The folder " + pathToUtf8(target) + " already holds a project.");
    }
    SECTION("a folder with a file in it")
    {
        writeFile(target / "plan.png", "png");
        requireRefused(checkNewProjectFolder(f.houses, "Cottage"), ErrorCode::AlreadyExists, withFiles);
    }
    SECTION("a folder with an empty folder in it")
    {
        fs::create_directories(target / "empty");
        requireRefused(checkNewProjectFolder(f.houses, "Cottage"), ErrorCode::AlreadyExists, withFiles);
    }
    SECTION("a folder with only a hidden system file in it")
    {
        const fs::path hidden = target / "desktop.ini";
        writeFile(hidden, "[.ShellClassInfo]");
        REQUIRE(SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
        requireRefused(checkNewProjectFolder(f.houses, "Cottage"), ErrorCode::AlreadyExists, withFiles);
        SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    SECTION("an AGENTS.md alone")
    {
        writeFile(target / "AGENTS.md", "mine");
        requireRefused(checkNewProjectFolder(f.houses, "Cottage"), ErrorCode::AlreadyExists, withFiles);
    }
}

TEST_CASE("checkNewProjectFolder of a whole path splits it into location and name", "[core][newproject]")
{
    NewProjectFixture f("newproject-whole");
    const fs::path target = f.houses / "Cottage";

    requireRefused(checkNewProjectFolder(fs::path{}), ErrorCode::InvalidValue, "Choose a folder for the project.");
    plynth::test::requireOk(checkNewProjectFolder(target));
    plynth::test::requireOk(checkNewProjectFolder(pathFromUtf8(pathToUtf8(target) + "\\")));
    plynth::test::requireOk(checkNewProjectFolder(f.houses / "x" / ".." / "Cottage"));
    requireRefused(checkNewProjectFolder(f.houses / "com3"), ErrorCode::InvalidValue,
                   "Windows keeps the name com3 for itself. Choose another name.");
    requireRefused(checkNewProjectFolder(f.houses / "house."), ErrorCode::InvalidValue,
                   "A folder name cannot end with a dot.");
    requireRefused(checkNewProjectFolder(f.tmp.path().root_path()), ErrorCode::InvalidValue,
                   "A project cannot be made in the top folder of a drive.");
    CHECK(checkNewProjectFolder(fs::path("Cottage")).code == ErrorCode::InvalidValue);

    writeFile(target / "project.json", "{}");
    requireRefused(checkNewProjectFolder(target), ErrorCode::AlreadyExists,
                   "The folder " + pathToUtf8(target) + " already holds a project.");
    requireRefused(checkNewProjectFolder(pathFromUtf8(pathToUtf8(target) + "\\")), ErrorCode::AlreadyExists,
                   "The folder " + pathToUtf8(target) + " already holds a project.");
}

TEST_CASE("createNewProject saves the project and writes AGENTS.md", "[core][newproject]")
{
    NewProjectFixture f("newproject-create");
    const fs::path target = f.houses / "deeper" / "Cottage";
    Project project = makeNewProject();
    project.settings.name = "Cottage";

    const NewProjectResult made = createNewProject(project, target, {});
    plynth::test::requireOk(made.status);
    plynth::test::requireOk(made.agentGuide.status);
    CHECK(made.agentGuide.result == AgentFileResult::Written);
    CHECK(made.agentGuide.file == fs::absolute(target / "AGENTS.md").lexically_normal());
    CHECK(readFile(target / "AGENTS.md") == "rules\n");
    CHECK(isProjectFolder(target));
    const ProjectLoad loaded = loadProject(target);
    plynth::test::requireOk(loaded.status);
    CHECK(loaded.project.settings.name == "Cottage");

    SECTION("a second time is refused and changes nothing")
    {
        writeFile(target / "AGENTS.md", "mine\n");
        const std::string before = readFile(target / "project.json");
        Project again = makeNewProject();
        const NewProjectResult second = createNewProject(again, target, {});
        CHECK(second.status.code == ErrorCode::AlreadyExists);
        CHECK(readFile(target / "project.json") == before);
        CHECK(readFile(target / "AGENTS.md") == "mine\n");
    }
}

TEST_CASE("createNewProject works in an empty folder that is there", "[core][newproject]")
{
    NewProjectFixture f("newproject-empty");
    const fs::path target = f.houses / "Cottage";
    fs::create_directories(target);
    Project project = makeNewProject();

    const NewProjectResult made = createNewProject(project, target, {});
    plynth::test::requireOk(made.status);
    CHECK(made.agentGuide.result == AgentFileResult::Written);
    CHECK(isProjectFolder(target));
    CHECK(fs::exists(target / "AGENTS.md"));
}

TEST_CASE("createNewProject leaves AGENTS.md out when told to", "[core][newproject]")
{
    NewProjectFixture f("newproject-noguide");
    const fs::path target = f.houses / "Cottage";
    Project project = makeNewProject();
    NewProjectOptions options;
    options.agentGuide = false;

    const NewProjectResult made = createNewProject(project, target, {}, options);
    plynth::test::requireOk(made.status);
    plynth::test::requireOk(made.agentGuide.status);
    CHECK(made.agentGuide.result == AgentFileResult::Kept);
    CHECK(isProjectFolder(target));
    CHECK_FALSE(fs::exists(target / "AGENTS.md"));
}

TEST_CASE("createNewProject refuses a taken folder and writes nothing", "[core][newproject]")
{
    NewProjectFixture f("newproject-refused");
    const fs::path target = f.houses / "Cottage";
    writeFile(target / "plan.png", "png");
    Project project = makeNewProject();

    const NewProjectResult made = createNewProject(project, target, {});
    CHECK(made.status.code == ErrorCode::AlreadyExists);
    CHECK(entriesOf(target) == std::vector<std::string>{"plan.png"});
    CHECK(readFile(target / "plan.png") == "png");

    Project named = makeNewProject();
    CHECK(createNewProject(named, f.houses / "AUX", {}).status.code == ErrorCode::InvalidValue);
    CHECK_FALSE(fs::exists(f.houses / "AUX"));
}

TEST_CASE("createNewProject keeps the project when AGENTS.md cannot be written", "[core][newproject]")
{
    NewProjectFixture f("newproject-notemplate");
    fs::remove(f.docs / pathFromUtf8(kAgentGuideTemplate));
    const fs::path target = f.houses / "Cottage";
    Project project = makeNewProject();

    const NewProjectResult made = createNewProject(project, target, {});
    plynth::test::requireOk(made.status);
    CHECK(made.agentGuide.status.code == ErrorCode::NotFound);
    CHECK(isProjectFolder(target));
    CHECK_FALSE(fs::exists(target / "AGENTS.md"));
}

TEST_CASE("createNewProject removes a folder it made when the project cannot be saved", "[core][newproject]")
{
    NewProjectFixture f("newproject-noimage");
    Project project = makeNewProject();
    REQUIRE_FALSE(project.plan.floors.empty());
    project.plan.floors[0].image = pathToUtf8(f.tmp.path() / "missing.png");

    SECTION("a new folder")
    {
        const fs::path target = f.houses / "Cottage";
        const NewProjectResult made = createNewProject(project, target, {});
        CHECK(made.status.code == ErrorCode::NotFound);
        CHECK_FALSE(fs::exists(target));
        plynth::test::requireOk(checkNewProjectFolder(target));
    }
    SECTION("an empty folder that was there stays")
    {
        const fs::path target = f.houses / "Cottage";
        fs::create_directories(target);
        const NewProjectResult made = createNewProject(project, target, {});
        CHECK(made.status.code == ErrorCode::NotFound);
        CHECK(fs::is_directory(target));
        CHECK(entriesOf(target).empty());
    }
}
