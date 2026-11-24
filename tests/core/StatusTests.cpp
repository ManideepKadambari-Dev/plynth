#include <plynth/core/Status.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <regex>
#include <set>
#include <string>

using namespace plynth;

TEST_CASE("every error code has a unique lower case name and a description", "[status]") {
    const std::regex pattern("^[a-z_]+$");
    std::set<std::string> names;
    for (const ErrorCode code : allErrorCodes()) {
        const std::string name(errorCodeName(code));
        INFO(name);
        REQUIRE(std::regex_match(name, pattern));
        REQUIRE(name != "ok");
        REQUIRE(names.insert(name).second);
        REQUIRE_FALSE(errorCodeDescription(code).empty());
    }
    REQUIRE(names.count("graphics_unavailable") == 1);
    REQUIRE(names.count("render_failed") == 1);
}

TEST_CASE("allErrorCodes holds each code once, in enum order, without None", "[status]") {
    const auto codes = allErrorCodes();
    REQUIRE(std::find(codes.begin(), codes.end(), ErrorCode::None) == codes.end());
    REQUIRE(static_cast<int>(codes.front()) == 1);
    for (std::size_t i = 0; i < codes.size(); ++i) {
        REQUIRE(static_cast<std::size_t>(codes[i]) == i + 1);
    }
}
