#pragma once

#include "plynth/core/Build.h"
#include "plynth/core/EditSession.h"

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
#include <string>

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

}
