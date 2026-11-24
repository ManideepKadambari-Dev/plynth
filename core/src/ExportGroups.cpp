#include "plynth/core/ExportGroups.h"

#include "BuiltPlan.h"
#include "FileIo.h"
#include "IdSource.h"

#include "plynth/core/Kit.h"
#include "plynth/core/MaterialSlots.h"
#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace plynth {

namespace fs = std::filesystem;

namespace {

struct RuleRegistry {
    std::mutex mutex;
    std::vector<std::unique_ptr<ExportGroupRule>> rules;
};

// The one rule registry, made on first use so registrars in any file can use it while the program starts.
RuleRegistry& ruleRegistry()
{
    static RuleRegistry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedRuleId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Lower case copy of ASCII text.
std::string lowerCase(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Trims spaces at both ends.
std::string trimmed(std::string_view text)
{
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && text[start] == ' ') ++start;
    while (end > start && text[end - 1] == ' ') --end;
    return std::string(text.substr(start, end - start));
}

// Says whether a name is allowed: after trimming 1 to 64 characters, only A to Z, a to z, 0 to 9, spaces,
// hyphens and underscores, with at least one letter or digit. Names are English only (owner decision).
bool isValidGroupName(std::string_view name)
{
    const std::string clean = trimmed(name);
    if (clean.empty() || clean.size() > kMaxExportGroupName) return false;
    bool alnum = false;
    for (const char c : clean) {
        const bool letterOrDigit = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!letterOrDigit && c != ' ' && c != '-' && c != '_') return false;
        alnum = alnum || letterOrDigit;
    }
    return alnum;
}

// The plain sentence for a refused name.
const char* groupNameRule()
{
    return "A group name uses only the letters A to Z, the digits 0 to 9, spaces, hyphens and underscores, "
           "with at least one letter or digit, and is 1 to 64 characters long.";
}

// The message for a refused name.
Status badNameStatus()
{
    return fail(ErrorCode::InvalidValue, groupNameRule());
}

// Finds a group by id. Null when there is none.
ExportGroup* findGroup(std::vector<ExportGroup>& groups, std::string_view id)
{
    for (ExportGroup& g : groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

// The same for a list that may not be changed.
const ExportGroup* findGroup(const std::vector<ExportGroup>& groups, std::string_view id)
{
    for (const ExportGroup& g : groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

// Says whether a group holds this exact member.
bool holdsMember(const ExportGroup& group, const ExportMember& member)
{
    return std::find(group.members.begin(), group.members.end(), member) != group.members.end();
}

// The group that holds this exact member. Null when none does.
template <typename Groups>
auto findHolder(Groups& groups, const ExportMember& member) -> decltype(&groups.front())
{
    for (auto& g : groups) {
        if (holdsMember(g, member)) return &g;
    }
    return nullptr;
}

// "kind:id", or "kind:*" for the member of a whole kind.
std::string memberText(const ExportMember& member)
{
    return member.kind + (member.id.empty() ? ":*" : ":" + member.id);
}

// The room with this id on any floor. Null when there is none.
const Room* findRoomAnywhere(const Plan& plan, std::string_view id)
{
    for (const Floor& floor : plan.floors) {
        if (const Room* room = findRoom(floor, id)) return room;
    }
    return nullptr;
}

// "Label id" for a room, as in "Kitchen r3".
std::string roomLabelWithId(const Room& room)
{
    return roomSlotLabel(room) + " " + room.id;
}

}

// Adds a rule unless its id is taken or badly formed.
bool registerExportGroupRule(std::unique_ptr<ExportGroupRule> rule)
{
    if (!rule) return false;
    const std::string id(rule->id());
    if (!isWellFormedRuleId(id)) return false;
    RuleRegistry& r = ruleRegistry();
    std::lock_guard lock(r.mutex);
    for (const auto& existing : r.rules) {
        if (existing->id() == id) return false;
    }
    r.rules.push_back(std::move(rule));
    return true;
}

// Adds the rule to the registry when the program starts.
ExportGroupRuleRegistrar::ExportGroupRuleRegistrar(std::unique_ptr<ExportGroupRule> rule)
{
    registerExportGroupRule(std::move(rule));
}

// Lists the rules by order, then id.
std::vector<const ExportGroupRule*> allExportGroupRules()
{
    RuleRegistry& r = ruleRegistry();
    std::lock_guard lock(r.mutex);
    std::vector<const ExportGroupRule*> out;
    for (const auto& rule : r.rules) out.push_back(rule.get());
    std::sort(out.begin(), out.end(), [](const ExportGroupRule* a, const ExportGroupRule* b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->id() < b->id();
    });
    return out;
}

// Asks every rule for its groups, then takes out members two groups claim and groups left empty.
std::vector<ExportGroup> automaticExportGroups(const Project& project)
{
    std::vector<ExportGroup> out;
    std::set<ExportMember> taken;
    std::set<std::string> ids;
    for (const ExportGroupRule* rule : allExportGroupRules()) {
        std::vector<ExportGroup> made;
        rule->addGroups(project, made);
        for (ExportGroup& group : made) {
            if (ids.count(group.id) != 0) continue;
            const bool hadMembers = !group.members.empty();
            std::vector<ExportMember> kept;
            for (ExportMember& m : group.members) {
                if (taken.insert(m).second) kept.push_back(std::move(m));
            }
            group.members = std::move(kept);
            if (hadMembers && group.members.empty()) continue;
            ids.insert(group.id);
            out.push_back(std::move(group));
        }
    }
    bool anyRest = false;
    for (ExportGroup& g : out) {
        if (!g.rest) continue;
        if (anyRest) g.rest = false;
        anyRest = true;
    }
    if (!anyRest && !out.empty()) out.front().rest = true;
    return out;
}

// Gives each group a file name from the project name and its own name, unique without regard to case.
void nameExportGroupFiles(std::vector<ExportGroup>& groups, std::string_view projectName)
{
    const std::string project = exportFileStem(projectName);
    // The kit file is written next to the group files, so no group may have its name.
    std::set<std::string> used{lowerCase(project + std::string(kKitFileSuffix))};
    for (ExportGroup& group : groups) {
        const std::string stem = project + "_" + exportFileStem(group.name.empty() ? group.id : group.name);
        std::string file = stem + ".fbx";
        for (int n = 2; !used.insert(lowerCase(file)).second; ++n) file = stem + "_" + std::to_string(n) + ".fbx";
        group.file = std::move(file);
    }
}

// Describes a member in plain words for the review list.
std::string exportMemberLabel(const Plan& plan, const ExportMember& member)
{
    if (member.id.empty()) return "Every " + member.kind;
    const std::string pair = member.kind + " " + member.id;
    if (member.kind == kKindWalls || member.kind == kKindThresholds || member.kind == kKindTrim) {
        const Floor* floor = findFloor(plan, member.id);
        if (!floor) return pair;
        const char* what = member.kind == kKindWalls ? "Walls" : (member.kind == kKindTrim ? "Trim" : "Thresholds");
        return std::string(what) + ", " + floor->name;
    }
    if (member.kind == kKindRailing || member.kind == kKindGuard) {
        return std::string(member.kind == kKindRailing ? "Railing of stair " : "Guard of stair ") + member.id;
    }
    if (member.kind == kKindDressing) {
        const Room* room = findRoomAnywhere(plan, member.id);
        if (!room) return pair;
        return "Set dressing of " + roomLabelWithId(*room);
    }
    if (member.kind == kKindFloor || member.kind == kKindCeiling) {
        const Room* room = findRoomAnywhere(plan, member.id);
        if (!room) return pair;
        return std::string(member.kind == kKindFloor ? "Floor of " : "Ceiling of ") + roomLabelWithId(*room);
    }
    if (member.kind == kKindDoor) return "Door " + member.id;
    if (member.kind == kKindWindow) return "Window " + member.id;
    if (member.kind == kKindItem || member.kind == kKindGenerator) {
        for (const Floor& floor : plan.floors) {
            if (const Item* item = findItem(floor, member.id)) {
                return (item->generator ? item->generator->name : item->asset) + " " + member.id;
            }
        }
    }
    return pair;
}

// Whether no edit of any kind is stored.
bool ExportGroupEdits::empty() const
{
    return added.empty() && renames.empty() && merges.empty() && moves.empty();
}

// Applies the edits in the fixed order: added groups, merges, moves, renames.
ExportGroupsApplied applyExportGroupEdits(const std::vector<ExportGroup>& automatic, const ExportGroupEdits& edits,
                                          std::string_view projectName)
{
    ExportGroupsApplied out;
    out.groups = automatic;
    std::vector<ExportGroup>& groups = out.groups;

    for (const ExportGroupNew& added : edits.added) {
        if (findGroup(groups, added.id)) {
            out.notes.push_back("Group " + added.id + " could not be added because that id is already used.");
            continue;
        }
        ExportGroup group;
        group.id = added.id;
        group.name = added.name;
        groups.push_back(std::move(group));
    }

    // Where a group that was merged away went.
    std::map<std::string, std::string> wentTo;
    const auto resolve = [&](std::string id) {
        for (std::size_t guard = 0; guard <= wentTo.size(); ++guard) {
            const auto it = wentTo.find(id);
            if (it == wentTo.end()) break;
            id = it->second;
        }
        return id;
    };

    for (const ExportGroupMerge& merge : edits.merges) {
        const ExportGroup* from = findGroup(groups, merge.group);
        if (!from) {
            out.notes.push_back("Group " + merge.group + " is gone, so it was not merged into " + merge.into + ".");
            continue;
        }
        const std::string intoId = resolve(merge.into);
        ExportGroup* into = findGroup(groups, intoId);
        if (!into) {
            out.notes.push_back("Group " + merge.into + " is gone, so " + merge.group + " was not merged into it.");
            continue;
        }
        if (into == from) {
            out.notes.push_back("Group " + merge.group + " could not be merged into itself.");
            continue;
        }
        const ExportGroup moved = *from;
        into->members.insert(into->members.end(), moved.members.begin(), moved.members.end());
        if (into->members.empty()) {
            into->kinds.insert(into->kinds.end(), moved.kinds.begin(), moved.kinds.end());
            into->ids.insert(into->ids.end(), moved.ids.begin(), moved.ids.end());
        }
        into->rest = into->rest || moved.rest;
        wentTo[moved.id] = intoId;
        groups.erase(std::remove_if(groups.begin(), groups.end(), [&](const ExportGroup& g) { return g.id == moved.id; }),
                     groups.end());
    }

    for (const ExportMemberMove& move : edits.moves) {
        ExportGroup* source = findHolder(groups, move.member);
        if (!source) {
            out.notes.push_back("The member " + memberText(move.member) + " is not in any group, so it was not moved.");
            continue;
        }
        ExportGroup* target = findGroup(groups, resolve(move.to));
        if (!target) {
            out.notes.push_back("Group " + move.to + " is gone, so " + memberText(move.member) + " was not moved to it.");
            continue;
        }
        if (source == target) continue;
        source->members.erase(std::find(source->members.begin(), source->members.end(), move.member));
        target->members.push_back(move.member);
    }

    for (const ExportGroupRename& rename : edits.renames) {
        ExportGroup* group = findGroup(groups, rename.group);
        if (!group) {
            out.notes.push_back("Group " + rename.group + " is gone, so its new name was not used.");
            continue;
        }
        group->name = rename.name;
    }

    nameExportGroupFiles(groups, projectName);
    return out;
}

// Renames a group.
Status renameExportGroup(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, std::string_view group,
                         std::string_view name)
{
    if (!findGroup(current, group)) return fail(ErrorCode::NotFound, "There is no group " + std::string(group) + ".");
    if (!isValidGroupName(name)) return badNameStatus();
    const std::string clean = trimmed(name);
    const std::string key = lowerCase(clean);
    for (const ExportGroup& other : current) {
        if (other.id != group && lowerCase(other.name) == key) {
            return fail(ErrorCode::AlreadyExists, "There is already a group named " + other.name + ".");
        }
    }
    for (ExportGroupRename& rename : edits.renames) {
        if (rename.group == group) {
            rename.name = clean;
            return {};
        }
    }
    edits.renames.push_back({std::string(group), clean});
    return {};
}

// Adds a merge of one group into another.
Status mergeExportGroups(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, std::string_view group,
                         std::string_view into)
{
    if (!findGroup(current, group)) return fail(ErrorCode::NotFound, "There is no group " + std::string(group) + ".");
    if (!findGroup(current, into)) return fail(ErrorCode::NotFound, "There is no group " + std::string(into) + ".");
    if (group == into) return fail(ErrorCode::InvalidValue, "A group cannot be merged into itself.");
    // A rename of a group that goes away would only leave a note at every export.
    const std::string removed(group);
    edits.renames.erase(std::remove_if(edits.renames.begin(), edits.renames.end(),
                                       [&](const ExportGroupRename& r) { return r.group == removed; }),
                        edits.renames.end());
    edits.merges.push_back({removed, std::string(into)});
    return {};
}

// Adds a move of one member to a group.
Status moveExportMember(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, const ExportMember& member,
                        std::string_view to)
{
    const ExportGroup* source = findHolder(current, member);
    if (!source) return fail(ErrorCode::NotFound, "There is no member " + memberText(member) + ".");
    if (!findGroup(current, to)) return fail(ErrorCode::NotFound, "There is no group " + std::string(to) + ".");
    if (source->id == to) return {};
    edits.moves.erase(std::remove_if(edits.moves.begin(), edits.moves.end(),
                                     [&](const ExportMemberMove& m) { return m.member == member; }),
                      edits.moves.end());
    edits.moves.push_back({member, std::string(to)});
    return {};
}

// Adds an empty group with a new id that no group and no stored edit names.
Status addExportGroup(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, std::string_view name,
                      std::string& newId)
{
    if (!isValidGroupName(name)) return badNameStatus();
    const std::string clean = trimmed(name);
    const std::string key = lowerCase(clean);
    for (const ExportGroup& other : current) {
        if (lowerCase(other.name) == key) return fail(ErrorCode::AlreadyExists, "There is already a group named " + other.name + ".");
    }
    const auto taken = [&](std::string_view id) {
        if (findGroup(current, id)) return true;
        const auto same = [&](const std::string& other) { return other == id; };
        if (std::any_of(edits.added.begin(), edits.added.end(), [&](const ExportGroupNew& a) { return same(a.id); })) return true;
        if (std::any_of(edits.renames.begin(), edits.renames.end(), [&](const ExportGroupRename& r) { return same(r.group); })) return true;
        if (std::any_of(edits.merges.begin(), edits.merges.end(),
                        [&](const ExportGroupMerge& m) { return same(m.group) || same(m.into); })) {
            return true;
        }
        return std::any_of(edits.moves.begin(), edits.moves.end(), [&](const ExportMemberMove& m) { return same(m.to); });
    };
    std::string id;
    if (!detail::countedIds()) {
        const std::string part = detail::newIdPart([&](std::string_view p) { return taken("group_" + std::string(p)); });
        if (!part.empty()) id = "group_" + part;
    }
    if (id.empty()) {
        int n = 1;
        while (taken("group_" + std::to_string(n))) ++n;
        id = "group_" + std::to_string(n);
    }
    newId = id;
    edits.added.push_back({newId, clean});
    return {};
}

namespace {

// Reads a plain string field. False when it is missing or not text.
bool readText(const Json& object, const char* key, std::string& out)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

// Says whether a group id is well formed: the schema's pattern.
bool isGroupIdText(std::string_view id)
{
    return isWellFormedRuleId(id) && id.size() <= 80;
}

// Says whether an id is "group_" and either a number from 1 or a digit and seven characters of kIdAlphabet.
bool isAddedGroupId(std::string_view id)
{
    if (id.size() < 7 || id.substr(0, 6) != "group_") return false;
    const std::string_view rest = id.substr(6);
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    if (rest[0] != '0' && std::all_of(rest.begin(), rest.end(), isDigit)) return true;
    if (rest.size() != kNewIdLength || !isDigit(rest[0])) return false;
    return std::all_of(rest.begin(), rest.end(), [](char c) { return kIdAlphabet.find(c) != std::string_view::npos; });
}

// Checks that an object has only the listed fields.
Status checkFields(const Json& object, std::initializer_list<const char*> allowed, const std::string& where)
{
    for (const auto& item : object.items()) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&](const char* k) { return item.key() == k; });
        if (!known) return fail(ErrorCode::BadFile, "Unknown field \"" + item.key() + "\".", where + "/" + item.key());
    }
    return {};
}

// Reads the list at key, or nothing when it is missing. The reader gets each entry and where it is.
template <typename Reader>
Status readList(const Json& parent, const char* key, const std::string& where, Reader reader)
{
    const auto it = parent.find(key);
    if (it == parent.end()) return {};
    const std::string at = where + "/" + key;
    if (!it->is_array()) return fail(ErrorCode::BadFile, std::string("\"") + key + "\" must be a list.", at);
    for (std::size_t i = 0; i < it->size(); ++i) {
        const Json& item = (*it)[i];
        const std::string itemAt = at + "/" + std::to_string(i);
        if (!item.is_object()) return fail(ErrorCode::BadFile, "Each entry must be an object.", itemAt);
        if (Status s = reader(item, itemAt); !s.ok()) return s;
    }
    return {};
}

// Reads a group id field.
Status readGroupId(const Json& item, const char* key, const std::string& where, std::string& out)
{
    if (!readText(item, key, out) || !isGroupIdText(out)) {
        return fail(ErrorCode::BadFile, std::string("\"") + key + "\" must be a group id.", where + "/" + key);
    }
    return {};
}

// Reads a group name field.
Status readGroupName(const Json& item, const std::string& where, std::string& out)
{
    if (!readText(item, "name", out) || !isValidGroupName(out)) {
        return fail(ErrorCode::BadFile, std::string(groupNameRule()) + " Fix this name in the file or remove the edit.", where + "/name");
    }
    return {};
}

}

// Writes the options in their file form, every list always present.
Json exportOptionsToJson(const ExportOptions& options)
{
    Json json = Json::object();
    json["format"] = std::string(kExportOptionsFormat);
    json["version"] = kExportOptionsVersion;

    Json added = Json::array();
    for (const ExportGroupNew& a : options.groups.added) added.push_back({{"id", a.id}, {"name", a.name}});
    Json renames = Json::array();
    for (const ExportGroupRename& r : options.groups.renames) renames.push_back({{"group", r.group}, {"name", r.name}});
    Json merges = Json::array();
    for (const ExportGroupMerge& m : options.groups.merges) merges.push_back({{"group", m.group}, {"into", m.into}});
    Json moves = Json::array();
    for (const ExportMemberMove& m : options.groups.moves) {
        Json entry = Json::object();
        entry["kind"] = m.member.kind;
        entry["id"] = m.member.id.empty() ? Json(nullptr) : Json(m.member.id);
        entry["to"] = m.to;
        moves.push_back(std::move(entry));
    }
    Json groups = Json::object();
    groups["added"] = std::move(added);
    groups["renames"] = std::move(renames);
    groups["merges"] = std::move(merges);
    groups["moves"] = std::move(moves);
    json["groups"] = std::move(groups);
    Json kit = Json::object();
    kit["enabled"] = options.kit.enabled;
    json["kit"] = std::move(kit);
    Json gameplay = Json::object();
    gameplay["enabled"] = options.gameplay.enabled;
    json["gameplay"] = std::move(gameplay);
    return json;
}

// Reads the options from their file form.
ExportOptionsRead exportOptionsFromJson(const Json& json)
{
    ExportOptionsRead result;
    const auto bad = [&](Status status) {
        result.status = std::move(status);
        result.options = {};
        return result;
    };
    if (!json.is_object()) return bad(fail(ErrorCode::BadFile, "The export options must be a JSON object."));
    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() || format->get<std::string>() != kExportOptionsFormat) {
        return bad(fail(ErrorCode::BadFile, "This is not an export options file.", "/format"));
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer()) {
        return bad(fail(ErrorCode::BadFile, "The export options have no version number.", "/version"));
    }
    const long long fileVersion = version->get<long long>();
    if (fileVersion > kExportOptionsVersion) {
        return bad(fail(ErrorCode::UnsupportedVersion, "The export options were written by a newer Plynth.", "/version"));
    }
    if (fileVersion < 1) return bad(fail(ErrorCode::BadFile, "The export options version is not valid.", "/version"));
    // Version 1 had no gameplay switch, so it may not carry one.
    if (fileVersion == 1) {
        if (Status s = checkFields(json, {"format", "version", "groups", "kit"}, ""); !s.ok()) return bad(s);
    } else if (Status s = checkFields(json, {"format", "version", "groups", "kit", "gameplay"}, ""); !s.ok()) {
        return bad(s);
    }

