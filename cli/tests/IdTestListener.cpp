#include "../../core/src/IdSource.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <optional>

namespace {

// Keeps ids counted during every test case, so tests that name ids such as "w1" keep them.
class CountedIdsListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    // Starts counting ids for the test case.
    void testCaseStarting(Catch::TestCaseInfo const&) override { counted_.emplace(); }

    // Stops counting ids when the test case ends.
    void testCaseEnded(Catch::TestCaseStats const&) override { counted_.reset(); }

private:
    std::optional<plynth::detail::ScopedCountedIds> counted_;
};

}

CATCH_REGISTER_LISTENER(CountedIdsListener)
