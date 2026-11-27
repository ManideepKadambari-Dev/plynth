#pragma once

#include <plynth/core/catalog/StoreFetch.h>

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth::detail {

// What the choice of the fetch folder reads from Windows, so a test can give its own values.
struct FetchFolderInputs {
    std::filesystem::path variable;     // the folder kFetchFolderVariable names, empty when not set
    std::filesystem::path localAppData; // %LOCALAPPDATA%, empty when not set
    std::filesystem::path shortRoot;    // the folder that holds PlynthFetch: %SystemDrive%/, empty when unknown
    std::string userId;                 // the 8 hex digits that name this user's folder in PlynthFetch
    // The short 8.3 form of a folder that exists, or the same path when Windows has none.
    std::function<std::filesystem::path(const std::filesystem::path&)> shortPath;
};

// The values the choice reads from this process and this Windows user.
FetchFolderInputs systemFetchFolderInputs();

// The SID of the signed in Windows user, empty when it cannot be read.
std::vector<unsigned char> currentUserSid();

// The first 8 hex digits of the FNV hash of a user's SID text.
std::string fetchUserId(std::string_view sidText);

// Picks the fetch folder from the given values by the rules of chooseFetchFolder. Makes nothing.
FetchFolderChoice chooseFetchFolder(const FetchFolderInputs& inputs);

// The sentence for a fetch when no folder is short enough.
std::string noShortFetchFolderSentence();

// Makes the last resort fetch folder fresh, with access for this user only and the low integrity label.
// Removes one this user left from an earlier fetch. CannotWrite with noShortFetchFolderSentence when it
// cannot be made or something else is in its place.
Status makeShortFetchFolder(const std::filesystem::path& folder);

// Removes the last resort fetch folder with everything in it, and PlynthFetch above it when that is empty.
// Never follows a link. False when something stayed.
bool removeShortFetchFolder(const std::filesystem::path& folder);

// runImportFetch in a folder that was already chosen.
FetchRun runImportFetchIn(const FetchFolderChoice& choice, const ImportFetch& fetch, const std::filesystem::path& blender,
                          const BlenderRunOptions& base, const std::filesystem::path& workFolder,
                          const std::function<void(const BlenderProgress&)>& onProgress = {}, std::stop_token stop = {});

}
