#pragma once

#include "plynth/core/Build.h"
#include "plynth/core/EditSession.h"
#include "plynth/core/ProjectFiles.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <thread>

// Helpers for build.lock, shared by the build and the export, which take the same lock.
namespace plynth::lockfile {

// Seconds since 1970 now.
inline long long epochSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Makes the lock file only when it does not exist yet. False when it exists or cannot be made.
inline bool createExclusive(const std::filesystem::path& file, const std::string& text)
{
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(handle);
    return true;
}

// Whether an existing lock still counts: younger than kBuildTimeout, by the time it holds and by the file's
// own write time, and, when it names a process, that process is still running. A lock whose content cannot
// be read counts by its write time alone.
inline bool lockIsLive(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(file, ec);
    if (!ec && std::filesystem::file_time_type::clock::now() - written > kBuildTimeout) return false;

    std::ifstream in(file, std::ios::binary);
    ProcessIdentity who;
    long long started = 0;
    if (in >> who.pid >> who.startKey >> started) {
        if (epochSeconds() - started > kBuildTimeout.count()) return false;
        return isProcessRunning(who);
    }
    return true;
}

// Watches build/cancel.request while a build or an export holds the lock, so "plynth build cancel" or
// "plynth export cancel" from another process stops it. A marker left from before is removed at the start, and
// the marker is removed again at the end. token() stops when the marker appears or the outer token stops.
class CancelWatch {
public:
    CancelWatch(const std::filesystem::path& buildFolder, std::stop_token outer)
        : marker_(buildFolder / pathFromUtf8(kBuildCancelFileName))
    {
        std::error_code ec;
        std::filesystem::remove(marker_, ec);
        watcher_ = std::jthread([this, outer](std::stop_token own) {
            while (!own.stop_requested()) {
                std::error_code ignored;
                if (outer.stop_requested() || std::filesystem::exists(marker_, ignored)) {
                    source_.request_stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
    }
    ~CancelWatch()
    {
        watcher_.request_stop();
        if (watcher_.joinable()) watcher_.join();
        std::error_code ec;
        std::filesystem::remove(marker_, ec);
    }
    CancelWatch(const CancelWatch&) = delete;
    CancelWatch& operator=(const CancelWatch&) = delete;

    std::stop_token token() const { return source_.get_token(); }

private:
    std::filesystem::path marker_;
    std::stop_source source_;
    std::jthread watcher_;
};

}
