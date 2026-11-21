#include "CatalogCommon.h"

#include "Common.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/RoomTypes.h>

#include <set>

namespace plynth::cli {

namespace {

// The library folder named by the settings, when there is a working one.
std::optional<LibraryPaths> settingsLibrary() {
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok() || !loaded.settings.library || !isLibraryFolder(*loaded.settings.library)) {
        return std::nullopt;
    }
    return libraryPaths(*loaded.settings.library);
}

// Registers a library's shapes once per folder, so a second command in one process does not refuse them again.
void registerShapes(const LibraryPaths& paths) {
    static std::set<std::filesystem::path> done;
    if (done.insert(paths.shapes).second) {
        registerShapeFolder(paths.shapes);
    }
}

}

void addLibraryOption(CLI::App& sub, std::string& value) {
    sub.add_option("--library", value, "Library folder. Default: the one in the settings.")->type_name("folder");
}

std::filesystem::path userPath(const RunContext& context, const std::string& text) {
    return (context.currentFolder / pathFromUtf8(text)).lexically_normal();
}

bool openCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                 OpenedCatalog& opened, Answer& failed) {
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        failed = failure(command, loaded.status);
        return false;
    }
    opened.settings = loaded.settings;
    std::optional<std::filesystem::path> option;
    if (!libraryOption.empty()) {
        option = userPath(context, libraryOption);
    }
    const LibraryChoice choice = chooseLibrary(option, opened.settings);
    if (!choice.status.ok()) {
        failed = failure(command, choice.status);
        return false;
    }
    opened.paths = choice.paths;
    Status status;
    opened.catalog = AssetCatalog::open(opened.paths, status);
    if (!status.ok()) {
        failed = failure(command, status);
        return false;
    }
    registerShapes(opened.paths);
    useRoomTypes(opened.catalog->roomTypeList());
    return true;
}

void registerLibraryShapes() {
    if (const auto paths = settingsLibrary()) {
        registerShapes(*paths);
    }
}

void useSettingsRoomTypes() {
    if (const auto paths = settingsLibrary()) {
        if (AssetCatalog::useLibraryRoomTypes(*paths).ok()) {
            return;
        }
    }
    useRoomTypes({});
}

Json assetEntry(const AssetRecord& record) {
    Json source = Json::object();
    source["kind"] = record.sourceKind;
    source["reference"] = record.sourceReference;
    source["imported_at"] = formatSessionTime(record.importedAt);
    Json size = Json::object();
    size["width"] = record.size.width;
    size["depth"] = record.size.depth;
    size["height"] = record.size.height;
    Json thumbnail = Json::object();
    thumbnail["state"] = std::string(thumbnailStateName(record.thumbnail));
    if (record.thumbnail == ThumbnailState::Ok) {
        thumbnail["file"] = pathToUtf8(record.thumbFile);
    } else {
        thumbnail["file"] = nullptr;
    }
    if (record.thumbnailError.empty()) {
        thumbnail["error"] = nullptr;
    } else {
        thumbnail["error"] = record.thumbnailError;
    }
    Json entry = Json::object();
    entry["id"] = record.id;
    entry["name"] = record.name;
    entry["categories"] = record.categories;
    entry["tags"] = record.tags;
    entry["license"] = record.license;
    entry["source"] = std::move(source);
    entry["size"] = std::move(size);
    entry["triangles"] = record.triangles;
    entry["state"] = std::string(assetStateName(record.state));
    if (record.problem.empty()) {
        entry["problem"] = nullptr;
    } else {
        entry["problem"] = record.problem;
    }
    entry["thumbnail"] = std::move(thumbnail);
    entry["folder"] = pathToUtf8(record.folder);
    return entry;
}

Json roomTypeEntry(const RoomTypeEntry& entry) {
    Json json = Json::object();
    json["id"] = entry.id;
    json["name"] = entry.name;
    json["built_in"] = entry.builtIn;
    json["categories"] = entry.categories;
    return json;
}

Json categoryEntry(const CategoryEntry& entry) {
    Json json = Json::object();
    json["id"] = entry.id;
    json["name"] = entry.name;
    json["built_in"] = entry.builtIn;
    json["assets"] = entry.assetCount;
    return json;
}

Json licenseEntry(const LicenseEntry& entry) {
    Json json = Json::object();
    json["id"] = entry.id;
    json["name"] = entry.name;
    json["url"] = entry.url;
    json["redistributable"] = entry.redistributable;
    return json;
}

Json rescanEntry(const RescanReport& report, const std::vector<std::string>& refreshed,
                 const Json& refreshFailed) {
    Json broken = Json::array();
    for (const auto& [folder, status] : report.broken) {
        Json item = Json::object();
        item["folder"] = folder;
        item["code"] = std::string(errorCodeName(status.code));
        item["message"] = status.message;
        broken.push_back(std::move(item));
    }
    Json entry = Json::object();
    entry["added"] = report.added;
    entry["updated"] = report.updated;
    entry["removed"] = report.removed;
    entry["unchanged"] = report.unchanged;
    entry["refreshed"] = refreshed;
    entry["refresh_failed"] = refreshFailed.is_array() ? refreshFailed : Json::array();
    entry["broken"] = std::move(broken);
    entry["inbox_waiting"] = report.inboxWaiting;
    return entry;
}

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == ',') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!text.empty()) {
        parts.push_back(current);
    }
    for (std::string& part : parts) {
        const auto first = part.find_first_not_of(' ');
        const auto last = part.find_last_not_of(' ');
        part = first == std::string::npos ? std::string() : part.substr(first, last - first + 1);
    }
    return parts;
}

}
