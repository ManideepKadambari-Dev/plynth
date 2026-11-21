#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace plynth::staging {

// A running import or refresh marks its work folder with a small file holding the process that owns it, so
// other code can tell a folder in use from one left behind (a kept failure or a crash).
void markLive(const std::filesystem::path& folder);
void unmarkLive(const std::filesystem::path& folder);

// True when the folder is marked by a process that is still running.
bool isLive(const std::filesystem::path& folder);

// Removes a folder tree, trying again for a moment when another program still holds a file.
void removeTree(const std::filesystem::path& folder);

// Removes every work folder that is not live. Used when a library is made or checked.
void clearIdle(const std::filesystem::path& stagingRoot);

// Removes work folders that are not live and have not changed for longer than the given time.
void cleanOld(const std::filesystem::path& stagingRoot, std::chrono::hours age);

// Removes the kept work folders of failed imports of one source (the text written by keepSource).
void removeKept(const std::filesystem::path& stagingRoot, const std::string& source);

// Notes which source a kept work folder was for.
void keepSource(const std::filesystem::path& folder, const std::string& source);

}
