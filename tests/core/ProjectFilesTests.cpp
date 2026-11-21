#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Writes a small file with the given bytes, creating its folder.
void writeFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

// Counts the files in a folder tree whose name ends with .tmp.
int countTmpFiles(const fs::path& root)
{
    int n = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (e.path().extension() == ".tmp") ++n;
    }
    return n;
}

// A project with one floor that uses the given image path.
Project projectWithImage(const std::string& image)
{
    Project p = makeNewProject();
    p.settings.name = "Flat";
    p.plan = plynth::test::smallPlan();
    p.plan.floors[0].image = image;
    return p;
}

// Lists every file under a folder with its size, so two folders can be compared.
std::string listing(const fs::path& root)
{
    std::string out;
    if (!fs::exists(root)) return out;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        out += pathToUtf8(fs::relative(e.path(), root));
        if (e.is_regular_file()) out += ":" + std::to_string(e.file_size());
        out += "\n";
    }
    return out;
}

}

TEST_CASE("UTF-8 path helpers", "[core][files]") {
    const std::string text = "plans/caf\xC3\xA9/\xC3\xBC.png";
    const fs::path p = pathFromUtf8(text);
    REQUIRE(pathToUtf8(p) == text);
    REQUIRE(pathToUtf8(p.filename()) == "\xC3\xBC.png");
    REQUIRE(pathToUtf8(fs::path(L"a\\b\\c.png")) == "a/b/c.png");
    REQUIRE(pathFromUtf8("").empty());
}

TEST_CASE("resolveImagePath", "[core][files]") {
    const fs::path folder = pathFromUtf8("D:/projects/flat");
    REQUIRE(resolveImagePath(folder, "plans/a.png") == folder / "plans" / "a.png");
    REQUIRE(resolveImagePath(folder, "E:/other/a.png") == fs::path("E:/other/a.png"));
    REQUIRE(resolveImagePath({}, "a.png") == fs::path("a.png"));
}

TEST_CASE("create and load a project", "[core][files]") {
    plynth::test::TempDir tmp("create");
    const fs::path image = tmp.path() / "src" / "plan.png";
    writeFile(image, "image bytes");
    const fs::path folder = tmp.path() / "My flat";

    Project project = projectWithImage(pathToUtf8(image));
    project.settings.exportMode = ExportMode::Grouped;
    project.settings.gridSize = 0.25;
    project.plan.scale.pixelsPerMetre = 80.0;
    plynth::test::requireOk(createProject(project, folder, {}));

    REQUIRE(project.plan.floors[0].image == "plans/plan.png");
    REQUIRE(fs::is_regular_file(folder / "project.json"));
    REQUIRE(fs::is_regular_file(folder / "plan.json"));
    REQUIRE(fs::is_regular_file(folder / "plans" / "plan.png"));
    REQUIRE(isProjectFolder(folder));
    REQUIRE(countTmpFiles(folder) == 0);

    const ProjectLoad loaded = loadProject(folder);
    plynth::test::requireOk(loaded.status);
    REQUIRE(loaded.project == project);
    REQUIRE_FALSE(loaded.migrated);
}

TEST_CASE("written files follow the format and validate against the schemas", "[core][files][schema]") {
    plynth::test::TempDir tmp("schema");
    Project project = makeNewProject();
    project.plan = plynth::test::fullPlan();
    project.plan.floors[0].image = std::nullopt;
    plynth::test::requireOk(createProject(project, tmp.path() / "p", {}));

    const std::string planText = plynth::test::readTextFile(tmp.path() / "p" / "plan.json");
    const std::string projectText = plynth::test::readTextFile(tmp.path() / "p" / "project.json");
    REQUIRE(plynth::test::schemaProblemForText("plan.schema.json", planText).empty());
    REQUIRE(plynth::test::schemaProblemForText("project.schema.json", projectText).empty());

    REQUIRE(planText.rfind("{\n  \"format\": \"plynth-plan\",\n  \"version\": 1,\n  \"scale\": {", 0) == 0);
    REQUIRE(planText.back() == '\n');
    REQUIRE(projectText.rfind("{\n  \"format\": \"plynth-project\",\n  \"version\": 1,\n  \"name\": \"Untitled\"", 0) == 0);
    REQUIRE(projectText.back() == '\n');
}

