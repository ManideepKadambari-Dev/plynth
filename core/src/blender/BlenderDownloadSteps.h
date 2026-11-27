#pragma once

#include "plynth/core/BlenderDownload.h"
#include "plynth/core/ZipListing.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

// The steps of the Blender download that the tests reach without the network.
namespace plynth::blender_download {

using ProgressFn = std::function<void(const BlenderDownloadProgress&)>;

// The tested series as "5.2".
std::string testedSeries();

// The release folder of a series: https://download.blender.org/release/Blender5.2/
std::string releaseFolderUrl(std::string_view series);

// The version in a name of the form blender-<series>.<n>-windows-x64.zip. Empty for any other name.
std::optional<BlenderVersion> zipVersion(std::string_view zipName, std::string_view series);

// The checksum list beside a zip: blender-5.2.3-windows-x64.zip gives blender-5.2.3.sha256.
std::string checksumListName(std::string_view zipName);

// The folder a zip is unpacked as: blender-5.2.3-windows-x64.zip gives blender-5.2.3-windows-x64.
std::string copyFolderName(std::string_view zipName);

// The size in bytes a release folder page shows on the line of a link to a file. 0 when the line has no plain
// number of bytes.
std::uint64_t listedSize(std::string_view page, std::string_view fileName);

// InvalidValue unless the offer names a Windows zip of its own version, in its release folder on
// download.blender.org, with a checksum of 64 lower case hex digits.
Status checkOffer(const BlenderOffer& offer);

// BadFile when the zip has no entries, an unsafe, encrypted or control character name, or no blender.exe at
// its top or in one folder at its top. exeFolder gets that folder, empty for the top.
Status checkBlenderZipEntries(const ZipListing& listing, std::string& exeFolder);

// Checks the zip at staging/offer.zipName against offer.sha256, lists it, unpacks it with Windows' tar.exe into
// staging, marks the copy, moves it to root/copyFolderName and inspects it there. staging is removed on every
// way out, and the moved copy too when it does not run or a stop came, so only a working copy is ever kept.
BlenderDownload installBlenderZip(const std::filesystem::path& root, const std::filesystem::path& staging,
                                  const BlenderOffer& offer, const ProgressFn& onProgress, std::stop_token stop);

}
