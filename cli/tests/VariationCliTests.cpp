#include "FakeBlender.h"
#include "FinishLibrary.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::FinishSample;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::utf8;

namespace {

// The bytes of the project's two files, to see that a command wrote nothing.
std::string projectBytes(const Sample& sample) {
    return readFile(sample.folder / "plan.json") + readFile(sample.folder / "project.json");
}

// The room entry of a preview answer.
nlohmann::json previewRoom(const Outcome& preview, const std::string& room) {
    nlohmann::json found = nlohmann::json::object();
    for (const auto& entry : preview.answer.at("result").at("rooms")) {
        if (entry.at("room") == room) {
            found = entry;
        }
    }
    return found;
}

// The asset the preview swaps item i1 to, or empty when the seed leaves it.
std::string swappedItem(const Outcome& preview) {
    std::string to;
    for (const auto& change : preview.answer.at("result").at("changes")) {
        if (change.at("target") == "item" && change.at("object") == "i1") {
            to = change.at("to").get<std::string>();
        }
    }
    return to;
}

// The first seed from 1 to 60 whose preview swaps item i1 to sofa_b. 0 when none does.
int seedThatSwapsSofa(const FinishSample& finish) {
    for (int seed = 1; seed <= 60; ++seed) {
        const auto preview = finish.run({"variation", "preview", "--seed", std::to_string(seed)});
        REQUIRE(preview.exitCode == kExitOk);
        if (swappedItem(preview) == "sofa_b") {
            return seed;
        }
    }
    return 0;
}

}

TEST_CASE("variation show and set change the seed and the switches", "[cli][variation]") {
    Sample sample("variation-set");
    const auto shown = sample.run({"variation", "show"});
    REQUIRE(shown.exitCode == kExitOk);
    CHECK(shown.answer.at("command") == "variation show");
    CHECK(shown.answer.at("result").at("seed") == 0);
    CHECK(shown.answer.at("result").at("furniture") == true);
    CHECK(shown.answer.at("result").at("materials") == true);

    const auto set = sample.run({"variation", "set", "--seed", "42", "--materials", "off"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("command") == "variation set");
    CHECK(set.answer.at("result").at("seed") == 42);
    CHECK(set.answer.at("result").at("furniture") == true);
    CHECK(set.answer.at("result").at("materials") == false);
    const auto saved = nlohmann::json::parse(readFile(sample.folder / "project.json"));
    CHECK(saved.at("variation").at("seed") == 42);
    CHECK(saved.at("variation").at("materials") == false);

    const auto top = sample.run({"variation", "set", "--seed", "4294967295", "--furniture", "off"});
    REQUIRE(top.exitCode == kExitOk);
    CHECK(top.answer.at("result").at("seed") == 4294967295u);
    CHECK(sample.run({"variation", "show"}).answer.at("result") == top.answer.at("result"));
}

TEST_CASE("variation set refuses bad seeds, bad switches and no option, changing nothing", "[cli][variation]") {
    Sample sample("variation-refuse");
    const std::string before = projectBytes(sample);
    for (const char* seed : {"4294967296", "abc", "1.5", "-1"}) {
        const auto outcome = sample.run({"variation", "set", std::string("--seed=") + seed});
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "invalid_value");
    }
    CHECK(errorCode(sample.run({"variation", "set", "--furniture", "maybe"})) == "bad_arguments");
    const auto none = sample.run({"variation", "set"});
    CHECK(none.exitCode == kExitBadArguments);
    CHECK(errorCode(none) == "bad_arguments");
    CHECK(projectBytes(sample) == before);
}

TEST_CASE("variation set follows the lock rules and the read commands never are refused", "[cli][variation]") {
    plynth::cli::testing::GuardedSettingsReset reset;
    Sample sample("variation-lock");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(sample.folder, state).ok());
    const std::string before = projectBytes(sample);

    CHECK(errorCode(sample.run({"variation", "set", "--seed", "3"})) == "locked");
    CHECK(projectBytes(sample) == before);
    CHECK(sample.run({"variation", "show"}).exitCode == kExitOk);
    CHECK(sample.run({"variation", "preview"}).exitCode == kExitOk);
}

