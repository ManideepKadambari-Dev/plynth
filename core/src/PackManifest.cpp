#include "PackSupport.h"

#include "plynth/core/ProjectFiles.h"

#include <fstream>
#include <set>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Stops reading at the first broken rule, keeping the reason and where it is.
struct Reader {
    Status status;

    bool bad(const std::string& message, const std::string& where)
    {
        if (status.ok()) status = fail(ErrorCode::BadFile, "pack.json: " + message, where);
        return false;
    }

    // A string field. Empty and false when it is missing or not a string.
    bool text(const Json& object, const char* key, std::string& out, const std::string& where)
    {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_string()) return bad(std::string("\"") + key + "\" must be a string.", where + "/" + key);
        out = it->get<std::string>();
        return true;
    }
};

// Checks that an object holds only the allowed keys.
bool onlyKeys(Reader& r, const Json& object, std::initializer_list<const char*> keys, const std::string& where)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string& key = it.key();
        bool known = false;
        for (const char* k : keys) known = known || key == k;
        if (!known) return r.bad("\"" + key + "\" is not a field of a pack.", where + "/" + key);
    }
    return true;
}

// Reads one entry and checks its own rules.
bool readEntry(Reader& r, const Json& json, const std::string& where, PackEntry& entry)
{
    if (!json.is_object()) return r.bad("An entry must be an object.", where);
    if (!onlyKeys(r, json, {"kind", "id", "name", "state", "reason", "license", "source", "files"}, where)) return false;
    std::string text;
    if (!r.text(json, "kind", text, where)) return false;
    const auto kind = packKindFromName(text);
    if (!kind) return r.bad("Unknown kind \"" + text + "\".", where + "/kind");
    entry.kind = *kind;
    if (!r.text(json, "id", entry.id, where)) return false;
    if (!checkCatalogId(entry.id).ok()) return r.bad("The id \"" + entry.id + "\" is not a valid id.", where + "/id");
    if (!r.text(json, "name", entry.name, where)) return false;
    if (!r.text(json, "state", text, where)) return false;
    const auto state = packStateFromName(text);
    if (!state) return r.bad("Unknown state \"" + text + "\".", where + "/state");
    entry.state = *state;
    entry.reason = PackReason::None;
    if (json.contains("reason")) {
        if (!r.text(json, "reason", text, where)) return false;
        const auto reason = packReasonFromName(text);
        if (!reason || *reason == PackReason::None) return r.bad("Unknown reason \"" + text + "\".", where + "/reason");
        entry.reason = *reason;
    }
    // A missing item has no info.json to take a source and a license from.
    const bool catalogItem = (entry.kind == PackKind::Asset || entry.kind == PackKind::Material) && entry.state != PackState::Missing;
    if (catalogItem) {
        if (!r.text(json, "license", entry.license, where)) return false;
        const auto source = json.find("source");
        if (source == json.end() || !source->is_object()) return r.bad("\"source\" must be an object.", where + "/source");
        if (!onlyKeys(r, *source, {"kind", "reference", "imported_at"}, where + "/source")) return false;
        if (!r.text(*source, "kind", entry.source.kind, where + "/source")) return false;
        if (!checkCatalogId(entry.source.kind).ok()) return r.bad("The source kind is not valid.", where + "/source/kind");
        if (!r.text(*source, "reference", entry.source.reference, where + "/source")) return false;
        if (!r.text(*source, "imported_at", text, where + "/source")) return false;
        const auto when = parseSessionTime(text);
        if (!when) return r.bad("\"imported_at\" is not a time.", where + "/source/imported_at");
        entry.source.importedAt = *when;
    } else if (json.contains("license") || json.contains("source")) {
        return r.bad("Only assets and materials that are not missing have a license and a source.", where);
    }

    const auto files = json.find("files");
    if (files == json.end() || !files->is_array()) return r.bad("\"files\" must be a list.", where + "/files");
    entry.files.clear();
    for (std::size_t i = 0; i < files->size(); ++i) {
        const std::string at = where + "/files/" + std::to_string(i);
        if (!(*files)[i].is_string()) return r.bad("A file must be a string.", at);
        entry.files.push_back((*files)[i].get<std::string>());
    }

    if (entry.state != PackState::Copied) {
        if (!entry.files.empty()) return r.bad("Only a copied entry lists files.", where + "/files");
        if (entry.reason == PackReason::None) return r.bad("An entry that is not copied needs a reason.", where);
        return true;
    }
    if (entry.reason != PackReason::None) return r.bad("A copied entry has no reason.", where + "/reason");
    if (packCopyRule(entry.kind, entry.source, entry.license) != PackReason::None)
        return r.bad("The entry " + entry.id + " is copied, but it must only be named in a pack.", where);
    if (entry.files.empty()) return r.bad("A copied entry lists its files.", where + "/files");
    const std::string prefix = pack::packPrefix(entry.kind, entry.id);
    const std::string single = pack::shapeFileName(entry.kind, entry.id);
    for (std::size_t i = 0; i < entry.files.size(); ++i) {
        const std::string& file = entry.files[i];
        const std::string at = where + "/files/" + std::to_string(i);
        std::string why;
        if (!pack::isSafeRelative(file, &why)) return r.bad(why, at);
        if (!pack::allowedFileType(file)) return r.bad("The file " + file + " has a type a pack does not carry.", at);
        if (file.rfind(prefix, 0) != 0 || file.size() == prefix.size())
            return r.bad("The file " + file + " is not inside " + prefix + ".", at);
        if (!single.empty() && file != prefix + single)
            return r.bad("The file " + file + " is not " + prefix + single + ".", at);
    }
    return true;
}

}

