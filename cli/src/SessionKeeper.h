#pragma once

#include <plynth/core/EditSession.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <thread>

namespace plynth::cli {

// Keeps an active edit session alive while a long command runs (a build, an export, a pack), so the lock does
// not expire in the middle of it and the app does not unlock. It refreshes the session's last activity now and
// then, the way every command does when it starts (allowCliCommand), and does nothing when no session is active.
class SessionKeeper {
public:
    explicit SessionKeeper(std::filesystem::path folder,
                           std::chrono::milliseconds every = std::chrono::seconds(kSessionExpirySeconds / 5))
        : folder_(std::move(folder)), every_(every)
    {
        worker_ = std::jthread([this](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any wake;
            std::unique_lock lock(mutex);
            while (!wake.wait_for(lock, stop, every_, [] { return false; })) {
                if (stop.stop_requested()) return;
                allowCliCommand(folder_, sessionNow(), true);
            }
        });
    }
    ~SessionKeeper()
    {
        worker_.request_stop();
        if (worker_.joinable()) worker_.join();
    }
    SessionKeeper(const SessionKeeper&) = delete;
    SessionKeeper& operator=(const SessionKeeper&) = delete;

private:
    std::filesystem::path folder_;
    std::chrono::milliseconds every_;
    std::jthread worker_;
};

}
