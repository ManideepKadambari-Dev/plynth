#include "TestSupport.h"

#include <plynth/core/Status.h>

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <set>
#include <string>

using plynth::cli::testing::runChecked;

TEST_CASE("help lists every error code once with a description", "[errors]") {
    const auto outcome = runChecked({"help"});
    const auto& list = outcome.answer.at("result").at("error_codes");
    REQUIRE(list.is_array());
    const std::regex pattern("^[a-z_]+$");
    std::set<std::string> codes;
    for (const auto& entry : list) {
        const std::string code = entry.at("code").get<std::string>();
        INFO(code);
        CHECK(std::regex_match(code, pattern));
        CHECK(codes.insert(code).second);
        CHECK_FALSE(entry.at("description").get<std::string>().empty());
        const std::string from = entry.at("from").get<std::string>();
        CHECK((from == "core" || from == "cli"));
    }
    for (const plynth::ErrorCode code : plynth::allErrorCodes()) {
        INFO(plynth::errorCodeName(code));
        CHECK(codes.count(std::string(plynth::errorCodeName(code))) == 1);
    }
    for (const char* code : {"unknown_command", "bad_arguments", "internal_error"}) {
        CHECK(codes.count(code) == 1);
    }
    CHECK(codes.count("ok") == 0);
}

TEST_CASE("help lists the three exit codes", "[errors]") {
    const auto outcome = runChecked({"help"});
    const auto& list = outcome.answer.at("result").at("exit_codes");
    REQUIRE(list.size() == 3);
    for (int i = 0; i < 3; ++i) {
        CHECK(list.at(i).at("code") == i);
        CHECK_FALSE(list.at(i).at("description").get<std::string>().empty());
    }
}

TEST_CASE("a failed answer uses a listed code", "[errors]") {
    const auto help = runChecked({"help"});
    std::set<std::string> codes;
    for (const auto& entry : help.answer.at("result").at("error_codes")) {
        codes.insert(entry.at("code").get<std::string>());
    }
    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {"bogus"}, {"render", "--view", "side"}, {"project", "info", "--project", "Z:/does/not/exist"}}) {
        const auto outcome = runChecked(args);
        REQUIRE_FALSE(outcome.answer.at("ok").get<bool>());
        INFO(outcome.text);
        CHECK(codes.count(outcome.answer.at("error").at("code").get<std::string>()) == 1);
    }
}
