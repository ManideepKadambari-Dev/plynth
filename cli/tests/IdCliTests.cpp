#include "MaterialLibrary.h"
#include "SampleProject.h"
#include "SettingsGuard.h"
#include "../../core/src/IdSource.h"

#include <plynth/core/Document.h>
#include <plynth/core/commands/ItemCommands.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::MaterialLibrary;
using plynth::cli::testing::readFile;
using plynth::cli::testing::Sample;

namespace {

using Words = std::vector<std::string>;

// The ids the product makes, for the life of the object.
struct SystemIds {
    plynth::detail::ScopedIdSource scope{plynth::detail::IdPartMaker(&plynth::detail::systemIdPart)};
};

// The plan file as JSON.
nlohmann::json planOf(const Sample& sample) {
    return nlohmann::json::parse(readFile(sample.folder / "plan.json"));
}

// Every id in the plan file.
std::set<std::string> idsOf(const nlohmann::json& plan) {
    std::set<std::string> out;
    for (const auto& floor : plan.at("floors")) {
        out.insert(floor.at("id").get<std::string>());
        for (const char* list : {"points", "walls", "rooms", "openings", "items", "stairs", "lights", "light_strips"}) {
            for (const auto& one : floor.at(list)) {
                out.insert(one.at("id").get<std::string>());
            }
        }
    }
    for (const auto& section : plan.at("roof_sections")) {
        out.insert(section.at("id").get<std::string>());
    }
    return out;
}

// Says whether an id is the prefix and eight characters of the new shape with a letter after the first.
bool newShape(const std::string& id, const std::string& prefix) {
    const std::regex shape("^" + prefix + "[0-9][0-9a-hjkmnp-tv-z]{7}$");
    return std::regex_match(id, shape) && id.substr(prefix.size() + 1).find_first_not_of("0123456789") != std::string::npos;
}

// Runs an add and returns its one new id that starts with the prefix and a digit.
std::string addedId(const Sample& sample, const Words& args, const std::string& prefix) {
    const auto before = idsOf(planOf(sample));
    const auto outcome = sample.run(args);
    INFO(outcome.text);
    REQUIRE(outcome.exitCode == kExitOk);
    for (const auto& id : idsOf(planOf(sample))) {
        if (!before.contains(id) && id.size() > prefix.size() && id.compare(0, prefix.size(), prefix) == 0 &&
            id[prefix.size()] >= '0' && id[prefix.size()] <= '9') {
            return id;
        }
    }
    FAIL("No new id");
    return {};
}

// One add command and the words that remove what it added.
struct AddRemove {
    std::string prefix;
    Words add;
    Words remove;
    Words options;
};

// Every kind of object the CLI adds and removes without a library.
std::vector<AddRemove> kinds() {
    return {
        {"f", {"floor", "add"}, {"floor", "remove"}, {"--yes"}},
        {"p", {"point", "add", "--x", "9", "--y", "9"}, {"point", "remove"}, {}},
        {"w", {"wall", "draw", "--from", "p1", "--to", "p3"}, {"wall", "remove"}, {"--cascade"}},
        {"r", {"room", "add", "--x", "3", "--y", "5"}, {"room", "remove"}, {}},
        {"o", {"opening", "add", "--wall", "w1", "--offset", "2", "--type", "door"}, {"opening", "remove"}, {}},
        {"s", {"stair", "add", "--floor", "f1", "--x", "2", "--y", "4"}, {"stair", "remove"}, {}},
        {"rf", {"roof", "set", "--floor", "f1", "--at", "3,5", "--type", "hip"}, {"roof", "clear"}, {}},
        {"l", {"light", "add", "--x", "3", "--y", "5"}, {"light", "remove"}, {}},
        {"ls", {"light-strip", "add", "--kind", "cove", "--points", "1.2,3.2,4.8,3.2"}, {"light-strip", "remove"}, {}},
    };
}

// Places an item in the plan through the core, as the app would.
std::string placeThroughCore(const Sample& sample) {
    plynth::Item item;
    item.asset = "sofa";
    item.x = 3.0;
    item.y = 5.0;
    item.size = {1.0, 0.5, 0.5};
    plynth::Document doc;
    REQUIRE(doc.open(sample.folder).ok());
    auto place = std::make_unique<plynth::PlaceItemCommand>("f1", item);
    const plynth::PlaceItemCommand* made = place.get();
    REQUIRE(doc.execute(std::move(place)).ok());
    const std::string id = made->itemId();
    REQUIRE(doc.save().ok());
    return id;
}

}