TEST_CASE("an image already in plans is not copied again", "[core][files]") {
    plynth::test::TempDir tmp("inplace");
    const fs::path image = tmp.path() / "plan.png";
    writeFile(image, "x");
    const fs::path folder = tmp.path() / "p";
    Project project = projectWithImage(pathToUtf8(image));
    plynth::test::requireOk(createProject(project, folder, {}));

    plynth::test::requireOk(saveProject(project, folder, folder));
    plynth::test::requireOk(saveProject(project, folder, folder));
    REQUIRE(project.plan.floors[0].image == "plans/plan.png");
    REQUIRE_FALSE(fs::exists(folder / "plans" / "plan-2.png"));
    REQUIRE(countTmpFiles(folder) == 0);
}

TEST_CASE("images with the same name get -2 and -3, and one source is copied once", "[core][files]") {
    plynth::test::TempDir tmp("clash");
    writeFile(tmp.path() / "a" / "plan.png", "first");
    writeFile(tmp.path() / "b" / "plan.png", "second");
    writeFile(tmp.path() / "c" / "plan.png", "third");
    const fs::path folder = tmp.path() / "p";

    Project project = projectWithImage(pathToUtf8(tmp.path() / "a" / "plan.png"));
    for (const char* dir : {"b", "c"}) {
        Floor f;
        f.id = newId(project.plan, "f");
        f.name = f.id;
        f.image = pathToUtf8(tmp.path() / dir / "plan.png");
        project.plan.floors.push_back(f);
    }
    Floor again;
    again.id = "f9";
    again.name = "Again";
    again.image = pathToUtf8(tmp.path() / "a" / "plan.png");
    project.plan.floors.push_back(again);

    plynth::test::requireOk(createProject(project, folder, {}));
    REQUIRE(project.plan.floors[0].image == "plans/plan.png");
    REQUIRE(project.plan.floors[1].image == "plans/plan-2.png");
    REQUIRE(project.plan.floors[2].image == "plans/plan-3.png");
    REQUIRE(project.plan.floors[3].image == "plans/plan.png");
    REQUIRE(plynth::test::readTextFile(folder / "plans" / "plan-2.png") == "second");
    REQUIRE_FALSE(fs::exists(folder / "plans" / "plan-4.png"));
}

TEST_CASE("images with non ASCII names are copied", "[core][files]") {
    plynth::test::TempDir tmp("unicode");
    const fs::path image = tmp.path() / pathFromUtf8("sr\xC3\xA7") / pathFromUtf8("plan-\xC3\xBC.png");
    writeFile(image, "u");
    Project project = projectWithImage(pathToUtf8(image));
    plynth::test::requireOk(createProject(project, tmp.path() / pathFromUtf8("p\xC3\xBC"), {}));
    REQUIRE(project.plan.floors[0].image == "plans/plan-\xC3\xBC.png");
    const ProjectLoad loaded = loadProject(tmp.path() / pathFromUtf8("p\xC3\xBC"));
    plynth::test::requireOk(loaded.status);
    REQUIRE(loaded.project == project);
}

TEST_CASE("save into another folder copies images and leaves the old folder alone", "[core][files]") {
    plynth::test::TempDir tmp("saveas");
    const fs::path image = tmp.path() / "plan.png";
    writeFile(image, "bytes");
    const fs::path oldFolder = tmp.path() / "old";
    Project project = projectWithImage(pathToUtf8(image));
    plynth::test::requireOk(createProject(project, oldFolder, {}));
    const std::string before = listing(oldFolder);
    const std::string planBefore = plynth::test::readTextFile(oldFolder / "plan.json");

    Project copy = project;
    copy.plan.scale.pixelsPerMetre = 33.0;
    const fs::path newFolder = tmp.path() / "new";
    plynth::test::requireOk(saveProject(copy, newFolder, oldFolder));
    REQUIRE(copy.plan.floors[0].image == "plans/plan.png");
    REQUIRE(plynth::test::readTextFile(newFolder / "plans" / "plan.png") == "bytes");
    REQUIRE(listing(oldFolder) == before);
    REQUIRE(plynth::test::readTextFile(oldFolder / "plan.json") == planBefore);
}

