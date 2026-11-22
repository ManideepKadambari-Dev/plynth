#include "plynth/core/catalog/MaterialInfo.h"

#include "../FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// A problem found while reading info.json, with the JSON pointer of the bad value.
struct ReadProblem {
    std::string where;
    std::string message;
};

// The object at a JSON pointer, with every key checked against the allowed ones and the required ones present.
const Json& objectAt(const Json& json, const std::string& where, std::initializer_list<const char*> required)
{
    if (!json.is_object()) throw ReadProblem{where, "Must be an object."};
    std::set<std::string> known;
    for (const char* key : required) known.insert(key);
    for (const auto& [key, value] : json.items()) {
        if (!known.count(key)) throw ReadProblem{where + "/" + key, "Unknown field \"" + key + "\"."};
    }
    for (const char* key : required) {
        if (!json.contains(key)) throw ReadProblem{where, std::string("Missing field \"") + key + "\"."};
    }
    return json;
}

// A text field.
std::string textAt(const Json& object, const char* key, const std::string& where)
{
    const Json& value = object.at(key);
    if (!value.is_string()) throw ReadProblem{where + "/" + key, "Must be text."};
    return value.get<std::string>();
}

// A whole number field of at least zero.
long long countAt(const Json& object, const char* key, const std::string& where)
{
    const Json& value = object.at(key);
    if (!value.is_number_integer() || value.get<long long>() < 0) {
        throw ReadProblem{where + "/" + key, "Must be a whole number of at least 0."};
    }
    return value.get<long long>();
}

// A number field.
double numberAt(const Json& object, const char* key, const std::string& where)
{
    const Json& value = object.at(key);
    if (!value.is_number()) throw ReadProblem{where + "/" + key, "Must be a number."};
    return value.get<double>();
}

// A time field.
SessionTime timeAt(const Json& object, const char* key, const std::string& where)
{
    const auto parsed = parseSessionTime(textAt(object, key, where));
    if (!parsed) throw ReadProblem{where + "/" + key, "Must be a UTC time such as 2026-11-21T10:00:00Z."};
    return *parsed;
}

// A list of text values at a top level key.
std::vector<std::string> textListAt(const Json& object, const char* key)
{
    const Json& list = object.at(key);
    if (!list.is_array()) throw ReadProblem{std::string("/") + key, "Must be a list."};
    std::vector<std::string> out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!list[i].is_string()) throw ReadProblem{std::string("/") + key + "/" + std::to_string(i), "Must be text."};
        out.push_back(list[i].get<std::string>());
    }
    return out;
}

// Turns a failed check into a read problem at a pointer.
void require(const Status& status, const std::string& where)
{
    if (!status.ok()) throw ReadProblem{where, status.message};
}

// Says whether a map file is textures/<channel>.<png, jpg or jpeg> and nothing else.
bool isMapFilePath(const std::string& file)
{
    const std::string prefix = std::string(kMaterialTexturesFolder) + "/";
    if (file.rfind(prefix, 0) != 0) return false;
    const std::string name = file.substr(prefix.size());
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string stem = name.substr(0, dot);
    const std::string ext = name.substr(dot + 1);
    if (ext != "png" && ext != "jpg" && ext != "jpeg") return false;
    return mapChannelFromName(stem).has_value();
}

// Reads one map of info.json.
MaterialMap mapAt(const Json& maps, MapChannel channel, bool required)
{
    const std::string name(mapChannelName(channel));
    const std::string where = "/maps/" + name;
    const Json& json = objectAt(maps.at(name), where, {"file", "from", "detail"});
    MaterialMap map;
    const auto origin = mapOriginFromName(textAt(json, "from", where));
    if (!origin) throw ReadProblem{where + "/from", "Must be file, value, baked or none."};
    map.origin = *origin;
    map.detail = textAt(json, "detail", where);
    const Json& file = json.at("file");
    if (map.origin == MapOrigin::None) {
        if (required) throw ReadProblem{where + "/from", "The " + name + " map must have a file."};
        if (!file.is_null()) throw ReadProblem{where + "/file", "A map with origin none has no file."};
        return map;
    }
    if (!file.is_string()) throw ReadProblem{where + "/file", "Must be the path of the map file."};
    map.file = file.get<std::string>();
    if (!isMapFilePath(map.file)) {
        throw ReadProblem{where + "/file", "Must be a file in textures/ such as textures/" + name + ".png."};
    }
    return map;
}

