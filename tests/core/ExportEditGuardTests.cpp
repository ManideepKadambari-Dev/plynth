#include "SchemaSupport.h"
#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ExportEditGuard.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using plynth::test::readTextFile;
using plynth::test::requireOk;
using plynth::test::TempDir;

namespace {

namespace fs = std::filesystem;
using Decision = ExportEditDecision;

// A moment in the tests, some seconds after a fixed start.
SessionTime atSecond(long long seconds)
{
    return SessionTime{std::chrono::seconds{1'790'000'000LL + seconds}};
}

// The options file of a folder.
fs::path optionsFile(const fs::path& folder)
{
    return folder / fs::path(std::string(kExportOptionsFileName));
}

// Writes text exactly as given.
void writeText(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream written(file, std::ios::binary | std::ios::trunc);
    written << text;
    written.close();
    REQUIRE(written.good());
}

// Every file and folder of a project with its bytes, without the session file, which is the CLI's own.
std::map<std::string, std::string> filesOf(const fs::path& folder)
{
    std::map<std::string, std::string> files;
    if (!fs::is_directory(folder)) return files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(folder)) {
        const std::string name = pathToUtf8(fs::relative(entry.path(), folder));
        if (name == kSessionFileName) continue;
        files[name] = entry.is_regular_file() ? readTextFile(entry.path()) : std::string("folder");
    }
    return files;
}

// The grouped project of the tests: room r1 living on a square, with the sofa i1 in it.
Project flatWithSofa()
{
    Project project = makeNewProject();
    project.settings.name = "Flat";
    project.settings.exportMode = ExportMode::Grouped;
    Floor floor = plynth::test::squareFloorWithWalls();
    Room room;
    room.id = "r1";
    room.type = "living";
    room.walls = {"w1", "w2", "w3", "w4"};
    floor.rooms = {room};
    Item sofa;
    sofa.id = "i1";
    sofa.asset = "sofa";
    sofa.room = "r1";
    sofa.x = 2.0;
    sofa.y = 2.0;
    sofa.size = {1.0, 1.0, 1.0};
    floor.items = {sofa};
    project.plan.floors[0] = floor;
    return project;
}

// The plan with a kitchen r2 and a table i2 added.
Plan withKitchen(Plan plan)
{
    Floor& floor = plan.floors[0];
    Room room;
    room.id = "r2";
    room.type = "kitchen";
    room.walls = {"w1", "w2"};
    floor.rooms.push_back(room);
    Item table = floor.items.at(0);
    table.id = "i2";
    table.asset = "table";
    table.room = "r2";
    floor.items.push_back(table);
    return plan;
}

// The plan with room r1 turned into a bedroom.
Plan withBedroom(Plan plan)
{
    plan.floors[0].rooms.at(0).type = "bedroom";
    return plan;
}

// The plan without its sofa.
Plan withoutSofa(Plan plan)
{
    plan.floors[0].items.clear();
    return plan;
}

// The plan with a corner moved, which changes no group.
Plan withMovedCorner(Plan plan)
{
    plan.floors[0].points.at(2).x += 0.5;
    return plan;
}

// Writes build/built-plan.json as a build does, with no event in the project folder itself.
void writeBuilt(const fs::path& folder, const Plan& plan)
{
    Json built = Json::object();
    built["plan_fingerprint"] = "abc";
    built["plan"] = planToJson(plan);
    writeText(folder / "build" / "built-plan.json", built.dump(2));
}

// Options with one added group, group_1, of a name.
ExportOptions addedGroup(const std::string& name)
{
    ExportOptions options;
    options.groups.added = {{"group_1", name}};
    return options;
}

// The options with the group Hero and the walls group renamed to Shell.
ExportOptions heroAndShell()
{
    ExportOptions options = addedGroup("Hero");
    options.groups.renames = {{"structure", "Shell"}};
    return options;
}

// Starts a session as "plynth session begin" does.
void startSession(const fs::path& folder, SessionTime now)
{
    requireOk(writeSession(folder, SessionLock{1, "test", now, now}));
}

// The change that renames a group, as the list does it.
ExportGroupChange renameChange(std::string group, std::string name)
{
    return [group = std::move(group), name = std::move(name)](ExportGroupEdits& edits, const std::vector<ExportGroup>& current) {
        return renameExportGroup(edits, current, group, name);
    };
}

// The change that merges a group into another.
ExportGroupChange mergeChange(std::string group, std::string into)
{
    return [group = std::move(group), into = std::move(into)](ExportGroupEdits& edits, const std::vector<ExportGroup>& current) {
        return mergeExportGroups(edits, current, group, into);
    };
}

// The change that moves a member.
ExportGroupChange moveChange(ExportMember member, std::string to)
{
    return [member = std::move(member), to = std::move(to)](ExportGroupEdits& edits, const std::vector<ExportGroup>& current) {
        return moveExportMember(edits, current, member, to);
    };
}

// The change that adds a group.
ExportGroupChange addChange(std::string name)
{
    return [name = std::move(name)](ExportGroupEdits& edits, const std::vector<ExportGroup>& current) {
        std::string id;
        return addExportGroup(edits, current, name, id);
    };
}

// The change that adds a group and moves a member into it, as Move to New group does.
ExportGroupChange addWithMemberChange(std::string name, ExportMember member)
{
    return [name = std::move(name), member = std::move(member)](ExportGroupEdits& edits, const std::vector<ExportGroup>& current) {
        std::string id;
        if (Status added = addExportGroup(edits, current, name, id); !added.ok()) return added;
        std::vector<ExportGroup> next = current;
        ExportGroup made;
        made.id = id;
        made.name = name;
        next.push_back(made);
        return moveExportMember(edits, next, member, id);
    };
}

// The change that throws every group edit away.
ExportGroupChange resetChange()
{
    return [](ExportGroupEdits& edits, const std::vector<ExportGroup>&) {
        edits = ExportGroupEdits{};
        return Status{};
    };
}

// The name a group has now, or nothing when it is gone.
std::string nameOfGroup(const ExportGroupReview& review, const std::string& id)
{
    for (const ExportGroup& group : review.groups) {
        if (group.id == id) return group.name;
    }
    return {};
}

// Fixture P of the fifth review: a saved grouped project with a built plan and the added group Hero, shown,
// with a ticket taken as a menu, dialog, editor or drag would.
struct GuardFixture {
    explicit GuardFixture(const std::string& tag, bool built = true, ExportEditor who = ExportEditor::App)
        : dir(tag), folder(dir.path() / "P"), project(flatWithSofa()), guard(who), now(atSecond(0))
    {
        fs::create_directories(folder);
        requireOk(createProject(project, folder, folder));
        requireOk(writeExportOptions(folder, addedGroup("Hero")));
        if (built) writeBuilt(folder, project.plan);
        REQUIRE(show());
        t = guard.ticket();
    }

    // The review of the files as they are.
    ExportGroupReview review() const { return reviewExportGroups(folder, project); }

    // Shows the files as they are.
    bool show() { return guard.show(folder, project, review(), now); }

    // Another program replaces the options file.
    void outside(const ExportOptions& options) const { requireOk(writeExportOptions(folder, options)); }

    // The options as saved.
    ExportOptions saved() const
    {
        const ExportOptionsRead read = readExportOptions(folder);
        requireOk(read.status);
        return read.options;
    }

    TempDir dir;
    fs::path folder;
    Project project;
    ExportEditGuard guard;
    SessionTime now;
    ExportEditTicket t;
};

// Checks that a write was refused and changed no file.
void requireRefused(const ExportEditResult& result, const fs::path& folder, const std::map<std::string, std::string>& before)
{
    CHECK(result.decision != Decision::Allowed);
    CHECK_FALSE(result.status.ok());
    CHECK(result.status.code == ErrorCode::Locked);
    CHECK(filesOf(folder) == before);
}

// Checks that a write was let through and worked.
void requireSaved(const ExportEditResult& result)
{
    CHECK(result.decision == Decision::Allowed);
    requireOk(result.status);
}

// H1g: after a refusal a new ticket renames group_1 and it is saved.
void requireNewEditWorks(GuardFixture& f)
{
    CHECK(f.guard.state() == Decision::Allowed);
    const ExportEditTicket fresh = f.guard.ticket();
    CHECK(f.guard.holds(fresh));
    requireSaved(f.guard.edit(fresh, f.now, renameChange("group_1", "Mine")));
    CHECK(nameOfGroup(f.review(), "group_1") == "Mine");
}

}

TEST_CASE("H1a a move from a menu that was open over an outside change never lands in Props", "[core][exportguard]") {
    GuardFixture f("guard-h1a");
    f.outside(addedGroup("Props"));
    CHECK(f.guard.folderChanged(f.now));
    REQUIRE(f.show());
    CHECK_FALSE(f.guard.holds(f.t));

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, moveChange({"item", "i1"}, "group_1"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK(f.saved() == addedGroup("Props"));
    requireNewEditWorks(f);
}

TEST_CASE("H1b merge, rename and move to a new group from an old menu are refused", "[core][exportguard]") {
    GuardFixture f("guard-h1b");
    f.outside(addedGroup("Props"));
    f.guard.folderChanged(f.now);
    REQUIRE(f.show());

    const auto before = filesOf(f.folder);
    for (const ExportGroupChange& change :
         {mergeChange("furniture_r1", "group_1"), renameChange("group_1", "Mine"), addWithMemberChange("Lamps", {"item", "i1"}),
          addChange("Lamps")}) {
        const ExportEditResult result = f.guard.edit(f.t, f.now, change);
        requireRefused(result, f.folder, before);
        CHECK(result.decision == Decision::ChangedOutside);
    }
    requireNewEditWorks(f);
}

TEST_CASE("H1c a reset asked before an outside edit does not wipe it", "[core][exportguard]") {
    GuardFixture f("guard-h1c");
    f.outside(heroAndShell());
    f.guard.folderChanged(f.now);
    REQUIRE(f.show());

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, resetChange());
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK(f.saved() == heroAndShell());
    requireNewEditWorks(f);
}

TEST_CASE("H1d a refresh that found the change first does not make the old ticket good", "[core][exportguard]") {
    GuardFixture f("guard-h1d");
    f.outside(addedGroup("Props"));
    REQUIRE(f.show());

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, moveChange({"item", "i1"}, "group_1"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    requireNewEditWorks(f);
}

TEST_CASE("H1e a menu open across a session that changed the groups is refused", "[core][exportguard]") {
    GuardFixture f("guard-h1e");
    startSession(f.folder, f.now);
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Locked);
    f.outside(addedGroup("Props"));
    requireOk(removeSession(f.folder));
    REQUIRE(f.show());

    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(f.t, f.now, moveChange({"item", "i1"}, "group_1")), f.folder, before);
    requireNewEditWorks(f);
}

