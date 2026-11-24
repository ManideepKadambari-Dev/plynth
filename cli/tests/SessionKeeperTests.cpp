#include "SampleProject.h"

#include "../src/SessionKeeper.h"

#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using plynth::cli::kExitOk;
using plynth::cli::testing::Sample;

namespace {

// The last activity of the project's session.
plynth::SessionTime lastActivity(const Sample& sample) {
    const plynth::SessionRead read = plynth::readSession(sample.folder, plynth::sessionNow());
    REQUIRE(read.state == plynth::SessionState::Active);
    return read.lock.lastActivity;
}

}

TEST_CASE("a long command keeps an active session alive while it runs", "[cli][gaps]") {
    Sample sample("session-keeper");
    REQUIRE(sample.run({"session", "begin", "--tool", "test"}).exitCode == kExitOk);
    const plynth::SessionTime before = lastActivity(sample);
    {
        const plynth::cli::SessionKeeper keeper(sample.folder, std::chrono::milliseconds(200));
        std::this_thread::sleep_for(std::chrono::milliseconds(2600));
    }
    const plynth::SessionTime kept = lastActivity(sample);
    CHECK(kept - before >= std::chrono::seconds(2));

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(lastActivity(sample) == kept);
    REQUIRE(sample.run({"session", "end"}).exitCode == kExitOk);
}

TEST_CASE("the session keeper writes nothing when no session is active", "[cli][gaps]") {
    Sample sample("session-keeper-none");
    {
        const plynth::cli::SessionKeeper keeper(sample.folder, std::chrono::milliseconds(100));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    CHECK_FALSE(std::filesystem::exists(sample.folder / "session.lock"));
}