// Reads the checked fields of info.json into a MaterialInfo.
MaterialInfo parseInfo(const Json& json, std::string_view folderName)
{
    objectAt(json, "", {"format", "version", "id", "name", "groups", "tags", "license", "source", "tile_size", "maps",
                        "texture_size", "files", "thumbnail", "cleanup"});
    if (!json.at("format").is_string() || json.at("format").get<std::string>() != kMaterialInfoFormat) {
        throw ReadProblem{"/format", "This is not a Plynth material file."};
    }
    if (!json.at("version").is_number_integer()) throw ReadProblem{"/version", "The version must be a whole number."};
    const long long version = json.at("version").get<long long>();
    if (version > kMaterialInfoVersion) throw ReadProblem{"/version", "UNSUPPORTED"};
    if (version < kMaterialInfoVersion) throw ReadProblem{"/version", "The version is not valid."};

    MaterialInfo info;
    info.id = textAt(json, "id", "");
    require(checkCatalogId(info.id), "/id");
    if (!folderName.empty() && info.id != folderName) {
        throw ReadProblem{"/id", "The id \"" + info.id + "\" differs from the folder name \"" + std::string(folderName) + "\"."};
    }
    info.name = textAt(json, "name", "");
    require(checkCatalogName(info.name), "/name");

    info.groups = textListAt(json, "groups");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < info.groups.size(); ++i) {
        require(checkCatalogId(info.groups[i]), "/groups/" + std::to_string(i));
        if (!seen.insert(info.groups[i]).second) throw ReadProblem{"/groups/" + std::to_string(i), "The group is listed twice."};
    }
    info.tags = textListAt(json, "tags");
    seen.clear();
    for (std::size_t i = 0; i < info.tags.size(); ++i) {
        require(checkTag(info.tags[i]), "/tags/" + std::to_string(i));
        if (!seen.insert(info.tags[i]).second) throw ReadProblem{"/tags/" + std::to_string(i), "The tag is listed twice."};
    }
    info.license = textAt(json, "license", "");
    require(checkCatalogId(info.license), "/license");

    const Json& source = objectAt(json.at("source"), "/source", {"kind", "reference", "imported_at"});
    info.source.kind = textAt(source, "kind", "/source");
    require(checkCatalogId(info.source.kind), "/source/kind");
    info.source.reference = textAt(source, "reference", "/source");
    info.source.importedAt = timeAt(source, "imported_at", "/source");

    const Json& tile = objectAt(json.at("tile_size"), "/tile_size", {"width", "height"});
    info.tileSize.width = numberAt(tile, "width", "/tile_size");
    info.tileSize.height = numberAt(tile, "height", "/tile_size");
    require(checkTileSize(info.tileSize), "/tile_size");

    const Json& maps = objectAt(json.at("maps"), "/maps", {"base_color", "normal", "roughness", "metallic", "opacity", "occlusion"});
    for (const MapChannel channel : allMapChannels()) {
        const bool required = channel != MapChannel::Opacity && channel != MapChannel::Occlusion;
        materialMap(info.maps, channel) = mapAt(maps, channel, required);
    }

    const Json& texture = objectAt(json.at("texture_size"), "/texture_size", {"width", "height"});
    info.textureSize.width = static_cast<int>(countAt(texture, "width", "/texture_size"));
    info.textureSize.height = static_cast<int>(countAt(texture, "height", "/texture_size"));
    if (info.textureSize.width < 1 || info.textureSize.height < 1) {
        throw ReadProblem{"/texture_size", "The size must be at least 1 pixel each way."};
    }

    const Json& files = objectAt(json.at("files"), "/files", {"bytes", "modified"});
    info.files.bytes = countAt(files, "bytes", "/files");
    info.files.modified = timeAt(files, "modified", "/files");

    const Json& thumb = objectAt(json.at("thumbnail"), "/thumbnail", {"file", "error", "engine", "rendered_at"});
    const Json& file = thumb.at("file");
    if (!file.is_null()) {
        if (!file.is_string() || file.get<std::string>() != kMaterialThumbFileName) {
            throw ReadProblem{"/thumbnail/file", "Must be \"thumb.png\" or null."};
        }
        info.thumbnail.rendered = true;
    }
    const Json& error = thumb.at("error");
    if (!error.is_null()) {
        if (!error.is_string()) throw ReadProblem{"/thumbnail/error", "Must be text or null."};
        info.thumbnail.error = error.get<std::string>();
    }
    const Json& engine = thumb.at("engine");
    if (!engine.is_null()) {
        if (!engine.is_string() || engine.get<std::string>() != "eevee") {
            throw ReadProblem{"/thumbnail/engine", "Must be \"eevee\" or null."};
        }
        info.thumbnail.engine = engine.get<std::string>();
    }
    if (!thumb.at("rendered_at").is_null()) info.thumbnail.renderedAt = timeAt(thumb, "rendered_at", "/thumbnail");

    const Json& cleanup = objectAt(json.at("cleanup"), "/cleanup", {"blender_version", "warnings"});
    info.blenderVersion = textAt(cleanup, "blender_version", "/cleanup");
    const Json& warnings = cleanup.at("warnings");
    if (!warnings.is_array()) throw ReadProblem{"/cleanup/warnings", "Must be a list."};
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        const std::string where = "/cleanup/warnings/" + std::to_string(i);
        const Json& w = objectAt(warnings[i], where, {"code", "message"});
        info.warnings.push_back({textAt(w, "code", where), textAt(w, "message", where)});
    }
    return info;
}

