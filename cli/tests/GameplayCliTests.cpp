#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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

TEST_CASE("export gameplay shows the switch and on and off change it in export-options.json", "[cli][gameplay]") {
    Sample sample("gameplay-switch");
    const auto shown = sample.run({"export", "gameplay"});
    REQUIRE(shown.exitCode == kExitOk);
    CHECK(shown.answer.at("command") == "export gameplay");
    CHECK(shown.answer.at("result").at("enabled") == false);
    CHECK_FALSE(fs::exists(sample.folder / "export-options.json"));

    const auto on = sample.run({"export", "gameplay", "on"});
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("command") == "export gameplay on");
    CHECK(on.answer.at("result").at("enabled") == true);
    const auto options = nlohmann::json::parse(readFile(sample.folder / "export-options.json"));
    CHECK(options.at("version") == 2);
    CHECK(options.at("gameplay").at("enabled") == true);
    CHECK(options.at("kit").at("enabled") == false);
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

TEST_CASE("an export with gameplay data on writes the file and reports it, off removes it", "[cli][gameplay]") {
    GameplaySample flat("gameplay-export");
    const auto plain = flat.exportNow();
    REQUIRE(plain.exitCode == kExitOk);
    CHECK(plain.answer.at("result").at("gameplay").at("enabled") == false);
    CHECK(plain.answer.at("result").at("gameplay").at("file").is_null());

    REQUIRE(flat.sample.run({"export", "gameplay", "on"}).exitCode == kExitOk);
    const auto on = flat.exportNow();
    REQUIRE(on.exitCode == kExitOk);
    const auto& gameplay = on.answer.at("result").at("gameplay");
    CHECK(gameplay.at("enabled") == true);
    REQUIRE(gameplay.at("file").is_string());
    const fs::path file = plynth::pathFromUtf8(gameplay.at("file").get<std::string>());
    CHECK(file.parent_path().filename() == "export");
    CHECK(file.filename().string().ends_with("_gameplay.json"));
    REQUIRE(fs::exists(file));
    CHECK(nlohmann::json::parse(readFile(file)).is_object());
    CHECK(gameplay.at("rooms") == 1);
    CHECK(gameplay.at("doors") == 1);
    CHECK(gameplay.at("lights") == 2);

    const auto report = flat.sample.run({"export", "report"});
    REQUIRE(report.exitCode == kExitOk);
    CHECK(report.answer.at("result").at("gameplay").at("enabled") == true);

    REQUIRE(flat.sample.run({"export", "gameplay", "off"}).exitCode == kExitOk);
    const auto off = flat.exportNow();
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("result").at("gameplay").at("enabled") == false);
    CHECK_FALSE(fs::exists(file));
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
