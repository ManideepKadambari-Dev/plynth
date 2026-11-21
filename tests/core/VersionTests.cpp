#include <catch2/catch_test_macros.hpp>

#include <plynth/core/Version.h>

#include <string_view>

TEST_CASE("version is set and matches the project version", "[core][version]") {
    REQUIRE_FALSE(plynth::version().empty());
    REQUIRE(plynth::version() == std::string_view(PLYNTH_EXPECTED_VERSION));
}
