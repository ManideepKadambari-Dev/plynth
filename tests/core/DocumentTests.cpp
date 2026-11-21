#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/Document.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Collects the events a document sends.
struct Recorder {
    std::vector<DocumentEvent> events;
    int id = 0;

    void attach(Document& doc)
    {
        id = doc.subscribe([this](const DocumentEvent& e) { events.push_back(e); });
    }
};

// Executes a command and requires success.
void run(Document& doc, std::unique_ptr<Command> c)
{
    plynth::test::requireOk(doc.execute(std::move(c)));
    plynth::test::requirePlanValid(doc.plan());
}

// Writes a small file, creating its folder.
void writeFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

}

TEST_CASE("a new document is untitled and unmodified", "[core][document]") {
    Document doc;
    REQUIRE(doc.project() == makeNewProject());
    REQUIRE(doc.plan() == makeEmptyPlan());
    REQUIRE_FALSE(doc.hasFolder());
    REQUIRE(doc.folder().empty());
    REQUIRE_FALSE(doc.isModified());
    REQUIRE_FALSE(doc.canUndo());
    REQUIRE_FALSE(doc.canRedo());
    REQUIRE(doc.undoName().empty());
    REQUIRE(doc.redoName().empty());
    REQUIRE_FALSE(doc.needsAutosave());
}