TEST_CASE("createProject refuses a folder that holds a project", "[core][files]") {
    plynth::test::TempDir tmp("exists");
    Project project = makeNewProject();
    plynth::test::requireOk(createProject(project, tmp.path() / "p", {}));
    const std::string before = listing(tmp.path() / "p");
    Project other = makeNewProject();
    other.settings.name = "Other";
    const Status s = createProject(other, tmp.path() / "p", {});
    REQUIRE(s.code == ErrorCode::AlreadyExists);
    REQUIRE(listing(tmp.path() / "p") == before);
    REQUIRE(loadProject(tmp.path() / "p").project.settings.name == "Untitled");
}

TEST_CASE("a missing image gives NotFound and writes nothing", "[core][files]") {
    plynth::test::TempDir tmp("missing");
    const fs::path folder = tmp.path() / "p";
    Project project = projectWithImage(pathToUtf8(tmp.path() / "nope.png"));
    const Project before = project;
    const Status s = saveProject(project, folder, {});
    REQUIRE(s.code == ErrorCode::NotFound);
    REQUIRE(project == before);
    REQUIRE_FALSE(fs::exists(folder / "plan.json"));
    REQUIRE_FALSE(fs::exists(folder / "project.json"));
}

TEST_CASE("a failed save leaves the old files as they were", "[core][files]") {
    plynth::test::TempDir tmp("failed");
    const fs::path folder = tmp.path() / "p";
    Project project = makeNewProject();
    plynth::test::requireOk(createProject(project, folder, {}));
    const std::string planBefore = plynth::test::readTextFile(folder / "plan.json");

    project.plan.floors[0].image = pathToUtf8(tmp.path() / "gone.png");
    REQUIRE(saveProject(project, folder, folder).code == ErrorCode::NotFound);
    REQUIRE(plynth::test::readTextFile(folder / "plan.json") == planBefore);

    Project invalid = makeNewProject();
    invalid.plan.floors.clear();
    REQUIRE(saveProject(invalid, folder, folder).code == ErrorCode::InvalidValue);
    REQUIRE(plynth::test::readTextFile(folder / "plan.json") == planBefore);

    Project noName = makeNewProject();
    noName.settings.name = "";
    REQUIRE(saveProject(noName, folder, folder).code == ErrorCode::InvalidValue);
    Project badGrid = makeNewProject();
    badGrid.settings.gridSize = 0.0;
    REQUIRE(saveProject(badGrid, folder, folder).code == ErrorCode::InvalidValue);
}

TEST_CASE("loadProject errors", "[core][files]") {
    plynth::test::TempDir tmp("load");

    SECTION("an empty folder is not a project") {
        const ProjectLoad r = loadProject(tmp.path());
        REQUIRE(r.status.code == ErrorCode::NotAProject);
        REQUIRE_FALSE(isProjectFolder(tmp.path()));
    }
    SECTION("a folder that does not exist is not a project") {
        REQUIRE(loadProject(tmp.path() / "nope").status.code == ErrorCode::NotAProject);
    }
    SECTION("plan.json is missing") {
        writeFile(tmp.path() / "project.json", R"({"format":"plynth-project","version":1,"name":"A"})");
        REQUIRE(loadProject(tmp.path()).status.code == ErrorCode::NotFound);
    }
    SECTION("a broken plan.json names the file") {
        writeFile(tmp.path() / "project.json", R"({"format":"plynth-project","version":1,"name":"A"})");
        writeFile(tmp.path() / "plan.json", R"({"format":"plynth-plan","version":1,"floors":[{"id":"f1","bogus":1}]})");
        const ProjectLoad r = loadProject(tmp.path());
        REQUIRE(r.status.code == ErrorCode::BadFile);
        REQUIRE(r.status.message.find("plan.json") != std::string::npos);
        REQUIRE(r.status.where == "/floors/0/bogus");
    }
    SECTION("text that is not JSON") {
        writeFile(tmp.path() / "project.json", R"({"format":"plynth-project","version":1,"name":"A"})");
        writeFile(tmp.path() / "plan.json", "{ not json");
        const ProjectLoad r = loadProject(tmp.path());
        REQUIRE(r.status.code == ErrorCode::BadFile);
        REQUIRE(r.status.message.find("plan.json") != std::string::npos);
    }
    SECTION("a broken project.json names the file") {
        writeFile(tmp.path() / "project.json", R"({"format":"plynth-project","version":1})");
        writeFile(tmp.path() / "plan.json", R"({"format":"plynth-plan","version":1,"floors":[{"id":"f1"}]})");
        const ProjectLoad r = loadProject(tmp.path());
        REQUIRE(r.status.code == ErrorCode::BadFile);
        REQUIRE(r.status.message.find("project.json") != std::string::npos);
    }
    SECTION("a newer version is reported as such") {
        writeFile(tmp.path() / "project.json", R"({"format":"plynth-project","version":1,"name":"A"})");
        writeFile(tmp.path() / "plan.json", R"({"format":"plynth-plan","version":9,"floors":[{"id":"f1"}]})");
        REQUIRE(loadProject(tmp.path()).status.code == ErrorCode::UnsupportedVersion);
    }
    SECTION("a short hand written project loads with defaults") {
        writeFile(tmp.path() / "project.json", R"({"format":"plynth-project","version":1,"name":"A"})");
        writeFile(tmp.path() / "plan.json", R"({"format":"plynth-plan","version":1,"floors":[{"id":"f1"}]})");
        const ProjectLoad r = loadProject(tmp.path());
        plynth::test::requireOk(r.status);
        REQUIRE(r.project.settings.name == "A");
        REQUIRE(r.project.plan.floors[0].name == "f1");
    }
}

