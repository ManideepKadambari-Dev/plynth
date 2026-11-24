#include "SampleProject.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/detection/Detector.h>
#include <plynth/core/detection/GrayImage.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

namespace {

// A detector for the tests: a 6 by 4 m building from (1, 1) with 0.3 m walls, split by a 0.15 m wall at x 4,
// and a window in the top wall. It ignores the image except for its size.
class FixedDetector : public plynth::Detector {
public:
    std::string_view id() const override { return "test-fixed"; }
    std::string_view name() const override { return "Fixed"; }
    std::string_view description() const override { return "Returns the same building every time."; }

    plynth::DetectionResult detect(const plynth::GrayImage& image, double, const plynth::DetectionParams&,
                                   std::stop_token stop) const override {
        plynth::DetectionResult result;
        result.imageWidth = image.width;
        result.imageHeight = image.height;
        if (stop.stop_requested()) {
            result.cancelled = true;
            return result;
        }
        result.points = {{1, 1}, {4, 1}, {7, 1}, {7, 5}, {4, 5}, {1, 5}};
        result.walls = {{0, 1, 0.3}, {1, 2, 0.3}, {2, 3, 0.3}, {3, 4, 0.3}, {4, 5, 0.3}, {5, 0, 0.3}, {1, 4, 0.15}};
        result.openings = {{0, plynth::OpeningType::Window, 1.5, 1.2}};
        return result;
    }
};

const plynth::DetectorRegistrar fixedRegistrar(std::make_unique<FixedDetector>());

// True when the image loader can read the sample. Tests that run a detector need it.
bool loaderWorks() {
    return plynth::loadGrayImage(PLYNTH_SAMPLE_PLAN).status.ok();
}

// A sample project with the scale 100 pixels per metre.
void setScale(const Sample& sample) {
    REQUIRE(sample.run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
}

// Runs the fixed detector on a sample project.
plynth::cli::testing::Outcome detectFixed(const Sample& sample, std::vector<std::string> extra = {}) {
    std::vector<std::string> args = {"detect", "--detector", "test-fixed"};
    args.insert(args.end(), extra.begin(), extra.end());
    return sample.run(args);
}

}

TEST_CASE("detect lists the proposals with the documented fields", "[cli][detect]") {
    REQUIRE(loaderWorks());
    Sample sample("detect-shape");
    setScale(sample);
    const auto before = readFile(sample.folder / "plan.json");

    const auto outcome = detectFixed(sample);
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("floor") == "f1");
    CHECK(result.at("detector") == "test-fixed");
    CHECK(result.at("pixels_per_metre") == 100.0);
    CHECK(result.at("image").at("width") == 2400);
    CHECK(result.at("image").at("height") == 1600);
    CHECK(result.at("elapsed_ms").is_number());
    const auto detectors = result.at("detectors").get<std::vector<std::string>>();
    CHECK(std::ranges::find(detectors, "test-fixed") != detectors.end());

    const auto& params = result.at("params");
    CHECK(params.at("min_thickness") == 0.08);
    CHECK(params.at("max_thickness") == 0.5);
    CHECK(params.at("sensitivity") == 0.5);
    CHECK(params.at("min_length") == 0.3);
    CHECK(params.at("max_opening_width") == 2.5);
    CHECK(params.at("rooms") == true);
    CHECK(params.at("openings") == true);

    const auto& counts = result.at("counts");
    CHECK(counts.at("points") == 6);
    CHECK(counts.at("walls") == 7);
    CHECK(counts.at("new_walls") == 7);
    CHECK(counts.at("existing_walls") == 0);
    CHECK(counts.at("overlapping_walls") == 0);
    CHECK(counts.at("rooms") == 2);
    CHECK(counts.at("new_rooms") == 2);
    CHECK(counts.at("openings") == 1);
    CHECK(counts.at("new_openings") == 1);
    CHECK(counts.at("existing_openings") == 0);

