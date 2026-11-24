#pragma once

#include "plynth/core/EditSession.h"
#include "plynth/core/Status.h"
#include "plynth/core/catalog/AssetCatalog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

inline constexpr std::string_view kUsedItemsFileName = "used-items.json";
inline constexpr std::string_view kUsedItemsFormat = "plynth-used-items";
inline constexpr std::string_view kLicenseReportFormat = "plynth-license-report";
inline constexpr int kLicenseReportVersion = 1;
inline constexpr int kUsedItemsVersion = 1;

enum class LicensedKind { Asset, Material };

// One asset or material a build used, with where it came from and its license.
struct LicensedItem {
    LicensedKind kind = LicensedKind::Asset;
    std::string id;
    std::string name;
    std::string sourceKind;
    std::string sourceReference;
    SessionTime importedAt{};
    std::string author; // empty when unknown
    LicenseEntry license;
    bool inLibrary = true;

    bool operator==(const LicensedItem&) const = default;
};

// Codes: unknown_license, missing_item, missing_author (for cc_by_4), personal_use.
struct LicenseWarning {
    LicensedKind kind = LicensedKind::Asset;
    std::string code;
    std::string id;
    std::string message;

    bool operator==(const LicenseWarning&) const = default;
};

struct LicenseReport {
    std::string project;
    std::string planFingerprint;
    std::vector<LicensedItem> items;
    std::vector<LicenseWarning> warnings;

    bool operator==(const LicenseReport&) const = default;
};

// The asset and material ids of the last build, saved as build/used-items.json.
struct UsedItems {
    std::string planFingerprint;
    std::vector<std::string> assets;
    std::vector<std::string> materials;

    bool operator==(const UsedItems&) const = default;
};

// Sorts by kind and id, keeps each item once, adds the warnings and cuts folders off source references.
// A web address is kept as it is.
LicenseReport makeLicenseReport(std::string project, std::string fingerprint, std::vector<LicensedItem> items);

// The report as JSON in the plynth-license-report format.
Json licenseReportToJson(const LicenseReport& report);

// The report as plain text. The last line says it is not legal advice.
std::string licenseReportText(const LicenseReport& report);

struct UsedItemsRead {
    Status status; // NotFound, CannotRead, BadFile, UnsupportedVersion
    UsedItems items;
};

// Reads build/used-items.json.
UsedItemsRead readUsedItems(const std::filesystem::path& file);

// Writes build/used-items.json through a temporary file. Ids are sorted and kept once.
Status writeUsedItems(const std::filesystem::path& file, const UsedItems& items);

}
