#include "SampleProject.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Render.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::Sample;

namespace {

// Says whether the answer's labels hold one of this kind, and with this id when one is given.
bool hasLabel(const nlohmann::json& result, const std::string& kind, const std::string& id = {}) {
    const auto& labels = result.at("labels");
    return std::any_of(labels.begin(), labels.end(), [&](const nlohmann::json& label) {
        return label.at("kind") == kind && (id.empty() || label.at("id") == id);
    });
}

}

TEST_CASE("render --plan saves the plan picture and answers with its fields", "[cli][render][plan]") {
    Sample sample("render-plan");
    sample.drawRoomSquare();
    const Outcome outcome = sample.run({"render", "--plan"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "render");
    const auto& result = outcome.answer.at("result");
    for (const char* key : {"file", "width", "height", "scale", "origin", "floor", "image", "labels"}) {
        INFO(key);
        CHECK(result.contains(key));
    }
    const std::filesystem::path file = plynth::pathFromUtf8(result.at("file").get<std::string>());
    CHECK(file == sample.folder / "renders" / "plan-f1.png");
    REQUIRE(std::filesystem::exists(file));
    int width = 0;
    int height = 0;
    REQUIRE(plynth::readPngSize(file, width, height));
    CHECK(result.at("width") == width);
    CHECK(result.at("height") == height);
    CHECK(std::max(width, height) == 2000);
    CHECK(result.at("scale").get<double>() > 0.0);
    CHECK(result.at("origin").contains("x"));
    CHECK(result.at("origin").contains("y"));
    CHECK(result.at("floor") == "f1");
    CHECK(result.at("image") == true);
    CHECK(hasLabel(result, "floor", "f1"));
    CHECK(hasLabel(result, "wall", "w1"));
    CHECK(hasLabel(result, "room"));
    CHECK_FALSE(outcome.answer.contains("warnings"));
}

TEST_CASE("render --plan takes its size, output and drawing options", "[cli][render][plan]") {
    Sample sample("render-plan-options");
    sample.drawRoomSquare();
    const Outcome outcome =
        sample.run({"render", "--plan", "--size", "500", "--no-image", "--no-labels", "--grid", "--out", "renders/marks.png"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    const std::filesystem::path file = plynth::pathFromUtf8(result.at("file").get<std::string>());
    CHECK(file == sample.folder / "renders" / "marks.png");
    CHECK(std::filesystem::exists(file));
    CHECK(std::max(result.at("width").get<int>(), result.at("height").get<int>()) == 500);
    CHECK(result.at("image") == false);
    CHECK(result.at("labels").empty());
}

TEST_CASE("render --plan needs no build and warns when the plan has no scale", "[cli][render][plan]") {
    Sample sample("render-plan-noscale");
    sample.drawSquare();
    const Outcome outcome = sample.run({"render", "--plan"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("image") == false);
    REQUIRE(outcome.answer.contains("warnings"));
    CHECK(outcome.answer.at("warnings").at(0).at("code") == "no_scale");
    CHECK_FALSE(std::filesystem::exists(sample.folder / "build"));
}

TEST_CASE("render --plan works while a session is active", "[cli][render][plan]") {
    Sample sample("render-plan-session");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"session", "begin"}).exitCode == kExitOk);
    const Outcome outcome = sample.run({"render", "--plan"});
    CHECK(outcome.exitCode == kExitOk);
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("render --plan refuses bad sizes, unknown floors and options of the 3D picture", "[cli][render][plan]") {
    Sample sample("render-plan-refusals");
    sample.drawRoomSquare();

    const Outcome size = sample.run({"render", "--plan", "--size", "800x600"});
    CHECK(size.exitCode == kExitFailed);
    CHECK(errorCode(size) == "invalid_value");

    const Outcome small = sample.run({"render", "--plan", "--size", "20"});
    CHECK(small.exitCode == kExitFailed);
    CHECK(errorCode(small) == "invalid_value");

    const Outcome floor = sample.run({"render", "--plan", "--floor", "f9"});
    CHECK(floor.exitCode == kExitFailed);
    CHECK(errorCode(floor) == "not_found");

    const Outcome view = sample.run({"render", "--plan", "--view", "top"});
    CHECK(view.exitCode == kExitBadArguments);
    CHECK(errorCode(view) == "bad_arguments");

    const Outcome grid = sample.run({"render", "--grid"});
    CHECK(grid.exitCode == kExitBadArguments);
    CHECK(errorCode(grid) == "bad_arguments");

    const Outcome guarded = sample.run({"render", "--plan", "--out", "plans/plan.png"});
    CHECK(guarded.exitCode == kExitFailed);
    CHECK(errorCode(guarded) == "invalid_value");
}

TEST_CASE("help lists the plan picture options of render", "[cli][render][plan]") {
    const Outcome outcome = plynth::cli::testing::runChecked({"help"});
    bool found = false;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        if (command.at("command") != "render") {
            continue;
        }
        found = true;
        std::vector<std::string> names;
        for (const auto& option : command.at("options")) {
            names.push_back(option.at("name").get<std::string>());
        }
        for (const char* wanted : {"--plan", "--no-image", "--no-labels", "--grid"}) {
            INFO(wanted);
            CHECK(std::find(names.begin(), names.end(), std::string(wanted)) != names.end());
        }
    }
    CHECK(found);
}
