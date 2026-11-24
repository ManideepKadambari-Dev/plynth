#include "TestSupport.h"
#include "SettingsGuard.h"

#include <plynth/cli/Cli.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitOk;
using plynth::cli::testing::runChecked;

namespace {

// The code of a failed answer.
std::string errorCode(const nlohmann::json& answer) {
    return answer.at("error").at("code").get<std::string>();
}

// The commands named in a help answer.
std::vector<std::string> helpNames(const nlohmann::json& answer) {
    std::vector<std::string> names;
    for (const auto& command : answer.at("result").at("commands")) {
        names.push_back(command.at("command").get<std::string>());
    }
    return names;
}

}

TEST_CASE("version answers with the program and format versions", "[cli]") {
    const auto outcome = runChecked({"version"});
    CHECK(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("ok") == true);
    CHECK(outcome.answer.at("command") == "version");
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("version") == PLYNTH_EXPECTED_VERSION);
    CHECK(result.at("plan_format") == 7);
    CHECK(result.at("project_format") == 6);
}

TEST_CASE("version is one compact line", "[cli]") {
    const auto outcome = runChecked({"version"});
    CHECK(outcome.text ==
          std::string(R"({"ok":true,"command":"version","result":{"version":")") + PLYNTH_EXPECTED_VERSION +
              "\",\"plan_format\":7,\"project_format\":6}}\n");
}

TEST_CASE("the pretty flag indents the answer and may come anywhere", "[cli]") {
    const auto first = runChecked({"--pretty", "version"});
    const auto last = runChecked({"version", "--pretty"});
    CHECK(first.text == last.text);
    CHECK(first.text.find("\n  \"ok\": true") != std::string::npos);
    CHECK(first.exitCode == kExitOk);
}

TEST_CASE("help lists every command with its options", "[cli]") {
    const auto outcome = runChecked({"help"});
    CHECK(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "help");
    const auto names = helpNames(outcome.answer);
    CHECK(std::ranges::find(names, "version") != names.end());
    CHECK(std::ranges::find(names, "help") != names.end());
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        CHECK(command.at("description").is_string());
        for (const auto& option : command.at("options")) {
            CHECK(option.at("positional").is_boolean());
            if (!option.at("positional").get<bool>()) {
                CHECK(option.at("name").get<std::string>().starts_with("--"));
            }
            CHECK(option.at("required").is_boolean());
            CHECK(option.at("description").is_string());
        }
    }
}

TEST_CASE("the help flag on any command gives the help answer", "[cli]") {
    const auto plain = runChecked({"help"});
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"--help"}, {"version", "--help"}, {"project", "--help"}, {"point", "add", "--help"}, {"-h"}}) {
        const auto outcome = runChecked(args);
        CHECK(outcome.exitCode == kExitOk);
        CHECK(outcome.text == plain.text);
    }
}

TEST_CASE("an unknown word gives unknown_command and exit 2", "[cli]") {
    const auto outcome = runChecked({"frobnicate"});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(outcome.answer.at("ok") == false);
    CHECK(outcome.answer.at("command") == "");
    CHECK(errorCode(outcome.answer) == "unknown_command");
}

TEST_CASE("no arguments gives unknown_command and exit 2", "[cli]") {
    const auto outcome = runChecked({});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(errorCode(outcome.answer) == "unknown_command");
}

TEST_CASE("a group word with no known second word gives unknown_command", "[cli]") {
    for (const auto& args : std::vector<std::vector<std::string>>{{"project"}, {"wall", "paint"}}) {
        const auto outcome = runChecked(args);
        CHECK(outcome.exitCode == kExitBadArguments);
        CHECK(errorCode(outcome.answer) == "unknown_command");
        CHECK(outcome.answer.at("command") == args.front());
    }
}

TEST_CASE("an option before any command gives unknown_command", "[cli]") {
    const auto outcome = runChecked({"--image", "plan.png"});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(errorCode(outcome.answer) == "unknown_command");
}

TEST_CASE("an unknown option gives bad_arguments and exit 2", "[cli]") {
    const auto outcome = runChecked({"version", "--nope"});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(outcome.answer.at("command") == "version");
    CHECK(errorCode(outcome.answer) == "bad_arguments");
}

TEST_CASE("an unexpected extra word gives bad_arguments", "[cli]") {
    const auto outcome = runChecked({"version", "extra"});
    CHECK(outcome.exitCode == kExitBadArguments);
    CHECK(errorCode(outcome.answer) == "bad_arguments");
}

TEST_CASE("every word of the first argument is recognised by isCommandWord", "[cli]") {
    for (const char* word : {"version", "help", "project", "scale", "point", "wall", "place", "item", "build"}) {
        CHECK(plynth::cli::isCommandWord(word));
    }
    CHECK_FALSE(plynth::cli::isCommandWord("D:/plans/flat"));
    CHECK_FALSE(plynth::cli::isCommandWord("--image"));
    CHECK_FALSE(plynth::cli::isCommandWord(""));
    CHECK_FALSE(plynth::cli::isCommandWord("Version"));
}

TEST_CASE("answers keep non ASCII text intact", "[cli]") {
    const auto outcome = runChecked({"frob\xC3\xBC"});
    CHECK(outcome.answer.at("error").at("message").get<std::string>().find("frob\xC3\xBC") != std::string::npos);
}

TEST_CASE("the launcher's unknown_command answer is byte for byte the CLI's", "[cli]") {
    const std::vector<std::vector<std::string>> cases = {
        {"bogus"},
        {"--bogus"},
        {"frob\xC3\xBC", "x"},
        {"say \"hi\"\there"},
        {"tab\there"},
        {"--pretty", "bogus"},
        {"bogus", "--pretty"},
        {"--pretty"},
        {},
    };
    for (const auto& args : cases) {
        std::ostringstream out;
        plynth::cli::testing::requireSafeSettings("running a CLI command");
        const int code = plynth::cli::run(args, out, std::filesystem::current_path());
        CHECK(code == plynth::cli::kExitBadArguments);
        CHECK(out.str() == plynth::cli::unknownCommandAnswer(args));
    }
}

TEST_CASE("the word list equals the first words of the commands help lists", "[cli]") {
    const auto outcome = runChecked({"help"});
    std::set<std::string> listed;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        const std::string text = command.at("command").get<std::string>();
        listed.insert(text.substr(0, text.find(' ')));
    }
    std::set<std::string> words;
    for (const auto word : plynth::cli::commandWords()) {
        words.insert(std::string(word));
    }
    CHECK(words == listed);
}
