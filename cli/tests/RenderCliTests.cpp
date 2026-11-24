#include "SampleProject.h"

#include <plynth/core/Build.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Render.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::CliHost;
using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::utf8;

namespace {

// Writes the start of a PNG of the given size, enough for the size check.
void writePngHead(const std::filesystem::path& file, int width, int height) {
    std::ofstream out(file, std::ios::binary);
    const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.write(reinterpret_cast<const char*>(signature), 8);
    const auto big = [&](unsigned value) {
        const char bytes[4] = {static_cast<char>(value >> 24), static_cast<char>(value >> 16),
                               static_cast<char>(value >> 8), static_cast<char>(value)};
        out.write(bytes, 4);
    };
    big(13);
    out.write("IHDR", 4);
    big(static_cast<unsigned>(width));
    big(static_cast<unsigned>(height));
    const char rest[9] = {8, 6, 0, 0, 0, 0, 0, 0, 0};
    out.write(rest, 9);
}

// A sample project with the square drawn and a stand in build of it.
struct BuiltSample {
    explicit BuiltSample(const std::string& name) : sample(name) {
        sample.drawSquare();
        build();
    }

    // Writes build/built-plan.json and building.fbx for the project as it is saved now.
    void build() const {
        const plynth::ProjectLoad load = plynth::loadProject(sample.folder);
        REQUIRE(load.status.ok());
        std::filesystem::create_directories(sample.folder / "build");
        plynth::Json json = plynth::Json::object();
        json["plan_fingerprint"] = plynth::planFingerprint(load.project);
        json["plan"] = plynth::planToJson(load.project.plan);
        std::ofstream(sample.folder / "build" / "built-plan.json", std::ios::binary) << json.dump(2);
        std::ofstream(sample.folder / "build" / "building.fbx", std::ios::binary) << "FBX";
    }

    // Runs a command on the project with the host.
    Outcome run(std::vector<std::string> args, const CliHost& host) const {
        args.push_back("--project");
        args.push_back(sample.project);
        return runChecked(args, std::filesystem::current_path(), host);
    }

    Sample sample;
};

// A host whose renderer writes a picture of the asked size and keeps what it was asked.
struct FakeHost {
    int calls = 0;
    plynth::RenderOptions seen;
    int cut = -2;

    CliHost get() {
        CliHost host;
        host.render = [this](const plynth::RenderShot& shot, std::stop_token) {
            ++calls;
            seen = shot.plan.options;
            cut = shot.plan.cutFloorIndex;
            writePngHead(shot.temporaryPng, shot.plan.options.width, shot.plan.options.height);
            return plynth::Status{};
        };
        return host;
    }
};

}

TEST_CASE("render saves the default picture and answers with its fields", "[cli][render]") {
    BuiltSample built("render-default");
    FakeHost fake;
    const Outcome outcome = built.run({"render"}, fake.get());
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "render");
    const auto& result = outcome.answer.at("result");
    const std::filesystem::path file = plynth::pathFromUtf8(result.at("file").get<std::string>());
    CHECK(file == built.sample.folder / "renders" / "three-quarter.png");
    CHECK(std::filesystem::exists(file));
    CHECK(result.at("width") == 1280);
    CHECK(result.at("height") == 720);
    CHECK(result.at("view") == "three-quarter");
    CHECK(result.at("yaw") == 45.0);
    CHECK(result.at("pitch") == 25.0);
    CHECK(result.at("floor").is_null());
    CHECK(result.at("roofs") == true);
    CHECK(result.at("current") == true);
    CHECK_FALSE(outcome.answer.contains("warnings"));
    CHECK(outcome.progress.empty());
    CHECK(fake.calls == 1);
}

TEST_CASE("render passes every option to the renderer", "[cli][render]") {
    BuiltSample built("render-options");
    FakeHost fake;
    const Outcome outcome = built.run({"render", "--size", "64x96", "--view", "top", "--yaw", "10", "--pitch", "30",
                                       "--floor", "f1", "--no-roofs", "--out", "renders/Bild ü.png"},
                                      fake.get());
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(fake.seen.width == 64);
    CHECK(fake.seen.height == 96);
    CHECK(fake.seen.view == plynth::RenderView::Top);
    CHECK(*fake.seen.yaw == 10.0);
    CHECK(*fake.seen.pitch == 30.0);
    CHECK_FALSE(fake.seen.showRoofs);
    CHECK(fake.cut == 0);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("floor") == "f1");
    CHECK(result.at("roofs") == false);
    CHECK(result.at("view") == "top");
    CHECK(std::filesystem::exists(built.sample.folder / std::filesystem::path(u8"renders/Bild ü.png")));
}

