#include "BuildFixtures.h"
#include "FakeDetector.h"
#include "IdTestSupport.h"
#include "SchemaSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/BuildJob.h>
#include <plynth/core/CommandStack.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/ExportJob.h>
#include <plynth/core/Kit.h>
#include <plynth/core/Materials.h>
#include <plynth/core/commands/DetectionCommands.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

namespace fs = std::filesystem;

// One of the eight prefixes.
std::string anyPrefix()
{
    return GENERATE(std::string("f"), "p", "w", "r", "o", "i", "s", "rf");
}

// A group from two commands.
std::unique_ptr<CommandGroup> pairOf(std::unique_ptr<Command> a, std::unique_ptr<Command> b)
{
    auto group = std::make_unique<CommandGroup>("Both");
    group->add(std::move(a));
    group->add(std::move(b));
    return group;
}

// A plan holding these ids as points of its first floor, so newId sees them in use.
Plan planWith(const std::vector<Id>& ids)
{
    Plan plan = makeEmptyPlan();
    double x = 0.0;
    for (const Id& id : ids) plan.floors[0].points.push_back({id, x += 1.0, 0.0, 0.0});
    return plan;
}

// The same part n times.
std::vector<std::string> repeated(const std::string& part, int n)
{
    return std::vector<std::string>(static_cast<std::size_t>(n), part);
}

// The part made from one output of the engine, worked out here by the rule of the contract.
std::string partFrom(std::uint64_t v)
{
    std::string part(1, static_cast<char>('0' + (((v >> 32) * 10) >> 32)));
    for (int k = 0; k < 7; ++k) part += kIdAlphabet[(v >> (5 * k)) & 31];
    return part;
}

}

TEST_CASE("new ids have the shape and are all different", "[core][ids]")
{
    const std::string prefix = anyPrefix();
    INFO("prefix " << prefix);
    SystemIds system;
    Plan plan = makeEmptyPlan();
    std::set<Id> seen = idsIn(plan);
    for (int n = 0; n < 10000; ++n) {
        const Id id = newId(plan, prefix);
        if (!newIdShape(id, prefix) || !isValidId(id) || !seen.insert(id).second) {
            FAIL("bad or repeated id " << id << " at " << n);
        }
        plan.floors[0].points.push_back({id, static_cast<double>(n), 0.0, 0.0});
    }
    requirePlanValid(plan);
    CHECK(schemaProblemForText("plan.schema.json", planToJson(plan).dump()) == "");
}

TEST_CASE("an id of a removed object is not given out again", "[core][ids]")
{
    const std::string prefix = anyPrefix();
    INFO("prefix " << prefix);
    SystemIds system;
    std::set<Id> given;
    Document doc;
    doc.replace(idBase(prefix), {});
    for (int round = 0; round < 1000; ++round) {
        const Id id = runAdd(doc, addOne(prefix), prefix);
        if (!given.insert(id).second) FAIL("given twice " << id << " in round " << round);
        requireOk(doc.execute(removeOne(prefix, id)));
    }

    TempDir dir("ids-random-reopen");
    Document saved;
    openNew(saved, idBase(prefix), dir.path() / "flat");
    const Id first = runAdd(saved, addOne(prefix), prefix);
    requireOk(saved.execute(removeOne(prefix, first)));
    requireOk(saved.save());
    Document later;
    requireOk(later.open(dir.path() / "flat"));
    CHECK(runAdd(later, addOne(prefix), prefix) != first);
}

TEST_CASE("a part in use is drawn again and a source that only repeats falls back to counting", "[core][ids]")
{
    const Plan plan = planWith({"i3", "i7k3q9xm2"});
    {
        detail::ScopedIdSource source(detail::scriptedIdParts({"7k3q9xm2", "3vk8m2qa"}));
        CHECK(newId(plan, "i") == "i3vk8m2qa");
    }
    detail::ScopedIdSource stuck(detail::scriptedIdParts(repeated("7k3q9xm2", kMaxIdDraws)));
    const Id counted = newId(plan, "i");
    CHECK(counted == "i4");
    CHECK_FALSE(idInUse(plan, counted));
}

TEST_CASE("the fallback counts on while the next number is in use", "[core][ids]")
{
    const Plan plan = planWith({"i999999999", "i1000000000", "i7k3q9xm2"});
    detail::ScopedIdSource stuck(detail::scriptedIdParts(repeated("7k3q9xm2", kMaxIdDraws)));
    const Id id = newId(plan, "i");
    CHECK(id == "i1000000001");
    CHECK_FALSE(idInUse(plan, id));
    detail::ScopedCountedIds counted;
    CHECK(newId(plan, "i") == "i1000000001");
}