    ExportOptions options;
    if (const auto groups = json.find("groups"); groups != json.end()) {
        if (!groups->is_object()) return bad(fail(ErrorCode::BadFile, "\"groups\" must be an object.", "/groups"));
        if (Status s = checkFields(*groups, {"added", "renames", "merges", "moves"}, "/groups"); !s.ok()) return bad(s);
        Status s = readList(*groups, "added", "/groups", [&](const Json& item, const std::string& at) -> Status {
            if (Status f = checkFields(item, {"id", "name"}, at); !f.ok()) return f;
            ExportGroupNew entry;
            if (!readText(item, "id", entry.id) || !isAddedGroupId(entry.id)) {
                return fail(ErrorCode::BadFile, "\"id\" must look like group_3vk8m2qa or group_1.", at + "/id");
            }
            if (Status n = readGroupName(item, at, entry.name); !n.ok()) return n;
            options.groups.added.push_back(std::move(entry));
            return {};
        });
        if (!s.ok()) return bad(s);
        s = readList(*groups, "renames", "/groups", [&](const Json& item, const std::string& at) -> Status {
            if (Status f = checkFields(item, {"group", "name"}, at); !f.ok()) return f;
            ExportGroupRename entry;
            if (Status g = readGroupId(item, "group", at, entry.group); !g.ok()) return g;
            if (Status n = readGroupName(item, at, entry.name); !n.ok()) return n;
            options.groups.renames.push_back(std::move(entry));
            return {};
        });
        if (!s.ok()) return bad(s);
        s = readList(*groups, "merges", "/groups", [&](const Json& item, const std::string& at) -> Status {
            if (Status f = checkFields(item, {"group", "into"}, at); !f.ok()) return f;
            ExportGroupMerge entry;
            if (Status g = readGroupId(item, "group", at, entry.group); !g.ok()) return g;
            if (Status g = readGroupId(item, "into", at, entry.into); !g.ok()) return g;
            options.groups.merges.push_back(std::move(entry));
            return {};
        });
        if (!s.ok()) return bad(s);
        s = readList(*groups, "moves", "/groups", [&](const Json& item, const std::string& at) -> Status {
            if (Status f = checkFields(item, {"kind", "id", "to"}, at); !f.ok()) return f;
            ExportMemberMove entry;
            if (!readText(item, "kind", entry.member.kind) || !isWellFormedRuleId(entry.member.kind)) {
                return fail(ErrorCode::BadFile, "\"kind\" must be a kind name.", at + "/kind");
            }
            const auto id = item.find("id");
            if (id == item.end()) return fail(ErrorCode::BadFile, "\"id\" is missing.", at + "/id");
            if (!id->is_null()) {
                if (!id->is_string() || !isValidId(id->get<std::string>())) {
                    return fail(ErrorCode::BadFile, "\"id\" must be a plan id or null.", at + "/id");
                }
                entry.member.id = id->get<std::string>();
            }
            if (Status g = readGroupId(item, "to", at, entry.to); !g.ok()) return g;
            options.groups.moves.push_back(std::move(entry));
            return {};
        });
        if (!s.ok()) return bad(s);
    }
    if (const auto kit = json.find("kit"); kit != json.end()) {
        if (!kit->is_object()) return bad(fail(ErrorCode::BadFile, "\"kit\" must be an object.", "/kit"));
        if (Status s = checkFields(*kit, {"enabled"}, "/kit"); !s.ok()) return bad(s);
        if (const auto enabled = kit->find("enabled"); enabled != kit->end()) {
            if (!enabled->is_boolean()) return bad(fail(ErrorCode::BadFile, "\"enabled\" must be true or false.", "/kit/enabled"));
            options.kit.enabled = enabled->get<bool>();
        }
    }
    if (const auto gameplay = json.find("gameplay"); gameplay != json.end()) {
        if (!gameplay->is_object()) return bad(fail(ErrorCode::BadFile, "\"gameplay\" must be an object.", "/gameplay"));
        if (Status s = checkFields(*gameplay, {"enabled"}, "/gameplay"); !s.ok()) return bad(s);
        if (const auto enabled = gameplay->find("enabled"); enabled != gameplay->end()) {
            if (!enabled->is_boolean()) {
                return bad(fail(ErrorCode::BadFile, "\"enabled\" must be true or false.", "/gameplay/enabled"));
            }
            options.gameplay.enabled = enabled->get<bool>();
        }
    }
    result.options = std::move(options);
    return result;
}

