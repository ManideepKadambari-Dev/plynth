#include "plynth/core/catalog/AssetInfo.h"

#include "../FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Counts characters in UTF-8 text, not bytes.
std::size_t characterCount(std::string_view text)
{
    std::size_t n = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

// The object at a JSON pointer, with every key checked against the allowed ones and the required ones present.
const Json& objectAt(const Json& json, const std::string& where, std::initializer_list<const char*> required,
                     std::initializer_list<const char*> optional = {})
{
    if (!json.is_object()) throw ReadProblem{where, "Must be an object."};
    std::set<std::string> known;
    for (const char* key : required) known.insert(key);
    for (const char* key : optional) known.insert(key);
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

// A length in metres: finite and not negative.
double lengthAt(const Json& object, const char* key, const std::string& where)
{
    const Json& value = object.at(key);
    if (!value.is_number()) throw ReadProblem{where + "/" + key, "Must be a number."};
    const double v = value.get<double>();
    if (!std::isfinite(v) || v < 0.0) throw ReadProblem{where + "/" + key, "Must be a length of at least 0 metres."};
    return v;
}

// A time field.
SessionTime timeAt(const Json& object, const char* key, const std::string& where)
{
    const std::string text = textAt(object, key, where);
    const auto parsed = parseSessionTime(text);
    if (!parsed) throw ReadProblem{where + "/" + key, "Must be a UTC time such as 2026-11-21T10:00:00Z."};
    return *parsed;
}

// A list of text values.
std::vector<std::string> textListAt(const Json& object, const char* key, const std::string& where)
{
    const Json& list = object.at(key);
    if (!list.is_array()) throw ReadProblem{where + "/" + key, "Must be a list."};
    std::vector<std::string> out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!list[i].is_string()) throw ReadProblem{where + "/" + key + "/" + std::to_string(i), "Must be text."};
        out.push_back(list[i].get<std::string>());
    }
    return out;
}

// Turns a failed check into a read problem at a pointer.
void require(const Status& status, const std::string& where)
{
    if (!status.ok()) throw ReadProblem{where, status.message};
}