TEST_CASE("execute, undo and redo notify with the right flags and floor", "[core][document]") {
    Document doc;
    Recorder rec;
    rec.attach(doc);

    run(doc, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    REQUIRE(rec.events.size() == 1);
    REQUIRE(rec.events[0].changes == (Change::Points | Change::History));
    REQUIRE(rec.events[0].floorId == "f1");
    REQUIRE(doc.isModified());
    REQUIRE(doc.canUndo());
    REQUIRE(doc.undoName() == "Add point");

    run(doc, std::make_unique<SetScaleCommand>(40.0));
    REQUIRE(rec.events[1].changes == (Change::Scale | Change::History));
    REQUIRE(rec.events[1].floorId.empty());

    doc.undo();
    REQUIRE(rec.events.size() == 3);
    REQUIRE(rec.events[2].changes == (Change::Scale | Change::History));
    REQUIRE_FALSE(doc.plan().scale.pixelsPerMetre);
    REQUIRE(doc.canRedo());
    REQUIRE(doc.redoName() == "Set scale");

    plynth::test::requireOk(doc.redo());
    REQUIRE(rec.events.size() == 4);
    REQUIRE(rec.events[3].changes == (Change::Scale | Change::History));
    REQUIRE(doc.plan().scale.pixelsPerMetre == 40.0);

    doc.undo();
    doc.undo();
    REQUIRE(rec.events.size() == 6);
    REQUIRE(rec.events[5].changes == (Change::Points | Change::History));
    REQUIRE(rec.events[5].floorId == "f1");
    doc.undo();
    REQUIRE(rec.events.size() == 6);
    plynth::test::requireOk(doc.redo());
    plynth::test::requireOk(doc.redo());
    plynth::test::requireOk(doc.redo());
    REQUIRE(rec.events.size() == 8);
}

TEST_CASE("a refused command sends nothing and changes nothing", "[core][document]") {
    Document doc;
    run(doc, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    Recorder rec;
    rec.attach(doc);
    const Project before = doc.project();
    const Status s = doc.execute(std::make_unique<AddWallCommand>("p1", "p9"));
    REQUIRE(s.code == ErrorCode::NotFound);
    REQUIRE(doc.project() == before);
    REQUIRE(rec.events.empty());
    REQUIRE(doc.undoName() == "Add point");
}

TEST_CASE("a merged drag is one undo step and still notifies", "[core][document]") {
    Document doc;
    run(doc, std::make_unique<AddPointCommand>("f1", 0.0, 0.0));
    Recorder rec;
    rec.attach(doc);
    run(doc, std::make_unique<MovePointCommand>("p1", 1.0, 1.0, 3));
    run(doc, std::make_unique<MovePointCommand>("p1", 2.0, 2.0, 3));
    REQUIRE(rec.events.size() == 2);
    REQUIRE(rec.events[1].changes == (Change::Points | Change::History));
    REQUIRE(rec.events[1].floorId == "f1");
    doc.undo();
    REQUIRE(doc.plan().floors[0].points[0].x == 0.0);
    REQUIRE(doc.undoName() == "Add point");
}

TEST_CASE("unsubscribe stops calls", "[core][document]") {
    Document doc;
    Recorder a;
    Recorder b;
    a.attach(doc);
    b.attach(doc);
    REQUIRE(a.id != b.id);
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    doc.unsubscribe(a.id);
    run(doc, std::make_unique<SetScaleCommand>(20.0));
    REQUIRE(a.events.size() == 1);
    REQUIRE(b.events.size() == 2);
    doc.unsubscribe(a.id);
    doc.unsubscribe(12345);
}

TEST_CASE("replace swaps the project and tells listeners everything changed", "[core][document]") {
    Document doc;
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    Recorder rec;
    rec.attach(doc);

    Project other = makeNewProject();
    other.settings.name = "Other";
    doc.replace(other, {});
    REQUIRE(rec.events.size() == 1);
    REQUIRE(rec.events[0].changes == Change::Everything);
    REQUIRE(doc.project() == other);
    REQUIRE_FALSE(doc.canUndo());
    REQUIRE_FALSE(doc.isModified());

    doc.replace(other, {}, true);
    REQUIRE(doc.isModified());
    REQUIRE_FALSE(doc.canUndo());
}

TEST_CASE("isModified across save, undo and redo", "[core][document]") {
    plynth::test::TempDir tmp("doc-modified");
    Document doc;
    run(doc, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    REQUIRE_FALSE(doc.isModified());

    run(doc, std::make_unique<AddPointCommand>("f1", 2.0, 2.0));
    REQUIRE(doc.isModified());
    doc.undo();
    REQUIRE_FALSE(doc.isModified());
    plynth::test::requireOk(doc.redo());
    REQUIRE(doc.isModified());
    plynth::test::requireOk(doc.save());
    REQUIRE_FALSE(doc.isModified());
    doc.undo();
    REQUIRE(doc.isModified());
}

TEST_CASE("save needs a folder", "[core][document]") {
    Document doc;
    REQUIRE(doc.save().code == ErrorCode::InvalidValue);
}

TEST_CASE("save as, open and round trip", "[core][document]") {
    plynth::test::TempDir tmp("doc-saveas");
    const fs::path image = tmp.path() / "src" / "plan.png";
    writeFile(image, "img");

    Document doc;
    Recorder rec;
    rec.attach(doc);
    run(doc, std::make_unique<SetFloorImageCommand>("f1", pathToUtf8(image)));
    run(doc, std::make_unique<AddPointCommand>("f1", 1.0, 2.0));
    run(doc, std::make_unique<AddPointCommand>("f1", 3.0, 2.0));
    run(doc, std::make_unique<AddWallCommand>("p1", "p2"));
    rec.events.clear();

    const fs::path folder = tmp.path() / "My flat";
    plynth::test::requireOk(doc.saveAs(folder));
    REQUIRE(doc.hasFolder());
    REQUIRE_FALSE(doc.isModified());
    REQUIRE(doc.plan().floors[0].image == "plans/plan.png");
    REQUIRE(fs::is_regular_file(folder / "plans" / "plan.png"));
    REQUIRE(rec.events.size() == 1);
    REQUIRE((rec.events[0].changes & Change::Folder) != 0);
    REQUIRE((rec.events[0].changes & Change::History) != 0);
    REQUIRE((rec.events[0].changes & Change::FloorImage) != 0);

    Document reopened;
    plynth::test::requireOk(reopened.open(folder));
    REQUIRE(reopened.project() == doc.project());
    REQUIRE_FALSE(reopened.isModified());
    REQUIRE(reopened.folder() == doc.folder());
}

TEST_CASE("save in place reports image rewrites only when they happen", "[core][document]") {
    plynth::test::TempDir tmp("doc-save");
    Document doc;
    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    Recorder rec;
    rec.attach(doc);

    run(doc, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    rec.events.clear();
    plynth::test::requireOk(doc.save());
    REQUIRE(rec.events.size() == 1);
    REQUIRE(rec.events[0].changes == Change::History);

    const fs::path image = tmp.path() / "plan.png";
    writeFile(image, "img");
    run(doc, std::make_unique<SetFloorImageCommand>("f1", pathToUtf8(image)));
    rec.events.clear();
    plynth::test::requireOk(doc.save());
    REQUIRE(rec.events.size() == 1);
    REQUIRE(rec.events[0].changes == (Change::History | Change::FloorImage));
    REQUIRE(doc.plan().floors[0].image == "plans/plan.png");
}

TEST_CASE("a failed save keeps the document as it was", "[core][document]") {
    plynth::test::TempDir tmp("doc-failsave");
    Document doc;
    run(doc, std::make_unique<SetFloorImageCommand>("f1", pathToUtf8(tmp.path() / "missing.png")));
    Recorder rec;
    rec.attach(doc);
    const Project before = doc.project();
    const Status s = doc.saveAs(tmp.path() / "p");
    REQUIRE(s.code == ErrorCode::NotFound);
    REQUIRE_FALSE(doc.hasFolder());
    REQUIRE(doc.isModified());
    REQUIRE(doc.project() == before);
    REQUIRE(rec.events.empty());
}

TEST_CASE("save as onto another project refuses", "[core][document]") {
    plynth::test::TempDir tmp("doc-clash");
    Project other = makeNewProject();
    other.settings.name = "Other";
    plynth::test::requireOk(createProject(other, tmp.path() / "other", {}));

    Document doc;
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    Recorder rec;
    rec.attach(doc);
    const Status s = doc.saveAs(tmp.path() / "other");
    REQUIRE(s.code == ErrorCode::AlreadyExists);
    REQUIRE_FALSE(doc.hasFolder());
    REQUIRE(doc.isModified());
    REQUIRE(rec.events.empty());
    REQUIRE(loadProject(tmp.path() / "other").project.settings.name == "Other");
}

TEST_CASE("save as to the current folder is a plain save", "[core][document]") {
    plynth::test::TempDir tmp("doc-same");
    Document doc;
    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    REQUIRE_FALSE(doc.isModified());
    REQUIRE(loadProject(tmp.path() / "p").project.plan.scale.pixelsPerMetre == 10.0);
}

TEST_CASE("save as to a new folder leaves the old folder untouched", "[core][document]") {
    plynth::test::TempDir tmp("doc-keepold");
    const fs::path image = tmp.path() / "plan.png";
    writeFile(image, "img");
    Document doc;
    run(doc, std::make_unique<SetFloorImageCommand>("f1", pathToUtf8(image)));
    plynth::test::requireOk(doc.saveAs(tmp.path() / "one"));
    const std::string planBefore = plynth::test::readTextFile(tmp.path() / "one" / "plan.json");

    run(doc, std::make_unique<SetScaleCommand>(77.0));
    plynth::test::requireOk(doc.saveAs(tmp.path() / "two"));
    REQUIRE(plynth::test::readTextFile(tmp.path() / "one" / "plan.json") == planBefore);
    REQUIRE(fs::is_regular_file(tmp.path() / "two" / "plans" / "plan.png"));
    REQUIRE(doc.folder().filename() == fs::path("two"));
    REQUIRE(loadProject(tmp.path() / "two").project.plan.scale.pixelsPerMetre == 77.0);
}

TEST_CASE("open errors leave the document unchanged", "[core][document]") {
    plynth::test::TempDir tmp("doc-open");
    Document doc;
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    Recorder rec;
    rec.attach(doc);
    const Project before = doc.project();
    REQUIRE(doc.open(tmp.path()).code == ErrorCode::NotAProject);
    REQUIRE(doc.project() == before);
    REQUIRE(doc.canUndo());
    REQUIRE(rec.events.empty());
}

TEST_CASE("open tells listeners everything changed", "[core][document]") {
    plynth::test::TempDir tmp("doc-open2");
    Project p = makeNewProject();
    plynth::test::requireOk(createProject(p, tmp.path() / "p", {}));
    Document doc;
    Recorder rec;
    rec.attach(doc);
    plynth::test::requireOk(doc.open(tmp.path() / "p"));
    REQUIRE(rec.events.size() == 1);
    REQUIRE(rec.events[0].changes == Change::Everything);
}

TEST_CASE("autosave and needsAutosave", "[core][document][autosave]") {
    plynth::test::TempDir tmp("doc-autosave");
    Document doc;
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    REQUIRE_FALSE(doc.needsAutosave());
    plynth::test::requireOk(doc.autosave());
    REQUIRE_FALSE(fs::exists(tmp.path() / "p"));

    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    REQUIRE_FALSE(doc.needsAutosave());

    run(doc, std::make_unique<AddPointCommand>("f1", 1.0, 1.0));
    REQUIRE(doc.needsAutosave());
    plynth::test::requireOk(doc.autosave());
    REQUIRE_FALSE(doc.needsAutosave());
    REQUIRE(findAutosave(tmp.path() / "p").exists);
    REQUIRE(loadAutosave(tmp.path() / "p").project == doc.project());
    REQUIRE_FALSE(fs::exists(tmp.path() / "p" / "autosave" / "plan.json.tmp"));

    doc.undo();
    REQUIRE(doc.needsAutosave());
}

TEST_CASE("autosave is removed by a save and by discard", "[core][document][autosave]") {
    plynth::test::TempDir tmp("doc-autosave2");
    Document doc;
    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    plynth::test::requireOk(doc.autosave());
    REQUIRE(findAutosave(tmp.path() / "p").exists);

    plynth::test::requireOk(doc.save());
    REQUIRE_FALSE(findAutosave(tmp.path() / "p").exists);
    REQUIRE_FALSE(doc.needsAutosave());

    run(doc, std::make_unique<SetScaleCommand>(20.0));
    plynth::test::requireOk(doc.autosave());
    REQUIRE(findAutosave(tmp.path() / "p").exists);
    plynth::test::requireOk(doc.discardAutosave());
    REQUIRE_FALSE(findAutosave(tmp.path() / "p").exists);
}

TEST_CASE("undoing back to the saved state removes a stale autosave", "[core][document][autosave]") {
    plynth::test::TempDir tmp("doc-autosave3");
    Document doc;
    plynth::test::requireOk(doc.saveAs(tmp.path() / "p"));
    run(doc, std::make_unique<SetScaleCommand>(10.0));
    plynth::test::requireOk(doc.autosave());
    doc.undo();
    REQUIRE_FALSE(doc.isModified());
    REQUIRE(doc.needsAutosave());
    plynth::test::requireOk(doc.autosave());
    REQUIRE_FALSE(findAutosave(tmp.path() / "p").exists);
    REQUIRE_FALSE(doc.needsAutosave());
}

TEST_CASE("recovering an autosave gives an unsaved document", "[core][document][autosave]") {
    plynth::test::TempDir tmp("doc-recover");
    Document first;
    plynth::test::requireOk(first.saveAs(tmp.path() / "p"));
    run(first, std::make_unique<SetScaleCommand>(10.0));
    plynth::test::requireOk(first.autosave());

    ProjectLoad recovered = loadAutosave(tmp.path() / "p");
    plynth::test::requireOk(recovered.status);
    Document second;
    second.replace(recovered.project, tmp.path() / "p", true);
    REQUIRE(second.isModified());
    REQUIRE(second.plan().scale.pixelsPerMetre == 10.0);
    REQUIRE_FALSE(second.canUndo());
    REQUIRE_FALSE(second.needsAutosave());
    plynth::test::requireOk(second.save());
    REQUIRE_FALSE(second.isModified());
    REQUIRE(loadProject(tmp.path() / "p").project.plan.scale.pixelsPerMetre == 10.0);
}