    REQUIRE(result.at("points").size() == 6);
    CHECK(result.at("points").at(1).at("x") == 4.0);
    REQUIRE(result.at("walls").size() == 7);
    const auto& wall = result.at("walls").at(0);
    CHECK(wall.at("from") == 0);
    CHECK(wall.at("to") == 1);
    CHECK(wall.at("thickness") == 0.3);
    CHECK_THAT(wall.at("length").get<double>(), WithinAbs(3.0, 1e-9));
    CHECK(wall.at("state") == "new");
    REQUIRE(result.at("openings").size() == 1);
    const auto& opening = result.at("openings").at(0);
    CHECK(opening.at("type") == "window");
    CHECK(opening.at("wall") == 0);
    CHECK(opening.at("hinge") == "start");
    CHECK(opening.at("swing") == "right");
    CHECK(opening.at("state") == "new");
    CHECK_THAT(opening.at("x").get<double>(), WithinAbs(2.5, 1e-9));
    CHECK_THAT(opening.at("y").get<double>(), WithinAbs(1.0, 1e-9));
    REQUIRE(result.at("rooms").size() == 2);
    const auto& room = result.at("rooms").at(0);
    CHECK(room.at("walls").is_array());
    CHECK(room.at("inside").at("x").is_number());
    CHECK(room.at("centre_line_area").get<double>() > 0.0);
    CHECK(room.at("state") == "new");
    CHECK_FALSE(result.contains("applied"));
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("detect --apply adds the walls and rooms and a second apply changes nothing", "[cli][detect]") {
    REQUIRE(loaderWorks());
    Sample sample("detect-apply");
    setScale(sample);

    const auto applied = detectFixed(sample, {"--apply"});
    REQUIRE(applied.exitCode == kExitOk);
    const auto& list = applied.answer.at("result").at("applied");
    CHECK(list.at("walls").size() == 7);
    CHECK(list.at("points").size() == 6);
    CHECK(list.at("split_walls").empty());
    CHECK(list.at("rooms").size() == 2);
    CHECK(list.at("openings").size() == 1);
    CHECK(list.at("skipped_openings") == 0);
    CHECK(list.at("skipped_walls") == 0);
    CHECK(list.at("skipped_rooms") == 0);
    CHECK(applied.answer.at("result").at("walls").at(0).at("state") == "new");

    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == 7);
    const auto openings = sample.run({"opening", "list"});
    REQUIRE(openings.exitCode == kExitOk);
    REQUIRE(openings.answer.at("result").at("openings").size() == 1);
    const auto& added = openings.answer.at("result").at("openings").at(0);
    CHECK(added.at("type") == "window");
    CHECK(added.at("sill") == 0.9);
    CHECK(added.at("height") == 1.2);
    CHECK(openings.answer.at("result").at("problems") == 0);
    const auto rooms = sample.run({"room", "list"}).answer.at("result").at("rooms");
    CHECK(rooms.size() == 2);
    const auto found = sample.run({"room", "find"}).answer.at("result").at("areas");
    CHECK(found.size() == 2);
    for (const auto& area : found) {
        CHECK(area.at("room").is_string());
    }

    const auto saved = readFile(sample.folder / "plan.json");
    const auto again = detectFixed(sample, {"--apply"});
    REQUIRE(again.exitCode == kExitOk);
    const auto& empty = again.answer.at("result").at("applied");
    CHECK(empty.at("walls").empty());
    CHECK(empty.at("points").empty());
    CHECK(empty.at("rooms").empty());
    CHECK(empty.at("openings").empty());
    CHECK(again.answer.at("result").at("counts").at("new_openings") == 0);
    CHECK(again.answer.at("result").at("counts").at("existing_openings") == 1);
    CHECK(again.answer.at("result").at("openings").at(0).at("state") == "existing");
    CHECK(again.answer.at("result").at("counts").at("new_walls") == 0);
    CHECK(again.answer.at("result").at("counts").at("new_rooms") == 0);
    CHECK(again.answer.at("result").at("counts").at("existing_walls") == 7);
    CHECK(readFile(sample.folder / "plan.json") == saved);

