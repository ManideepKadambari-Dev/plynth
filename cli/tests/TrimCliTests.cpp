#include "MaterialLibrary.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// The ids of the trim profiles answer.
std::vector<std::string> profileIds(const nlohmann::json& answer) {
    std::vector<std::string> ids;
    for (const auto& profile : answer.at("result").at("profiles")) {
        ids.push_back(profile.at("id").get<std::string>());
    }
    return ids;
}

// Writes a custom trim profile file into a library's shapes folder.
void writeProfile(const std::filesystem::path& library, const std::string& id, const std::string& name) {
    const auto folder = library / "shapes";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / (id + ".profile.json"), std::ios::binary)
        << R"({"format": "plynth-trim-profile", "version": 1, "id": ")" << id << R"(", "name": ")" << name
        << R"(", "outline": [[0, 0], [1, 0], [1, 0.5], [0.5, 1], [0, 1]]})";
}

}

TEST_CASE("trim show gives every kind with its defaults and the railing height", "[cli][trim]") {
    Sample sample("trim-show");
    const auto outcome = sample.run({"trim", "show"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "trim show");
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("skirting").at("enabled") == true);
    CHECK(result.at("skirting").at("profile") == "classic");
    CHECK(result.at("skirting").at("profile_found") == true);
    CHECK(result.at("skirting").at("height") == 0.10);
    CHECK(result.at("skirting").at("depth") == 0.015);
    CHECK(result.at("cornice").at("profile") == "cove");
    CHECK(result.at("casing").at("profile") == "bevel");
    CHECK(result.at("sill").at("profile") == "rounded");
    CHECK(result.at("railing").at("profile") == "round");
    CHECK(result.at("railing_height") == 0.9);
}

TEST_CASE("trim profiles lists the built-in profiles in order without a project", "[cli][trim]") {
    plynth::cli::testing::GuardedSettingsReset reset;
    TempFolder temp("trim-profiles");
    const auto outcome = runChecked({"trim", "profiles"}, temp.path());
    REQUIRE(outcome.exitCode == kExitOk);
    const auto ids = profileIds(outcome.answer);
    REQUIRE(ids.size() >= 6);
    const std::vector<std::string> first(ids.begin(), ids.begin() + 6);
    CHECK(first == std::vector<std::string>{"square", "bevel", "rounded", "classic", "cove", "round"});
    const auto& profile = outcome.answer.at("result").at("profiles").at(0);
    CHECK(profile.at("name") == "Square");
    CHECK(profile.at("built_in") == true);
}

TEST_CASE("trim profiles --library adds the library's custom profiles and refuses a folder that is not one",
          "[cli][trim]") {
    plynth::cli::testing::GuardedSettingsReset reset;
    TempFolder temp("trim-profiles-library");
    const auto library = temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library)}).exitCode == kExitOk);
    writeProfile(library, "trimtest_ogee", "Ogee");

    const auto outcome = runChecked({"trim", "profiles", "--library", utf8(library)});
    REQUIRE(outcome.exitCode == kExitOk);
    bool found = false;
    for (const auto& profile : outcome.answer.at("result").at("profiles")) {
        if (profile.at("id") == "trimtest_ogee") {
            found = true;
            CHECK(profile.at("name") == "Ogee");
            CHECK(profile.at("built_in") == false);
        }
    }
    CHECK(found);

    const auto refused = runChecked({"trim", "profiles", "--library", utf8(temp.path() / "nothing")});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "no_library");
}

TEST_CASE("trim set changes one kind and answers as trim show", "[cli][trim]") {
    Sample sample("trim-set");
    const auto off = sample.run({"trim", "set", "cornice", "--off"});
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("command") == "trim set");
    CHECK(off.answer.at("result").at("cornice").at("enabled") == false);
    CHECK(off.answer.at("result").at("skirting").at("enabled") == true);

    const auto sized = sample.run({"trim", "set", "skirting", "--profile", "square", "--height", "0.12", "--depth",
                                   "0.02"});
    REQUIRE(sized.exitCode == kExitOk);
    const auto& skirting = sized.answer.at("result").at("skirting");
    CHECK(skirting.at("profile") == "square");
    CHECK(skirting.at("height") == 0.12);
    CHECK(skirting.at("depth") == 0.02);
    CHECK(sized.answer.at("result").at("cornice").at("enabled") == false);

    const auto railing = sample.run({"trim", "set", "railing", "--railing-height", "1.0", "--on"});
    REQUIRE(railing.exitCode == kExitOk);
    CHECK(railing.answer.at("result").at("railing_height") == 1.0);

    const auto saved = nlohmann::json::parse(readFile(sample.folder / "project.json"));
    CHECK(saved.at("trim").at("cornice").at("enabled") == false);
    CHECK(saved.at("trim").at("skirting").at("profile") == "square");
    CHECK(saved.at("trim").at("railing_height") == 1.0);
    CHECK(sample.run({"trim", "show"}).answer.at("result") == railing.answer.at("result"));
}

