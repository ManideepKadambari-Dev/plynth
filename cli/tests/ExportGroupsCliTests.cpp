#include "FakeBlender.h"
#include "SampleProject.h"
#include "SettingsGuard.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;
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

// Checks a JSON value against one of the schemas.
void requireSchema(const std::string& file, const nlohmann::json& value) {
    std::ifstream in(fs::path(PLYNTH_SCHEMA_DIR) / file);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(in));
    REQUIRE_NOTHROW(validator.validate(value));
}

// Adds a sofa i1 in the room r1 to the plan, as a project file edit, so groups have a furniture member.
void addItem(const fs::path& folder) {
    const fs::path file = folder / "plan.json";
    nlohmann::json plan = nlohmann::json::parse(readFile(file));
    plan["floors"][0]["items"] = nlohmann::json::array(
        {{{"id", "i1"}, {"asset", "sofa"}, {"room", "r1"}, {"x", 3.0}, {"y", 5.0},
          {"size", {{"width", 2.0}, {"depth", 0.9}, {"height", 0.85}}}}});
    std::ofstream(file, std::ios::binary | std::ios::trunc) << plan.dump(2);
}

// A built square room with a sofa i1 in the living room r1, a fake Blender, and the export mode given.
struct GroupedSample {
    explicit GroupedSample(const std::string& name, const std::string& mode = "grouped") : sample(name) {
        sample.drawRoomSquare();
        addItem(sample.folder);
        if (mode != "single") {
            REQUIRE(sample.run({"project", "set", "--export", mode}).exitCode == kExitOk);
        }
        fake = installFakeBlender(sample.temp.path() / "fb", "5.2.0");
        const auto built = sample.run({"build", "--blender", utf8(fake)});
        REQUIRE(built.exitCode == kExitOk);
    }

    // Runs a command on the project.
    Outcome run(std::vector<std::string> args) const { return sample.run(std::move(args)); }

    // Runs export with the fake Blender.
    Outcome exportNow() const { return sample.run({"export", "--blender", utf8(fake)}); }

    // The groups of the answer as id, in order.
    static std::vector<std::string> groupIds(const Outcome& outcome) {
        std::vector<std::string> ids;
        for (const auto& group : outcome.answer.at("result").at("groups")) {
            ids.push_back(group.at("id").get<std::string>());
        }
        return ids;
    }

    // The group of an answer with an id, or null.
    static nlohmann::json group(const Outcome& outcome, const std::string& id) {
        for (const auto& group : outcome.answer.at("result").at("groups")) {
            if (group.at("id") == id) {
                return group;
            }
        }
        return nullptr;
    }

    // True when a group has a member of this kind and id (null for a kind member).
    static bool hasMember(const nlohmann::json& group, const std::string& kind, const nlohmann::json& id) {
        for (const auto& member : group.at("members")) {
            if (member.at("kind") == kind && member.at("id") == id) {
                return true;
            }
        }
        return false;
    }

    fs::path optionsFile() const { return sample.folder / "export-options.json"; }

    SettingsReset reset;
    Sample sample;
    fs::path fake;
};

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::ranges::find(list, value) != list.end();
}

}

TEST_CASE("export groups lists the automatic groups with labels", "[cli][export][groups]") {
    GroupedSample flat("groups-list");
    const auto outcome = flat.run({"export", "groups"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "export groups");
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("exporter") == "grouped");
    CHECK(result.at("reviewable") == true);
    CHECK(result.at("from_build") == true);
    CHECK(result.at("notes").empty());
    CHECK(result.at("kit").at("enabled") == false);
    CHECK(GroupedSample::groupIds(outcome) == std::vector<std::string>{"structure", "surfaces", "furniture_r1"});

    const auto structure = GroupedSample::group(outcome, "structure");
    CHECK(structure.at("name") == "Walls");
    CHECK(structure.at("file") == "flat_Walls.fbx");
    CHECK(structure.at("rest") == true);
    CHECK(structure.at("automatic") == true);
    CHECK(GroupedSample::hasMember(structure, "walls", "f1"));
    for (const auto& member : structure.at("members")) {
        CHECK_FALSE(member.at("label").get<std::string>().empty());
    }

    const auto surfaces = GroupedSample::group(outcome, "surfaces");
    CHECK(surfaces.at("rest") == false);
    CHECK(GroupedSample::hasMember(surfaces, "floor", "r1"));
    CHECK(GroupedSample::hasMember(surfaces, "slab", nullptr));

    const auto furniture = GroupedSample::group(outcome, "furniture_r1");
    CHECK(furniture.at("name") == "Furniture Living");
    CHECK(GroupedSample::hasMember(furniture, "item", "i1"));
    CHECK_FALSE(fs::exists(flat.optionsFile()));
}