TEST_CASE("H1f a dialog open across a session that changed nothing is refused", "[core][exportguard]") {
    GuardFixture f("guard-h1f");
    startSession(f.folder, f.now);
    REQUIRE(f.show());
    requireOk(removeSession(f.folder));
    REQUIRE(f.show());
    CHECK_FALSE(f.guard.holds(f.t));

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, addChange("Lamps"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::Locked);
    requireNewEditWorks(f);
}

TEST_CASE("H1h a change of only the kit switch locks nothing and is kept", "[core][exportguard]") {
    GuardFixture f("guard-h1h");
    ExportOptions options = addedGroup("Hero");
    options.links[std::string(kUnrealLinkId)].kit = true;
    f.outside(options);
    f.guard.folderChanged(f.now);
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.holds(f.t));
    REQUIRE(f.show());
    CHECK(f.guard.holds(f.t));
    CHECK(f.guard.message().empty());

    requireSaved(f.guard.edit(f.t, f.now, renameChange("structure", "Shell")));
    ExportOptions expected = heroAndShell();
    expected.links[std::string(kUnrealLinkId)].kit = true;
    CHECK(f.saved() == expected);
}

TEST_CASE("H1i the guard's own write locks nothing", "[core][exportguard]") {
    GuardFixture f("guard-h1i");
    requireSaved(f.guard.edit(f.t, f.now, addChange("Lamps")));
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
}

TEST_CASE("H2 no write gets through once a session runs, seen or not", "[core][exportguard]") {
    GuardFixture f("guard-h2");
    ExportEditResult result;
    std::map<std::string, std::string> before;

    SECTION("a group edit") {
        startSession(f.folder, f.now);
        before = filesOf(f.folder);
        result = f.guard.edit(f.t, f.now, renameChange("structure", "Shell"));
    }
    SECTION("a group edit with a ticket taken after the session began") {
        startSession(f.folder, f.now);
        before = filesOf(f.folder);
        result = f.guard.edit(f.guard.ticket(), f.now, renameChange("structure", "Shell"));
    }
    SECTION("the kit switch") {
        startSession(f.folder, f.now);
        before = filesOf(f.folder);
        result = f.guard.setKit(f.now, true);
    }
    SECTION("a repair") {
        writeText(optionsFile(f.folder), "{not json");
        REQUIRE(f.show());
        const ExportEditTicket repairing = f.guard.ticket();
        startSession(f.folder, f.now);
        before = filesOf(f.folder);
        result = f.guard.repair(repairing, f.now);
    }

    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::Locked);
    CHECK(f.guard.state() == Decision::Locked);
    CHECK(f.guard.message() == kExportSessionLockMessage);
    CHECK_FALSE(fs::exists(f.folder / "export-options.json.bad"));
}

TEST_CASE("H2 the session is read again right before the write", "[core][exportguard]") {
    GuardFixture f("guard-h2-late");
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, [&f](ExportGroupEdits& edits, const std::vector<ExportGroup>& current) {
        startSession(f.folder, f.now);
        return renameExportGroup(edits, current, "structure", "Shell");
    });
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::Locked);
}