// Reads the checked fields of info.json into an AssetInfo.
AssetInfo parseInfo(const Json& json, std::string_view folderName)
{
    objectAt(json, "", {"format", "version", "id", "name", "categories", "tags", "license", "source", "size",
                        "triangles", "objects", "materials", "textures", "blend", "thumbnail", "cleanup"});
    if (!json.at("format").is_string() || json.at("format").get<std::string>() != kAssetInfoFormat) {
        throw ReadProblem{"/format", "This is not a Plynth asset file."};
    }
    if (!json.at("version").is_number_integer()) throw ReadProblem{"/version", "The version must be a whole number."};
    const long long version = json.at("version").get<long long>();
    if (version > kAssetInfoVersion) {
        throw ReadProblem{"/version", "UNSUPPORTED"};
    }
    if (version < kAssetInfoVersion) throw ReadProblem{"/version", "The version is not valid."};

    AssetInfo info;
    info.id = textAt(json, "id", "");
    require(checkCatalogId(info.id), "/id");
    if (!folderName.empty() && info.id != folderName) {
        throw ReadProblem{"/id", "The id \"" + info.id + "\" differs from the folder name \"" + std::string(folderName) + "\"."};
    }
    info.name = textAt(json, "name", "");
    require(checkCatalogName(info.name), "/name");

    info.categories = textListAt(json, "categories", "");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < info.categories.size(); ++i) {
        require(checkCatalogId(info.categories[i]), "/categories/" + std::to_string(i));
        if (!seen.insert(info.categories[i]).second) {
            throw ReadProblem{"/categories/" + std::to_string(i), "The category is listed twice."};
        }
    }
    info.tags = textListAt(json, "tags", "");
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

    const Json& size = objectAt(json.at("size"), "/size", {"width", "depth", "height"});
    info.size.width = lengthAt(size, "width", "/size");
    info.size.depth = lengthAt(size, "depth", "/size");
    info.size.height = lengthAt(size, "height", "/size");

    info.triangles = countAt(json, "triangles", "");
    info.objects = static_cast<int>(countAt(json, "objects", ""));
    info.materials = textListAt(json, "materials", "");
    info.textures = textListAt(json, "textures", "");

    const Json& blend = objectAt(json.at("blend"), "/blend", {"file", "bytes", "modified"});
    if (textAt(blend, "file", "/blend") != kAssetBlendFileName) {
        throw ReadProblem{"/blend/file", "Must be \"asset.blend\"."};
    }
    info.blendBytes = countAt(blend, "bytes", "/blend");
    info.blendModified = timeAt(blend, "modified", "/blend");

    const Json& thumb = objectAt(json.at("thumbnail"), "/thumbnail", {"file", "error", "engine", "rendered_at"});
    const Json& file = thumb.at("file");
    if (!file.is_null()) {
        if (!file.is_string() || file.get<std::string>() != kAssetThumbFileName) {
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
        if (!engine.is_string() || (engine.get<std::string>() != "eevee" && engine.get<std::string>() != "cycles")) {
            throw ReadProblem{"/thumbnail/engine", "Must be \"eevee\", \"cycles\" or null."};
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

}

// Reads and checks info.json.
AssetInfoRead readAssetInfo(const fs::path& file, std::string_view folderName)
{
    AssetInfoRead result;
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

// Says whether every side of a size is above zero.
bool isMeasured(const AssetSize& size)
{
    return std::isfinite(size.width) && std::isfinite(size.depth) && std::isfinite(size.height) && size.width > 0.0
        && size.depth > 0.0 && size.height > 0.0;
}

// Writes info.json through a temporary file and a rename.
Status writeAssetInfo(const fs::path& file, const AssetInfo& info)
{
    Json json = Json::object();
    json["format"] = std::string(kAssetInfoFormat);
    json["version"] = kAssetInfoVersion;
    json["id"] = info.id;
    json["name"] = info.name;
    json["categories"] = info.categories;
    json["tags"] = info.tags;
    json["license"] = info.license;
    json["source"] = Json{{"kind", info.source.kind},
                          {"reference", info.source.reference},
                          {"imported_at", formatSessionTime(info.source.importedAt)}};
    json["size"] = Json{{"width", info.size.width}, {"depth", info.size.depth}, {"height", info.size.height}};
    json["triangles"] = info.triangles;
    json["objects"] = info.objects;
    json["materials"] = info.materials;
    json["textures"] = info.textures;
    json["blend"] = Json{{"file", std::string(kAssetBlendFileName)},
                         {"bytes", info.blendBytes},
                         {"modified", formatSessionTime(info.blendModified)}};
    const bool timed = info.thumbnail.rendered || info.thumbnail.renderedAt.time_since_epoch().count() != 0;
    json["thumbnail"] = Json{
        {"file", info.thumbnail.rendered ? Json(std::string(kAssetThumbFileName)) : Json(nullptr)},
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

// Checks an asset, category or room type id.
Status checkCatalogId(std::string_view id)
{
    const std::string quoted = "\"" + std::string(id) + "\"";
    if (id.empty()) return fail(ErrorCode::InvalidValue, "The id is empty.");
    if (id.size() > kMaxAssetIdLength) {
        return fail(ErrorCode::InvalidValue, "The id " + quoted + " is longer than " + std::to_string(kMaxAssetIdLength) + " characters.");
    }
    if (!isLower(id.front())) return fail(ErrorCode::InvalidValue, "The id " + quoted + " must start with a lower case letter.");
    for (const char c : id) {
        if (!isLower(c) && !isDigit(c) && c != '_') {
            return fail(ErrorCode::InvalidValue,
                        "The id " + quoted + " may only use lower case letters a to z, digits and \"_\".");
        }
    }
    return {};
}

// Checks a tag.
Status checkTag(std::string_view tag)
{
    const std::string quoted = "\"" + std::string(tag) + "\"";
    if (tag.empty()) return fail(ErrorCode::InvalidValue, "A tag is empty.");
    if (tag.size() > kMaxTagLength) {
        return fail(ErrorCode::InvalidValue, "The tag " + quoted + " is longer than " + std::to_string(kMaxTagLength) + " characters.");
    }
    if (!isLower(tag.front()) && !isDigit(tag.front())) {
        return fail(ErrorCode::InvalidValue, "The tag " + quoted + " must start with a lower case letter or a digit.");
    }
    for (const char c : tag) {
        if (!isLower(c) && !isDigit(c) && c != '_' && c != '-') {
            return fail(ErrorCode::InvalidValue,
                        "The tag " + quoted + " may only use lower case letters a to z, digits, \"_\" and \"-\".");
        }
    }
    return {};
}

// Checks a name.
Status checkCatalogName(std::string_view name)
{
    const bool blank = std::all_of(name.begin(), name.end(), [](char c) { return c == ' ' || c == '\t'; });
    if (name.empty() || blank) return fail(ErrorCode::InvalidValue, "The name is empty.");
    if (characterCount(name) > kMaxAssetNameLength) {
        return fail(ErrorCode::InvalidValue, "The name is longer than " + std::to_string(kMaxAssetNameLength) + " characters.");
    }
    return {};
}

// Makes an id from text.
std::string catalogIdFromName(std::string_view text)
{
    std::string id;
    bool pendingSeparator = false;
    for (const char raw : text) {
        char c = raw;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (isLower(c) || isDigit(c)) {
            if (pendingSeparator && !id.empty()) id.push_back('_');
            pendingSeparator = false;
            id.push_back(c);
        } else {
            pendingSeparator = true;
        }
    }
    if (id.empty()) return "asset";
    if (isDigit(id.front())) id = "a_" + id;
    if (id.size() > kMaxAssetIdLength) id.resize(kMaxAssetIdLength);
    while (!id.empty() && id.back() == '_') id.pop_back();
    return id.empty() ? "asset" : id;
}

// Makes an id that is not taken.
std::string uniqueCatalogId(const std::string& base, const std::function<bool(std::string_view)>& taken)
{
    if (!taken(base)) return base;
    for (int n = 2;; ++n) {
        const std::string suffix = "_" + std::to_string(n);
        std::string stem = base;
        if (stem.size() + suffix.size() > kMaxAssetIdLength) stem.resize(kMaxAssetIdLength - suffix.size());
        const std::string candidate = stem + suffix;
        if (!taken(candidate)) return candidate;
    }
}

}