TEST_CASE("parts without the shape are skipped", "[core][ids]")
{
    const Plan plan = planWith({"i1", "i2", "i3", "i4", "i5", "i6", "i7", "i8", "i9", "i123456789"});
    detail::ScopedIdSource source(detail::scriptedIdParts(
        {"12345678", "fabcdefg", "7K3Q9XM2", "7k3q9xmi", "7k3q9xml", "7k3q9xmo", "7k3q9xmu", "7k3q9xm", "7k3q9xm22",
         "7k3q9x-2", "", "7k3q9xm2"}));
    const Id id = newId(plan, "i");
    CHECK(id == "i7k3q9xm2");
    for (const Id& old : idsIn(plan)) CHECK(id != old);
    detail::ScopedIdSource bad(detail::scriptedIdParts(repeated("12345678", kMaxIdDraws)));
    CHECK(newId(plan, "i") == "i123456790");
}

TEST_CASE("seeded parts are the same in every run and follow the standard engine", "[core][ids]")
{
    std::vector<std::string> first;
    std::vector<std::string> second;
    detail::IdPartMaker a = detail::seededIdParts(5489);
    detail::IdPartMaker b = detail::seededIdParts(5489);
    for (int n = 0; n < 100; ++n) {
        first.push_back(a());
        second.push_back(b());
    }
    CHECK(first == second);
    CHECK(first.front() == "76nbdfvk");
    detail::IdPartMaker c = detail::seededIdParts(5489);
    std::string last;
    for (int n = 0; n < 10000; ++n) last = c();
    CHECK(last == partFrom(9981545732273789042ULL));
    CHECK(last == "5j3pxq0p");

    std::mt19937_64 engine(77);
    detail::IdPartMaker d = detail::seededIdParts(77);
    for (int n = 0; n < 50; ++n) CHECK(d() == partFrom(engine()));
    detail::IdPartMaker after = detail::scriptedIdParts({"7k3q9xm2"});
    CHECK(after() == "7k3q9xm2");
    CHECK(after() == "1y1q3m5q");
}

TEST_CASE("the product source gives parts of the shape", "[core][ids]")
{
    int withoutLetter = 0;
    for (int n = 0; n < 20000; ++n) {
        const std::string part = detail::systemIdPart();
        REQUIRE(part.size() == kNewIdLength);
        REQUIRE(part[0] >= '0');
        REQUIRE(part[0] <= '9');
        REQUIRE(part.find_first_not_of(kIdAlphabet) == std::string::npos);
        if (!newIdPartShape(part)) ++withoutLetter;
    }
    CHECK(withoutLetter < 20);
}