TEST_CASE("H3a a repair question open when a session begins writes nothing", "[core][exportguard]") {
    GuardFixture f("guard-h3a");
    writeText(optionsFile(f.folder), "{not json");
    REQUIRE(f.show());
    const ExportEditTicket asked = f.guard.ticket();
    CHECK(f.guard.holds(asked));
    startSession(f.folder, f.now);
    REQUIRE(f.show());

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.repair(asked, f.now);
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::Locked);
}

TEST_CASE("H3b a repair question open across a session is refused, a new one repairs", "[core][exportguard]") {
    GuardFixture f("guard-h3b");
    writeText(optionsFile(f.folder), "{not json");
    REQUIRE(f.show());
    const ExportEditTicket asked = f.guard.ticket();
    startSession(f.folder, f.now);
    REQUIRE(f.show());
    requireOk(removeSession(f.folder));
    REQUIRE(f.show());

    const auto before = filesOf(f.folder);
    requireRefused(f.guard.repair(asked, f.now), f.folder, before);

    requireSaved(f.guard.repair(f.guard.ticket(), f.now));
    CHECK(readTextFile(f.folder / "export-options.json.bad") == "{not json");
    CHECK(f.saved() == ExportOptions{});
    CHECK(f.guard.message().empty());
}

TEST_CASE("H3c a dialog opened in grouped mode writes nothing when shown as single during a session", "[core][exportguard]") {
    GuardFixture f("guard-h3c");
    startSession(f.folder, f.now);
    f.project.settings.exportMode = ExportMode::Single;
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Locked);

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, addChange("Lamps"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::Locked);

    requireOk(removeSession(f.folder));
    REQUIRE(f.show());
    requireRefused(f.guard.edit(f.t, f.now, addChange("Lamps")), f.folder, before);
}

TEST_CASE("H4 an edit after a build changed the groups is refused and locks", "[core][exportguard]") {
    GuardFixture f("guard-h4");
    ExportGroupChange change;

    SECTION("H4a a name the build just made") {
        writeBuilt(f.folder, withKitchen(f.project.plan));
        change = renameChange("structure", "Furniture Kitchen");
    }
    SECTION("H4a a name that is still free") {
        writeBuilt(f.folder, withKitchen(f.project.plan));
        change = renameChange("structure", "Shell");
    }
    SECTION("H4b the room behind the group is another kind now") {
        writeBuilt(f.folder, withBedroom(f.project.plan));
        change = renameChange("furniture_r1", "Lounge");
    }
    SECTION("H4c the member is gone") {
        writeBuilt(f.folder, withoutSofa(f.project.plan));
        change = moveChange({"item", "i1"}, "group_1");
    }
    SECTION("the built plan is gone and the project's plan differs") {
        f.project.plan = withKitchen(f.project.plan);
        REQUIRE(f.show());
        CHECK(f.guard.holds(f.t));
        fs::remove(f.folder / "build" / "built-plan.json");
        change = renameChange("structure", "Shell");
    }

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, change);
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK(f.guard.state() == Decision::ChangedOutside);
    CHECK(f.guard.message() == kExportChangeLockMessage);

    REQUIRE(f.show());
    CHECK(f.guard.message() == kExportBuildChangedMessage);
    requireNewEditWorks(f);
    CHECK(f.guard.message().empty());
}

TEST_CASE("H5a the reason stays after the reload until the next good edit", "[core][exportguard]") {
    GuardFixture f("guard-h5a");
    f.outside(heroAndShell());
    CHECK(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::ChangedOutside);
    CHECK(f.guard.message() == kExportChangeLockMessage);
    CHECK_FALSE(f.guard.holds(f.t));

    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message() == kExportGroupsChangedMessage);
    REQUIRE(f.show());
    CHECK(f.guard.message() == kExportGroupsChangedMessage);
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.message() == kExportGroupsChangedMessage);

    const auto before = filesOf(f.folder);
    const ExportEditResult bad = f.guard.edit(f.guard.ticket(), f.now, renameChange("group_1", ""));
    CHECK_FALSE(bad.status.ok());
    CHECK(filesOf(f.folder) == before);
    CHECK_FALSE(f.guard.message().empty());

    requireNewEditWorks(f);
    CHECK(f.guard.message().empty());
}

TEST_CASE("H5b a refresh that found the change first still says why", "[core][exportguard]") {
    GuardFixture f("guard-h5b");
    CHECK(f.guard.holds(f.t));
    f.outside(heroAndShell());
    REQUIRE(f.show());
    CHECK(f.guard.message() == kExportGroupsChangedMessage);
    CHECK_FALSE(f.guard.holds(f.t));

    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")), f.folder, before);
    CHECK_FALSE(f.guard.message().empty());
}

TEST_CASE("H6 an expired session ends the lock with no file change", "[core][exportguard]") {
    bool built = true;
    SECTION("with a build") {
        built = true;
    }
    SECTION("with no build") {
        built = false;
    }
    GuardFixture f("guard-h6", built);
    startSession(f.folder, f.now);
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Locked);
    CHECK(f.guard.message() == kExportSessionLockMessage);

    f.now = atSecond(kSessionExpirySeconds);
    f.guard.folderChanged(f.now);
    CHECK(f.guard.state() == Decision::Locked);

    f.now = atSecond(kSessionExpirySeconds + 1);
    CHECK(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Locked);
    CHECK(f.guard.folderChanged(f.now));
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message() != kExportSessionLockMessage);
    requireNewEditWorks(f);
}

TEST_CASE("H6 a show alone ends the lock of an expired, backdated or unreadable session", "[core][exportguard]") {
    GuardFixture f("guard-h6-show");
    startSession(f.folder, f.now);
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Locked);

    SECTION("expired") {
        f.now = atSecond(kSessionExpirySeconds + 1);
    }
    SECTION("the clock went back") {
        f.now = atSecond(-kSessionExpirySeconds - 1);
    }
    SECTION("the file cannot be read") {
        writeText(f.folder / std::string(kSessionFileName), "{not json");
    }
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    requireNewEditWorks(f);
}

TEST_CASE("H6 a lost reload is asked for again", "[core][exportguard]") {
    GuardFixture f("guard-h6-lost");
    f.outside(addedGroup("Props"));
    CHECK(f.guard.folderChanged(f.now));
    for (int second = 1; second <= 3; ++second) {
        CHECK(f.guard.folderChanged(atSecond(second)));
        CHECK(f.guard.state() == Decision::ChangedOutside);
    }
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK_FALSE(f.guard.folderChanged(f.now));
}

TEST_CASE("H7a a project with no build does not lock when another file appears", "[core][exportguard]") {
    GuardFixture f("guard-h7a", false);
    writeText(f.folder / "another.txt", "hello");
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
    CHECK(f.guard.holds(f.t));

    f.project.plan = withKitchen(f.project.plan);
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
}

