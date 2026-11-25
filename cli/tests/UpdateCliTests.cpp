#include "SampleProject.h"
#include "SettingsGuard.h"
#include "TestSupport.h"

#include <plynth/cli/Words.h>
#include <plynth/core/Settings.h>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using plynth::cli::kExitBadArguments;
using plynth::cli::kExitFailed;
using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::runChecked;

namespace {

// The settings of the test start from a first run.
void clearSettings() {
    plynth::cli::testing::removeSettingsFileSafely();
}

}

TEST_CASE("update is a command word", "[cli][update]") {
    CHECK(plynth::cli::isCommandWord("update"));
}

TEST_CASE("update and update --check refuse a copy that is not installed", "[cli][update]") {
    clearSettings();
    for (const std::vector<std::string>& args : {std::vector<std::string>{"update"},
                                                  std::vector<std::string>{"update", "--check"}}) {
        const auto outcome = runChecked(args);
        CHECK(outcome.exitCode == kExitFailed);
        CHECK(errorCode(outcome) == "update_not_installed");
        const std::string message = outcome.answer.at("error").at("message").get<std::string>();
        CHECK(message.find("was not installed") != std::string::npos);
        CHECK(message.find("github.com/ManideepKadambari-Dev/plynth/releases") != std::string::npos);
        CHECK(outcome.progress.empty());
    }
    clearSettings();
}

TEST_CASE("update --status answers without a network and says not installed", "[cli][update]") {
    clearSettings();
    const auto outcome = runChecked({"update", "--status"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("install") == "not_installed");
    CHECK(result.at("check_at_start") == false);
    CHECK(result.at("last").is_null());
    CHECK(result.at("key_ids").is_array());
    CHECK(result.at("version").is_string());
    clearSettings();
}

TEST_CASE("settings set --update-check changes the switch and update --check still refuses by hand", "[cli][update]") {
    clearSettings();
    const auto off = runChecked({"settings", "set", "--update-check", "false"});
    REQUIRE(off.exitCode == kExitOk);
    CHECK(off.answer.at("result").at("settings").at("update_check") == false);

    const auto shown = runChecked({"settings", "show"});
    CHECK(shown.answer.at("result").at("settings").at("update_check") == false);

    const auto status = runChecked({"update", "--status"});
    REQUIRE(status.exitCode == kExitOk);
    CHECK(status.answer.at("result").at("check_at_start") == false);

    const auto check = runChecked({"update", "--check"});
    CHECK(errorCode(check) == "update_not_installed");

    const auto on = runChecked({"settings", "set", "--update-check", "true"});
    REQUIRE(on.exitCode == kExitOk);
    CHECK(on.answer.at("result").at("settings").at("update_check") == true);
    clearSettings();
}

TEST_CASE("settings show gives update_check true on a first run", "[cli][update]") {
    clearSettings();
    const auto shown = runChecked({"settings", "show"});
    REQUIRE(shown.exitCode == kExitOk);
    CHECK(shown.answer.at("result").at("settings").at("update_check") == true);
    clearSettings();
}

TEST_CASE("update refuses bad options", "[cli][update]") {
    clearSettings();
    const auto both = runChecked({"update", "--check", "--status"});
    CHECK(both.exitCode == kExitBadArguments);
    CHECK(errorCode(both) == "bad_arguments");

    const auto unknown = runChecked({"update", "--now"});
    CHECK(unknown.exitCode == kExitBadArguments);
    CHECK(errorCode(unknown) == "bad_arguments");

    const auto subcommand = runChecked({"update", "check"});
    CHECK(subcommand.exitCode == kExitBadArguments);
    CHECK(errorCode(subcommand) == "bad_arguments");

    const auto badSwitch = runChecked({"settings", "set", "--update-check", "maybe"});
    CHECK(badSwitch.exitCode == kExitBadArguments);
    CHECK(errorCode(badSwitch) == "bad_arguments");

    const auto missing = runChecked({"settings", "set", "--update-check"});
    CHECK(missing.exitCode == kExitBadArguments);
    CHECK(errorCode(missing) == "bad_arguments");
    clearSettings();
}

TEST_CASE("help lists update and its three error codes", "[cli][update]") {
    const auto outcome = runChecked({"help"});
    bool listed = false;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        listed = listed || command.at("command").get<std::string>() == "update";
    }
    CHECK(listed);
    std::set<std::string> codes;
    for (const auto& entry : outcome.answer.at("result").at("error_codes")) {
        codes.insert(entry.at("code").get<std::string>());
    }
    for (const char* code : {"update_not_installed", "update_not_available", "update_blocked", "update_in_progress",
                             "signature_invalid", "network_failed"}) {
        INFO(code);
        CHECK(codes.count(code) == 1);
    }
}
