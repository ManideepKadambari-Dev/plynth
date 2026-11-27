#include "FakeBlender.h"
#include "MaterialLibrary.h"

#include <plynth/core/Library.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/AssetInfo.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::MaterialSettingsReset;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::PrivateUser;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::utf8;

namespace {

const char* const kAddress = "asset_base_id:0c1d4a2e-1111-2222-3333-444455556666";

// A command that starts Blender. check marks the two commands that report on Blender instead of refusing.
struct Command {
    std::vector<std::string> words;
    bool check = false;
    bool project = false;
};

// Every command with a --blender option that needs Blender here.
std::vector<Command> commands() {
    return {{{"assets", "import", kAddress}},
            {{"materials", "import", kAddress}},
            {{"assets", "inbox"}},
            {{"materials", "inbox"}},
            {{"assets", "rescan"}},
            {{"assets", "thumbnail", "stale"}},
            {{"library", "rescan"}},
            {{"materials", "rescan"}},
            {{"build"}, false, true},
            {{"export"}, false, true},
            {{"doctor"}, true},
            {{"library", "integrations"}, true}};
}

// A buildable project and a library with work waiting for each command.
struct Waiting {
    Waiting() : sample("eo"), user(sample.temp) {
        sample.drawRoomSquare();
        library = sample.temp.path() / "Library";
        REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == plynth::cli::kExitOk);
        const auto paths = plynth::libraryPaths(library);
        std::filesystem::create_directories(paths.assetInbox);
        std::ofstream(paths.assetInbox / "chair.obj") << "v 0 0 0\n";
        std::filesystem::create_directories(paths.materialInbox);
        std::ofstream(paths.materialInbox / "Oak_BaseColor.png") << "x";

        plynth::Status status;
        auto catalog = plynth::AssetCatalog::open(paths, status);
        REQUIRE(status.ok());
        const auto folder = paths.assetItems / "stale";
        std::filesystem::create_directories(folder);
        std::ofstream(folder / "asset.blend", std::ios::binary) << "changed since";
        plynth::AssetInfo info;
        info.id = "stale";
        info.name = "Stale";
        info.source.kind = "local_file";
        info.source.reference = "stale.obj";
        info.source.importedAt = plynth::sessionNow();
        info.size = {0.5, 0.5, 0.9};
        info.triangles = 12;
        info.blendBytes = 1;
        info.blenderVersion = "5.2.0";
        REQUIRE(plynth::writeAssetInfo(folder / "info.json", info).ok());
        REQUIRE(catalog->indexAsset(info).ok());
        other = installFakeBlender(sample.temp.path() / "other", "5.2.0");
    }

    MaterialSettingsReset reset;
    Sample sample;
    PrivateUser user;
    std::filesystem::path library;
    std::filesystem::path other;
};

// The state of an item in a list, found by its id.
std::string stateOf(const nlohmann::json& list, const std::string& id) {
    for (const auto& entry : list) {
        if (entry.at("id") == id) {
            return entry.at("state");
        }
    }
    return {};
}

// Says whether an answer refuses the empty option, or reports that Blender is missing.
bool refuses(const Command& command, const Outcome& outcome) {
    const auto& answer = outcome.answer;
    if (answer.at("ok") == false) {
        const std::string code = answer.at("error").at("code");
        return code == "blender_not_found" || code == "invalid_value" || code == "bad_arguments";
    }
    const auto& result = answer.at("result");
    if (command.check) {
        return result.contains("items") ? stateOf(result.at("items"), "blender") == "missing"
                                        : stateOf(result.at("integrations"), "blenderkit") == "not_checked";
    }
    // A rescan lists what it could not refresh and still answers ok.
    return std::ranges::find(command.words, "rescan") != command.words.end() &&
           answer.dump().find("blender_not_found") != std::string::npos;
}

// Runs a command with the empty option written one way.
void expectRefused(const Waiting& waiting, const Command& command, bool equalsForm) {
    auto args = command.words;
    if (equalsForm) {
        args.push_back("--blender=");
    } else {
        args.insert(args.end(), {"--blender", ""});
    }
    const Outcome outcome = command.project ? waiting.sample.run(args) : runChecked(args);
    CAPTURE(args, outcome.text);
    CHECK(refuses(command, outcome));
    CHECK_FALSE(std::filesystem::exists(waiting.other.parent_path() / "fake_args.log"));
}

}

TEST_CASE("an empty --blender never falls back to PLYNTH_BLENDER", "[cli][blender_option][safety]") {
    for (const Command& command : commands()) {
        for (const bool equalsForm : {false, true}) {
            const Waiting waiting;
            const ScopedEnvironment variable("PLYNTH_BLENDER", utf8(waiting.other));
            expectRefused(waiting, command, equalsForm);
        }
    }
}

// The two checks are left out here: without the variable their search would start this machine's Blender.
TEST_CASE("an empty --blender never falls back to the Blender in the settings", "[cli][blender_option][safety]") {
    for (const Command& command : commands()) {
        if (command.check) {
            continue;
        }
        for (const bool equalsForm : {false, true}) {
            const Waiting waiting;
            const ScopedEnvironment variable("PLYNTH_BLENDER", "");
            REQUIRE(runChecked({"settings", "set", "--blender", utf8(waiting.other)}).exitCode == plynth::cli::kExitOk);
            std::filesystem::remove(waiting.other.parent_path() / "fake_args.log");
            expectRefused(waiting, command, equalsForm);
        }
    }
}
