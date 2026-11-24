#include "GuideSupport.h"

#include <plynth/core/Tour.h>

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace plynth;

namespace {

const char* kTour = R"({"format": "plynth-tour", "version": 1,
 "controls": {"home-new": {"name": "New project", "text": "Starts an empty project."},
              "home-sample-*": {"name": "Sample project", "text": "Opens a copy of this sample that you can change."},
              "plan-wall": {"name": "Wall tool", "text": "Draws walls. Click twice to place one."}},
 "tours": [{"id": "plan", "name": "Plan view", "steps": [{"control": "home-new", "screen": "home"},
                                                         {"control": "home-sample-apartment", "screen": "home"},
                                                         {"control": "plan-wall", "screen": "plan"}]}]})";

// The tour text with one part replaced.
std::string tourWith(std::string_view from, std::string_view to)
{
    std::string text = kTour;
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

// A tour with steps a to e on the plan screen.
TourInfo fiveSteps()
{
    TourInfo tour{"plan", "Plan view", {}};
    for (const char* id : {"a", "b", "c", "d", "e"}) tour.steps.push_back({id, "plan"});
    return tour;
}

// A present check that knows only the listed controls.
std::function<bool(const TourStep&)> only(std::set<std::string> ids)
{
    return [ids](const TourStep& step) { return ids.contains(step.control); };
}

}

TEST_CASE("A good tour file is read", "[tour]")
{
    const TourRead read = readTour(kTour);
    REQUIRE(read.status.ok());
    REQUIRE(read.data.controls.size() == 3);
    CHECK(read.data.controls[0] == TourEntry{"home-new", "New project", "Starts an empty project."});
    CHECK(read.data.controls[1].id == "home-sample-*");
    REQUIRE(read.data.tours.size() == 1);
    CHECK(read.data.tours[0].id == "plan");
    CHECK(read.data.tours[0].name == "Plan view");
    REQUIRE(read.data.tours[0].steps.size() == 3);
    CHECK(read.data.tours[0].steps[2] == TourStep{"plan-wall", "plan"});
    CHECK(findTour(read.data, "plan") == &read.data.tours[0]);
    CHECK(findTour(read.data, "wall") == nullptr);
}

TEST_CASE("The tour reader refuses each broken rule", "[tour]")
{
    CHECK(readTour("{").status.code == ErrorCode::BadFile);
    CHECK(readTour(tourWith("plynth-tour", "plynth-samples")).status.where == "/format");
    CHECK(readTour(tourWith("\"version\": 1", "\"version\": 2")).status.code == ErrorCode::UnsupportedVersion);
    CHECK(readTour(tourWith("\"version\": 1", "\"version\": 0")).status.where == "/version");

    const TourRead repeated = readTour(tourWith("\"plan-wall\": {", "\"home-new\": {"));
    CHECK(repeated.status.code == ErrorCode::BadFile);
    CHECK(repeated.status.where == "/controls/home-new");

    CHECK(readTour(tourWith("Starts an empty project.", "")).status.where == "/controls/home-new/text");
    CHECK(readTour(tourWith("Starts an empty project.", "   ")).status.where == "/controls/home-new/text");
    CHECK(readTour(tourWith("\"name\": \"New project\"", "\"name\": \"\"")).status.where == "/controls/home-new/name");
    CHECK(readTour(tourWith("Starts an empty project.", std::string(241, 'a'))).status.where == "/controls/home-new/text");
    CHECK(readTour(tourWith("Starts an empty project.", std::string(240, 'a'))).status.ok());
    CHECK(readTour(tourWith("Starts an empty project.", "One. Two. Three.")).status.where == "/controls/home-new/text");
    CHECK(readTour(tourWith("Starts an empty project.", "Starts a 2.5 m wall. Then stops.")).status.ok());
    CHECK(readTour(tourWith("Starts an empty project.", "Starts a project - empty.")).status.where == "/controls/home-new/text");
    CHECK(readTour(tourWith("Starts an empty project.", "Starts a project -- empty.")).status.where == "/controls/home-new/text");
    CHECK(readTour(tourWith("Starts an empty project.", "Starts a project \xE2\x80\x94 empty.")).status.where ==
          "/controls/home-new/text");
    CHECK(readTour(tourWith("Starts an empty project.", "Starts a two-floor project.")).status.ok());

    CHECK(readTour(tourWith("\"screen\": \"plan\"", "\"screen\": \"garden\"")).status.where == "/tours/0/steps/2/screen");
    CHECK(readTour(tourWith("{\"control\": \"plan-wall\"", "{\"control\": \"plan-door\"")).status.where ==
          "/tours/0/steps/2/control");

    const std::string noSteps = R"({"format": "plynth-tour", "version": 1, "controls": {},
        "tours": [{"id": "plan", "name": "Plan view", "steps": []}]})";
    CHECK(readTour(noSteps).status.where == "/tours/0/steps");

    const std::string twoTours = tourWith("]}]}", R"(]}, {"id": "plan", "name": "Again", "steps": [{"control": "home-new", "screen": "home"}]}]})");
    CHECK(readTour(twoTours).status.where == "/tours/1/id");
}

TEST_CASE("An entry is found by exact id or the longest wildcard", "[tour]")
{
    TourData data;
    data.controls = {{"home-*", "Home", "Home control."},
                     {"home-sample-*", "Sample", "Opens a sample."},
                     {"home-sample-house", "House", "Opens the house."}};
    REQUIRE(tourEntry(data, "home-sample-apartment") != nullptr);
    CHECK(tourEntry(data, "home-sample-apartment")->id == "home-sample-*");
    CHECK(tourEntry(data, "home-new")->id == "home-*");
    CHECK(tourEntry(data, "home-sample-house")->id == "home-sample-house");
    CHECK(tourEntry(data, "plan-wall") == nullptr);
    CHECK(tourEntry(data, "home") == nullptr);
}