TEST_CASE("H7b a project that is not shown is not compared", "[core][exportguard]") {
    GuardFixture f("guard-h7b");
    const fs::path q = f.dir.path() / "Q";
    fs::create_directories(q);
    Project other = flatWithSofa();
    other.settings.exportMode = ExportMode::Single;
    requireOk(createProject(other, q, q));
    REQUIRE(f.guard.show(q, other, reviewExportGroups(q, other), f.now));

    f.outside(addedGroup("Props"));
    writeBuilt(f.folder, withKitchen(f.project.plan));
    startSession(f.folder, f.now);
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());

    requireOk(removeSession(f.folder));
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
    CHECK_FALSE(f.guard.holds(f.t));
}

TEST_CASE("H7c a review older than the files never ends a lock", "[core][exportguard]") {
    GuardFixture f("guard-h7c");
    const ExportGroupReview old = f.review();
    f.outside(heroAndShell());
    f.guard.folderChanged(f.now);
    CHECK_FALSE(f.guard.show(f.folder, f.project, old, f.now));
    CHECK(f.guard.state() == Decision::ChangedOutside);
    CHECK_FALSE(f.guard.holds(f.guard.ticket()));

    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(f.guard.ticket(), f.now, renameChange("group_1", "Mine")), f.folder, before);
    CHECK(f.guard.folderChanged(f.now));
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message() == kExportGroupsChangedMessage);
}

TEST_CASE("an out of date review is refused when nothing was noticed yet", "[core][exportguard]") {
    GuardFixture f("guard-old-review");
    const ExportGroupReview old = f.review();
    SECTION("the options changed") {
        f.outside(heroAndShell());
    }
    SECTION("the file broke") {
        writeText(optionsFile(f.folder), "{not json");
    }
    SECTION("a build changed the groups") {
        writeBuilt(f.folder, withKitchen(f.project.plan));
    }
    CHECK_FALSE(f.guard.show(f.folder, f.project, old, f.now));
    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(f.guard.ticket(), f.now, addChange("Lamps")), f.folder, before);
}

TEST_CASE("a review from before the guard's own write is out of date", "[core][exportguard]") {
    GuardFixture f("guard-own-old");
    const ExportGroupReview old = f.review();
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
    CHECK_FALSE(f.guard.show(f.folder, f.project, old, f.now));
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
}

TEST_CASE("a guard with no project refuses every write", "[core][exportguard]") {
    GuardFixture f("guard-none");
    const auto before = filesOf(f.folder);
    ExportEditGuard empty;
    CHECK_FALSE(empty.holds(empty.ticket()));
    CHECK_FALSE(empty.edit(empty.ticket(), f.now, addChange("Lamps")).status.ok());
    CHECK_FALSE(empty.setKit(f.now, true).status.ok());
    CHECK_FALSE(empty.repair(empty.ticket(), f.now).status.ok());

    f.guard.show({}, f.project, ExportGroupReview{}, f.now);
    CHECK_FALSE(f.guard.holds(f.t));
    CHECK_FALSE(f.guard.edit(f.t, f.now, addChange("Lamps")).status.ok());
    CHECK_FALSE(f.guard.setKit(f.now, true).status.ok());
    CHECK(filesOf(f.folder) == before);
}

TEST_CASE("the default ticket is never good", "[core][exportguard]") {
    GuardFixture f("guard-default");
    CHECK_FALSE(f.guard.holds(ExportEditTicket{}));
    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(ExportEditTicket{}, f.now, addChange("Lamps")), f.folder, before);
}

TEST_CASE("a ticket taken while locked is not good after the reload", "[core][exportguard]") {
    GuardFixture f("guard-locked-ticket");
    ExportEditTicket taken;
    SECTION("a change lock") {
        f.outside(heroAndShell());
        f.guard.folderChanged(f.now);
        taken = f.guard.ticket();
    }
    SECTION("a session lock") {
        startSession(f.folder, f.now);
        CHECK(f.guard.folderChanged(f.now));
        taken = f.guard.ticket();
        requireOk(removeSession(f.folder));
        CHECK(f.guard.folderChanged(f.now));
        CHECK(f.guard.state() == Decision::Locked);
    }
    REQUIRE(f.show());
    CHECK_FALSE(f.guard.holds(taken));
    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(taken, f.now, addChange("Lamps")), f.folder, before);
}