// The write time of a file in whole seconds, or the epoch when it cannot be read.
SessionTime writeTimeOf(const fs::path& file)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) return {};
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::time_point_cast<std::chrono::seconds>(system);
}

// One map as it is written to info.json.
Json mapToJson(const MaterialMap& map)
{
    return Json{{"file", map.file.empty() ? Json(nullptr) : Json(map.file)},
                {"from", std::string(mapOriginName(map.origin))},
                {"detail", map.detail}};
}

}

// Every channel in file order.
std::span<const MapChannel> allMapChannels()
{
    static constexpr std::array<MapChannel, 6> all = {MapChannel::BaseColor, MapChannel::Normal,  MapChannel::Roughness,
                                                      MapChannel::Metallic,  MapChannel::Opacity, MapChannel::Occlusion};
    return all;
}

// The name used in files and answers.
std::string_view mapChannelName(MapChannel channel)
{
    switch (channel) {
    case MapChannel::BaseColor: return "base_color";
    case MapChannel::Normal: return "normal";
    case MapChannel::Roughness: return "roughness";
    case MapChannel::Metallic: return "metallic";
    case MapChannel::Opacity: return "opacity";
    case MapChannel::Occlusion: return "occlusion";
    }
    return "";
}

// The channel of a name.
std::optional<MapChannel> mapChannelFromName(std::string_view name)
{
    for (const MapChannel channel : allMapChannels()) {
        if (mapChannelName(channel) == name) return channel;
    }
    return std::nullopt;
}

// The name used in files and answers.
std::string_view mapOriginName(MapOrigin origin)
{
    switch (origin) {
    case MapOrigin::File: return "file";
    case MapOrigin::Value: return "value";
    case MapOrigin::Baked: return "baked";
    case MapOrigin::None: return "none";
    }
    return "none";
}

// The origin of a name.
std::optional<MapOrigin> mapOriginFromName(std::string_view name)
{
    for (const MapOrigin origin : {MapOrigin::File, MapOrigin::Value, MapOrigin::Baked, MapOrigin::None}) {
        if (mapOriginName(origin) == name) return origin;
    }
    return std::nullopt;
}

// The map of a channel.
const MaterialMap& materialMap(const MaterialMaps& maps, MapChannel channel)
{
    switch (channel) {
    case MapChannel::BaseColor: return maps.baseColor;
    case MapChannel::Normal: return maps.normal;
    case MapChannel::Roughness: return maps.roughness;
    case MapChannel::Metallic: return maps.metallic;
    case MapChannel::Opacity: return maps.opacity;
    case MapChannel::Occlusion: return maps.occlusion;
    }
    return maps.baseColor;
}

// The map of a channel, to change.
MaterialMap& materialMap(MaterialMaps& maps, MapChannel channel)
{
    return const_cast<MaterialMap&>(materialMap(static_cast<const MaterialMaps&>(maps), channel));
}

// Says whether both sides are in range.
Status checkTileSize(const TileSize& size)
{
    const auto inRange = [](double v) { return std::isfinite(v) && v >= kMinTileSize && v <= kMaxTileSize; };
    if (!inRange(size.width) || !inRange(size.height)) {
        return fail(ErrorCode::InvalidValue, "The tile size must be from 0.01 to 100 metres on each side.");
    }
    return {};
}