TEST_CASE("each add in its own run after removing the newest gets a new id of the shape", "[cli][ids]") {
    const std::size_t k = GENERATE(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u);
    const AddRemove kind = kinds().at(k);
    INFO(kind.prefix);
    const Sample sample("ids-random-" + kind.prefix);
    sample.drawRoomSquare();
    if (kind.prefix == "r") REQUIRE(sample.run({"room", "remove", "r1"}).exitCode == kExitOk);
    if (kind.prefix == "s") REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    SystemIds system;

    std::set<std::string> given;
    for (int round = 0; round < 3; ++round) {
        const auto id = addedId(sample, kind.add, kind.prefix);
        CHECK(newShape(id, kind.prefix));
        CHECK(given.insert(id).second);
        Words remove = kind.remove;
        remove.push_back(id);
        remove.insert(remove.end(), kind.options.begin(), kind.options.end());
        REQUIRE(sample.run(remove).exitCode == kExitOk);
    }
}

TEST_CASE("detect apply after removes gives no removed id", "[cli][ids]") {
    const Sample sample("ids-random-detect");
    sample.drawRoomSquare();
    SystemIds system;
    std::set<std::string> removed = idsOf(planOf(sample));
    removed.erase("f1");
    for (const char* point : {"p1", "p2", "p3", "p4"}) {
        if (!idsOf(planOf(sample)).contains(point)) continue;
        REQUIRE(sample.run({"point", "remove", point, "--cascade"}).exitCode == kExitOk);
    }
    REQUIRE(idsOf(planOf(sample)) == std::set<std::string>{"f1"});
    REQUIRE(sample.run({"detect", "--detector", "test-fixed", "--apply"}).exitCode == kExitOk);
    for (const auto& id : idsOf(planOf(sample))) {
        INFO(id);
        CHECK_FALSE(removed.contains(id));
        if (id != "f1") CHECK(newShape(id, id.substr(0, 1)));
    }
}

TEST_CASE("item remove in its own run and a new place give another id", "[cli][ids]") {
    const Sample sample("ids-random-item");
    sample.drawRoomSquare();
    SystemIds system;
    const std::string first = placeThroughCore(sample);
    CHECK(newShape(first, "i"));
    REQUIRE(sample.run({"item", "remove", first}).exitCode == kExitOk);
    CHECK(placeThroughCore(sample) != first);
}

TEST_CASE("a new export group has an id of the shape that move accepts", "[cli][ids]") {
    const Sample sample("ids-random-groups");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"project", "set", "--export", "grouped"}).exitCode == kExitOk);
    SystemIds system;
    const std::string sofa = placeThroughCore(sample);
    const auto added = sample.run({"export", "groups", "new", "--name", "Hero"});
    REQUIRE(added.exitCode == kExitOk);
    const std::string hero = added.answer.at("result").at("new_group").get<std::string>();
    CHECK(newShape(hero, "group_"));
    const auto moved = sample.run({"export", "groups", "move", "item:" + sofa, "--to", hero});
    INFO(moved.text);
    REQUIRE(moved.exitCode == kExitOk);
    bool found = false;
    for (const auto& group : moved.answer.at("result").at("groups")) {
        if (group.at("id") != hero) continue;
        for (const auto& member : group.at("members")) {
            if (member.at("kind") == "item" && member.at("id") == sofa) found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("material set takes a stair with a new id", "[cli][ids]") {
    plynth::cli::testing::MaterialSettingsReset reset;
    MaterialLibrary library("ids-random-material-lib");
    const Sample sample("ids-random-material");
    sample.drawRoomSquare();
    REQUIRE(sample.run({"floor", "add"}).exitCode == kExitOk);
    SystemIds system;
    const std::string stair = addedId(sample, {"stair", "add", "--floor", "f1", "--x", "2", "--y", "4"}, "s");
    CHECK(newShape(stair, "s"));
    const auto set = sample.run({"material", "set", stair, "oak_planks"});
    INFO(set.text);
    REQUIRE(set.exitCode == kExitOk);
    CHECK(errorCode(sample.run({"material", "set", "s3vk8m2qa", "oak_planks"})) == "not_found");
}

TEST_CASE("a refused edit leaves plan.json byte for byte", "[cli][ids]") {
    const Sample sample("ids-random-refused");
    sample.drawRoomSquare();
    SystemIds system;
    const auto before = readFile(sample.folder / "plan.json");
    CHECK(sample.run({"point", "remove", "p3vk8m2qa"}).exitCode != kExitOk);
    CHECK(sample.run({"project", "info"}).exitCode == kExitOk);
    CHECK(readFile(sample.folder / "plan.json") == before);
}
