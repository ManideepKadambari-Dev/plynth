#include "plynth/core/LicenseReport.h"

#include "FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The word for a kind in the report files.
const char* kindName(LicensedKind kind)
{
    return kind == LicensedKind::Asset ? "asset" : "material";
}

// Says whether a reference is a web address.
bool isWebAddress(std::string_view text)
{
    return text.starts_with("https://") || text.starts_with("http://");
}

// The file name of a reference. A web address stays as it is.
std::string withoutFolders(const std::string& reference)
{
    if (isWebAddress(reference)) return reference;
    const std::size_t slash = reference.find_last_of("/\\");
    std::string name = slash == std::string::npos ? reference : reference.substr(slash + 1);
    if (const std::size_t colon = name.find(':'); colon != std::string::npos && colon == 1) name = name.substr(2);
    return name;
}

// Says whether a time was set.
bool hasTime(SessionTime time)
{
    return time.time_since_epoch().count() != 0;
}

// The warnings of one item.
void addWarnings(const LicensedItem& item, std::vector<LicenseWarning>& out)
{
    const std::string what = std::string(kindName(item.kind)) + " \"" + item.id + "\"";
    if (!item.inLibrary) {
        out.push_back({item.kind, "missing_item", item.id, "The " + what + " is not in the library, so its license is unknown."});
        return;
    }
    if (item.license.id.empty() || item.license.id == "unknown") {
        out.push_back({item.kind, "unknown_license", item.id, "The " + what + " has no known license."});
    }
    if (item.license.id == "cc_by_4" && item.author.empty()) {
        out.push_back({item.kind, "missing_author", item.id, "The " + what + " needs credit to its author, but no author is set."});
    }
    if (item.license.id == "personal_use") {
        out.push_back({item.kind, "personal_use", item.id, "The " + what + " is for personal use only."});
    }
}

// Ids sorted and kept once.
std::vector<std::string> sortedOnce(std::vector<std::string> ids)
{
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// A list of ids from the used items file.
std::vector<std::string> idListAt(const Json& json, const char* key)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array()) throw std::runtime_error(std::string("The value \"") + key + "\" must be a list.");
    std::vector<std::string> ids;
    for (const Json& value : *it) {
        if (!value.is_string() || value.get<std::string>().empty()) {
            throw std::runtime_error(std::string("Each value of \"") + key + "\" must be an id.");
        }
        ids.push_back(value.get<std::string>());
    }
    return ids;
}

}

// Sorts, removes repeats, cuts folders off references and adds the warnings.
LicenseReport makeLicenseReport(std::string project, std::string fingerprint, std::vector<LicensedItem> items)
{
    LicenseReport report;
    report.project = std::move(project);
    report.planFingerprint = std::move(fingerprint);
    std::stable_sort(items.begin(), items.end(), [](const LicensedItem& a, const LicensedItem& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.id < b.id;
    });
    std::set<std::pair<int, std::string>> seen;
    for (LicensedItem& item : items) {
        if (!seen.insert({static_cast<int>(item.kind), item.id}).second) continue;
        item.sourceReference = withoutFolders(item.sourceReference);
        addWarnings(item, report.warnings);
        report.items.push_back(std::move(item));
    }
    return report;
}

// Writes the report in the plynth-license-report format.
Json licenseReportToJson(const LicenseReport& report)
{
    Json json = Json::object();
    json["format"] = std::string(kLicenseReportFormat);
    json["version"] = kLicenseReportVersion;
    json["project"] = report.project;
    json["plan_fingerprint"] = report.planFingerprint;
    Json items = Json::array();
    for (const LicensedItem& item : report.items) {
        Json entry = Json::object();
        entry["kind"] = kindName(item.kind);
        entry["id"] = item.id;
        entry["name"] = item.name;
        entry["in_library"] = item.inLibrary;
        entry["author"] = item.author.empty() ? Json(nullptr) : Json(item.author);
        Json source = Json::object();
        source["kind"] = item.sourceKind;
        source["reference"] = item.sourceReference;
        source["imported_at"] = hasTime(item.importedAt) ? Json(formatSessionTime(item.importedAt)) : Json(nullptr);
        entry["source"] = std::move(source);
        Json license = Json::object();
        license["id"] = item.license.id.empty() ? std::string("unknown") : item.license.id;
        license["name"] = item.license.name;
        license["url"] = item.license.url.empty() ? Json(nullptr) : Json(item.license.url);
        license["redistributable"] = item.license.redistributable;
        entry["license"] = std::move(license);
        items.push_back(std::move(entry));
    }
    json["items"] = std::move(items);
    Json warnings = Json::array();
    for (const LicenseWarning& w : report.warnings) {
        warnings.push_back({{"code", w.code}, {"kind", kindName(w.kind)}, {"id", w.id}, {"message", w.message}});
    }
    json["warnings"] = std::move(warnings);
    return json;
}