TEST_CASE("autosave write, find, load and remove", "[core][files][autosave]") {
    plynth::test::TempDir tmp("autosave");
    const fs::path folder = tmp.path() / "p";
    Project saved = makeNewProject();
    plynth::test::requireOk(createProject(saved, folder, {}));
    REQUIRE_FALSE(findAutosave(folder).exists);
    REQUIRE(loadAutosave(folder).status.code == ErrorCode::NotFound);

    Project edited = saved;
    edited.plan.scale.pixelsPerMetre = 61.0;
    edited.plan.floors[0].image = "plans/not-copied.png";
    plynth::test::requireOk(writeAutosave(edited, folder));
    REQUIRE(fs::is_regular_file(folder / "autosave" / "plan.json"));
    REQUIRE(fs::is_regular_file(folder / "autosave" / "project.json"));
    REQUIRE_FALSE(fs::exists(folder / "autosave" / "plans"));
    REQUIRE(countTmpFiles(folder) == 0);

    const AutosaveInfo info = findAutosave(folder);
    REQUIRE(info.exists);
    REQUIRE_FALSE(info.olderThanSaved);

    const ProjectLoad loaded = loadAutosave(folder);
    plynth::test::requireOk(loaded.status);
    REQUIRE(loaded.project == edited);
    REQUIRE(loadProject(folder).project == saved);

    plynth::test::requireOk(removeAutosave(folder));
    REQUIRE_FALSE(fs::exists(folder / "autosave"));
    REQUIRE_FALSE(findAutosave(folder).exists);
    plynth::test::requireOk(removeAutosave(folder));
}

TEST_CASE("autosave older than the saved plan is flagged", "[core][files][autosave]") {
    plynth::test::TempDir tmp("autosave-old");
    const fs::path folder = tmp.path() / "p";
    Project project = makeNewProject();
    plynth::test::requireOk(createProject(project, folder, {}));
    plynth::test::requireOk(writeAutosave(project, folder));

    const auto now = fs::file_time_type::clock::now();
    const auto old = now - std::chrono::hours(2);
    fs::last_write_time(folder / "autosave" / "plan.json", old);
    fs::last_write_time(folder / "autosave" / "project.json", old);
    REQUIRE(findAutosave(folder).olderThanSaved);

    fs::last_write_time(folder / "plan.json", now - std::chrono::hours(3));
    REQUIRE_FALSE(findAutosave(folder).olderThanSaved);
}

TEST_CASE("autosave refuses an invalid project", "[core][files][autosave]") {
    plynth::test::TempDir tmp("autosave-bad");
    Project p = makeNewProject();
    p.plan.floors.clear();
    REQUIRE(writeAutosave(p, tmp.path()).code == ErrorCode::InvalidValue);
    REQUIRE_FALSE(fs::exists(tmp.path() / "autosave"));
}