TEST_CASE("material targets take new ids and still know room types", "[core][ids]")
{
    Project p = idBase();
    Floor& f = p.plan.floors[0];
    for (Wall& w : f.walls) {
        if (w.id == "w1") w.id = "w7k3q9xm2";
    }
    Room room;
    room.id = "r7k3q9xm2";
    room.type = "storage";
    room.walls = {"w7k3q9xm2", "w2", "w3", "w4"};
    f.rooms.push_back(room);
    Stair stair;
    stair.id = "s7k3q9xm2";
    stair.x = 1.0;
    stair.y = 1.0;
    f.stairs.push_back(stair);
    RoofSection section;
    section.id = "rf7k3q9xm2";
    section.floor = "f1";
    section.x = 2.0;
    section.y = 2.0;
    p.plan.roofSections.push_back(section);
    requirePlanValid(p.plan);

    const auto target = [&p](std::string_view text) { return parseMaterialTarget(p.plan, text); };
    const MaterialTargetParse onStair = target("s7k3q9xm2");
    requireOk(onStair.status);
    CHECK(onStair.target.kind == MaterialTarget::Kind::Stair);
    CHECK(onStair.target.object == "s7k3q9xm2");
    const MaterialTargetParse onRoof = target("rf7k3q9xm2");
    requireOk(onRoof.status);
    CHECK(onRoof.target.kind == MaterialTarget::Kind::Roof);
    const MaterialTargetParse onWall = target("w7k3q9xm2.inside");
    requireOk(onWall.status);
    CHECK(onWall.target.kind == MaterialTarget::Kind::Wall);
    CHECK(onWall.target.object == "w7k3q9xm2");
    const MaterialTargetParse onRoom = target("r7k3q9xm2.floor");
    requireOk(onRoom.status);
    CHECK(onRoom.target.kind == MaterialTarget::Kind::Room);
    CHECK(onRoom.target.object == "r7k3q9xm2");
    const MaterialTargetParse byType = target("storage.floor");
    requireOk(byType.status);
    CHECK(byType.target.object == "r7k3q9xm2");

    const MaterialTargetParse noWall = target("w3vk8m2qa.inside");
    CHECK(noWall.status.code == ErrorCode::NotFound);
    CHECK(noWall.status.message == "There is no wall w3vk8m2qa.");
    const MaterialTargetParse noRoom = target("r3vk8m2qa.floor");
    CHECK(noRoom.status.code == ErrorCode::NotFound);
    CHECK(noRoom.status.message == "There is no room r3vk8m2qa.");
    const MaterialTargetParse noStair = target("s3vk8m2qa");
    CHECK(noStair.status.code == ErrorCode::NotFound);
    CHECK(noStair.status.message == "There is no stair s3vk8m2qa.");
    const MaterialTargetParse noRoof = target("rf3vk8m2qa");
    CHECK(noRoof.status.code == ErrorCode::NotFound);
    CHECK(noRoof.status.message == "There is no roof section rf3vk8m2qa.");
    for (const char* old : {"w2.inside", "w3.outside"}) requireOk(target(old).status);
    CHECK(target("study").status.code != ErrorCode::NotFound);

    const MaterialTargetParse noType = target("r2d2.floor");
    CHECK(noType.status.code == ErrorCode::NotFound);
    CHECK(noType.status.message == "There is no room r2d2.");
    Room droid;
    droid.id = "r3vk8m2qb";
    droid.type = "r2d2";
    p.plan.floors[0].rooms.push_back(droid);
    requirePlanValid(p.plan);
    const MaterialTargetParse byOddType = target("r2d2.floor");
    requireOk(byOddType.status);
    CHECK(byOddType.target.kind == MaterialTarget::Kind::Room);
    CHECK(byOddType.target.object == "r3vk8m2qb");
    Room wallLike;
    wallLike.id = "r4vk8m2qc";
    wallLike.type = "w9x";
    p.plan.floors[0].rooms.push_back(wallLike);
    const MaterialTargetParse byWallType = target("w9x.ceiling");
    requireOk(byWallType.status);
    CHECK(byWallType.target.object == "r4vk8m2qc");
}

TEST_CASE("undo and redo give the same ids with a seeded source", "[core][ids]")
{
    const std::string prefix = anyPrefix();
    INFO("prefix " << prefix);
    std::vector<std::set<Id>> runs;
    for (int run = 0; run < 2; ++run) {
        detail::ScopedIdSource source(detail::seededIdParts(42));
        Document doc;
        doc.replace(idBase(prefix), {});
        const Id id = runAdd(doc, addOne(prefix), prefix);
        CHECK(newIdShape(id, prefix));
        const std::set<Id> made = idsIn(doc.plan());
        doc.undo();
        requireOk(doc.redo());
        CHECK(idsIn(doc.plan()) == made);
        runs.push_back(made);
    }
    CHECK(runs[0] == runs[1]);
}

TEST_CASE("undo and redo of a drawn wall with splits and of accepted detection keep every id", "[core][ids]")
{
    detail::ScopedIdSource source(detail::seededIdParts(7));
    WallEnd from;
    from.kind = WallEnd::Kind::OnWall;
    from.id = "w1";
    from.offset = 2.0;
    WallEnd to = from;
    to.id = "w3";
    Document doc;
    doc.replace(idBase(), {});
    requireOk(doc.execute(std::make_unique<DrawWallCommand>("f1", from, to)));
    const std::set<Id> drawn = idsIn(doc.plan());
    CHECK(drawn.size() == idsIn(idBase().plan).size() + 5);
    doc.undo();
    requireOk(doc.redo());
    CHECK(idsIn(doc.plan()) == drawn);

    Project empty = makeNewProject();
    empty.plan.scale.pixelsPerMetre = 100.0;
    Document detected;
    detected.replace(empty, {});
    DetectionResult found = fixedProposals();
    proposeRooms(found);
    requireOk(detected.execute(std::make_unique<ApplyDetectionCommand>("f1", found, true)));
    const std::set<Id> made = idsIn(detected.plan());
    for (const Id& id : made) {
        if (id != "f1") CHECK(newIdShape(id, std::string(1, id[0])));
    }
    detected.undo();
    requireOk(detected.redo());
    CHECK(idsIn(detected.plan()) == made);
}

