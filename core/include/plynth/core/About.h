#pragma once

#include "plynth/core/Status.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

inline constexpr std::string_view kSourceLicense = "GPL-3.0-or-later";
inline constexpr std::string_view kBinaryLicense = "GPL-3.0-only";
inline constexpr std::string_view kSampleLicense = "CC0-1.0";
inline constexpr std::string_view kCopyrightLine = "Copyright 2026 the Plynth authors (see AUTHORS)";
inline constexpr std::string_view kNoticesFileName = "THIRD_PARTY_NOTICES.md";
inline constexpr std::string_view kLicenseTextsFolder = "texts";

struct AboutInfo {
    Status status; // NotFound when the licenses folder or its notices file is missing
    std::string version;
    std::string copyright;
    std::string sourceLicense;
    std::string binaryLicense;
    std::string sampleLicense;
    std::string notices; // the Markdown of THIRD_PARTY_NOTICES.md, empty when missing
    std::filesystem::path noticesFile;
    std::vector<std::filesystem::path> licenseFiles; // files of licenses/texts, sorted by name
};

// The version and licenses, also when the licenses folder is missing. status says what is missing.
AboutInfo aboutInfo(const std::filesystem::path& licensesFolder);

}