// Reads the options file of a project.
ExportOptionsRead readExportOptions(const fs::path& projectFolder)
{
    ExportOptionsRead result;
    const fs::path file = projectFolder / pathFromUtf8(kExportOptionsFileName);
    std::error_code ec;
    if (!fs::exists(file, ec)) return result;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be read.");
        return result;
    }
    const Json json = Json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        result.status = fail(ErrorCode::BadFile, std::string(kExportOptionsFileName) + " is not valid JSON.");
        return result;
    }
    result = exportOptionsFromJson(json);
    if (!result.status.ok()) {
        result.status.message = std::string(kExportOptionsFileName) + ": " + result.status.message;
        return result;
    }
    result.found = true;
    return result;
}

// Writes the options file, or removes it when the options are the defaults.
Status writeExportOptions(const fs::path& projectFolder, const ExportOptions& options)
{
    const fs::path file = projectFolder / pathFromUtf8(kExportOptionsFileName);
    if (options == ExportOptions{}) {
        std::error_code ec;
        fs::remove(file, ec);
        if (ec && fs::exists(file)) {
            return fail(ErrorCode::CannotWrite, "The file " + pathToUtf8(file) + " could not be removed.");
        }
        return {};
    }
    const std::string text = exportOptionsToJson(options).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return fileio::writeAtomic(file, text);
}