TEST_CASE("group edits change the answer and keep export-options.json valid", "[cli][export][groups]") {
    GroupedSample flat("groups-edit");
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");

    const auto renamed = flat.run({"export", "groups", "rename", "furniture_r1", "--name", "Lounge props"});
    REQUIRE(renamed.exitCode == kExitOk);
    CHECK(renamed.answer.at("command") == "export groups rename");
    CHECK(GroupedSample::group(renamed, "furniture_r1").at("name") == "Lounge props");
    CHECK(GroupedSample::group(renamed, "furniture_r1").at("file") == "flat_Lounge_props.fbx");
    REQUIRE(fs::exists(flat.optionsFile()));
    requireSchema("export-options.schema.json", nlohmann::json::parse(readFile(flat.optionsFile())));

    const auto added = flat.run({"export", "groups", "new", "--name", "Hero"});
    REQUIRE(added.exitCode == kExitOk);
    CHECK(added.answer.at("result").at("new_group") == "group_1");
    CHECK(GroupedSample::group(added, "group_1").at("automatic") == false);
    CHECK(GroupedSample::group(added, "group_1").at("members").empty());

    const auto itemMoved = flat.run({"export", "groups", "move", "item:i1", "--to", "group_1"});
    REQUIRE(itemMoved.exitCode == kExitOk);
    CHECK(GroupedSample::hasMember(GroupedSample::group(itemMoved, "group_1"), "item", "i1"));
    CHECK_FALSE(GroupedSample::hasMember(GroupedSample::group(itemMoved, "furniture_r1"), "item", "i1"));

    const auto kindMoved = flat.run({"export", "groups", "move", "slab:*", "--to", "structure"});
    REQUIRE(kindMoved.exitCode == kExitOk);
    CHECK(GroupedSample::hasMember(GroupedSample::group(kindMoved, "structure"), "slab", nullptr));
    CHECK_FALSE(GroupedSample::hasMember(GroupedSample::group(kindMoved, "surfaces"), "slab", nullptr));

    const auto merged = flat.run({"export", "groups", "merge", "furniture_r1", "--into", "group_1"});
    REQUIRE(merged.exitCode == kExitOk);
    CHECK_FALSE(contains(GroupedSample::groupIds(merged), "furniture_r1"));
    requireSchema("export-options.schema.json", nlohmann::json::parse(readFile(flat.optionsFile())));

    const auto listed = flat.run({"export", "groups"});
    CHECK(listed.answer.at("result") == merged.answer.at("result"));

    const auto reset = flat.run({"export", "groups", "reset"});
    REQUIRE(reset.exitCode == kExitOk);
    CHECK(GroupedSample::groupIds(reset) == std::vector<std::string>{"structure", "surfaces", "furniture_r1"});
    CHECK(GroupedSample::group(reset, "furniture_r1").at("name") == "Furniture Living");

    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}

TEST_CASE("reset keeps the kit switch", "[cli][export][groups]") {
    GroupedSample flat("groups-reset-kit");
    REQUIRE(flat.run({"export", "kit", "on"}).exitCode == kExitOk);
    REQUIRE(flat.run({"export", "groups", "new", "--name", "Hero"}).exitCode == kExitOk);
    const auto reset = flat.run({"export", "groups", "reset"});
    REQUIRE(reset.exitCode == kExitOk);
    CHECK(reset.answer.at("result").at("kit").at("enabled") == true);
    REQUIRE(fs::exists(flat.optionsFile()));
    const auto file = nlohmann::json::parse(readFile(flat.optionsFile()));
    CHECK(file.at("kit").at("enabled") == true);
}