TEST_CASE("redo refuses when its id was given to another object meanwhile", "[core][ids]")
{
    const std::string kind = GENERATE(std::string("f"), "p", "w", "r", "o", "i", "s", "rf", "split");
    INFO("command " << kind);
    const bool split = kind == "split";
    const Project start = idBase(split ? std::string_view{} : std::string_view(kind));
    const auto command = [&]() -> std::unique_ptr<Command> {
        if (split) return std::make_unique<SplitWallCommand>("w1", 2.0);
        return addOne(kind);
    };

    std::vector<Id> made;
    {
        detail::ScopedIdSource source(detail::seededIdParts(11));
        Project probe = start;
        requireOk(command()->apply(probe));
        for (const Id& id : idsIn(probe.plan)) {
            if (!idsIn(start.plan).contains(id)) made.push_back(id);
        }
    }
    REQUIRE(made.size() == (split ? 2u : 1u));
    for (const Id& id : made) {
        INFO("id " << id);
        detail::ScopedIdSource source(detail::seededIdParts(11));
        CommandStack stack;
        Project p = start;
        requireOk(stack.push(command(), p));
        REQUIRE(idInUse(p.plan, id));
        stack.undo(p);
        p.plan.floors[1].points.push_back({id, 9.0, 9.0, 0.0});
        requirePlanValid(p.plan);
        const Project held = p;
        CHECK(stack.redo(p).code == ErrorCode::AlreadyExists);
        CHECK(p == held);
        CHECK_FALSE(stack.canRedo());
    }
}

TEST_CASE("a batch that repeats one part still gives every add its own id", "[core][ids]")
{
    detail::ScopedIdSource source(detail::scriptedIdParts(repeated("7k3q9xm2", 600)));
    Document doc;
    doc.replace(idBase(), {});
    auto group = std::make_unique<CommandGroup>("Points");
    for (int n = 0; n < 500; ++n) group->add(std::make_unique<AddPointCommand>("f1", 10.0 + n, 10.0));
    const std::set<Id> before = idsIn(doc.plan());
    requireOk(doc.execute(std::move(group)));
    std::set<Id> added;
    for (const Id& id : idsIn(doc.plan())) {
        if (!before.contains(id)) added.insert(id);
    }
    CHECK(added.size() == 500);
    CHECK(added.contains("p7k3q9xm2"));
    requirePlanValid(doc.plan());

    const Project held = doc.project();
    auto failing = std::make_unique<CommandGroup>("Half");
    for (int n = 0; n < 250; ++n) failing->add(std::make_unique<AddPointCommand>("f1", 10.0 + n, 20.0));
    failing->add(std::make_unique<RemoveWallCommand>("w99"));
    for (int n = 0; n < 250; ++n) failing->add(std::make_unique<AddPointCommand>("f1", 10.0 + n, 30.0));
    CHECK_FALSE(doc.execute(std::move(failing)).ok());
    CHECK(doc.project() == held);
}

TEST_CASE("counted ids while a test asks for them, and random inside a source", "[core][ids]")
{
    detail::ScopedCountedIds counted;
    Plan plan = makeEmptyPlan();
    CHECK(newId(plan, "w") == "w1");
    plan.floors[0].points.push_back({"w1", 0.0, 0.0, 0.0});
    CHECK(newId(plan, "w") == "w2");

    ExportGroupEdits edits;
    std::string id;
    requireOk(addExportGroup(edits, {}, "One", id));
    CHECK(id == "group_1");
    requireOk(addExportGroup(edits, applyExportGroupEdits({}, edits, "Flat").groups, "Two", id));
    CHECK(id == "group_2");

    {
        detail::ScopedIdSource source(detail::seededIdParts(3));
        CHECK(newIdShape(newId(plan, "w"), "w"));
        requireOk(addExportGroup(edits, applyExportGroupEdits({}, edits, "Flat").groups, "Three", id));
        CHECK(newIdShape(id, "group_"));
    }
    CHECK(newId(plan, "w") == "w2");
    try {
        detail::ScopedIdSource thrown(detail::seededIdParts(4));
        throw std::runtime_error("leave");
    } catch (const std::runtime_error&) {
    }
    CHECK(newId(plan, "w") == "w2");
}

TEST_CASE("a source set on one thread is used by another", "[core][ids]")
{
    detail::ScopedIdSource source(detail::scriptedIdParts({"7k3q9xm2"}));
    const Plan plan = makeEmptyPlan();
    Id other;
    std::thread worker([&]() { other = newId(plan, "i"); });
    worker.join();
    CHECK(other == "i7k3q9xm2");
}

namespace {

// A grouped project with a square of new ids, a room and an item in it, saved in a temp folder.
struct GroupedFlat {
    explicit GroupedFlat(const std::string& tag) : dir(tag)
    {
        Project p = idBase();
        p.settings.exportMode = ExportMode::Grouped;
        p.settings.name = "Flat";
        openNew(doc, p, folder());
        room = runAdd(doc, std::make_unique<AddRoomCommand>("f1", Vec2{2.0, 2.0}, std::string("living")), "r");
        item = runAdd(doc, placeItem(), "i");
        requireOk(doc.save());
    }