// The manifest in its file form.
Json packManifestToJson(const PackManifest& manifest)
{
    Json json = Json::object();
    json["format"] = std::string(kPackFormat);
    json["version"] = kPackVersion;
    json["project"] = manifest.project;
    json["created"] = formatSessionTime(manifest.created);
    Json rooms = Json::array();
    for (const RoomType& room : manifest.roomTypes) rooms.push_back(Json{{"id", room.id}, {"name", room.name}});
    json["room_types"] = std::move(rooms);
    Json entries = Json::array();
    for (const PackEntry& entry : manifest.entries) entries.push_back(pack::entryToJson(entry));
    json["entries"] = std::move(entries);
    return json;
}

// Reads a manifest and checks every rule a pack must keep.
PackRead packManifestFromJson(const Json& json)
{
    PackRead read;
    Reader r;
    if (!json.is_object()) {
        r.bad("The file must hold an object.", "");
        read.status = r.status;
        return read;
    }
    const auto version = json.find("version");
    if (version != json.end() && version->is_number_integer() && version->get<long long>() > kPackVersion) {
        read.status = fail(ErrorCode::UnsupportedVersion, "The pack was written by a newer Plynth (pack version " +
                                                              std::to_string(version->get<long long>()) + ").", "/version");
        return read;
    }
    std::string text;
    const bool headerOk = onlyKeys(r, json, {"format", "version", "project", "created", "room_types", "entries"}, "") &&
                          r.text(json, "format", text, "");
    if (headerOk && text != kPackFormat) r.bad("This is not a Plynth pack.", "/format");
    if (r.status.ok() && (version == json.end() || !version->is_number_integer() || version->get<long long>() != kPackVersion))
        r.bad("\"version\" must be " + std::to_string(kPackVersion) + ".", "/version");
    if (r.status.ok()) r.text(json, "project", read.manifest.project, "");
    if (r.status.ok() && r.text(json, "created", text, "")) {
        const auto when = parseSessionTime(text);
        if (!when) r.bad("\"created\" is not a time.", "/created");
        else read.manifest.created = *when;
    }
    if (r.status.ok()) {
        const auto rooms = json.find("room_types");
        if (rooms == json.end() || !rooms->is_array()) {
            r.bad("\"room_types\" must be a list.", "/room_types");
        } else {
            for (std::size_t i = 0; i < rooms->size() && r.status.ok(); ++i) {
                const std::string at = "/room_types/" + std::to_string(i);
                const Json& room = (*rooms)[i];
                RoomType type;
                if (!room.is_object()) { r.bad("A room type must be an object.", at); break; }
                if (!onlyKeys(r, room, {"id", "name"}, at) || !r.text(room, "id", type.id, at) || !r.text(room, "name", type.name, at)) break;
                if (!checkCatalogId(type.id).ok()) { r.bad("The room type id is not valid.", at + "/id"); break; }
                if (!checkCatalogName(type.name).ok()) { r.bad("The room type name is not valid.", at + "/name"); break; }
                read.manifest.roomTypes.push_back(std::move(type));
            }
        }
    }
    if (r.status.ok()) {
        const auto entries = json.find("entries");
        if (entries == json.end() || !entries->is_array()) {
            r.bad("\"entries\" must be a list.", "/entries");
        } else {
            std::set<std::pair<int, std::string>> ids;
            std::set<std::string> files;
            for (std::size_t i = 0; i < entries->size(); ++i) {
                const std::string at = "/entries/" + std::to_string(i);
                PackEntry entry;
                if (!readEntry(r, (*entries)[i], at, entry)) break;
                if (!ids.insert({static_cast<int>(entry.kind), entry.id}).second) { r.bad("The " + std::string(packKindName(entry.kind)) + " " + entry.id + " is listed twice.", at); break; }
                bool twice = false;
                for (const std::string& file : entry.files) twice = !files.insert(file).second || twice;
                if (twice) { r.bad("A file is listed twice.", at + "/files"); break; }
                read.manifest.entries.push_back(std::move(entry));
            }
        }
    }
    read.status = r.status;
    if (!read.status.ok()) read.manifest = {};
    return read;
}