    const auto plain = detectFixed(sample);
    CHECK(plain.answer.at("result").at("counts").at("new_walls") == 0);
    CHECK_FALSE(plain.answer.at("result").contains("applied"));
}

TEST_CASE("a wall drawn by hand comes back existing and is not added twice", "[cli][detect]") {
    REQUIRE(loaderWorks());
    Sample sample("detect-existing");
    setScale(sample);
    REQUIRE(sample.draw({"--from-x", "1", "--from-y", "1", "--to-x", "4", "--to-y", "1", "--thickness", "0.3"})
                .exitCode == kExitOk);

    const auto listed = detectFixed(sample);
    REQUIRE(listed.exitCode == kExitOk);
    const auto& counts = listed.answer.at("result").at("counts");
    CHECK(counts.at("existing_walls") == 1);
    CHECK(counts.at("new_walls") == 6);
    CHECK(listed.answer.at("result").at("walls").at(0).at("state") == "existing");

    const auto applied = detectFixed(sample, {"--apply"});
    REQUIRE(applied.exitCode == kExitOk);
    CHECK(applied.answer.at("result").at("applied").at("walls").size() == 6);
    CHECK(applied.answer.at("result").at("applied").at("skipped_walls") == 1);
    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == 7);
}

TEST_CASE("detect --no-rooms and --no-openings leave them out", "[cli][detect]") {
    REQUIRE(loaderWorks());
    Sample sample("detect-flags");
    setScale(sample);

    const auto plain = detectFixed(sample, {"--no-rooms", "--no-openings"});
    REQUIRE(plain.exitCode == kExitOk);
    const auto& result = plain.answer.at("result");
    CHECK(result.at("params").at("rooms") == false);
    CHECK(result.at("params").at("openings") == false);
    CHECK(result.at("rooms").empty());
    CHECK(result.at("openings").empty());
    CHECK(result.at("counts").at("rooms") == 0);

    const auto applied = detectFixed(sample, {"--no-rooms", "--apply"});
    REQUIRE(applied.exitCode == kExitOk);
    CHECK(applied.answer.at("result").at("applied").at("walls").size() == 7);
    CHECK(applied.answer.at("result").at("applied").at("rooms").empty());
    CHECK(sample.run({"room", "list"}).answer.at("result").at("rooms").empty());
}