TEST_CASE("group edits are refused with the codes the plan lists", "[cli][export][groups]") {
    GroupedSample flat("groups-refuse");

    auto refused = flat.run({"export", "groups", "rename", "nothing", "--name", "X"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "not_found");
    refused = flat.run({"export", "groups", "move", "item:i9", "--to", "structure"});
    CHECK(errorCode(refused) == "not_found");
    refused = flat.run({"export", "groups", "move", "item:i1", "--to", "nothing"});
    CHECK(errorCode(refused) == "not_found");
    refused = flat.run({"export", "groups", "merge", "nothing", "--into", "structure"});
    CHECK(errorCode(refused) == "not_found");

    refused = flat.run({"export", "groups", "rename", "structure", "--name", "---"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    refused = flat.run({"export", "groups", "merge", "structure", "--into", "structure"});
    CHECK(errorCode(refused) == "invalid_value");
    refused = flat.run({"export", "groups", "move", "item", "--to", "structure"});
    CHECK(errorCode(refused) == "invalid_value");
    refused = flat.run({"export", "groups", "new", "--name", ""});
    CHECK(refused.exitCode != kExitOk);

    refused = flat.run({"export", "groups", "rename", "furniture_r1", "--name", "walls"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "already_exists");

    refused = flat.run({"export", "groups", "rename", "structure"});
    CHECK(refused.exitCode != kExitOk);
    CHECK(errorCode(refused) == "bad_arguments");

    CHECK_FALSE(fs::exists(flat.optionsFile()));
}

TEST_CASE("a bad export-options.json is bad_file", "[cli][export][groups]") {
    GroupedSample flat("groups-badfile");
    std::ofstream(flat.optionsFile(), std::ios::binary) << "{not json";
    const auto listed = flat.run({"export", "groups"});
    CHECK(listed.exitCode == kExitFailed);
    CHECK(errorCode(listed) == "bad_file");
    const auto edited = flat.run({"export", "groups", "new", "--name", "Hero"});
    CHECK(errorCode(edited) == "bad_file");
    const auto kit = flat.run({"export", "kit", "on"});
    CHECK(errorCode(kit) == "bad_file");
    CHECK(readFile(flat.optionsFile()) == "{not json");
}

TEST_CASE("group names are English only", "[cli][export][groups]") {
    GroupedSample flat("groups-english");
    auto refused = flat.run({"export", "groups", "new", "--name", std::string("Caf") + '\xC3' + '\xA9'});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK_FALSE(refused.answer.at("error").at("message").get<std::string>().empty());
    refused = flat.run({"export", "groups", "rename", "structure", "--name", "Walls/1"});
    CHECK(errorCode(refused) == "invalid_value");
    CHECK_FALSE(fs::exists(flat.optionsFile()));
    CHECK(flat.run({"export", "groups", "new", "--name", "Hero_prop-2"}).exitCode == kExitOk);
}

TEST_CASE("reset repairs a bad export-options.json and keeps a readable kit switch", "[cli][export][groups]") {
    const std::vector<std::pair<std::string, bool>> bads = {
        {"{not json", false},
        {R"({"format":"plynth-export-options","version":1,"groups":{"bogus":1},"kit":{"enabled":true}})", true},
        {R"({"format":"plynth-export-options","version":1,"groups":{"added":[{"id":"group_1","name":"Caf?"}]},"kit":{"enabled":true}})", true}};
    int n = 0;
    for (const auto& [text, kitOn] : bads) {
        GroupedSample flat("groups-reset-bad" + std::to_string(n++));
        std::ofstream(flat.optionsFile(), std::ios::binary) << text;
        REQUIRE(errorCode(flat.run({"export", "groups"})) == "bad_file");
        const auto reset = flat.run({"export", "groups", "reset"});
        REQUIRE(reset.exitCode == kExitOk);
        CHECK(GroupedSample::groupIds(reset) == std::vector<std::string>{"structure", "surfaces", "furniture_r1"});
        CHECK(reset.answer.at("result").at("kit").at("enabled") == kitOn);
        const fs::path backup = flat.sample.folder / "export-options.json.bad";
        CHECK(fs::exists(backup));
        CHECK(readFile(backup) == text);
        REQUIRE(reset.answer.at("result").contains("repaired"));
        CHECK(reset.answer.at("result").at("repaired").at("backup") == utf8(backup));
        CHECK(flat.run({"export", "groups"}).exitCode == kExitOk);
    }
}

TEST_CASE("reset leaves a newer export-options.json alone", "[cli][export][groups]") {
    GroupedSample flat("groups-reset-newer");
    const std::string text = R"({"format":"plynth-export-options","version":99})";
    std::ofstream(flat.optionsFile(), std::ios::binary) << text;
    const auto reset = flat.run({"export", "groups", "reset"});
    CHECK(reset.exitCode == kExitFailed);
    CHECK(errorCode(reset) == "unsupported_version");
    CHECK(readFile(flat.optionsFile()) == text);
    CHECK_FALSE(fs::exists(flat.sample.folder / "export-options.json.bad"));
}

TEST_CASE("reset repairs a bad file in a single project and refuses a good one", "[cli][export][groups]") {
    GroupedSample flat("groups-reset-single", "single");
    const std::string bad = "{not json";
    std::ofstream(flat.optionsFile(), std::ios::binary) << bad;
    const auto reset = flat.run({"export", "groups", "reset"});
    REQUIRE(reset.exitCode == kExitOk);
    CHECK(reset.answer.at("result").contains("repaired"));
    CHECK(readFile(flat.sample.folder / "export-options.json.bad") == bad);
    CHECK_FALSE(fs::exists(flat.optionsFile()));
    CHECK(flat.exportNow().exitCode == kExitOk);

    const std::string good = R"({"format":"plynth-export-options","version":1,"kit":{"enabled":true}})";
    std::ofstream(flat.optionsFile(), std::ios::binary) << good;
    const auto refused = flat.run({"export", "groups", "reset"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(readFile(flat.optionsFile()) == good);
}

TEST_CASE("an item is one member of its group", "[cli][export][groups]") {
    GroupedSample flat("groups-one-member");
    const auto listed = flat.run({"export", "groups"});
    REQUIRE(listed.exitCode == kExitOk);
    int itemMembers = 0;
    for (const auto& group : listed.answer.at("result").at("groups")) {
        for (const auto& member : group.at("members")) {
            CHECK(member.at("kind") != "generator");
            if (member.at("kind") == "item") {
                ++itemMembers;
            }
        }
    }
    CHECK(itemMembers == 1);
    const auto moved = flat.run({"export", "groups", "move", "item:i1", "--to", "structure"});
    REQUIRE(moved.exitCode == kExitOk);
    // The room's set dressing stays with the room's group.
    // A copy: group() returns its answer by value.
    const auto left = GroupedSample::group(moved, "furniture_r1").at("members");
    REQUIRE(left.size() == 1);
    CHECK(left[0].at("kind") == "dressing");
}

TEST_CASE("a single project lists its one group and refuses edits", "[cli][export][groups]") {
    GroupedSample flat("groups-single", "single");
    const auto listed = flat.run({"export", "groups"});
    REQUIRE(listed.exitCode == kExitOk);
    const auto& result = listed.answer.at("result");
    CHECK(result.at("exporter") == "single");
    CHECK(result.at("reviewable") == false);
    CHECK(result.at("groups").size() == 1);

    const auto refused = flat.run({"export", "groups", "rename", "building", "--name", "All"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(refused.answer.at("error").at("message").get<std::string>().find("project set --export grouped") !=
          std::string::npos);
    CHECK(flat.run({"export", "groups", "new", "--name", "Hero"}).exitCode == kExitFailed);
    CHECK(flat.run({"export", "groups", "reset"}).exitCode == kExitFailed);
    CHECK_FALSE(fs::exists(flat.optionsFile()));
}

TEST_CASE("export kit shows and changes the switch", "[cli][export][kit]") {
    GroupedSample flat("kit-switch");
    const std::string plan = readFile(flat.sample.folder / "plan.json");
    const std::string project = readFile(flat.sample.folder / "project.json");

    auto shown = flat.run({"export", "kit"});
    REQUIRE(shown.exitCode == kExitOk);
    CHECK(shown.answer.at("result").at("enabled") == false);

    const auto on = flat.run({"export", "kit", "on"});
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("command") == "export kit on");
    CHECK(on.answer.at("result").at("enabled") == true);
    shown = flat.run({"export", "kit"});
    CHECK(shown.answer.at("result").at("enabled") == true);
    requireSchema("export-options.schema.json", nlohmann::json::parse(readFile(flat.optionsFile())));

    const auto off = flat.run({"export", "kit", "off"});
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("result").at("enabled") == false);
    CHECK_FALSE(fs::exists(flat.optionsFile()));

    CHECK(flat.run({"export", "kit", "maybe"}).exitCode != kExitOk);
    CHECK(readFile(flat.sample.folder / "plan.json") == plan);
    CHECK(readFile(flat.sample.folder / "project.json") == project);
}

TEST_CASE("switching the export mode keeps the build current", "[cli][export][groups]") {
    GroupedSample flat("groups-mode", "single");
    const auto builtFingerprint = [&] {
        const auto json = nlohmann::json::parse(readFile(flat.sample.folder / "build" / "built-plan.json"));
        return json.at("plan_fingerprint").get<std::string>();
    };
    const auto currentFingerprint = [&] {
        const auto checked = flat.run({"build", "--check", "--blender", utf8(flat.fake)});
        REQUIRE(checked.exitCode == kExitOk);
        return checked.answer.at("result").at("plan_fingerprint").get<std::string>();
    };
    REQUIRE(currentFingerprint() == builtFingerprint());

    const auto set = flat.run({"project", "set", "--export", "grouped"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("export_mode") == "grouped");
    const auto info = flat.run({"project", "info"});
    CHECK(info.answer.at("result").at("export_mode") == "grouped");
    CHECK(currentFingerprint() == builtFingerprint());

    const auto listed = flat.run({"export", "groups"});
    CHECK(listed.answer.at("result").at("reviewable") == true);

    CHECK(flat.run({"project", "set", "--export", "pairs"}).exitCode != kExitOk);
}

TEST_CASE("export of a grouped project writes the group files", "[cli][export][groups]") {
    GroupedSample flat("groups-export");
    const auto outcome = flat.exportNow();
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("exported") == true);
    CHECK(fs::exists(flat.sample.folder / "export" / "flat_Walls.fbx"));
    CHECK(fs::exists(flat.sample.folder / "export" / "flat_Floors.fbx"));
    CHECK(fs::exists(flat.sample.folder / "export" / "flat_Furniture_Living.fbx"));

    REQUIRE(flat.run({"export", "groups", "rename", "furniture_r1", "--name", "Lounge props"}).exitCode == kExitOk);
    const auto again = flat.exportNow();
    REQUIRE(again.exitCode == kExitOk);
    CHECK(fs::exists(flat.sample.folder / "export" / "flat_Lounge_props.fbx"));
    CHECK_FALSE(fs::exists(flat.sample.folder / "export" / "flat_Furniture_Living.fbx"));
}

TEST_CASE("group and kit edits follow the session rules", "[cli][export][groups]") {
    GroupedSample flat("groups-session");
    plynth::AppState state;
    state.process = plynth::currentProcess();
    state.modified = false;
    state.updated = plynth::sessionNow();
    REQUIRE(plynth::writeAppState(flat.sample.folder, state).ok());

    const auto refused = flat.run({"export", "groups", "new", "--name", "Hero"});
    CHECK(refused.exitCode == kExitFailed);
    CHECK(errorCode(refused) == "locked");
    CHECK(flat.run({"export", "kit", "on"}).exitCode == kExitFailed);
    CHECK_FALSE(fs::exists(flat.optionsFile()));

    CHECK(flat.run({"export", "groups"}).exitCode == kExitOk);
    CHECK(flat.run({"export", "kit"}).exitCode == kExitOk);
}

TEST_CASE("help lists the group and kit commands", "[cli][export][groups]") {
    const auto outcome = plynth::cli::testing::runChecked({"help"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.text.find("export groups rename") != std::string::npos);
    CHECK(outcome.text.find("export groups merge") != std::string::npos);
    CHECK(outcome.text.find("export groups move") != std::string::npos);
    CHECK(outcome.text.find("export groups new") != std::string::npos);
    CHECK(outcome.text.find("export groups reset") != std::string::npos);
    CHECK(outcome.text.find("export kit on") != std::string::npos);
    CHECK(outcome.text.find("project set") != std::string::npos);
    CHECK(outcome.text.find("--export") != std::string::npos);
}

TEST_CASE("the --name help lists the allowed characters", "[cli][export][groups]") {
    const auto outcome = plynth::cli::testing::runChecked({"export", "--help"});
    CHECK(outcome.text.find("A-Z") != std::string::npos);
    CHECK(outcome.text.find("hyphen") != std::string::npos);
}
