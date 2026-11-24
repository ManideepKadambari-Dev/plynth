#pragma once

#include <filesystem>
#include <string_view>

namespace plynth {

// The folders the program ships beside itself: help pages, samples and license texts.
enum class ResourceKind { Docs, Samples, Licenses };

inline constexpr std::string_view kDocsVariable = "PLYNTH_DOCS";
inline constexpr std::string_view kLicensesVariable = "PLYNTH_LICENSES";
// Samples uses kSamplesVariable of Library.h.

// The folder name of a kind: "docs", "samples" or "licenses".
std::string_view resourceFolderName(ResourceKind kind);

// The file that must be inside a folder of that kind: "GETTING_STARTED.md", "samples.json" or
// "THIRD_PARTY_NOTICES.md".
std::string_view resourceMarker(ResourceKind kind);

// The name of the variable that can point at a folder of that kind.
std::string_view resourceVariable(ResourceKind kind);

// Picks the first folder that holds the marker: fromVariable itself, then programFolder/<name>, then
// sourceTree/<name> when sourceTree is not empty. Never looks in parent folders. Empty when none counts.
std::filesystem::path findResourceFolder(ResourceKind kind, const std::filesystem::path& fromVariable,
                                         const std::filesystem::path& programFolder,
                                         const std::filesystem::path& sourceTree);

// The same with the real variable, the running program's folder, and the source tree only in developer
// builds. Empty when not found.
std::filesystem::path resourceFolder(ResourceKind kind);

}
