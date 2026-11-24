#include "CatalogCommon.h"

#include "Common.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/RoomTypes.h>
#include <plynth/core/TrimProfiles.h>
#include <plynth/core/catalog/AssetInfo.h>

#include <algorithm>
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

// Refuses an empty Blender path, so an option that was given never falls back to another Blender.
CLI::Validator notEmptyBlenderPath() {
    return CLI::Validator(
        [](std::string& value) {
            return value.empty() ? std::string("The Blender path is empty. Give the path of blender.exe or leave the option out.")
                                 : std::string();
        },
        "", "NotEmpty");
}

// Registers a library's opening shapes and trim profiles once per folder, so a second command in one process
// does not refuse them again.
void registerShapes(const LibraryPaths& paths) {
    static std::set<std::filesystem::path> done;
    if (done.insert(paths.shapes).second) {
        registerShapeFolder(paths.shapes);
        registerTrimProfileFolder(paths.shapes);
    }
}

}

void addLibraryOption(CLI::App& sub, std::string& value) {
    sub.add_option("--library", value, "Library folder. Default: the one in the settings.")->type_name("folder");
}

bool checkIdArgument(const std::string& command, const std::string& id, Answer& failed) {
    const Status status = checkCatalogId(id);
    if (!status.ok()) {
        failed = failure(command, status);
        return false;
    }
    return true;
}

void addBlenderOption(CLI::App& sub, std::string& value) {
    sub.add_option("--blender", value, "The blender.exe to use. Default: the setup check's choice.")
        ->type_name("file")
        ->check(notEmptyBlenderPath());
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

bool openMaterialCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                         OpenedMaterialCatalog& opened, Answer& failed) {
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
    opened.catalog = MaterialCatalog::open(opened.paths, status);
    if (!status.ok()) {
        failed = failure(command, status);
        return false;
    }
    return true;
}

bool openMaterialCatalogIfAny(const std::string& command, const RunContext& context,
                              const std::string& libraryOption, OpenedMaterialCatalog& opened, bool& found,
                              Answer& failed) {
    found = false;
    if (libraryOption.empty()) {
        const SettingsLoad loaded = loadSettings();
        if (!loaded.status.ok()) {
            failed = failure(command, loaded.status);
            return false;
        }
        const LibraryChoice choice = chooseLibrary(std::nullopt, loaded.settings);
        if (choice.status.code == ErrorCode::NoLibrary) {
            return true;
        }
    }
    if (!openMaterialCatalog(command, context, libraryOption, opened, failed)) {
        return false;
    }
    found = true;
    return true;
}

bool makeImportContext(const std::string& command, const RunContext& context, const OpenedCatalog& opened,
                       const std::string& blenderOption, ImportContext& importContext, Answer& failed) {
    std::optional<std::filesystem::path> option;
    if (!blenderOption.empty()) {
        option = userPath(context, blenderOption);
    }
    const BlenderChoice choice = chooseBlender(option, opened.settings);
    if (!choice.status.ok()) {
        failed = failure(command, choice.status);
        return false;
    }
    importContext.library = opened.paths;
    importContext.blender = choice.install;
    importContext.settings = opened.settings;
    return true;
}

Json rescanAndRefresh(const std::string& command, const RunContext& context, OpenedCatalog& opened,
                      const std::string& blenderOption, bool noBlender) {
    const RescanReport report = opened.catalog->rescan();
    std::vector<std::string> refreshed;
    Json refreshFailed = Json::array();
    if (!noBlender && !report.needsRefresh.empty()) {
        ImportContext importContext;
        Answer failed;
        const bool haveBlender = makeImportContext(command, context, opened, blenderOption, importContext, failed);
        for (const std::string& id : report.needsRefresh) {
            Status status = haveBlender ? Status{} : fail(ErrorCode::BlenderNotFound, failed.message);
            if (haveBlender) {
                status = refreshAsset(*opened.catalog, importContext, id).status;
            }
            if (status.ok()) {
                refreshed.push_back(id);
            } else {
                Json item = Json::object();
                item["id"] = id;
                item["code"] = std::string(errorCodeName(status.code));
                item["message"] = status.message;
                refreshFailed.push_back(std::move(item));
            }
        }
    }
    return rescanEntry(report, refreshed, refreshFailed);
}

void registerShapesOf(const LibraryPaths& paths) {
    registerShapes(paths);
}

bool registerShapesFor(const std::string& command, const RunContext& context, const std::string& libraryOption,
                       Answer& failed) {
    if (libraryOption.empty()) {
        registerLibraryShapes();
        return true;
    }
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        failed = failure(command, loaded.status);
        return false;
    }
    const LibraryChoice choice = chooseLibrary(userPath(context, libraryOption), loaded.settings);
    if (!choice.status.ok()) {
        failed = failure(command, choice.status);
        return false;
    }
    registerShapes(choice.paths);
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
    entry["measured"] = isMeasured(record.size);
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

Json materialEntry(const MaterialRecord& record) {
    Json source = Json::object();
    source["kind"] = record.sourceKind;
    source["reference"] = record.sourceReference;
    source["imported_at"] = formatSessionTime(record.importedAt);
    Json tile = Json::object();
    tile["width"] = record.tileSize.width;
    tile["height"] = record.tileSize.height;
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
    entry["groups"] = record.groups;
    entry["tags"] = record.tags;
    entry["license"] = record.license;
    entry["source"] = std::move(source);
    entry["tile_size"] = std::move(tile);
    entry["state"] = std::string(materialStateName(record.state));
    if (record.problem.empty()) {
        entry["problem"] = nullptr;
    } else {
        entry["problem"] = record.problem;
    }
    entry["thumbnail"] = std::move(thumbnail);
    entry["folder"] = pathToUtf8(record.folder);
    return entry;
}

Json materialGroupEntry(const MaterialGroupEntry& entry) {
    Json json = Json::object();
    json["id"] = entry.id;
    json["name"] = entry.name;
    json["built_in"] = entry.builtIn;
    json["materials"] = entry.materialCount;
    return json;
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
    Json waiting = Json::array();
    for (const std::string& id : report.needsRefresh) {
        if (std::ranges::find(refreshed, id) == refreshed.end()) {
            waiting.push_back(id);
        }
    }
    entry["needs_refresh"] = std::move(waiting);
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