// Reads "1.2x0.6" or "1.2".
Status parseTileSize(std::string_view text, TileSize& out)
{
    const auto number = [](std::string_view part, double& value) {
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
        if (part.empty()) return false;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), value);
        return ec == std::errc() && end == part.data() + part.size();
    };
    const std::size_t x = text.find_first_of("xX");
    TileSize size;
    bool ok;
    if (x == std::string_view::npos) {
        ok = number(text, size.width);
        size.height = size.width;
    } else {
        ok = number(text.substr(0, x), size.width) && number(text.substr(x + 1), size.height);
    }
    if (!ok) {
        return fail(ErrorCode::InvalidValue, "The tile size \"" + std::string(text) + "\" is not a size such as 1.2x0.6 or 2.");
    }
    if (Status s = checkTileSize(size); !s.ok()) return s;
    out = size;
    return {};
}

// Says whether the material has no opacity map.
bool isOpaque(const MaterialInfo& info)
{
    return info.maps.opacity.origin == MapOrigin::None;
}

// Reads and checks info.json.
MaterialInfoRead readMaterialInfo(const fs::path& file, std::string_view folderName)
{
    MaterialInfoRead result;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const Json json = Json::parse(buffer.str(), nullptr, false);
    if (json.is_discarded()) {
        result.status = fail(ErrorCode::BadFile, "The file " + pathToUtf8(file.filename()) + " is not valid JSON.");
        return result;
    }
    try {
        result.info = parseInfo(json, folderName);
    } catch (const ReadProblem& problem) {
        if (problem.message == "UNSUPPORTED") {
            result.status = fail(ErrorCode::UnsupportedVersion,
                                 "The file " + pathToUtf8(file.filename()) + " was written by a newer Plynth.", problem.where);
        } else {
            result.status = fail(ErrorCode::BadFile, pathToUtf8(file.filename()) + ": " + problem.message, problem.where);
        }
    } catch (const Json::exception& e) {
        result.status = fail(ErrorCode::BadFile, pathToUtf8(file.filename()) + ": " + e.what());
    }
    return result;
}

// Writes info.json through a temporary file and a rename.
Status writeMaterialInfo(const fs::path& file, const MaterialInfo& info)
{
    Json json = Json::object();
    json["format"] = std::string(kMaterialInfoFormat);
    json["version"] = kMaterialInfoVersion;
    json["id"] = info.id;
    json["name"] = info.name;
    json["groups"] = info.groups;
    json["tags"] = info.tags;
    json["license"] = info.license;
    json["source"] = Json{{"kind", info.source.kind},
                          {"reference", info.source.reference},
                          {"imported_at", formatSessionTime(info.source.importedAt)}};
    json["tile_size"] = Json{{"width", info.tileSize.width}, {"height", info.tileSize.height}};
    Json maps = Json::object();
    for (const MapChannel channel : allMapChannels()) {
        maps[std::string(mapChannelName(channel))] = mapToJson(materialMap(info.maps, channel));
    }
    json["maps"] = std::move(maps);
    json["texture_size"] = Json{{"width", info.textureSize.width}, {"height", info.textureSize.height}};
    json["files"] = Json{{"bytes", info.files.bytes}, {"modified", formatSessionTime(info.files.modified)}};
    const bool timed = info.thumbnail.rendered || info.thumbnail.renderedAt.time_since_epoch().count() != 0;
    json["thumbnail"] = Json{
        {"file", info.thumbnail.rendered ? Json(std::string(kMaterialThumbFileName)) : Json(nullptr)},
        {"error", info.thumbnail.error.empty() ? Json(nullptr) : Json(info.thumbnail.error)},
        {"engine", info.thumbnail.engine.empty() ? Json(nullptr) : Json(info.thumbnail.engine)},
        {"rendered_at", timed ? Json(formatSessionTime(info.thumbnail.renderedAt)) : Json(nullptr)}};
    Json warnings = Json::array();
    for (const BlenderWarning& w : info.warnings) warnings.push_back(Json{{"code", w.code}, {"message", w.message}});
    json["cleanup"] = Json{{"blender_version", info.blenderVersion}, {"warnings", std::move(warnings)}};

    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    return fileio::writeAtomic(file, json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
}

// The size and latest write time of the map files.
MaterialFiles measureMaterialFiles(const fs::path& folder, const MaterialMaps& maps)
{
    MaterialFiles out;
    for (const MapChannel channel : allMapChannels()) {
        const MaterialMap& map = materialMap(maps, channel);
        if (map.file.empty()) continue;
        const fs::path file = folder / pathFromUtf8(map.file);
        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        if (ec) continue;
        out.bytes += static_cast<long long>(size);
        out.modified = std::max(out.modified, writeTimeOf(file));
    }
    return out;
}

}
