#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// Clears the settings file when a test starts and when it ends.
struct SettingsReset {
    SettingsReset() { clear(); }
    ~SettingsReset() { clear(); }
    static void clear() {
        plynth::cli::testing::removeSettingsFileSafely();
    }
};

// A folder holding two small shape files.
std::filesystem::path makeShapeFolder(const TempFolder& temp) {
    const auto folder = temp.path() / "shapes-source";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "oval.shape.json") << "{}";
    std::ofstream(folder / "arch.shape.json") << "{}";
    return folder;
}

std::vector<std::string> names(const nlohmann::json& list) {
    std::vector<std::string> out;
    for (const auto& item : list) {
        out.push_back(item.get<std::string>());
    }
    return out;
}

}

TEST_CASE("library add-sample-shapes copies the shapes and lists them as skipped the second time",
          "[cli][library]") {
    SettingsReset reset;
    TempFolder temp("library-shapes");
    const auto library = temp.path() / "Library";
    const auto source = makeShapeFolder(temp);
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);

    const auto first = runChecked({"library", "add-sample-shapes", "--library", utf8(library), "--from",
                                   utf8(source)});
    REQUIRE(first.exitCode == kExitOk);
    CHECK(names(first.answer.at("result").at("copied")) == std::vector<std::string>{"arch.shape.json", "oval.shape.json"});
    CHECK(first.answer.at("result").at("skipped").empty());
    CHECK(std::filesystem::exists(library / "shapes" / "oval.shape.json"));

    const auto second = runChecked({"library", "add-sample-shapes", "--library", utf8(library), "--from",
                                    utf8(source)});
    REQUIRE(second.exitCode == kExitOk);
    CHECK(second.answer.at("result").at("copied").empty());
    CHECK(names(second.answer.at("result").at("skipped")) ==
          std::vector<std::string>{"arch.shape.json", "oval.shape.json"});
}

TEST_CASE("library add-sample-shapes refuses a missing folder and missing samples", "[cli][library]") {
    SettingsReset reset;
    TempFolder temp("library-shapes-missing");
    const auto library = temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);

    const auto missing = runChecked({"library", "add-sample-shapes", "--library", utf8(library), "--from",
                                     utf8(temp.path() / "nowhere")});
    CHECK(errorCode(missing) == "not_found");
}

TEST_CASE("library init copies the sample shapes found through PLYNTH_SAMPLES", "[cli][library]") {
    SettingsReset reset;
    TempFolder temp("library-init-shapes");
    // The variable is set with a narrow string, so its folder must have an ASCII name.
    const auto samples = std::filesystem::temp_directory_path() / ("plynth-cli-test-samples-" + std::to_string(_getpid()));
    std::error_code ignored;
    std::filesystem::remove_all(samples, ignored);
    std::filesystem::create_directories(samples / "shapes");
    std::ofstream(samples / "shapes" / "oval.shape.json") << "{}";
    ScopedEnvironment variable("PLYNTH_SAMPLES", utf8(samples));

    const auto library = temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);
    CHECK(std::filesystem::exists(library / "shapes" / "oval.shape.json"));

    std::filesystem::remove(library / "shapes" / "oval.shape.json");
    const auto added = runChecked({"library", "add-sample-shapes", "--library", utf8(library)});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(names(added.answer.at("result").at("copied")) == std::vector<std::string>{"oval.shape.json"});
    std::filesystem::remove_all(samples, ignored);
}

TEST_CASE("help lists library add-sample-shapes", "[cli][help]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> commands;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        commands.push_back(command.at("command").get<std::string>());
    }
    CHECK(std::ranges::find(commands, "library add-sample-shapes") != commands.end());
}
