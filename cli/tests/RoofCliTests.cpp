#include "MaterialLibrary.h"
#include "SampleProject.h"

#include <algorithm>
#include <plynth/cli/Cli.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::MaterialLibrary;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

TEST_CASE("roof types lists the five types and what each uses", "[cli][roof]") {
    const auto types = runChecked({"roof", "types"});
    REQUIRE(types.exitCode == kExitOk);
    std::vector<std::string> names;
    for (const auto& type : types.answer.at("result").at("types")) {
        names.push_back(type.at("id").get<std::string>());
        CHECK(type.at("name").is_string());
        CHECK(type.at("uses").is_array());
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"dutch_gable", "flat", "gable", "hip", "jerkinhead", "pyramid", "sawtooth", "shed"});
}

TEST_CASE("roof list shows the default and the section over the walls", "[cli][roof]") {
    Sample sample("roof-list");
    sample.drawRoomSquare();
    const auto list = sample.run({"roof", "list"});
    REQUIRE(list.exitCode == kExitOk);
    const auto& result = list.answer.at("result");
    CHECK(result.at("default").at("type") == "flat");
    CHECK(result.at("default").at("direction").is_null());
    REQUIRE(result.at("sections").size() == 1);
    const auto& section = result.at("sections").at(0);
    CHECK(section.at("floor") == "f1");
    CHECK(section.at("index") == 0);
    CHECK(section.at("section").is_null());
    CHECK(section.at("type_built") == "flat");
    CHECK(section.at("label").size() == 2);
    CHECK(section.at("outline").size() >= 4);
    CHECK(section.at("material").at("origin").is_string());
    CHECK(result.at("unused").empty());
    CHECK(errorCode(sample.run({"roof", "list", "--floor", "f9"})) == "not_found");
}

TEST_CASE("roof set default changes the default and refuses bad values", "[cli][roof]") {
    Sample sample("roof-default");
    const auto set = sample.run({"roof", "set", "default", "--type", "gable", "--pitch", "35", "--direction", "90"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("type") == "gable");
    CHECK(set.answer.at("result").at("pitch") == 35.0);
    CHECK(set.answer.at("result").at("direction") == 90.0);
    const auto auto_ = sample.run({"roof", "set", "default", "--direction", "auto"});
    REQUIRE(auto_.exitCode == kExitOk);
    CHECK(auto_.answer.at("result").at("direction").is_null());

    const auto before = readFile(sample.folder / "plan.json");
    CHECK(errorCode(sample.run({"roof", "set", "default", "--type", "dome"})) == "invalid_value");
    CHECK(errorCode(sample.run({"roof", "set", "default", "--type", "gable", "--pitch", "2"})) == "invalid_value");
    CHECK(sample.run({"roof", "set", "default"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"roof", "set", "default", "--direction", "up"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"roof", "set", "default", "--material", "x"}).exitCode == kExitBadArguments);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("roof set at a point adds an entry, then edits it, and roof clear removes it", "[cli][roof]") {
    Sample sample("roof-entry");
    sample.drawRoomSquare();
    const auto added = sample.run({"roof", "set", "--floor", "f1", "--at", "3,5", "--type", "hip"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("section") == "rf1");
    CHECK(added.answer.at("result").at("settings").at("type") == "hip");

    const auto again = sample.run({"roof", "set", "--at", "3,5", "--pitch", "40"});
    REQUIRE(again.exitCode == kExitOk);
    CHECK(again.answer.at("result").at("section") == "rf1");
    CHECK(again.answer.at("result").at("settings").at("pitch") == 40.0);
    CHECK(again.answer.at("result").at("settings").at("type") == "hip");

    const auto byId = sample.run({"roof", "set", "rf1", "--type", "shed"});
    REQUIRE(byId.exitCode == kExitOk);
    CHECK(byId.answer.at("result").at("type_built") == "shed");

    const auto list = sample.run({"roof", "list"});
    CHECK(list.answer.at("result").at("sections").at(0).at("section") == "rf1");

    CHECK(errorCode(sample.run({"roof", "clear", "rf9"})) == "not_found");
    const auto cleared = sample.run({"roof", "clear", "rf1"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("removed") == "rf1");
    CHECK(sample.run({"roof", "list"}).answer.at("result").at("sections").at(0).at("section").is_null());
}

TEST_CASE("roof set refuses a point in no section and unknown entries", "[cli][roof]") {
    Sample sample("roof-refuse");
    sample.drawRoomSquare();
    const auto before = readFile(sample.folder / "plan.json");
    const auto outside = sample.run({"roof", "set", "--at", "20,20", "--type", "gable"});
    CHECK(outside.exitCode == kExitFailed);
    CHECK(errorCode(outside) == "invalid_value");
    CHECK(errorCode(sample.run({"roof", "set", "rf9", "--type", "gable"})) == "not_found");
    CHECK(errorCode(sample.run({"roof", "set", "--floor", "f9", "--at", "3,5", "--type", "gable"})) == "not_found");
    CHECK(sample.run({"roof", "set", "--type", "gable"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"roof", "set", "--at", "3;5", "--type", "gable"}).exitCode == kExitBadArguments);
    CHECK(sample.run({"roof", "set", "rf1", "--at", "3,5", "--type", "gable"}).exitCode == kExitBadArguments);
    CHECK(readFile(sample.folder / "plan.json") == before);
}

TEST_CASE("a roof section takes a material through roof set and material set", "[cli][roof]") {
    MaterialLibrary library("roof-material");
    Sample sample("roof-material-project");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"roof", "set", "--at", "3,5", "--type", "gable", "--material", "grey_tile"}).exitCode ==
            kExitOk);
    const auto list = sample.run({"roof", "list"});
    CHECK(list.answer.at("result").at("sections").at(0).at("material").at("material") == "grey_tile");
    CHECK(list.answer.at("result").at("sections").at(0).at("material").at("origin") == "roof");

    const auto cleared = sample.run({"material", "set", "rf1", "none"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("previous") == "grey_tile");
    CHECK(errorCode(sample.run({"roof", "set", "rf1", "--material", "no_such_material"})) == "not_found");

    const auto materials = sample.run({"material", "list"});
    REQUIRE(materials.exitCode == kExitOk);
    CHECK(materials.answer.at("result").at("roofs").size() == 1);
    CHECK(materials.answer.at("result").at("stairs").is_array());
}

TEST_CASE("help lists the roof commands", "[cli][roof]") {
    const auto help = runChecked({"help"});
    REQUIRE(help.exitCode == kExitOk);
    const std::string text = help.answer.dump();
    for (const char* name : {"roof set", "roof clear", "roof list", "roof types"}) {
        CHECK(text.find(name) != std::string::npos);
    }
}
