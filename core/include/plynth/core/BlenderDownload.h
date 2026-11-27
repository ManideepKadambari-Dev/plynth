#pragma once

#include "Blender.h"
#include "Status.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace plynth {

// "Get Blender for me" (DESIGN.md section 10 "What a user needs", owner 2026-11-27, issue 37). Only after the
// user says yes, in the setup check, in the installer's tick box or with "plynth blender get --yes", Plynth
// downloads the official portable Blender (the Windows zip) from blender.org into its own folder, checks it
// against the SHA-256 checksum blender.org publishes beside it, unpacks it and uses it. Blender is never inside
// the Plynth installer, and an installed Blender is still used when one is found (preferredBlender ranks a
// downloaded copy like any other).
//
// Where it goes: downloadedBlenderRoot(), which is <install folder>/portable-blender for an installed copy (not "Blender": Windows names are not case sensitive and the scripts folder is "blender")
// (Update.h currentInstallation) and %LOCALAPPDATA%/Plynth/Blender otherwise. Each version in its own folder,
// <root>/blender-5.2.3-windows-x64/blender.exe, with a file .plynth-download beside it naming the zip and its
// checksum. findBlenderCandidates lists those with BlenderSource::Downloaded. The uninstaller removes the folder
// of an installed copy.
//
// Requests go only to download.blender.org over HTTPS, the redirects too (the updater's HTTP code with its own
// allow list). The series is the tested one (kTestedBlenderVersion, "5.2"): the release folder
//   https://download.blender.org/release/Blender5.2/
// is read for the highest blender-5.2.<n>-windows-x64.zip and its blender-5.2.<n>.sha256 list. Unpacking runs
// Windows' own tar.exe (%SystemRoot%/System32/tar.exe) on the checked zip, after listZipEntries (ZipListing.h)
// refused any unsafe name, into an empty staging folder that is renamed into place at the end, so a failed or
// stopped download leaves nothing half done.

inline constexpr std::string_view kBlenderDownloadHost = "download.blender.org";
inline constexpr std::string_view kBlenderReleaseFolder = "https://download.blender.org/release/";
inline constexpr std::string_view kDownloadedBlenderMarker = ".plynth-download";
inline constexpr std::uint64_t kMaxBlenderZipBytes = 2ull * 1024 * 1024 * 1024;

// The folder downloaded copies go into.
std::filesystem::path downloadedBlenderRoot();

// A portable Blender blender.org offers.
struct BlenderOffer {
    BlenderVersion version;
    std::string zipUrl;       // https://download.blender.org/release/Blender5.2/blender-5.2.3-windows-x64.zip
    std::string zipName;      // blender-5.2.3-windows-x64.zip
    std::string sha256;       // 64 lower case hex digits, from the .sha256 list
    std::uint64_t size = 0;   // from the listing when it gives one, else 0

    bool operator==(const BlenderOffer&) const = default;
};

struct BlenderOfferCheck {
    Status status; // NetworkFailed when the folder or the checksum list could not be read or has no Windows zip
    std::optional<BlenderOffer> offer;
};

// Reads the release folder of the tested series and its checksum list. Makes two requests. Blocks.
BlenderOfferCheck findBlenderOffer(std::stop_token stop = {});

// The names inside a release folder page and a checksum list, for the tests. pickBlenderZip gives the highest
// blender-<series>.<n>-windows-x64.zip linked in the page, empty when there is none. sha256For gives the
// checksum the list has for a file name, empty when it has none or it is not 64 hex digits.
std::optional<std::string> pickBlenderZip(std::string_view page, std::string_view series);
std::optional<std::string> sha256For(std::string_view list, std::string_view fileName);

enum class BlenderDownloadStage {
    Downloading, // "downloading": done and total are bytes
    Checking,    // "checking": the checksum
    Unpacking,   // "unpacking"
    Inspecting,  // "inspecting": inspectBlender on the new copy
};

std::string_view blenderDownloadStageName(BlenderDownloadStage stage);

struct BlenderDownloadProgress {
    BlenderDownloadStage stage = BlenderDownloadStage::Downloading;
    long long done = 0;
    long long total = 0; // 0 when not known
};

struct BlenderDownload {
    // Ok with install set. NetworkFailed, ChecksumFailed (the zip is deleted), BadFile (an unsafe name in the
    // zip, or no blender.exe in it), CannotWrite (disk full or folder refused), Cancelled, BlenderFailed when the
    // new copy does not run. AlreadyExists is never given: a copy of that version already there is inspected and
    // returned as it is.
    Status status;
    std::optional<BlenderInstall> install;
};

// Downloads, checks, unpacks and inspects an offer into downloadedBlenderRoot(). Never changes the settings; the
// front end saves the new path as the chosen Blender when the user agreed to use it. Blocks.
BlenderDownload downloadBlender(const BlenderOffer& offer,
                                const std::function<void(const BlenderDownloadProgress&)>& onProgress = {},
                                std::stop_token stop = {});

// The SHA-256 of a file as 64 lower case hex digits, through Windows' own cryptography. CannotRead.
struct FileHash {
    Status status;
    std::string sha256;
};
FileHash sha256OfFile(const std::filesystem::path& file, std::stop_token stop = {});

}