TEST_CASE("detect refuses without a scale and leaves the plan alone", "[cli][detect]") {
    Sample sample("detect-noscale");
    const auto before = readFile(sample.folder / "plan.json");

    for (const char* apply : {"", "--apply"}) {
        std::vector<std::string> extra;
        if (std::string(apply) == "--apply") {
            extra.push_back(apply);
        }
        const auto outcome = detectFixed(sample, extra);
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "invalid_value");
        CHECK(outcome.answer.at("error").at("message").get<std::string>().starts_with("Set the scale first."));
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("detect refuses a floor without an image", "[cli][detect]") {
    plynth::cli::testing::TempFolder temp("detect-noimage");
    const std::string folder = plynth::cli::testing::utf8(temp.path() / "empty");
    REQUIRE(runChecked({"project", "new", "--project", folder}).exitCode == kExitOk);
    const auto outcome = runChecked({"detect", "--detector", "test-fixed", "--project", folder});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");
}

TEST_CASE("detect refuses an unknown detector and an unknown floor", "[cli][detect]") {
    Sample sample("detect-unknown");
    setScale(sample);
    const auto before = readFile(sample.folder / "plan.json");

    const auto detector = sample.run({"detect", "--detector", "nope"});
    CHECK(detector.exitCode == kExitFailed);
    CHECK(errorCode(detector) == "invalid_value");
    const auto message = detector.answer.at("error").at("message").get<std::string>();
    CHECK(message.find("test-fixed") != std::string::npos);

    CHECK(errorCode(sample.run({"detect", "--detector", "test-fixed", "--floor", "f9"})) == "not_found");
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("detect refuses bad option values", "[cli][detect]") {
    Sample sample("detect-bad");
    setScale(sample);
    const auto before = readFile(sample.folder / "plan.json");

    const std::vector<std::vector<std::string>> bad = {
        {"--min-thickness", "0.5", "--max-thickness", "0.2"},
        {"--sensitivity", "1.5"},
        {"--min-length", "0"},
        {"--max-opening-width", "-1"},
    };
    for (const auto& options : bad) {
        const auto outcome = detectFixed(sample, options);
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "invalid_value");
    }
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("detect --apply is refused with locked while the app has the project open", "[cli][detect]") {
    REQUIRE(loaderWorks());
    Sample sample("detect-locked");
    setScale(sample);

    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const auto before = readFile(sample.folder / "plan.json");

    const auto refused = detectFixed(sample, {"--apply"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "locked");
    CHECK(readFile(sample.folder / "plan.json") == before);

    CHECK(detectFixed(sample).exitCode == kExitOk);
}

TEST_CASE("the detect command is listed by help", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    CHECK(std::ranges::find(names, "detect") != names.end());
}

TEST_CASE("detect finds the walls, rooms and openings of apartment-01", "[cli][detect][sample]") {
    REQUIRE(plynth::findDetector("orthogonal") != nullptr);
    REQUIRE(loaderWorks());
    Sample sample("detect-sample");
    setScale(sample);

    const auto outcome = sample.run({"detect"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& counts = outcome.answer.at("result").at("counts");
    CHECK(counts.at("points") == 12);
    CHECK(counts.at("walls") == 16);
    CHECK(counts.at("rooms") == 5);
    CHECK(counts.at("openings") == 9);
    CHECK(counts.at("new_openings") == 9);
    CHECK(counts.at("existing_openings") == 0);
    for (const auto& opening : outcome.answer.at("result").at("openings")) {
        CHECK((opening.at("hinge") == "start" || opening.at("hinge") == "end"));
        CHECK((opening.at("swing") == "left" || opening.at("swing") == "right"));
        CHECK(opening.at("state") == "new");
    }
}

TEST_CASE("detect --apply on apartment-01 adds 16 walls and 5 rooms that match room find", "[cli][detect][sample]") {
    REQUIRE(plynth::findDetector("orthogonal") != nullptr);
    REQUIRE(loaderWorks());
    Sample sample("detect-sample-apply");
    setScale(sample);

    const auto applied = sample.run({"detect", "--apply"});
    REQUIRE(applied.exitCode == kExitOk);
    const auto& result = applied.answer.at("result");
    CHECK(result.at("applied").at("walls").size() == 16);
    CHECK(result.at("applied").at("rooms").size() == 5);
    CHECK(result.at("applied").at("openings").size() == 9);
    CHECK(result.at("applied").at("skipped_openings") == 0);

    const auto openings = sample.run({"opening", "list"});
    CHECK(openings.answer.at("result").at("openings").size() == 9);
    CHECK(openings.answer.at("result").at("problems") == 0);
    const auto second = sample.run({"detect"});
    CHECK(second.answer.at("result").at("counts").at("existing_openings") == 9);
    CHECK(second.answer.at("result").at("counts").at("new_openings") == 0);

    std::vector<double> proposed;
    for (const auto& room : result.at("rooms")) {
        proposed.push_back(room.at("centre_line_area").get<double>());
    }
    std::ranges::sort(proposed);

    CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() == 16);
    const auto rooms = sample.run({"room", "list"}).answer.at("result").at("rooms");
    CHECK(rooms.size() == 5);
    for (const auto& room : rooms) {
        CHECK(room.at("closed") == true);
        CHECK(room.at("area").get<double>() > 0.0);
    }
    std::vector<double> found;
    const auto findAnswer = sample.run({"room", "find"});
    for (const auto& area : findAnswer.answer.at("result").at("areas")) {
        found.push_back(area.at("centre_line_area").get<double>());
    }
    std::ranges::sort(found);
    REQUIRE(found.size() == proposed.size());
    for (std::size_t i = 0; i < found.size(); ++i) {
        CHECK_THAT(found[i], WithinAbs(proposed[i], 1e-3));
    }
}

TEST_CASE("detect --no-openings --apply adds no openings", "[cli][detect][sample]") {
    REQUIRE(plynth::findDetector("orthogonal") != nullptr);
    REQUIRE(loaderWorks());
    Sample sample("detect-no-openings");
    setScale(sample);

    const auto applied = sample.run({"detect", "--no-openings", "--apply"});
    REQUIRE(applied.exitCode == kExitOk);
    CHECK(applied.answer.at("result").at("applied").at("walls").size() == 16);
    CHECK(applied.answer.at("result").at("applied").at("openings").empty());
    CHECK(sample.run({"opening", "list"}).answer.at("result").at("openings").empty());
}

TEST_CASE("detect on a floor with an image offset lists and applies positions shifted by the offset", "[cli][detect]") {
    REQUIRE(loaderWorks());
    Sample sample("detect-offset");
    setScale(sample);
    REQUIRE(sample.run({"floor", "set", "--image-offset", "1,0"}).exitCode == kExitOk);

    const auto listed = detectFixed(sample);
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("points").at(0).at("x") == 2.0);
    CHECK(listed.answer.at("result").at("points").at(0).at("y") == 1.0);

    REQUIRE(detectFixed(sample, {"--apply"}).exitCode == kExitOk);
    const auto points = sample.run({"point", "list"}).answer.at("result").at("points");
    CHECK(points.at(0).at("x") == 2.0);
    CHECK(points.at(0).at("y") == 1.0);
    const auto again = detectFixed(sample);
    CHECK(again.answer.at("result").at("counts").at("new_walls") == 0);
}

TEST_CASE("detect with an image offset counts like core, with and without moved marks", "[cli][detect]") {
    REQUIRE(loaderWorks());
    for (const double offsetX : {0.2, 1.0}) {
        for (const bool marksMoved : {true, false}) {
            INFO("offset " << offsetX << ", marks moved " << marksMoved);
            Sample sample("detect-offset-marks");
            setScale(sample);
            REQUIRE(sample.run({"floor", "set", "--image-offset", std::to_string(offsetX) + ",0"}).exitCode ==
                    kExitOk);
            const std::string x = std::to_string(1.0 + (marksMoved ? offsetX : 0.0));
            REQUIRE(sample.run({"wall", "draw", "--from-x", x, "--from-y", "1", "--to-x", x, "--to-y", "5",
                                "--thickness", "0.3"})
                        .exitCode == kExitOk);

            const auto listed = detectFixed(sample);
            REQUIRE(listed.exitCode == kExitOk);
            const auto& counts = listed.answer.at("result").at("counts");
            CHECK(counts.at("walls") == 7);
            CHECK(counts.at("existing_walls") == (marksMoved ? 1 : 0));
            CHECK(counts.at("overlapping_walls") == 0);
            CHECK(counts.at("new_walls") == (marksMoved ? 6 : 7));

            const auto applied = detectFixed(sample, {"--apply"});
            REQUIRE(applied.exitCode == kExitOk);
            CHECK(applied.answer.at("result").at("applied").at("walls").size() == counts.at("new_walls"));
            CHECK(applied.answer.at("result").at("applied").at("skipped_walls") == counts.at("existing_walls"));
            CHECK(sample.run({"wall", "list"}).answer.at("result").at("walls").size() ==
                  1u + counts.at("new_walls").get<std::size_t>());
        }
    }
}