TEST_CASE("a ticket taken after a change nobody noticed is refused", "[core][exportguard]") {
    GuardFixture f("guard-late-ticket");
    f.outside(addedGroup("Props"));
    const ExportEditTicket late = f.guard.ticket();
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(late, f.now, moveChange({"item", "i1"}, "group_1"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK(f.guard.state() == Decision::ChangedOutside);
}

TEST_CASE("a change that was put back does not make the old ticket good", "[core][exportguard]") {
    GuardFixture f("guard-aba");
    f.outside(addedGroup("Props"));
    f.guard.folderChanged(f.now);
    f.outside(addedGroup("Hero"));
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")), f.folder, before);
}

TEST_CASE("an outside undo of the guard's own write is a change", "[core][exportguard]") {
    GuardFixture f("guard-undo");
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
    f.outside(addedGroup("Hero"));
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.guard.ticket(), f.now, addChange("Lamps"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("a saved edit ends its ticket and the next one works before a reload", "[core][exportguard]") {
    GuardFixture f("guard-saved");
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
    CHECK_FALSE(f.guard.holds(f.t));
    const auto before = filesOf(f.folder);
    requireRefused(f.guard.edit(f.t, f.now, addChange("Lamps")), f.folder, before);

    requireSaved(f.guard.edit(f.guard.ticket(), f.now, addChange("Lamps")));
    CHECK(nameOfGroup(f.review(), "group_1") == "Mine");
    CHECK(nameOfGroup(f.review(), "group_2") == "Lamps");
}

TEST_CASE("a refused change writes nothing and keeps the ticket", "[core][exportguard]") {
    GuardFixture f("guard-bad-change");
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, renameChange("group_1", "Walls"));
    CHECK(result.decision == Decision::Allowed);
    CHECK(result.status.code == ErrorCode::AlreadyExists);
    CHECK(filesOf(f.folder) == before);
    CHECK(f.guard.holds(f.t));
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
}

TEST_CASE("a ticket dies when another folder or mode is shown, also after coming back", "[core][exportguard]") {
    GuardFixture f("guard-elsewhere");
    const fs::path q = f.dir.path() / "Q";
    fs::create_directories(q);
    Project other = flatWithSofa();
    requireOk(createProject(other, q, q));
    requireOk(writeExportOptions(q, addedGroup("Hero")));
    writeBuilt(q, other.plan);

    SECTION("another folder with the same ids") {
        REQUIRE(f.guard.show(q, other, reviewExportGroups(q, other), f.now));
    }
    SECTION("another folder and back") {
        REQUIRE(f.guard.show(q, other, reviewExportGroups(q, other), f.now));
        REQUIRE(f.show());
    }
    SECTION("single and back") {
        f.project.settings.exportMode = ExportMode::Single;
        REQUIRE(f.show());
        f.project.settings.exportMode = ExportMode::Grouped;
        REQUIRE(f.show());
        CHECK(f.guard.message().empty());
    }
    CHECK_FALSE(f.guard.holds(f.t));
    const auto beforeP = filesOf(f.folder);
    const auto beforeQ = filesOf(q);
    requireRefused(f.guard.edit(f.t, f.now, moveChange({"item", "i1"}, "group_1")), f.folder, beforeP);
    CHECK(filesOf(q) == beforeQ);
}

TEST_CASE("a single project takes no group edit and locks on no group change", "[core][exportguard]") {
    GuardFixture f("guard-single");
    f.project.settings.exportMode = ExportMode::Single;
    REQUIRE(f.show());
    const auto before = filesOf(f.folder);
    CHECK_FALSE(f.guard.edit(f.guard.ticket(), f.now, addChange("Lamps")).status.ok());
    CHECK(filesOf(f.folder) == before);

    f.outside(addedGroup("Props"));
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Allowed);
    f.project.settings.exportMode = ExportMode::Grouped;
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
}

TEST_CASE("the kit switch does not end an open edit", "[core][exportguard]") {
    GuardFixture f("guard-kit-edit");
    requireSaved(f.guard.setKit(f.now, true));
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.holds(f.t));
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
    CHECK(engineLinkOptions(f.saved(), kUnrealLinkId).kit);
}

TEST_CASE("the kit switch during a change lock keeps the outside groups and the lock", "[core][exportguard]") {
    GuardFixture f("guard-kit-locked");
    f.outside(addedGroup("Props"));
    SECTION("noticed") {
        f.guard.folderChanged(f.now);
    }
    SECTION("not noticed yet") {
    }
    requireSaved(f.guard.setKit(f.now, true));
    ExportOptions expected = addedGroup("Props");
    expected.links[std::string(kUnrealLinkId)].kit = true;
    CHECK(f.saved() == expected);
    CHECK(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::ChangedOutside);
    CHECK_FALSE(f.guard.holds(f.t));
}

TEST_CASE("the gameplay switch is written like the kit switch", "[core][exportguard][gameplay]") {
    GuardFixture f("guard-gameplay");
    requireSaved(f.guard.setGameplay(f.now, true));
    CHECK(engineLinkOptions(f.saved(), kUnrealLinkId).gameplay);
    CHECK_FALSE(engineLinkOptions(f.saved(), kUnrealLinkId).kit);
    CHECK_FALSE(f.saved().gameplay.enabled);
    CHECK(f.guard.holds(f.t));
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
    CHECK(engineLinkOptions(f.saved(), kUnrealLinkId).gameplay);
    requireSaved(f.guard.setGameplay(f.now, false));
    CHECK_FALSE(engineLinkOptions(f.saved(), kUnrealLinkId).gameplay);
    CHECK(f.saved().links.empty());

    writeText(optionsFile(f.folder), "{not json");
    const auto before = filesOf(f.folder);
    CHECK_FALSE(f.guard.setGameplay(f.now, true).status.ok());
    CHECK(filesOf(f.folder) == before);
}

TEST_CASE("the kit switch never writes over a file that cannot be read", "[core][exportguard]") {
    GuardFixture f("guard-kit-bad");
    writeText(optionsFile(f.folder), "{not json");
    const auto before = filesOf(f.folder);
    CHECK_FALSE(f.guard.setKit(f.now, true).status.ok());
    CHECK(filesOf(f.folder) == before);
}

TEST_CASE("what is not a change leaves the ticket good", "[core][exportguard]") {
    GuardFixture f("guard-not-change");
    SECTION("another layout of the JSON") {
        writeText(optionsFile(f.folder),
                  "{ \"version\": 1, \"groups\": {\"added\": [{\"name\": \"Hero\", \"id\": \"group_1\"}]},\r\n"
                  "  \"format\": \"plynth-export-options\" }");
    }
    SECTION("the project's name, which changes the file names") {
        f.project.settings.name = "Penthouse";
    }
    SECTION("the project's plan while a build gives the groups") {
        f.project.plan = withKitchen(f.project.plan);
    }
    SECTION("a build that changed no group") {
        writeBuilt(f.folder, withMovedCorner(f.project.plan));
    }
    SECTION("another file") {
        writeText(f.folder / "renders" / "check.txt", "x");
    }
    CHECK_FALSE(f.guard.folderChanged(f.now));
    REQUIRE(f.show());
    CHECK(f.guard.holds(f.t));
    CHECK(f.guard.message().empty());
    requireSaved(f.guard.edit(f.t, f.now, renameChange("group_1", "Mine")));
}

TEST_CASE("other saved edits are a change even when every name looks the same", "[core][exportguard]") {
    GuardFixture f("guard-same-look");
    ExportOptions options = addedGroup("Hero");
    options.groups.renames = {{"structure", "Walls"}};
    f.outside(options);
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, resetChange());
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("a second window's write is a change for the first", "[core][exportguard]") {
    GuardFixture f("guard-two");
    ExportEditGuard second;
    REQUIRE(second.show(f.folder, f.project, f.review(), f.now));
    requireSaved(second.edit(second.ticket(), f.now, renameChange("group_1", "Theirs")));

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, renameChange("group_1", "Mine"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("an edit on a file that broke is refused and locks", "[core][exportguard]") {
    GuardFixture f("guard-broke");
    writeText(optionsFile(f.folder), "{not json");
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, renameChange("group_1", "Mine"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK(f.guard.state() == Decision::ChangedOutside);
}

TEST_CASE("a repair never replaces a file that was fixed meanwhile", "[core][exportguard]") {
    GuardFixture f("guard-fixed");
    SECTION("grouped") {
    }
    SECTION("single") {
        f.project.settings.exportMode = ExportMode::Single;
    }
    writeText(optionsFile(f.folder), "{not json");
    REQUIRE(f.show());
    const ExportEditTicket asked = f.guard.ticket();
    f.outside(heroAndShell());

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.repair(asked, f.now);
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("a repair works in single mode and keeps a readable kit switch", "[core][exportguard]") {
    GuardFixture f("guard-repair-single");
    f.project.settings.exportMode = ExportMode::Single;
    writeText(optionsFile(f.folder), "{\"kit\":{\"enabled\":true},\"broken\":1}");
    REQUIRE(f.show());
    const ExportEditTicket asked = f.guard.ticket();
    requireSaved(f.guard.repair(asked, f.now));
    CHECK(engineLinkOptions(f.saved(), kUnrealLinkId).kit);
    CHECK_FALSE(f.guard.holds(asked));
}

TEST_CASE("a repair never touches a file of a newer Plynth", "[core][exportguard]") {
    GuardFixture f("guard-newer");
    writeText(optionsFile(f.folder), "{\"format\":\"plynth-export-options\",\"version\":99}");
    REQUIRE(f.show());
    const auto before = filesOf(f.folder);
    CHECK_FALSE(f.guard.repair(f.guard.ticket(), f.now).status.ok());
    CHECK(filesOf(f.folder) == before);
}

TEST_CASE("after a repair a new edit works before the reload", "[core][exportguard]") {
    GuardFixture f("guard-repaired");
    writeText(optionsFile(f.folder), "{not json");
    REQUIRE(f.show());
    requireSaved(f.guard.repair(f.guard.ticket(), f.now));
    requireSaved(f.guard.edit(f.guard.ticket(), f.now, addChange("Lamps")));
}

TEST_CASE("a session comes before a change and the change is told afterwards", "[core][exportguard]") {
    GuardFixture f("guard-both");
    f.outside(heroAndShell());
    CHECK(f.guard.folderChanged(f.now));
    startSession(f.folder, f.now);
    CHECK(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Locked);
    CHECK(f.guard.message() == kExportSessionLockMessage);

    requireOk(removeSession(f.folder));
    CHECK(f.guard.folderChanged(f.now));
    CHECK(f.guard.state() == Decision::Locked);
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message() == kExportGroupsChangedMessage);
}

TEST_CASE("only a build gives the build message, saved edits give the outside one", "[core][exportguard]") {
    GuardFixture f("guard-why");
    writeBuilt(f.folder, withKitchen(f.project.plan));
    SECTION("only the build") {
        CHECK(f.guard.folderChanged(f.now));
        REQUIRE(f.show());
        CHECK(f.guard.message() == kExportBuildChangedMessage);
    }
    SECTION("the build and the saved edits") {
        f.outside(heroAndShell());
        REQUIRE(f.show());
        CHECK(f.guard.message() == kExportGroupsChangedMessage);
    }
}

TEST_CASE("the CLI's own session does not lock the CLI", "[core][exportguard]") {
    GuardFixture f("guard-cli-session", true, ExportEditor::Cli);
    requireOk(writeAppState(f.folder, AppState{currentProcess(), false, f.now}));
    startSession(f.folder, f.now);
    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    requireSaved(f.guard.edit(f.guard.ticket(), f.now, renameChange("group_1", "Mine")));
    requireSaved(f.guard.setKit(f.now, true));
}

TEST_CASE("the app having the project open locks a CLI with no session", "[core][exportguard]") {
    GuardFixture f("guard-cli-app", true, ExportEditor::Cli);
    requireOk(writeAppState(f.folder, AppState{currentProcess(), false, f.now}));
    const auto before = filesOf(f.folder);
    ExportEditResult result;
    SECTION("a group edit") {
        result = f.guard.edit(f.t, f.now, renameChange("group_1", "Mine"));
    }
    SECTION("the kit switch") {
        result = f.guard.setKit(f.now, true);
    }
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::Locked);
}

TEST_CASE("a CLI edit on changed groups is refused", "[core][exportguard]") {
    GuardFixture f("guard-cli-change", true, ExportEditor::Cli);
    f.outside(addedGroup("Props"));
    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, moveChange({"item", "i1"}, "group_1"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("H4 the first readable built plan of a built project is compared with the groups that were shown",
          "[core][exportguard]") {
    // With building.blend and no built plan the list is shown and editable, with the project's own plan.
    GuardFixture f("guard-first-plan", false);
    writeText(f.folder / "build" / "building.blend", "blend");
    REQUIRE(f.show());
    const ExportEditTicket shown = f.guard.ticket();
    writeBuilt(f.folder, withBedroom(f.project.plan));

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(shown, f.now, renameChange("furniture_r1", "Lounge"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("H4 an id a build gave to another thing does not take the edit meant for the old one", "[core][exportguard]") {
    GuardFixture f("guard-reused-id");
    // The sofa i1 was deleted and the table placed next got the id i1, because ids count on from the highest one.
    Plan rebuilt = f.project.plan;
    rebuilt.floors[0].items.at(0).asset = "table";
    writeBuilt(f.folder, rebuilt);

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(f.t, f.now, moveChange({"item", "i1"}, "group_1"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
}

TEST_CASE("H1 a room id a build gave to another kind of room does not take the edit meant for the old one",
          "[core][exportguard]") {
    GuardFixture f("guard-reused-room");
    ExportOptions options = addedGroup("Hero");
    options.groups.renames = {{"furniture_r1", "Lounge"}};
    f.outside(options);
    REQUIRE(f.show());
    const ExportEditTicket opened = f.guard.ticket();
    // The renamed group keeps its name and members, so only the row "Floor of Living r1" tells.
    writeBuilt(f.folder, withBedroom(f.project.plan));

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.edit(opened, f.now, moveChange({"floor", "r1"}, "group_1"));
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK(f.guard.state() == Decision::ChangedOutside);

    REQUIRE(f.show());
    CHECK(f.guard.message() == kExportBuildChangedMessage);
    requireNewEditWorks(f);
}

TEST_CASE("H3 a repair needs a file that was shown as unreadable", "[core][exportguard]") {
    GuardFixture f("guard-repair-unasked");
    bool breaks = true;
    SECTION("grouped, broke afterwards") {
    }
    SECTION("single, broke afterwards") {
        f.project.settings.exportMode = ExportMode::Single;
    }
    SECTION("grouped, still readable") {
        breaks = false;
    }
    SECTION("single, still readable") {
        f.project.settings.exportMode = ExportMode::Single;
        breaks = false;
    }
    REQUIRE(f.show());
    const ExportEditTicket opened = f.guard.ticket();
    if (breaks) writeText(optionsFile(f.folder), "{not json");

    const auto before = filesOf(f.folder);
    const ExportEditResult result = f.guard.repair(opened, f.now);
    requireRefused(result, f.folder, before);
    CHECK(result.decision == Decision::ChangedOutside);
    CHECK_FALSE(fs::exists(f.folder / "export-options.json.bad"));
}

TEST_CASE("H7 the user's own plan edit ends an old ticket with no lock", "[core][exportguard]") {
    GuardFixture f("guard-own-plan", false);
    writeText(f.folder / "build" / "building.blend", "blend");
    REQUIRE(f.show());
    const ExportEditTicket opened = f.guard.ticket();
    bool otherRows = true;
    SECTION("a room is added") {
        f.project.plan = withKitchen(f.project.plan);
    }
    SECTION("only a row's label differs") {
        f.project.plan.floors[0].items.at(0).asset = "table";
    }
    SECTION("no row differs") {
        f.project.plan = withMovedCorner(f.project.plan);
        otherRows = false;
    }
    CHECK_FALSE(f.guard.folderChanged(f.now));
    CHECK(f.guard.holds(opened));

    REQUIRE(f.show());
    CHECK(f.guard.state() == Decision::Allowed);
    CHECK(f.guard.message().empty());
    CHECK(f.guard.holds(opened) != otherRows);
    if (otherRows) {
        const auto before = filesOf(f.folder);
        requireRefused(f.guard.edit(opened, f.now, moveChange({"item", "i1"}, "group_1")), f.folder, before);
        CHECK(f.guard.state() == Decision::Allowed);
    }
    requireNewEditWorks(f);
}

namespace {

// A repeatable source of numbers, the same on every compiler.
class GuardNumbers {
public:
    explicit GuardNumbers(std::uint64_t seed) : state_(seed) {}

    // A whole number from 0 up to count, 0 when count is 0.
    std::size_t pick(std::size_t count)
    {
        state_ += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        return count == 0 ? 0 : static_cast<std::size_t>(z % count);
    }

private:
    std::uint64_t state_;
};

// True when a review of the sweep differs from the one before by the header's rule. The sweep always has a
// built plan, so the groups always count in grouped mode.
bool groupsChanged(const ExportGroupReview& earlier, const ExportGroupReview& later)
{
    if (!earlier.reviewable || !later.reviewable) return false;
    if (earlier.status.ok() != later.status.ok()) return true;
    if (!later.status.ok()) return false;
    if (earlier.options.groups != later.options.groups) return true;
    if (earlier.groups.size() != later.groups.size()) return true;
    for (std::size_t i = 0; i < later.groups.size(); ++i) {
        const ExportGroup& a = earlier.groups[i];
        const ExportGroup& b = later.groups[i];
        if (a.id != b.id || a.name != b.name || a.rest != b.rest || a.members != b.members) return true;
        for (const ExportMember& m : b.members) {
            if (exportMemberLabel(earlier.plan, m) != exportMemberLabel(later.plan, m)) return true;
        }
    }
    return false;
}

// A ticket of the sweep and whether it was good when it was taken.
struct SweepTicket {
    ExportEditTicket ticket;
    std::uint64_t round = 0;
    bool open = false;
};

// Two projects under one guard, and what the header's rules say the guard must answer.
struct Sweep {
    explicit Sweep(const std::string& tag) : dir(tag)
    {
        for (const char* name : {"P", "Q"}) {
            const fs::path made = dir.path() / name;
            fs::create_directories(made);
            Project flat = flatWithSofa();
            requireOk(createProject(flat, made, made));
            requireOk(writeExportOptions(made, addedGroup("Hero")));
            writeBuilt(made, flat.plan);
            folders.push_back(made);
            projects.push_back(std::move(flat));
        }
        shown = current();
        REQUIRE(guard.show(here(), project(), shown, now()));
        takeTicket();
    }

    // The folder that is shown.
    const fs::path& here() const { return folders[which]; }

    // The project that is shown.
    Project& project() { return projects[which]; }

    // The time of the step.
    SessionTime now() const { return atSecond(second); }

    // The review of the files as they are.
    ExportGroupReview current() { return reviewExportGroups(here(), project()); }

    // Whether a session runs now.
    bool sessionRuns() const { return readSession(here(), now()).state == SessionState::Active; }

    // Whether editing must be locked.
    bool locked() const { return sessionLock || changeLock; }

    // Whether a ticket must still be good.
    bool good(const SweepTicket& taken) const { return taken.open && taken.round == round && !locked(); }

    // Takes a ticket and remembers whether it must be good.
    void takeTicket()
    {
        tickets.push_back({guard.ticket(), round, !locked()});
        if (tickets.size() > 12) tickets.pop_front();
    }

    // Calls show with the review as it is and works out what that must lead to. elsewhere is true when the
    // folder or the mode is another one than before.
    void showNow(bool elsewhere)
    {
        const ExportGroupReview review = current();
        const bool running = sessionRuns();
        const bool changed = !elsewhere && groupsChanged(shown, review);
        const bool onlyBuild = changed && shown.status.ok() == review.status.ok() && shown.options.groups == review.options.groups;
        if (elsewhere || changed || (running && !sessionLock)) ++round;
        const bool seenBefore = changeLock;
        sessionLock = running;
        changeLock = false;
        shown = review;
        REQUIRE(guard.show(here(), project(), review, now()));
        if (!changed || running) return;
        const std::string why = guard.message();
        if (seenBefore) {
            // Several changes may lie under one lock, so either reason is right.
            const bool told = why == kExportBuildChangedMessage || why == kExportGroupsChangedMessage;
            REQUIRE(told);
        } else {
            REQUIRE(why == (onlyBuild ? kExportBuildChangedMessage : kExportGroupsChangedMessage));
        }
    }

    // Calls folderChanged and works out what that must lead to.
    void folderEvent(bool checkAnswer)
    {
        const bool running = sessionRuns();
        bool started = false;
        if (running && !sessionLock) {
            sessionLock = true;
            started = true;
        }
        if (!changeLock && groupsChanged(shown, current())) {
            changeLock = true;
            started = true;
        }
        if (started) ++round;
        const bool mustShow = started || (!running && locked());
        const bool answer = guard.folderChanged(now());
        if (!checkAnswer) return;
        if (mustShow) REQUIRE(answer);
        if (!mustShow && !locked()) REQUIRE_FALSE(answer);
    }

    // One of the group edits a user can make on what is shown.
    ExportGroupChange someChange(GuardNumbers& numbers, int step) const
    {
        const std::vector<ExportGroup>& groups = shown.groups;
        const std::string fresh = "Name " + std::to_string(step);
        if (groups.empty()) return addChange(fresh);
        const ExportGroup& from = groups[numbers.pick(groups.size())];
        const ExportGroup& other = groups[numbers.pick(groups.size())];
        switch (numbers.pick(8)) {
        case 0: return addChange(fresh);
        case 1: return renameChange(from.id, "");
        case 2: return mergeChange(from.id, other.id);
        case 3:
            if (!from.members.empty() && from.id != other.id) return moveChange(from.members.front(), other.id);
            return addChange(fresh);
        case 4: return renameChange(from.id, from.id == other.id ? std::string("---") : other.name);
        default: return renameChange(from.id, fresh);
        }
    }

    // Edits with one of the tickets and checks what must happen to the file.
    void editNow(const SweepTicket& taken, const ExportGroupChange& change)
    {
        const bool running = sessionRuns();
        const ExportGroupReview review = current();
        const auto before = filesOf(here());
        const bool mayWrite = !running && shown.reviewable && shown.status.ok() && good(taken) && !groupsChanged(shown, review);
        ExportOptions expected = review.options;
        Status wanted;
        if (mayWrite) wanted = change(expected.groups, review.groups);

        const ExportEditResult result = guard.edit(taken.ticket, now(), change);
        if (mayWrite) {
            REQUIRE(result.decision == Decision::Allowed);
            REQUIRE(result.status.code == wanted.code);
            if (!wanted.ok()) {
                REQUIRE(filesOf(here()) == before);
                return;
            }
            REQUIRE(readExportOptions(here()).options == expected);
            shown = current();
            ++round;
            ++saved;
            REQUIRE(guard.message().empty());
            return;
        }
        REQUIRE_FALSE(result.status.ok());
        REQUIRE(filesOf(here()) == before);
        if (shown.reviewable && shown.status.ok()) {
            REQUIRE(result.decision != Decision::Allowed);
            if (running || sessionLock) {
                REQUIRE(result.decision == Decision::Locked);
            } else if (changeLock || good(taken)) {
                REQUIRE(result.decision == Decision::ChangedOutside);
            }
        }
        ++refused;
        folderEvent(false);
    }

    // Repairs with one of the tickets and checks what must happen to the file.
    void repairNow(const SweepTicket& taken)
    {
        if (!shown.reviewable || shown.status.ok()) return;
        const bool running = sessionRuns();
        const bool stillBad = !current().status.ok();
        const auto before = filesOf(here());
        const bool mayWrite = !running && good(taken) && stillBad;
        const ExportEditResult result = guard.repair(taken.ticket, now());
        if (mayWrite) {
            REQUIRE(result.decision == Decision::Allowed);
            REQUIRE(result.status.ok());
            REQUIRE(readExportOptions(here()).status.ok());
            shown = current();
            ++round;
            return;
        }
        REQUIRE(result.decision != Decision::Allowed);
        REQUIRE(filesOf(here()) == before);
        folderEvent(false);
    }

    // Sets the kit and checks that only a session refuses it.
    void kitNow(bool on)
    {
        const bool running = sessionRuns();
        // A session that is over and not shown yet is left to the named tests.
        if (sessionLock && !running) return;
        const ExportGroupReview review = current();
        const auto before = filesOf(here());
        const ExportEditResult result = guard.setKit(now(), on);
        if (!running && review.status.ok()) {
            REQUIRE(result.status.ok());
            ExportOptions expected = review.options;
            EngineLinkOptions link = engineLinkOptions(expected, kUnrealLinkId);
            link.kit = on;
            if (link == EngineLinkOptions{}) expected.links.erase(std::string(kUnrealLinkId));
            else expected.links[std::string(kUnrealLinkId)] = link;
            REQUIRE(readExportOptions(here()).options == expected);
            // A change may be found here or at the next folder event.
            folderEvent(false);
            return;
        }
        if (running) REQUIRE(result.decision == Decision::Locked);
        REQUIRE_FALSE(result.status.ok());
        REQUIRE(filesOf(here()) == before);
        folderEvent(false);
    }

    // Another program edits, breaks or fixes the options.
    void changeOutside(GuardNumbers& numbers, int step)
    {
        if (numbers.pick(8) == 0) {
            writeText(optionsFile(here()), "{not json");
            return;
        }
        const ExportGroupReview review = current();
        ExportOptions options = review.status.ok() ? review.options : addedGroup("Props");
        const std::size_t how = numbers.pick(5);
        if (how == 0) options.links[std::string(kUnrealLinkId)].kit = !engineLinkOptions(options, kUnrealLinkId).kit;
        if (how == 1) options.groups = {};
        if (how == 2) options.groups = addedGroup("Props").groups;
        if (how >= 3 && !review.groups.empty()) {
            const std::string id = review.groups[numbers.pick(review.groups.size())].id;
            requireOk(renameExportGroup(options.groups, review.groups, id, "Outside " + std::to_string(step)));
        }
        requireOk(writeExportOptions(here(), options));
    }

    // Writes another built plan.
    void buildOutside(GuardNumbers& numbers)
    {
        const Plan plan = flatWithSofa().plan;
        const std::size_t how = numbers.pick(5);
        if (how == 0) writeBuilt(here(), plan);
        if (how == 1) writeBuilt(here(), withKitchen(plan));
        if (how == 2) writeBuilt(here(), withBedroom(plan));
        if (how == 3) writeBuilt(here(), withoutSofa(plan));
        if (how == 4) writeBuilt(here(), withMovedCorner(plan));
    }

    // Shows a review taken earlier when it is out of date now.
    void showOld()
    {
        if (!hasOld || !groupsChanged(old, current())) return;
        REQUIRE_FALSE(guard.show(here(), project(), old, now()));
        folderEvent(false);
    }

    // The lock and every ticket must be as the rules say.
    void check() const
    {
        const Decision wanted = sessionLock ? Decision::Locked : changeLock ? Decision::ChangedOutside : Decision::Allowed;
        REQUIRE(guard.state() == wanted);
        if (sessionLock) REQUIRE(guard.message() == kExportSessionLockMessage);
        if (!sessionLock && changeLock) REQUIRE(guard.message() == kExportChangeLockMessage);
        for (const SweepTicket& taken : tickets) REQUIRE(guard.holds(taken.ticket) == good(taken));
    }

    TempDir dir;
    std::vector<fs::path> folders;
    std::vector<Project> projects;
    std::size_t which = 0;
    long long second = 0;
    ExportEditGuard guard;
    ExportGroupReview shown;
    ExportGroupReview old;
    bool hasOld = false;
    bool sessionLock = false;
    bool changeLock = false;
    std::uint64_t round = 0;
    std::deque<SweepTicket> tickets;
    int saved = 0;
    int refused = 0;
};

}

TEST_CASE("a seeded sweep keeps every ticket right across locks, unlocks, folders and modes", "[core][exportguard]") {
    int saved = 0;
    int refused = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        INFO("seed " << seed);
        GuardNumbers numbers(seed);
        Sweep sweep("guard-sweep-" + std::to_string(seed));
        std::string last;
        for (int step = 0; step < 100; ++step) {
            const std::size_t what = numbers.pick(32);
            last += " " + std::to_string(step) + ":" + std::to_string(what);
            if (last.size() > 160) last.erase(0, last.find(' ', 1));
            INFO("steps, step:action" << last);
            if (what < 5) sweep.showNow(false);
            if (what == 5 || what == 6) sweep.folderEvent(true);
            if (what >= 7 && what < 11) sweep.takeTicket();
            if (what == 11 || what == 12) sweep.changeOutside(numbers, step);
            if (what == 13) startSession(sweep.here(), sweep.now());
            if (what == 14 || what == 15) requireOk(removeSession(sweep.here()));
            if (what == 16) sweep.second += 100 + static_cast<long long>(numbers.pick(300));
            if (what == 17) sweep.buildOutside(numbers);
            if (what == 18) {
                Project& project = sweep.project();
                project.settings.exportMode = project.settings.exportMode == ExportMode::Grouped ? ExportMode::Single : ExportMode::Grouped;
                sweep.hasOld = false;
                sweep.showNow(true);
                if (project.settings.exportMode == ExportMode::Single && numbers.pick(4) != 0) {
                    project.settings.exportMode = ExportMode::Grouped;
                    sweep.showNow(true);
                }
            }
            if (what == 19) {
                sweep.which = 1 - sweep.which;
                sweep.hasOld = false;
                sweep.showNow(true);
            }
            if (what >= 20 && what < 27) {
                const bool newest = numbers.pick(4) != 0;
                const SweepTicket taken = newest ? sweep.tickets.back() : sweep.tickets[numbers.pick(sweep.tickets.size())];
                const ExportGroupChange change = sweep.someChange(numbers, step);
                sweep.editNow(taken, change);
            }
            if (what == 27) sweep.kitNow(numbers.pick(2) == 0);
            if (what == 28) sweep.repairNow(sweep.tickets.back());
            if (what == 29) {
                sweep.old = sweep.current();
                sweep.hasOld = true;
            }
            if (what == 30) sweep.showOld();
            if (what == 31) {
                sweep.showNow(false);
                sweep.takeTicket();
            }
            sweep.check();
        }
        saved += sweep.saved;
        refused += sweep.refused;
    }
    CHECK(saved > 20);
    CHECK(refused > 20);
}