TEST_CASE("variation preview without a library changes nothing and writes nothing", "[cli][variation]") {
    plynth::cli::testing::GuardedSettingsReset reset;
    Sample sample("variation-preview-bare");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"opening", "add", "--wall", "w2", "--offset", "2"}).exitCode == kExitOk);
    const std::string before = projectBytes(sample);

    const auto preview = sample.run({"variation", "preview", "--seed", "7"});
    REQUIRE(preview.exitCode == kExitOk);
    CHECK(preview.answer.at("command") == "variation preview");
    const auto& result = preview.answer.at("result");
    CHECK(result.at("seed") == 7);
    CHECK(result.at("library").is_null());
    CHECK(result.at("changes").empty());
    CHECK(result.at("frames").empty());
    CHECK(result.at("prop_count") == 0);
    const auto room = previewRoom(preview, "r1");
    CHECK(room.at("floor") == "f1");
    CHECK(room.at("dressing") == true);
    CHECK(room.at("props").empty());

    CHECK(projectBytes(sample) == before);
    CHECK_FALSE(fs::exists(sample.folder / "build"));
    CHECK(sample.run({"variation", "show"}).answer.at("result").at("seed") == 0);
}

TEST_CASE("variation preview picks the fitting door and hangs pictures, and dressing off leaves a room bare",
          "[cli][variation]") {
    const FinishSample finish("variation-preview");
    const std::string before = projectBytes(finish.sample);

    const auto preview = finish.run({"variation", "preview"});
    REQUIRE(preview.exitCode == kExitOk);
    const auto& result = preview.answer.at("result");
    CHECK(result.at("seed") == 0);
    CHECK(result.at("library").is_string());
    CHECK(result.at("changes").empty());
    REQUIRE(result.at("frames").size() == 1);
    CHECK(result.at("frames").at(0).at("opening") == "o1");
    CHECK(result.at("frames").at(0).at("asset") == "oak_door");
    const auto room = previewRoom(preview, "r1");
    REQUIRE_FALSE(room.at("props").empty());
    for (const auto& prop : room.at("props")) {
        CHECK(prop.at("asset") == "sea_print");
        CHECK(prop.at("rule") == "pictures");
        CHECK(prop.at("host").get<std::string>().starts_with("w"));
        CHECK(prop.at("z").is_number());
    }
    CHECK(result.at("prop_count") == room.at("props").size());
    CHECK(finish.run({"variation", "preview"}).answer.at("result") == result);
    CHECK(projectBytes(finish.sample) == before);
    CHECK_FALSE(fs::exists(finish.sample.folder / "build"));

    REQUIRE(finish.run({"room", "set", "r1", "--dressing", "off"}).exitCode == kExitOk);
    const auto bare = finish.run({"variation", "preview"});
    REQUIRE(bare.exitCode == kExitOk);
    CHECK(previewRoom(bare, "r1").at("dressing") == false);
    CHECK(previewRoom(bare, "r1").at("props").empty());
    CHECK(bare.answer.at("result").at("prop_count") == 0);

    const std::string marked = projectBytes(finish.sample);
    REQUIRE(finish.run({"opening", "set", "o1", "--frame", "generated"}).exitCode == kExitOk);
    CHECK(finish.run({"variation", "preview"}).answer.at("result").at("frames").empty());
    CHECK(projectBytes(finish.sample) != marked);
}

TEST_CASE("variation preview refuses a bad seed and a library folder that is not one", "[cli][variation]") {
    const FinishSample finish("variation-preview-refuse");
    CHECK(errorCode(finish.run({"variation", "preview", "--seed", "x"})) == "invalid_value");
    const auto library = finish.run({"variation", "preview", "--library", utf8(finish.sample.temp.path() / "none")});
    CHECK(library.exitCode == kExitFailed);
    CHECK(errorCode(library) == "no_library");
}

