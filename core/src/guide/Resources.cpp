#include "plynth/core/Resources.h"

#include "plynth/core/Library.h"
#include "plynth/core/ProjectFiles.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Says whether a folder holds the marker file of its kind.
bool holdsMarker(ResourceKind kind, const fs::path& folder)
{
    if (folder.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(folder / pathFromUtf8(resourceMarker(kind)), ec);
}

// The value of an environment variable as a path. Empty when it is not set.
fs::path variablePath(std::string_view name)
{
    const std::wstring wide(name.begin(), name.end());
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(wide.c_str(), value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) return {};
    value.resize(length);
    return fs::path(value);
}

// The folder of the running program.
fs::path runningProgramFolder()
{
    std::wstring program(32768, L'\0');
    const DWORD got = GetModuleFileNameW(nullptr, program.data(), static_cast<DWORD>(program.size()));
    if (got == 0 || got >= program.size()) return {};
    program.resize(got);
    return fs::path(program).parent_path();
}

}

// The folder name of each kind.
std::string_view resourceFolderName(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Docs: return "docs";
    case ResourceKind::Samples: return "samples";
    case ResourceKind::Licenses: return "licenses";
    }
    return {};
}

// The marker file of each kind.
std::string_view resourceMarker(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Docs: return "GETTING_STARTED.md";
    case ResourceKind::Samples: return "samples.json";
    case ResourceKind::Licenses: return "THIRD_PARTY_NOTICES.md";
    }
    return {};
}

// The variable of each kind.
std::string_view resourceVariable(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Docs: return kDocsVariable;
    case ResourceKind::Samples: return kSamplesVariable;
    case ResourceKind::Licenses: return kLicensesVariable;
    }
    return {};
}

// Tries the three places in order and keeps the first that holds the marker.
fs::path findResourceFolder(ResourceKind kind, const fs::path& fromVariable, const fs::path& programFolder,
                            const fs::path& sourceTree)
{
    const fs::path folderName = pathFromUtf8(resourceFolderName(kind));
    if (holdsMarker(kind, fromVariable)) return fromVariable;
    if (!programFolder.empty() && holdsMarker(kind, programFolder / folderName)) return programFolder / folderName;
    if (!sourceTree.empty() && holdsMarker(kind, sourceTree / folderName)) return sourceTree / folderName;
    return {};
}

// Finds a folder with the real variable, the program's folder and, in developer builds, the source tree.
fs::path resourceFolder(ResourceKind kind)
{
    fs::path sourceTree;
#if defined(PLYNTH_DEV_SAMPLES) && PLYNTH_DEV_SAMPLES
    // A developer build finds the source tree through the path of this file. Release builds do not carry it.
    sourceTree = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
#endif
    return findResourceFolder(kind, variablePath(resourceVariable(kind)), runningProgramFolder(), sourceTree);
}

}