// Replaces a bad options file with the defaults, keeping the kit and gameplay switches and a copy of the old file.
ExportOptionsRepair repairExportOptions(const fs::path& projectFolder)
{
    ExportOptionsRepair repair;
    const ExportOptionsRead read = readExportOptions(projectFolder);
    repair.options = read.options;
    if (read.status.ok() || read.status.code == ErrorCode::UnsupportedVersion || read.status.code == ErrorCode::CannotRead) {
        repair.status = read.status;
        return repair;
    }

    const fs::path file = projectFolder / pathFromUtf8(kExportOptionsFileName);
    ExportOptions fresh;
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    in.close();
    const Json json = Json::parse(text.str(), nullptr, false);
    if (json.is_object()) {
        const auto kit = json.find("kit");
        if (kit != json.end() && kit->is_object()) {
            const auto enabled = kit->find("enabled");
            if (enabled != kit->end() && enabled->is_boolean()) fresh.kit.enabled = enabled->get<bool>();
        }
        const auto gameplay = json.find("gameplay");
        if (gameplay != json.end() && gameplay->is_object()) {
            const auto enabled = gameplay->find("enabled");
            if (enabled != gameplay->end() && enabled->is_boolean()) fresh.gameplay.enabled = enabled->get<bool>();
        }
    }
    const fs::path backup = projectFolder / pathFromUtf8(std::string(kExportOptionsFileName) + ".bad");
    if (Status s = fileio::writeAtomic(backup, text.str()); !s.ok()) {
        repair.status = s;
        return repair;
    }
    if (Status s = writeExportOptions(projectFolder, fresh); !s.ok()) {
        repair.status = s;
        return repair;
    }
    repair.options = fresh;
    repair.repaired = true;
    return repair;
}