    // The project folder.
    fs::path folder() const { return dir.path() / "flat"; }

    // The groups the export would make now.
    ExportGroupReview review() const { return reviewExportGroups(folder(), doc.project()); }

    TempDir dir;
    Document doc;
    Id room;
    Id item;
};

// The group with this id, or null.
const ExportGroup* groupById(const std::vector<ExportGroup>& groups, const std::string& id)
{
    for (const ExportGroup& g : groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

// Says whether a group has this member.
bool holds(const ExportGroup* group, const ExportMember& member)
{
    return group && std::find(group->members.begin(), group->members.end(), member) != group->members.end();
}

// Writes the built plan the way a build does.
void writeBuilt(const fs::path& folder, const Project& project)
{
    Json built = Json::object();
    built["plan_fingerprint"] = planFingerprint(project);
    built["plan"] = planToJson(project.plan);
    putFile(folder / "build" / "built-plan.json", built.dump());
}

}

TEST_CASE("a saved move of a removed item does not land on the next item", "[core][ids]")
{
    SystemIds system;
    GroupedFlat flat("ids-random-move");
    const ExportMember sofa{std::string(kKindItem), flat.item};
    ExportGroupReview review = flat.review();
    requireOk(review.status);
    ExportOptions options = review.options;
    std::string hero;
    requireOk(addExportGroup(options.groups, review.groups, "Hero", hero));
    CHECK(newIdShape(hero, "group_"));
    const auto current = applyExportGroupEdits(review.automatic, options.groups, "Flat").groups;
    requireOk(moveExportMember(options.groups, current, sofa, hero));
    requireOk(writeExportOptions(flat.folder(), options));
    CHECK(holds(groupById(flat.review().groups, hero), sofa));

    requireOk(flat.doc.execute(removeOne("i", flat.item)));
    requireOk(flat.doc.save());
    Document later;
    requireOk(later.open(flat.folder()));
    const Id table = runAdd(later, placeItem("table"), "i");
    CHECK(table != flat.item);
    const ExportGroupReview after = reviewExportGroups(flat.folder(), later.project());
    requireOk(after.status);
    CHECK(after.notes.size() == 1);
    CHECK(groupById(after.groups, hero)->members.empty());
    CHECK(holds(groupById(after.groups, "furniture_" + flat.room), ExportMember{std::string(kKindItem), table}));
}

TEST_CASE("a saved room name does not land on the next room", "[core][ids]")
{
    SystemIds system;
    GroupedFlat flat("ids-random-rename");
    ExportGroupReview review = flat.review();
    ExportOptions options = review.options;
    requireOk(renameExportGroup(options.groups, review.groups, "furniture_" + flat.room, "Den"));
    requireOk(writeExportOptions(flat.folder(), options));

    requireOk(flat.doc.execute(std::make_unique<RemoveRoomCommand>(flat.room)));
    requireOk(flat.doc.save());
    Document later;
    requireOk(later.open(flat.folder()));
    const Id room = runAdd(later, std::make_unique<AddRoomCommand>("f1", Vec2{2.0, 2.0}, std::string("living")), "r");
    CHECK(room != flat.room);
    const ExportGroupReview after = reviewExportGroups(flat.folder(), later.project());
    const ExportGroup* group = groupById(after.groups, "furniture_" + room);
    REQUIRE(group);
    CHECK(group->name != "Den");
    CHECK(after.notes.size() == 1);
}

TEST_CASE("an unsaved id that was built is not given out after closing", "[core][ids]")
{
    SystemIds system;
    TempDir dir("ids-random-built");
    const fs::path folder = dir.path() / "flat";
    Id first;
    {
        Document doc;
        openNew(doc, idBase(), folder);
        first = runAdd(doc, placeItem(), "i");
        writeBuilt(folder, doc.project());
    }
    Document later;
    requireOk(later.open(folder));
    CHECK(runAdd(later, placeItem(), "i") != first);
}

namespace {

// A repeatable source of numbers for the sweep.
class SweepNumbers {
public:
    // Starts from a seed.
    explicit SweepNumbers(std::uint64_t seed) : state_(seed) {}

    // A number below count.
    std::size_t below(std::size_t count)
    {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::size_t>((state_ >> 33) % count);
    }

private:
    std::uint64_t state_;
};

// One sweep: its seed, its steps and whether the product source makes the ids.
struct SweepRun {
    int seed;
    int steps;
    bool system;
};

}

TEST_CASE("no id ever names two objects or is given out twice in a sweep", "[core][ids][sweep]")
{
    const SweepRun run = GENERATE(SweepRun{1, 2500, false}, SweepRun{2, 2500, false}, SweepRun{3, 2500, false},
                                  SweepRun{4, 2500, false}, SweepRun{5, 5000, true}, SweepRun{6, 5000, true});
    INFO("seed " << run.seed);
    std::optional<detail::ScopedIdSource> source;
    if (run.system) {
        source.emplace(detail::IdPartMaker(&detail::systemIdPart));
    } else {
        source.emplace(detail::seededIdParts(static_cast<std::uint64_t>(run.seed)));
    }
    SweepNumbers random(static_cast<std::uint64_t>(run.seed));
    TempDir dir("ids-random-sweep-" + std::to_string(run.seed));
    const fs::path folder = dir.path() / "flat";
    Document doc;
    openNew(doc, idBase("rf"), folder);
    std::set<Id> seen = idsIn(doc.plan());
    std::set<Id> saved = seen;

    for (int step = 0; step < run.steps; ++step) {
        const std::set<Id> before = idsIn(doc.plan());
        const Project whole = doc.project();
        const std::string prefix(std::array<std::string_view, 8>{"f", "p", "w", "r", "o", "i", "s", "rf"}[random.below(8)]);
        std::vector<Id> removable;
        for (const Id& id : before) {
            if (hasPrefix(id, prefix) && id != "f1") removable.push_back(id);
        }
        const std::size_t action = random.below(12);
        if (action < 6) {
            std::unique_ptr<Command> command;
            if (action < 3 || removable.empty()) {
                command = addOne(prefix);
            } else if (action == 3) {
                command = removeOne(prefix, removable[random.below(removable.size())]);
            } else {
                auto group = std::make_unique<CommandGroup>("Batch");
                group->add(removeOne(prefix, removable.back()));
                group->add(addOne(prefix));
                group->add(addOne("p"));
                if (action == 5) group->add(removeOne("i", "i888888"));
                command = std::move(group);
            }
            if (doc.execute(std::move(command)).ok()) {
                for (const Id& id : idsIn(doc.plan())) {
                    if (before.contains(id)) continue;
                    if (!seen.insert(id).second) FAIL("step " << step << " gave out " << id << " again");
                }
            } else if (!(doc.project() == whole)) {
                FAIL("step " << step << " failed and changed the project");
            }
        } else if (action < 8) {
            doc.undo();
        } else if (action == 8) {
            static_cast<void>(doc.redo());
        } else if (action == 9) {
            requireOk(doc.save());
            saved = idsIn(doc.plan());
        } else if (action == 10) {
            requireOk(doc.reload());
        } else {
            requireOk(doc.open(folder));
            if (!(idsIn(doc.plan()) == saved)) FAIL("step " << step << " opened other ids than were saved");
        }
        for (const Id& id : idsIn(doc.plan())) {
            if (!seen.contains(id)) FAIL("step " << step << " shows an id never given " << id);
        }
        if (!checkPlan(doc.plan()).ok()) FAIL("step " << step << " broke the plan");
    }
}

TEST_CASE("furniture groups of rooms with new ids", "[core][ids]")
{
    SystemIds system;
    Project p = idBase();
    p.settings.name = "Flat";
    Floor& f = p.plan.floors[0];
    const char* ends[4][2] = {{"p11", "p12"}, {"p12", "p13"}, {"p13", "p14"}, {"p14", "p11"}};
    f.points.push_back({"p11", 6.0, 0.0, 0.0});
    f.points.push_back({"p12", 10.0, 0.0, 0.0});
    f.points.push_back({"p13", 10.0, 4.0, 0.0});
    f.points.push_back({"p14", 6.0, 4.0, 0.0});
    for (int k = 0; k < 4; ++k) {
        Wall w;
        w.id = "w1" + std::to_string(k + 1);
        w.from = ends[k][0];
        w.to = ends[k][1];
        w.thickness = 0.2;
        f.walls.push_back(w);
    }
    Document doc;
    doc.replace(p, {});
    const Id left = runAdd(doc, std::make_unique<AddRoomCommand>("f1", Vec2{2.0, 2.0}, std::string("bedroom")), "r");
    const Id right = runAdd(doc, std::make_unique<AddRoomCommand>("f1", Vec2{8.0, 2.0}, std::string("bedroom")), "r");
    runAdd(doc, placeItem("bed", {2.0, 2.0}), "i");
    runAdd(doc, placeItem("bed", {8.0, 2.0}), "i");

    std::vector<ExportGroup> groups = automaticExportGroups(doc.project());
    nameExportGroupFiles(groups, "Flat");
    const std::regex name("^[A-Za-z0-9 _-]*[A-Za-z0-9][A-Za-z0-9 _-]*$");
    const std::regex groupId("^[a-z][a-z0-9_]*$");
    for (const Id& room : {left, right}) {
        INFO("room " << room);
        const ExportGroup* group = groupById(groups, "furniture_" + room);
        REQUIRE(group);
        CHECK(group->name == "Furniture Bedroom " + room);
        CHECK(group->name.size() <= kMaxExportGroupName);
        CHECK(std::regex_match(group->name, name));
        CHECK(std::regex_match(group->id, groupId));
        CHECK(group->file == "Flat_" + exportFileStem(group->name) + ".fbx");
    }
}

namespace {

// Adds a 4 m square room at x shift with its own points and walls. The walls go round in the given id order, so
// one room can have ids that sort along its loop and another the other way round. Inside materials alternate
// when given.
void addSquareRoom(Project& p, double shift, const std::vector<Id>& walls, const Id& room,
                   const std::optional<std::string>& firstMaterial = {}, const std::optional<std::string>& otherMaterial = {})
{
    Floor& f = p.plan.floors[0];
    const double xs[4] = {shift, shift + 4.0, shift + 4.0, shift};
    const double ys[4] = {0.0, 0.0, 4.0, 4.0};
    for (int k = 0; k < 4; ++k) f.points.push_back({room + "p" + std::to_string(k), xs[k], ys[k], 0.0});
    Room r;
    r.id = room;
    r.type = "living";
    for (int k = 0; k < 4; ++k) {
        Wall w;
        w.id = walls[static_cast<std::size_t>(k)];
        w.from = room + "p" + std::to_string(k);
        w.to = room + "p" + std::to_string((k + 1) % 4);
        w.thickness = 0.2;
        w.materialInside = k % 2 == 0 ? firstMaterial : otherMaterial;
        f.walls.push_back(w);
        r.walls.push_back(w.id);
    }
    f.rooms.push_back(r);
}

// The kit of a project, with the test materials.
ModularKit kitOfProject(const Project& p)
{
    return modularKit(p, makeBuildPlan(p, BuildAssetSource{}, sampleMaterialSource()));
}

// A project with no floor content and no default materials.
Project emptyKitProject()
{
    Project p = makeNewProject();
    p.settings.gridSize = 0.5;
    clearMaterialDefaults(p);
    return p;
}

}

TEST_CASE("the kit reuses every piece whatever order the wall ids sort in", "[core][ids]")
{
    // Every corner joins two walls of the same inside material, so who owns the corner cannot show.
    const std::optional<std::string> material =
        GENERATE(std::optional<std::string>{}, std::optional<std::string>("white_paint"));
    INFO("material " << material.value_or("none"));
    const std::vector<Id> along{"w1kmnpqr", "w3kmnpqr", "w5kmnpqr", "w7kmnpqr"};
    const std::vector<Id> back{"w9kmnpqr", "w8kmnpqr", "w6kmnpqr", "w4kmnpqr"};

    Project one = emptyKitProject();
    addSquareRoom(one, 0.0, along, "r1kmnpqr", material, material);
    Project two = one;
    addSquareRoom(two, 6.0, back, "r2kmnpqr", material, material);
    requirePlanValid(two.plan);

    const ModularKit a = kitOfProject(one);
    const ModularKit b = kitOfProject(two);
    REQUIRE_FALSE(a.pieces.empty());
    REQUIRE(b.pieces.size() == a.pieces.size());
    for (std::size_t k = 0; k < a.pieces.size(); ++k) {
        INFO("piece " << a.pieces[k].id);
        CHECK(b.pieces[k].id == a.pieces[k].id);
        CHECK(b.pieces[k].count == 2 * a.pieces[k].count);
    }
}

TEST_CASE("corners of differing materials keep the first room's pieces", "[core][ids]")
{
    // The second room may get other corner pieces, as its corner owners differ; the first room must not change.
    const std::vector<Id> along{"w1kmnpqr", "w3kmnpqr", "w5kmnpqr", "w7kmnpqr"};
    const std::vector<Id> back{"w9kmnpqr", "w8kmnpqr", "w6kmnpqr", "w4kmnpqr"};
    Project one = emptyKitProject();
    addSquareRoom(one, 0.0, along, "r1kmnpqr", std::string("white_paint"), std::string("tile"));
    Project two = one;
    addSquareRoom(two, 6.0, back, "r2kmnpqr", std::string("white_paint"), std::string("tile"));

    const ModularKit a = kitOfProject(one);
    const ModularKit b = kitOfProject(two);
    std::set<std::string> inB;
    for (const KitPiece& piece : b.pieces) inB.insert(piece.id);
    for (const KitPiece& piece : a.pieces) {
        INFO("piece " << piece.id);
        CHECK(inB.contains(piece.id));
    }
    const auto firstRoom = [&along](const ModularKit& kit) {
        std::vector<KitPlacement> out;
        for (const KitPlacement& at : kit.placements) {
            const bool onWall = std::find(along.begin(), along.end(), at.wall) != along.end();
            if (onWall || at.point.starts_with("r1kmnpqr")) out.push_back(at);
        }
        return out;
    };
    const std::vector<KitPlacement> before = firstRoom(a);
    const std::vector<KitPlacement> after = firstRoom(b);
    REQUIRE(before.size() == a.placements.size());
    CHECK(std::is_permutation(before.begin(), before.end(), after.begin(), after.end()));
}

TEST_CASE("jobs and the kit layout of a plan with new ids fit their schemas", "[core][ids]")
{
    SystemIds system;
    TempDir dir("ids-random-jobs");
    Project start = makeNewProject();
    start.settings.name = "Flat";
    start.settings.exportMode = ExportMode::Grouped;
    clearMaterialDefaults(start);
    Document doc;
    doc.replace(start, {});
    const Vec2 corners[4] = {{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}};
    Id firstPoint;
    Id lastPoint;
    Id firstWall;
    for (int k = 0; k < 4; ++k) {
        const WallEnd from = k == 0 ? WallEnd::newPoint(corners[0]) : WallEnd::atPoint(lastPoint);
        const WallEnd to = k == 3 ? WallEnd::atPoint(firstPoint) : WallEnd::newPoint(corners[k + 1]);
        auto draw = std::make_unique<DrawWallCommand>("f1", from, to);
        const DrawWallCommand* made = draw.get();
        requireOk(doc.execute(std::move(draw)));
        if (k == 0) {
            firstPoint = made->fromPointId();
            firstWall = made->wallId();
        }
        lastPoint = made->toPointId();
    }
    runAdd(doc, std::make_unique<AddRoomCommand>("f1", Vec2{2.0, 2.0}, std::string("living")), "r");
    runAdd(doc, std::make_unique<AddOpeningCommand>(doorOn(firstWall, 2.0)), "o");
    runAdd(doc, placeItem(), "i");
    runAdd(doc, std::make_unique<AddFloorCommand>("Upper"), "f");
    for (const Id& id : idsIn(doc.plan())) {
        if (id != "f1") CHECK(newIdShape(id, std::string(1, id[0])));
    }

    const Project& project = doc.project();
    const BuildPlan plan = makeBuildPlan(project, BuildAssetSource{}, sampleMaterialSource());
    const Json build = makeBuildJob(project, plan, BuildSettings{}, dir.path(), "build-1a2b3c4d", dir.path() / "result.json");
    CHECK(schemaProblem("blender-job.schema.json", nlohmann::json::parse(build.dump())) == "");

    std::vector<ExportGroup> groups = automaticExportGroups(project);
    nameExportGroupFiles(groups, project.settings.name);
    const ModularKit kit = modularKit(project, plan);
    const Json job = makeExportJob(project, groups, {}, ExportBuildState{}, ExportSettings{}, dir.path(),
                                   dir.path() / "export" / ".staging", "export-1a2b3c4d", dir.path() / "r.json", &kit);
    CHECK(schemaProblem("blender-job.schema.json", nlohmann::json::parse(job.dump())) == "");
    CHECK(schemaProblem("kit-layout.schema.json", nlohmann::json::parse(kitLayoutToJson(kit, "Flat", "Flat_Kit.fbx").dump())) ==
          "");
}

TEST_CASE("a plan with new ids saves, loads and keeps its fingerprint", "[core][ids]")
{
    SystemIds system;
    TempDir dir("ids-random-save");
    Document doc;
    openNew(doc, idBase(), dir.path() / "flat");
    for (const std::string_view prefix : {"p", "i", "s", "f"}) runAdd(doc, addOne(prefix), prefix);
    requireOk(doc.save());
    const Json written = planToJson(doc.plan());
    CHECK(written.at("version") == kPlanFormatVersion);
    CHECK((kPlanFormatVersion >= 5 && kPlanFormatVersion <= 7));
    const PlanRead read = planFromJson(written);
    requireOk(read.status);
    CHECK_FALSE(read.migrated);
    CHECK(read.plan == doc.plan());
    Document later;
    requireOk(later.open(dir.path() / "flat"));
    CHECK(later.plan() == doc.plan());

    const std::string fingerprint = planFingerprint(later.project());
    runAdd(later, placeItem("table", {3.0, 3.0}), "i");
    later.undo();
    CHECK(planFingerprint(later.project()) == fingerprint);
    CHECK_FALSE(later.isModified());
}
