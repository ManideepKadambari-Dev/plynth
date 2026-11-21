#include "Staging.h"

#include "plynth/core/EditSession.h"
#include "plynth/core/ProjectFiles.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

namespace plynth::staging {

namespace fs = std::filesystem;

namespace {

constexpr const char* kMarkerName = ".live";
constexpr const char* kSourceName = "source.txt";

// Whether a directory entry is a folder.
bool isFolder(const fs::directory_entry& entry)
{
    std::error_code ec;
    return entry.is_directory(ec);
}

}

// Writes the marker.
void markLive(const fs::path& folder)
{
    const ProcessIdentity me = currentProcess();
    std::ofstream out(folder / kMarkerName, std::ios::binary | std::ios::trunc);
    out << me.pid << " " << me.startKey << "\n";
}

// Removes the marker.
void unmarkLive(const fs::path& folder)
{
    std::error_code ec;
    fs::remove(folder / kMarkerName, ec);
}

// Reads the marker and asks whether that process still runs.
bool isLive(const fs::path& folder)
{
    std::ifstream in(folder / kMarkerName, std::ios::binary);
    if (!in) return false;
    ProcessIdentity who;
    in >> who.pid >> who.startKey;
    return in && isProcessRunning(who);
}

// Removes a tree with a few tries.
void removeTree(const fs::path& folder)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        fs::remove_all(folder, ec);
        if (!ec && !fs::exists(folder, ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Removes the folders nobody is using.
void clearIdle(const fs::path& stagingRoot)
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(stagingRoot, ec)) {
        if (!isFolder(entry)) {
            std::error_code ignore;
            fs::remove(entry.path(), ignore);
        } else if (!isLive(entry.path())) {
            removeTree(entry.path());
        }
    }
}

// Removes old folders nobody is using.
void cleanOld(const fs::path& stagingRoot, std::chrono::hours age)
{
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (const auto& entry : fs::directory_iterator(stagingRoot, ec)) {
        if (!isFolder(entry) || isLive(entry.path())) continue;
        std::error_code timeError;
        const auto changed = fs::last_write_time(entry.path(), timeError);
        if (!timeError && now - changed > age) removeTree(entry.path());
    }
}

// Removes the kept folders of one source.
void removeKept(const fs::path& stagingRoot, const std::string& source)
{
    if (source.empty()) return;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(stagingRoot, ec)) {
        if (!isFolder(entry) || isLive(entry.path())) continue;
        std::string noted;
        {
            std::ifstream in(entry.path() / kSourceName, std::ios::binary);
            if (!in) continue;
            std::ostringstream text;
            text << in.rdbuf();
            noted = text.str();
        }
        while (!noted.empty() && (noted.back() == '\n' || noted.back() == '\r')) noted.pop_back();
        if (noted == source) removeTree(entry.path());
    }
}

// Notes the source of a kept folder.
void keepSource(const fs::path& folder, const std::string& source)
{
    std::ofstream out(folder / kSourceName, std::ios::binary | std::ios::trunc);
    out << source << "\n";
}

}