// Works out the groups of a project for review and export.
ExportGroupReview reviewExportGroups(const fs::path& projectFolder, const Project& project)
{
    ExportGroupReview review;
    review.plan = project.plan;
    const Exporter* exporter = findExporter(exportModeName(project.settings.exportMode));
    if (!exporter) {
        review.status = fail(ErrorCode::InvalidValue, "There is no exporter for the export mode " +
                                                          std::string(exportModeName(project.settings.exportMode)) + ".");
        return review;
    }
    review.exporter = std::string(exporter->id());
    review.reviewable = exporter->reviewable();

    ExportOptionsRead read = readExportOptions(projectFolder);
    review.options = read.options;
    if (!read.status.ok()) {
        review.status = read.status;
        return review;
    }

    Project source = project;
    if (std::optional<BuiltPlan> built = readBuiltPlan(projectFolder)) {
        source.plan = std::move(built->plan);
        review.fromBuild = true;
    }
    review.plan = source.plan;
    if (Status s = exporter->groups(source, review.automatic); !s.ok()) {
        review.status = s;
        return review;
    }
    if (review.reviewable) {
        ExportGroupsApplied applied = applyExportGroupEdits(review.automatic, review.options.groups, project.settings.name);
        review.groups = std::move(applied.groups);
        review.notes = std::move(applied.notes);
    } else {
        review.groups = review.automatic;
    }
    return review;
}

}
