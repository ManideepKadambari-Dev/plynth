#include "SampleProject.h"

#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::Outcome;
using plynth::cli::testing::readFile;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::Sample;

namespace {

// Splits command words such as "export groups" into a list.
std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : text) {
        if (c == ' ') {
            words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    words.push_back(word);
    return words;
}

// The plan and settings files of a project, to see that a refused command changed nothing.
std::string projectFiles(const Sample& sample) {
    return readFile(sample.folder / "plan.json") + readFile(sample.folder / "project.json");
}

// Edit commands that work on the sample square when nothing holds the project.
std::vector<std::vector<std::string>> editCommands() {
    return {
        {"point", "add", "--x", "9", "--y", "9"},
        {"wall", "draw", "--from-x", "8", "--from-y", "1", "--to-x", "9", "--to-y", "1"},
        {"wall", "set", "w1", "--thickness", "0.25"},
        {"room", "add", "--x", "3", "--y", "3", "--type", "living"},
        {"opening", "add", "--wall", "w1", "--offset", "2", "--type", "door"},
        {"scale", "set", "--pixels-per-metre", "90"},
        {"project", "set", "--grid", "0.25"},
        {"floor", "add", "--name", "Upper"},
        {"material", "set", "default.floor", "none"},
        {"light", "add", "--x", "3", "--y", "3"},
        {"trim", "set", "skirting", "--off"},
        {"variation", "set", "--seed", "7"},
    };
}

}

TEST_CASE("every command has a description, and so does each of its options", "[cli][whole]") {
    const Outcome help = runChecked({"help"});
    std::set<std::string> seen;
    for (const auto& command : help.answer.at("result").at("commands")) {
        const std::string words = command.at("command").get<std::string>();
        INFO(words);
        CHECK(seen.insert(words).second);
        CHECK_FALSE(command.at("description").get<std::string>().empty());
        for (const auto& option : command.at("options")) {
            INFO(option.at("name").get<std::string>());
            CHECK_FALSE(option.at("description").get<std::string>().empty());
        }
        CHECK(plynth::cli::isCommandWord(split(words).front()));
    }
    CHECK(seen.size() > 100);
}

TEST_CASE("every command answers an unknown option with one valid JSON answer", "[cli][whole]") {
    const Outcome help = runChecked({"help"});
    for (const auto& command : help.answer.at("result").at("commands")) {
        std::vector<std::string> args = split(command.at("command").get<std::string>());
        args.push_back("--no-such-option-anywhere");
        INFO(command.at("command").get<std::string>());
        const Outcome outcome = runChecked(args);
        CHECK(outcome.exitCode == kExitBadArguments);
        CHECK(errorCode(outcome) == "bad_arguments");
    }
}

TEST_CASE("every edit command refuses a project the app holds without a session, and changes nothing", "[cli][whole]") {
    Sample sample("whole-locked");
    sample.drawSquare();
    REQUIRE(plynth::writeAppState(sample.folder, plynth::AppState{plynth::currentProcess(), false, plynth::sessionNow()}).ok());
    const std::string before = projectFiles(sample);
    for (const std::vector<std::string>& args : editCommands()) {
        const Outcome outcome = sample.run(args);
        INFO(outcome.text);
        CHECK(errorCode(outcome) == "locked");
    }
    CHECK(projectFiles(sample) == before);

    // With a session the same commands are let through. Some may still refuse for their own reasons, but none
    // as locked.
    REQUIRE(sample.run({"session", "begin", "--tool", "test"}).exitCode == kExitOk);
    for (const std::vector<std::string>& args : editCommands()) {
        const Outcome outcome = sample.run(args);
        INFO(outcome.text);
        CHECK((outcome.exitCode == kExitOk || errorCode(outcome) != "locked"));
    }
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("a refused edit leaves the project files byte for byte", "[cli][whole]") {
    Sample sample("whole-refused");
    sample.drawSquare();
    const std::string before = projectFiles(sample);
    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {"wall", "set", "w1", "--thickness", "-1"},
             {"point", "move", "p99", "--x", "1", "--y", "1"},
             {"scale", "set", "--pixels-per-metre", "0"},
             {"opening", "add", "--wall", "w1", "--offset", "99", "--type", "door"},
             {"room", "set", "r99", "--type", "bedroom"},
             {"light", "set", "l99", "--x", "1"},
             {"trim", "set", "skirting", "--depth", "9"},
             {"variation", "set", "--seed", "minus"},
             {"material", "set", "w1.sideways", "oak"},
         }) {
        const Outcome outcome = sample.run(args);
        INFO(outcome.text);
        CHECK(outcome.exitCode != kExitOk);
    }
    CHECK(projectFiles(sample) == before);
}