TEST_CASE("render refuses bad values with a clear code and draws nothing", "[cli][render]") {
    BuiltSample built("render-refusals");
    FakeHost fake;
    const auto refused = [&](std::vector<std::string> args, const std::string& code, int exit) {
        args.insert(args.begin(), "render");
        const Outcome outcome = built.run(args, fake.get());
        INFO(outcome.text);
        CHECK(outcome.exitCode == exit);
        CHECK(errorCode(outcome) == code);
    };
    refused({"--size", "big"}, "invalid_value", kExitFailed);
    refused({"--size", "1280x"}, "invalid_value", kExitFailed);
    refused({"--size", "63x720"}, "invalid_value", kExitFailed);
    refused({"--size", "1280x4097"}, "invalid_value", kExitFailed);
    refused({"--size", "99999999999x720"}, "invalid_value", kExitFailed);
    refused({"--yaw", "360"}, "invalid_value", kExitFailed);
    refused({"--pitch", "1"}, "invalid_value", kExitFailed);
    refused({"--floor", "f9"}, "not_found", kExitFailed);
    refused({"--out", "renders/pic.jpg"}, "invalid_value", kExitFailed);
    refused({"--out", "build/pic.png"}, "invalid_value", kExitFailed);
    refused({"--view", "side"}, "bad_arguments", kExitBadArguments);
    refused({"--yaw", "north"}, "bad_arguments", kExitBadArguments);
    refused({"--bogus"}, "bad_arguments", kExitBadArguments);
    CHECK(fake.calls == 0);
    CHECK_FALSE(std::filesystem::exists(built.sample.folder / "renders"));
}

TEST_CASE("render without a build, a project or a renderer says why", "[cli][render]") {
    Sample sample("render-missing");
    FakeHost fake;
    Outcome outcome = runChecked({"render", "--project", sample.project}, std::filesystem::current_path(), fake.get());
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_found");

    plynth::cli::testing::TempFolder empty("render-empty");
    outcome = runChecked({"render", "--project", utf8(empty.path())}, std::filesystem::current_path(), fake.get());
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "not_a_project");
    CHECK(fake.calls == 0);

    BuiltSample built("render-nohost");
    outcome = built.run({"render"}, CliHost{});
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "graphics_unavailable");
}

TEST_CASE("render of a changed plan still saves the picture with a warning", "[cli][render]") {
    BuiltSample built("render-stale");
    REQUIRE(built.sample.run({"wall", "set", "w1", "--thickness", "0.3"}).exitCode == kExitOk);
    FakeHost fake;
    const Outcome outcome = built.run({"render"}, fake.get());
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("current") == false);
    REQUIRE(outcome.answer.contains("warnings"));
    CHECK(outcome.answer.at("warnings").at(0).at("code") == "build_out_of_date");
}

TEST_CASE("a failing renderer gives its code and keeps the old picture", "[cli][render]") {
    BuiltSample built("render-fails");
    const std::filesystem::path old = built.sample.folder / "renders" / "top.png";
    std::filesystem::create_directories(old.parent_path());
    std::ofstream(old, std::ios::binary) << "OLD";
    CliHost host;
    host.render = [](const plynth::RenderShot&, std::stop_token) {
        return plynth::fail(plynth::ErrorCode::RenderFailed, "Only the background was drawn.");
    };
    const Outcome outcome = built.run({"render", "--view", "top"}, host);
    CHECK(outcome.exitCode == kExitFailed);
    CHECK(errorCode(outcome) == "render_failed");
    CHECK(plynth::cli::testing::readFile(old) == "OLD");
}

TEST_CASE("help lists render with its options", "[cli][render]") {
    const Outcome outcome = runChecked({"help"});
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
        for (const char* wanted : {"--project", "--out", "--size", "--view", "--yaw", "--pitch", "--floor", "--no-roofs"}) {
            INFO(wanted);
            CHECK(std::find(names.begin(), names.end(), std::string(wanted)) != names.end());
        }
    }
    CHECK(found);
    CHECK(plynth::cli::isCommandWord("render"));
}