// Reads pack/pack.json of a project.
PackRead readPack(const fs::path& projectFolder)
{
    PackRead read;
    const fs::path file = projectFolder / pathFromUtf8(kPackFolderName) / pathFromUtf8(kPackFileName);
    std::error_code ec;
    if (pack::isLink(file) || pack::isLink(file.parent_path())) {
        read.status = fail(ErrorCode::BadFile, "The pack folder or pack.json is a link. Packs never use links.");
        return read;
    }
    if (!fs::is_regular_file(file, ec)) {
        read.status = fail(ErrorCode::NotFound, "The project has no pack. Run plynth pack on the other machine first.");
        return read;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        read.status = fail(ErrorCode::CannotRead, "pack.json could not be opened.");
        return read;
    }
    const Json json = Json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        read.status = fail(ErrorCode::BadFile, "pack.json is not valid JSON.");
        return read;
    }
    return packManifestFromJson(json);
}

// The result of plynth pack.
Json packReportToJson(const PackReport& report)
{
    Json json = Json::object();
    json["folder"] = pathToUtf8(report.folder);
    json["checked_only"] = report.checkedOnly;
    json["complete"] = report.complete;
    json["bytes"] = report.bytes;
    int copied = 0, references = 0, missing = 0;
    Json entries = Json::array();
    for (const PackEntry& entry : report.manifest.entries) {
        copied += entry.state == PackState::Copied;
        references += entry.state == PackState::Reference;
        missing += entry.state == PackState::Missing;
        entries.push_back(pack::entryToJson(entry));
    }
    json["copied"] = copied;
    json["references"] = references;
    json["missing"] = missing;
    Json rooms = Json::array();
    for (const RoomType& room : report.manifest.roomTypes) rooms.push_back(Json{{"id", room.id}, {"name", room.name}});
    json["room_types"] = std::move(rooms);
    json["entries"] = std::move(entries);
    return json;
}

// The result of plynth unpack.
Json unpackReportToJson(const UnpackReport& report)
{
    Json json = Json::object();
    json["added"] = report.added;
    json["complete"] = report.complete;
    Json entries = Json::array();
    for (const UnpackEntry& entry : report.entries) {
        Json item = pack::entryToJson(entry.entry);
        item["action"] = std::string(unpackActionName(entry.action));
        if (!entry.command.empty()) item["command"] = entry.command;
        entries.push_back(std::move(item));
    }
    json["entries"] = std::move(entries);
    return json;
}

}