// Writes the report as plain text for people.
std::string licenseReportText(const LicenseReport& report)
{
    std::ostringstream out;
    out << "License report for " << report.project << "\n";
    out << "Plan fingerprint: " << report.planFingerprint << "\n";
    for (const LicensedKind kind : {LicensedKind::Asset, LicensedKind::Material}) {
        out << "\n" << (kind == LicensedKind::Asset ? "Assets" : "Materials") << "\n";
        bool any = false;
        for (const LicensedItem& item : report.items) {
            if (item.kind != kind) continue;
            any = true;
            out << "  " << (item.name.empty() ? item.id : item.name) << " (" << item.id << ")\n";
            const std::string licenseId = item.license.id.empty() ? std::string("unknown") : item.license.id;
            out << "    License: " << (item.license.name.empty() ? licenseId : item.license.name) << " [" << licenseId << "]";
            if (!item.license.url.empty()) out << " " << item.license.url;
            out << "\n";
            out << "    Author: " << (item.author.empty() ? std::string("not given") : item.author) << "\n";
            if (item.inLibrary) {
                out << "    Source: " << item.sourceKind;
                if (!item.sourceReference.empty()) out << ", " << item.sourceReference;
                if (hasTime(item.importedAt)) out << ", imported " << formatSessionTime(item.importedAt);
                out << "\n";
            } else {
                out << "    Not in the library.\n";
            }
        }
        if (!any) out << "  None.\n";
    }
    if (!report.warnings.empty()) {
        out << "\nWarnings\n";
        for (const LicenseWarning& w : report.warnings) out << "  " << w.message << "\n";
    }
    out << "\nThis report lists what Plynth knows about each item. It is not legal advice.\n";
    return out.str();
}

// Reads build/used-items.json.
UsedItemsRead readUsedItems(const fs::path& file)
{
    UsedItemsRead out;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        out.status = fail(ErrorCode::NotFound, "Build the project first.");
        return out;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        out.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be opened.");
        return out;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const Json json = Json::parse(buffer.str(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        out.status = fail(ErrorCode::BadFile, "The file " + pathToUtf8(file) + " is not valid JSON.");
        return out;
    }
    try {
        const auto format = json.find("format");
        if (format == json.end() || !format->is_string() || format->get<std::string>() != kUsedItemsFormat) {
            out.status = fail(ErrorCode::BadFile, "The file is not a Plynth used items file.", "/format");
            return out;
        }
        const auto version = json.find("version");
        if (version == json.end() || !version->is_number_integer() || version->get<long long>() < 1) {
            out.status = fail(ErrorCode::BadFile, "The value \"version\" must be a whole number.", "/version");
            return out;
        }
        if (version->get<long long>() > kUsedItemsVersion) {
            out.status = fail(ErrorCode::UnsupportedVersion, "The used items file was written by a newer Plynth.", "/version");
            return out;
        }
        const auto fingerprint = json.find("plan_fingerprint");
        if (fingerprint == json.end() || !fingerprint->is_string()) {
            out.status = fail(ErrorCode::BadFile, "The value \"plan_fingerprint\" must be text.", "/plan_fingerprint");
            return out;
        }
        out.items.planFingerprint = fingerprint->get<std::string>();
        out.items.assets = idListAt(json, "assets");
        out.items.materials = idListAt(json, "materials");
    } catch (const std::runtime_error& e) {
        out.status = fail(ErrorCode::BadFile, e.what());
        out.items = {};
    }
    return out;
}

// Writes build/used-items.json with sorted ids.
Status writeUsedItems(const fs::path& file, const UsedItems& items)
{
    Json json = Json::object();
    json["format"] = std::string(kUsedItemsFormat);
    json["version"] = kUsedItemsVersion;
    json["plan_fingerprint"] = items.planFingerprint;
    json["assets"] = sortedOnce(items.assets);
    json["materials"] = sortedOnce(items.materials);
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    return fileio::writeAtomic(file, json.dump(2) + "\n");
}

}