TEST_CASE("trim set refuses bad kinds, sizes, profiles and options and changes nothing", "[cli][trim]") {
    Sample sample("trim-refuse");
    const std::string before = readFile(sample.folder / "project.json");

    const auto kind = sample.run({"trim", "set", "dado", "--off"});
    CHECK(kind.exitCode == kExitFailed);
    CHECK(errorCode(kind) == "invalid_value");
    CHECK(errorCode(sample.run({"trim", "set", "skirting", "--height", "0.001"})) == "invalid_value");
    CHECK(errorCode(sample.run({"trim", "set", "skirting", "--depth", "0.6"})) == "invalid_value");
    CHECK(errorCode(sample.run({"trim", "set", "railing", "--railing-height", "2"})) == "invalid_value");
    CHECK(errorCode(sample.run({"trim", "set", "skirting", "--off", "--height", "0.9"})) == "invalid_value");
    const auto profile = sample.run({"trim", "set", "skirting", "--profile", "baroque"});
    CHECK(profile.exitCode == kExitFailed);
    CHECK(errorCode(profile) == "not_found");

    const auto both = sample.run({"trim", "set", "skirting", "--on", "--off"});
    CHECK(both.exitCode == kExitBadArguments);
    CHECK(errorCode(both) == "bad_arguments");
    CHECK(errorCode(sample.run({"trim", "set", "skirting"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"trim", "set", "skirting", "--railing-height", "1"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"trim", "set", "--off"})) == "bad_arguments");

    CHECK(readFile(sample.folder / "project.json") == before);
}

TEST_CASE("trim set follows the lock rules and trim show never is refused", "[cli][trim]") {
    Sample sample("trim-lock");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const std::string before = readFile(sample.folder / "project.json");

    const auto refused = sample.run({"trim", "set", "sill", "--off"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "locked");
    CHECK(readFile(sample.folder / "project.json") == before);
    CHECK(sample.run({"trim", "show"}).exitCode == kExitOk);

    REQUIRE(sample.run({"session", "begin"}).exitCode == kExitOk);
    CHECK(sample.run({"trim", "set", "sill", "--off"}).exitCode == kExitOk);
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("material set takes the trim and railing defaults with the usual answer", "[cli][trim]") {
    const plynth::cli::testing::MaterialLibrary library("trim-materials");
    Sample sample("trim-materials-project");
    const auto trim = sample.run({"material", "set", "default.trim", "painted_wood"});
    REQUIRE(trim.exitCode == kExitOk);
    CHECK(trim.answer.at("result").at("target") == "default.trim");
    CHECK(trim.answer.at("result").at("material") == "painted_wood");
    CHECK(trim.answer.at("result").contains("previous"));
    CHECK(trim.answer.at("result").contains("resolved"));
    const auto railing = sample.run({"material", "set", "default.railing", "brushed_steel"});
    REQUIRE(railing.exitCode == kExitOk);
    const auto saved = nlohmann::json::parse(readFile(sample.folder / "project.json"));
    CHECK(saved.at("materials").at("trim") == "painted_wood");
    CHECK(saved.at("materials").at("railing") == "brushed_steel");

    const auto cleared = sample.run({"material", "set", "default.railing", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("previous") == "brushed_steel");
    const auto listed = sample.run({"material", "list"});
    REQUIRE(listed.exitCode == kExitOk);
    CHECK(listed.answer.at("result").at("defaults").at("trim") == "painted_wood");
    CHECK(listed.answer.at("result").at("defaults").at("railing").is_null());
}

TEST_CASE("help lists the trim commands", "[cli][trim]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"trim show", "trim profiles", "trim set"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
    CHECK(plynth::cli::isCommandWord("trim"));
}