TEST_CASE("variation preview equals what the build then uses", "[cli][variation]") {
    const FinishSample finish("variation-build");
    REQUIRE(finish.run({"place", "sofa_a", "--x", "3", "--y", "6"}).exitCode == kExitOk);
    const int seed = seedThatSwapsSofa(finish);
    REQUIRE(seed > 0);

    REQUIRE(finish.run({"variation", "set", "--seed", std::to_string(seed)}).exitCode == kExitOk);
    const std::string marked = readFile(finish.sample.folder / "plan.json");
    const auto preview = finish.run({"variation", "preview"});
    REQUIRE(preview.exitCode == kExitOk);
    CHECK(preview.answer.at("result").at("seed") == seed);
    CHECK(swappedItem(preview) == "sofa_b");

    const fs::path fake = installFakeBlender(finish.sample.temp.path() / "fb", "5.2.0");
    const auto built = finish.run({"build", "--blender", utf8(fake)});
    REQUIRE(built.exitCode == kExitOk);
    CHECK(readFile(finish.sample.folder / "plan.json") == marked);

    const auto builtPlan = nlohmann::json::parse(readFile(finish.sample.folder / "build" / "built-plan.json"));
    const auto& floor = builtPlan.at("plan").at("floors").at(0);
    for (const auto& item : floor.at("items")) {
        if (item.at("id") == "i1") {
            CHECK(item.at("asset") == "sofa_b");
        }
    }
    std::vector<std::pair<std::string, std::string>> builtFrames;
    for (const auto& opening : floor.at("openings")) {
        if (opening.at("frame").is_string()) {
            builtFrames.emplace_back(opening.at("id").get<std::string>(), opening.at("frame").get<std::string>());
        }
    }
    std::vector<std::pair<std::string, std::string>> previewFrames;
    for (const auto& frame : preview.answer.at("result").at("frames")) {
        previewFrames.emplace_back(frame.at("opening").get<std::string>(), frame.at("asset").get<std::string>());
    }
    CHECK(builtFrames == previewFrames);
    CHECK(previewFrames == std::vector<std::pair<std::string, std::string>>{{"o1", "oak_door"}});
}

TEST_CASE("variation copies saves one project per seed and refuses what the rules refuse", "[cli][variation]") {
    plynth::cli::testing::GuardedSettingsReset reset;
    Sample sample("variation-copies");
    sample.drawRoomSquare();
    const std::string before = projectBytes(sample);
    const fs::path out = sample.temp.path() / "street";

    const auto made = sample.run({"variation", "copies", "--seeds", "1,2,3", "--out", utf8(out)});
    REQUIRE(made.exitCode == kExitOk);
    CHECK(made.answer.at("command") == "variation copies");
    const auto& folders = made.answer.at("result").at("folders");
    REQUIRE(folders.size() == 3);
    for (std::size_t i = 0; i < folders.size(); ++i) {
        const auto& entry = folders.at(i);
        CHECK(entry.at("seed") == i + 1);
        const fs::path folder = plynth::pathFromUtf8(entry.at("folder").get<std::string>());
        CHECK(fs::equivalent(folder.parent_path(), out));
        CHECK(folder.filename().string().ends_with("_v" + std::to_string(i + 1)));
        const auto copy = runChecked({"variation", "show", "--project", entry.at("folder").get<std::string>()});
        REQUIRE(copy.exitCode == kExitOk);
        CHECK(copy.answer.at("result").at("seed") == i + 1);
    }
    CHECK(projectBytes(sample) == before);

    const auto again = sample.run({"variation", "copies", "--seeds", "3,4", "--out", utf8(out)});
    CHECK(again.exitCode == kExitFailed);
    CHECK(errorCode(again) == "already_exists");

    CHECK(errorCode(sample.run({"variation", "copies", "--seeds", "5,5", "--out", utf8(out)})) == "invalid_value");
    CHECK(errorCode(sample.run({"variation", "copies", "--seeds", "5,x", "--out", utf8(out)})) == "invalid_value");
    CHECK(errorCode(sample.run({"variation", "copies", "--seeds", "5,,6", "--out", utf8(out)})) == "invalid_value");
    CHECK(errorCode(sample.run({"variation", "copies", "--seeds", "5"})) == "bad_arguments");
    CHECK(errorCode(sample.run({"variation", "copies", "--out", utf8(out)})) == "bad_arguments");
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(out)) {
        ++count;
    }
    CHECK(count == 3);
}

TEST_CASE("help lists the variation commands", "[cli][variation]") {
    const auto outcome = runChecked({"help"});
    std::vector<std::string> names;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    for (const char* wanted : {"variation show", "variation set", "variation preview", "variation copies"}) {
        CHECK(std::ranges::find(names, wanted) != names.end());
    }
    CHECK(plynth::cli::isCommandWord("variation"));
}