TEST_CASE("The tours come in Help menu order", "[tour]")
{
    const std::vector<std::string_view> ids(tourIds().begin(), tourIds().end());
    CHECK(ids == std::vector<std::string_view>{"plan", "wall", "build_export", "assets", "materials"});
    const std::vector<std::string_view> screens(tourScreens().begin(), tourScreens().end());
    CHECK(screens == std::vector<std::string_view>{"home", "plan", "wall", "build", "export", "assets", "materials"});
}

TEST_CASE("Tour steps walk forward and back", "[tour]")
{
    const TourInfo tour = fiveSteps();
    const auto all = only({"a", "b", "c", "d", "e"});
    CHECK(tourStep(tour, std::nullopt, 1, all) == 0u);
    CHECK(tourStep(tour, 0u, 1, all) == 1u);
    CHECK(tourStep(tour, 4u, 1, all) == std::nullopt);
    CHECK(tourStep(tour, std::nullopt, -1, all) == 4u);
    CHECK(tourStep(tour, 3u, -1, all) == 2u);
    CHECK(tourStep(tour, 0u, -1, all) == std::nullopt);
    CHECK(tourStep(tour, 0u, 0, all) == std::nullopt);
    CHECK(tourStep(tour, std::nullopt, 1, {}) == 0u);
}

TEST_CASE("Tour steps skip controls that are not present", "[tour]")
{
    const TourInfo tour = fiveSteps();
    const auto middle = only({"b", "c", "d"});
    CHECK(tourStep(tour, std::nullopt, 1, middle) == 1u);
    CHECK(tourStep(tour, 3u, 1, middle) == std::nullopt);
    CHECK(tourStep(tour, std::nullopt, -1, middle) == 3u);
    CHECK(tourStep(tour, 1u, -1, middle) == std::nullopt);

    const auto gap = only({"a", "e"});
    CHECK(tourStep(tour, 0u, 1, gap) == 4u);
    CHECK(tourStep(tour, 4u, -1, gap) == 0u);

    CHECK(tourStep(tour, std::nullopt, 1, only({})) == std::nullopt);
    CHECK(tourStep(tour, std::nullopt, -1, only({})) == std::nullopt);
    CHECK(tourStep(TourInfo{}, std::nullopt, 1, {}) == std::nullopt);
}

TEST_CASE("The entry check lists each kind of fault", "[tour]")
{
    TourData data;
    data.controls = {{"home-new", "New", "New."}, {"home-sample-*", "Sample", "Sample."}, {"old-button", "Old", "Old."}};
    const std::vector<std::string> ids = {"home-new", "home-sample-apartment", "home-sample-house", "plan-wall",
                                          "plan-wall", "build-run"};
    const std::vector<std::string> expected = {"scale", "room-types", "about"};
    const std::vector<std::string> opened = {"scale", "about"};

    const TourCheck check = checkTourEntries(data, ids, 2, expected, opened);
    CHECK(!check.ok());
    CHECK(check.withoutId == 2);
    CHECK(check.withoutEntry == std::vector<std::string>{"build-run", "plan-wall"});
    CHECK(check.unused == std::vector<std::string>{"old-button"});
    CHECK(check.dialogsNotOpened == std::vector<std::string>{"room-types"});

    const std::vector<std::string> good = {"home-new", "home-sample-x", "old-button"};
    CHECK(checkTourEntries(data, good, 0, expected, expected).ok());
}

TEST_CASE("The tour example validates against its schema", "[tour]")
{
    CHECK(plynth::test::schemaProblem("tour.schema.json", nlohmann::json::parse(kTour)).empty());
    CHECK(!plynth::test::schemaProblem("tour.schema.json", nlohmann::json::parse(tourWith("plynth-tour", "x"))).empty());
}

TEST_CASE("The shipped tour file is read and covers every control id set in the app", "[tour]")
{
    const std::filesystem::path app = PLYNTH_APP_DIR;
    const std::string text = plynth::test::readFile(app / "resources" / "tour.json");
    REQUIRE(!text.empty());
    const TourRead read = readTour(text);
    INFO(read.status.message << " at " << read.status.where);
    REQUIRE(read.status.ok());
    CHECK(plynth::test::schemaProblem("tour.schema.json", nlohmann::json::parse(text)).empty());

    for (std::string_view id : tourIds()) {
        INFO(id);
        const TourInfo* tour = findTour(read.data, id);
        REQUIRE(tour != nullptr);
        CHECK(tour->steps.size() >= 5);
        CHECK(tour->steps.size() <= 12);
    }

    const std::regex literal(R"re(setProperty\("tourId",\s*QStringLiteral\("([a-z0-9-]+)"\)\))re");
    std::size_t found = 0;
    for (const char* folder : {"src", "guide"}) {
        if (!std::filesystem::is_directory(app / folder)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(app / folder)) {
            const auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".h") continue;
            const std::string source = plynth::test::readFile(entry.path());
            for (std::sregex_iterator it(source.begin(), source.end(), literal), end; it != end; ++it) {
                const std::string id = (*it)[1].str();
                INFO(entry.path().filename().string() << ": " << id);
                CHECK(tourEntry(read.data, id) != nullptr);
                ++found;
            }
        }
    }
    CHECK(found > 50);
}
