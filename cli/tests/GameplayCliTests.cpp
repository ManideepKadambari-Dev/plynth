#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;
using plynth::cli::testing::utf8;

namespace {

// A built square room with a door and two lights and a fake Blender, ready to export.
struct GameplaySample {
    explicit GameplaySample(const std::string& name) : sample(name) {
        sample.drawRoomSquare();
        REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2", "--type", "door"}).exitCode == kExitOk);
        REQUIRE(sample.run({"light", "add", "--x", "3", "--y", "5"}).exitCode == kExitOk);
        REQUIRE(sample.run({"light", "add", "--x", "2", "--y", "4", "--kind", "standing"}).exitCode == kExitOk);
        fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0");
        const auto built = sample.run({"build", "--blender", utf8(fake)});
        REQUIRE(built.exitCode == kExitOk);
    }

    // Runs export with the fake Blender, past any check the fake answers fail.
    Outcome exportNow() const { return sample.run({"export", "--blender", utf8(fake), "--force"}); }

    plynth::cli::testing::GuardedSettingsReset reset;
    Sample sample;
    fs::path fake;
};

}

TEST_CASE("export gameplay shows the switch and on and off change it in links.unreal", "[cli][gameplay]") {
    Sample sample("gameplay-switch");
    const auto shown = sample.run({"export", "gameplay"});
    REQUIRE(shown.exitCode == kExitOk);
    CHECK(shown.answer.at("command") == "export gameplay");
    CHECK(shown.answer.at("result").at("enabled") == false);
    CHECK(shown.answer.at("result").at("note").get<std::string>().find("links.unreal.gameplay") != std::string::npos);
    CHECK_FALSE(fs::exists(sample.folder / "export-options.json"));

    const auto on = sample.run({"export", "gameplay", "on"});
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("command") == "export gameplay on");
    CHECK(on.answer.at("result").at("enabled") == true);
    CHECK(on.answer.at("result").at("note").get<std::string>().find("Unreal Engine link") != std::string::npos);
    const auto options = nlohmann::json::parse(readFile(sample.folder / "export-options.json"));
    CHECK(options.at("version") == 4);
    CHECK_FALSE(options.contains("gameplay"));
    CHECK_FALSE(options.contains("kit"));
    CHECK(options.at("links").at("unreal").at("gameplay") == true);
    CHECK(options.at("links").at("unreal").at("kit") == false);
    CHECK(sample.run({"export", "gameplay"}).answer.at("result").at("enabled") == true);
    CHECK(sample.run({"export", "kit"}).answer.at("result").at("enabled") == false);

    const auto off = sample.run({"export", "gameplay", "off"});
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("result").at("enabled") == false);
    CHECK_FALSE(fs::exists(sample.folder / "export-options.json"));
}

TEST_CASE("the gameplay and kit switches are kept apart", "[cli][gameplay]") {
    Sample sample("gameplay-kit");
    REQUIRE(sample.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(sample.run({"export", "gameplay", "on"}).exitCode == kExitOk);
    REQUIRE(sample.run({"export", "kit", "off"}).exitCode == kExitOk);
    CHECK(sample.run({"export", "gameplay"}).answer.at("result").at("enabled") == true);
    CHECK(sample.run({"export", "kit"}).answer.at("result").at("enabled") == false);
}

TEST_CASE("a version 3 export-options.json keeps its switches under links.unreal", "[cli][gameplay]") {
    Sample sample("gameplay-version-3");
    std::ofstream(sample.folder / "export-options.json", std::ios::binary)
        << R"({"format":"plynth-export-options","version":3,"kit":{"enabled":true},"gameplay":{"enabled":true}})";
    CHECK(sample.run({"export", "kit"}).answer.at("result").at("enabled") == true);
    CHECK(sample.run({"export", "gameplay"}).answer.at("result").at("enabled") == true);

    REQUIRE(sample.run({"export", "kit", "off"}).exitCode == kExitOk);
    const auto options = nlohmann::json::parse(readFile(sample.folder / "export-options.json"));
    CHECK(options.at("version") == 4);
    CHECK_FALSE(options.contains("kit"));
    CHECK_FALSE(options.contains("gameplay"));
    CHECK(options.at("links").at("unreal").at("kit") == false);
    CHECK(options.at("links").at("unreal").at("gameplay") == true);
}

TEST_CASE("export gameplay refuses other words and options", "[cli][gameplay]") {
    Sample sample("gameplay-refuse");
    const auto maybe = sample.run({"export", "gameplay", "maybe"});
    CHECK(maybe.exitCode == kExitBadArguments);
    CHECK(errorCode(maybe) == "bad_arguments");
    CHECK(errorCode(sample.run({"export", "gameplay", "on", "--name", "x"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"export", "gameplay", "on", "--check"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"export", "gameplay", "on", "now"})) == "bad_arguments");
    CHECK_FALSE(fs::exists(sample.folder / "export-options.json"));
}

TEST_CASE("export gameplay on follows the lock rules and export gameplay never is refused", "[cli][gameplay]") {
    Sample sample("gameplay-lock");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());

    const auto refused = sample.run({"export", "gameplay", "on"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "locked");
    CHECK_FALSE(fs::exists(sample.folder / "export-options.json"));
    CHECK(sample.run({"export", "gameplay"}).exitCode == kExitOk);
}

TEST_CASE("plain export writes no gameplay data even with the switch on", "[cli][gameplay]") {
    GameplaySample flat("gameplay-export");
    REQUIRE(flat.sample.run({"export", "gameplay", "on"}).exitCode == kExitOk);
    const auto on = flat.exportNow();
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("result").at("profile") == "plain");
    const auto& gameplay = on.answer.at("result").at("gameplay");
    CHECK(gameplay.at("enabled") == false);
    CHECK(gameplay.at("file").is_null());
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(flat.sample.folder / "export", ignored)) {
        CHECK_FALSE(entry.path().filename().string().ends_with("_gameplay.json"));
    }

    const auto report = flat.sample.run({"export", "report"});
    REQUIRE(report.exitCode == kExitOk);
    CHECK(report.answer.at("result").at("gameplay").at("enabled") == false);
}

TEST_CASE("help describes export gameplay", "[cli][gameplay]") {
    const auto outcome = plynth::cli::testing::runChecked({"help"});
    bool described = false;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        if (command.at("command") == "export") {
            described = command.at("description").get<std::string>().find("export gameplay") != std::string::npos;
        }
    }
    CHECK(described);
}
