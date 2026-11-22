#pragma once

#include "Exporter.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Grouped export (DESIGN.md section 7.16): automatic groups made by group rules, shown for review before an
// export, and the user's edits (rename, merge, move members between groups, new groups) kept in the project's
// export options file. Group edits are not plan changes and are not commands: they live outside plan.json and
// project.json, so changing them never makes the build out of date (planFingerprint does not see them).

// The plynth_kind values of built objects (BuildJob.h, Collision.h). Some are listed before any build makes
// them, so rules can name them; a member with an empty id of a kind nothing has is harmless.
inline constexpr std::string_view kKindWalls = "walls";           // id: the floor
inline constexpr std::string_view kKindThresholds = "thresholds"; // id: the floor
inline constexpr std::string_view kKindFloor = "floor";           // id: the room
inline constexpr std::string_view kKindCeiling = "ceiling";       // id: the room
inline constexpr std::string_view kKindDoor = "door";             // id: the opening
inline constexpr std::string_view kKindWindow = "window";         // id: the opening
inline constexpr std::string_view kKindItem = "item";             // id: an item that places an asset
inline constexpr std::string_view kKindSlab = "slab";             // id: the floor
inline constexpr std::string_view kKindStair = "stair";           // id: the stair
inline constexpr std::string_view kKindRoof = "roof";             // id: the floor
inline constexpr std::string_view kKindGenerator = "generator";   // id: an item that a generator makes
inline constexpr std::string_view kKindLight = "light";           // id: the light
inline constexpr std::string_view kKindKit = "kit";               // kit pieces (Kit.h); never in a group

// The built-in rules and the groups they make, in this order (one file each in core/src/exporters/, named
// GroupRule<Name>.cpp, so the existing exporters glob picks them up):
//   structure  order 10  group "structure", name "Walls", rest set: walls of every floor, every door and
//                        window fill (DESIGN.md: "walls, windows and doors")
//   surfaces   order 20  group "surfaces", name "Floors": every room's floor and ceiling, the thresholds of
//                        every floor, and every slab, stair, roof and light (members with an empty id)
//                        (DESIGN.md: "floors, roof and lights")
//   furniture  order 30  per room that holds items, in plan order: group "furniture_<room id>" (lower case,
//                        characters other than a to z, 0 to 9 and "_" as "_"), name "Furniture <Label>" where
//                        Label is roomSlotLabel (MaterialSlots.h), with " <room id>" added when two rooms share
//                        the label; one member for each of its items, as below. Items with no room go to
//                        group "furniture", name "Furniture" (DESIGN.md: "furniture, one FBX per room")
// Exact members only for objects the plan says the build makes: walls for a floor with walls, thresholds for
// a floor with a door or plain opening at sill 0, floor and ceiling for each room, door or window for each
// opening of that type, and one member for each item. An item is an asset or a generator, never both:
//   an item that places an asset     kind "item" and the item's id, no generator member
//   an item that a generator makes   kind "generator" and the item's id, no item member
// Each is the plynth_kind and plynth_id the build gives that item's objects, so moving that one member moves
// all of the item. While an item can only place an asset, every item gets an item member.

// A group rule: makes some of the automatic groups from the plan that was built.
class ExportGroupRule {
public:
    virtual ~ExportGroupRule() = default;

    // Stable lower case id, for example "furniture".
    virtual std::string_view id() const = 0;
    // Where its groups go in the list; lower first. Ties go by id.
    virtual int order() const = 0;
    // Adds its groups to out, with id, name, members and rest. Files are named later (nameExportGroupFiles).
    virtual void addGroups(const Project& project, std::vector<ExportGroup>& out) const = 0;
};

// Adds a group rule. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerExportGroupRule(std::unique_ptr<ExportGroupRule> rule);

// Registers a group rule while the program starts.
struct ExportGroupRuleRegistrar {
    explicit ExportGroupRuleRegistrar(std::unique_ptr<ExportGroupRule> rule);
};

// Every group rule, in order.
std::vector<const ExportGroupRule*> allExportGroupRules();

// The automatic groups of a project: every rule's groups in rule order. A member a group of an earlier rule
// already has is dropped from later ones, a group that loses all its members this way is dropped, and only the
// first group with rest keeps it (the first group gets it when none has it). A group a rule gave no members
// stays. Files are named by nameExportGroupFiles.
std::vector<ExportGroup> automaticExportGroups(const Project& project);

// Gives every group its file: exportFileStem(projectName), "_", exportFileStem(group name), ".fbx", for
// example "My_Flat_Furniture_Kitchen.fbx"; a file name already taken (compared without case) gets _2, _3 and
// so on before ".fbx".
void nameExportGroupFiles(std::vector<ExportGroup>& groups, std::string_view projectName);

// A plain label for a member, for the review list, from the plan that was built: "Walls, Floor 1",
// "Thresholds, Floor 1", "Floor of Kitchen r3", "Ceiling of Kitchen r3", "Door o3", "Window o5",
// "<asset> i4" for an item member (the item's asset id), "<generator> i4" for a generator member (the
// generator's name), "Every <kind>" for an empty id, and "<kind> <id>" for anything else.
std::string exportMemberLabel(const Plan& plan, const ExportMember& member);

// The user's group edits. They refer to group ids and plan ids, so they survive later builds; an edit that no
// longer applies is skipped with a note.
struct ExportGroupNew {
    std::string id;   // "group_1", "group_2", ...
    std::string name;

    bool operator==(const ExportGroupNew&) const = default;
};

struct ExportGroupRename {
    std::string group;
    std::string name;

    bool operator==(const ExportGroupRename&) const = default;
};

struct ExportGroupMerge {
    std::string group; // goes away; its members (and rest) go to into
    std::string into;

    bool operator==(const ExportGroupMerge&) const = default;
};

struct ExportMemberMove {
    ExportMember member;
    std::string to;

    bool operator==(const ExportMemberMove&) const = default;
};

struct ExportGroupEdits {
    std::vector<ExportGroupNew> added;
    std::vector<ExportGroupRename> renames;
    std::vector<ExportGroupMerge> merges;
    std::vector<ExportMemberMove> moves;

    bool empty() const;
    bool operator==(const ExportGroupEdits&) const = default;
};

// The groups after the edits, and a plain sentence for each edit that was skipped.
struct ExportGroupsApplied {
    std::vector<ExportGroup> groups;
    std::vector<std::string> notes; // for example "Group furniture_r9 is gone, so its new name was not used."
};

// Applies edits to automatic groups, in this order whatever order they were made in: added groups go after the
// automatic ones (empty), then merges in their order (a merge into a group that was merged away goes to where
// that group went; a merge of a group that is gone is skipped), then moves in their order (a member that is in
// no group, or a target that is gone, is skipped; the member leaves its group and joins the target), then
// renames (of a group that is gone: skipped). Then nameExportGroupFiles. Groups left with no member stay in
// the list and the review shows them. Such a group takes no object and is not written (Exporter.h step 3),
// unless it has rest, when it takes what is left.
ExportGroupsApplied applyExportGroupEdits(const std::vector<ExportGroup>& automatic, const ExportGroupEdits& edits,
                                          std::string_view projectName);

// Group names are English only: the letters A to Z and a to z, the digits 0 to 9, spaces, hyphens and
// underscores, with at least one letter or digit, 1 to 64 characters after trimming spaces. Nothing else is
// allowed, so no accented letters and no other alphabets. Automatic names follow the same rule. Room, floor
// and project names are not limited by it. Names are stored trimmed.
inline constexpr std::size_t kMaxExportGroupName = 64;

// The edit functions the app and the CLI use. current is applyExportGroupEdits of the automatic groups with the
// edits as they are now. Each checks first and changes edits only when it returns Ok. NotFound when a group or
// member is not in current; InvalidValue for a bad name or a merge of a group into itself; AlreadyExists for a
// name another group of current has (compared without case).
// A rename replaces an earlier rename of the same group.
Status renameExportGroup(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, std::string_view group,
                         std::string_view name);
// Adds a merge.
Status mergeExportGroups(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, std::string_view group,
                         std::string_view into);
// Adds a move, replacing an earlier move of the same member. Moving a member to the group it is in is Ok and
// changes nothing.
Status moveExportMember(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, const ExportMember& member,
                        std::string_view to);
// Adds an empty group with the next free id "group_<n>", which newId receives.
Status addExportGroup(ExportGroupEdits& edits, const std::vector<ExportGroup>& current, std::string_view name,
                      std::string& newId);

// The kit options (Kit.h). Off by default.
struct KitOptions {
    bool enabled = false;

    bool operator==(const KitOptions&) const = default;
};

// The project's export options file: projectFolder/export-options.json, schema/export-options.schema.json.
// It belongs to the project like project.json (it is not in build/ or export/, which can be deleted).
inline constexpr std::string_view kExportOptionsFileName = "export-options.json";
inline constexpr std::string_view kExportOptionsFormat = "plynth-export-options";
inline constexpr int kExportOptionsVersion = 1;

struct ExportOptions {
    ExportGroupEdits groups;
    KitOptions kit;

    bool operator==(const ExportOptions&) const = default;
};

// The file form: {"format", "version", "groups": {"added", "renames", "merges", "moves"}, "kit": {"enabled"}}.
Json exportOptionsToJson(const ExportOptions& options);

struct ExportOptionsRead {
    Status status;       // BadFile when the file is not valid, UnsupportedVersion when it is newer
    ExportOptions options;
    bool found = false;  // false when there is no file: options are the defaults and status is Ok
};

// Reads the options from their file form, with the same rules as the other project files (unknown fields
// refused, missing optional fields get their defaults).
ExportOptionsRead exportOptionsFromJson(const Json& json);

// Reads projectFolder/export-options.json. A missing file is Ok with default options.
ExportOptionsRead readExportOptions(const std::filesystem::path& projectFolder);

// Writes projectFolder/export-options.json through a temporary file and a rename. Writing the default options
// removes the file instead, so a project nobody changed has none.
Status writeExportOptions(const std::filesystem::path& projectFolder, const ExportOptions& options);

// The result of repairExportOptions.
struct ExportOptionsRepair {
    Status status;           // Ok, UnsupportedVersion for a newer file (left as it is), CannotRead or CannotWrite
    bool repaired = false;   // the file was bad and has been replaced
    ExportOptions options;   // what the project has now
};

// Makes a bad export-options.json usable again, for "plynth export groups reset" and the app's Reset groups.
// A missing or valid file is left alone (repaired false). A file written by a newer Plynth is never touched
// (UnsupportedVersion). Any other bad file, whether it is not JSON or breaks a rule such as a group name, is
// copied to export-options.json.bad and replaced by the default options, keeping the kit switch when the old
// file still has a readable one. All group edits in it are dropped.
ExportOptionsRepair repairExportOptions(const std::filesystem::path& projectFolder);

// What the Export tab and "plynth export groups" show before an export.
struct ExportGroupReview {
    Status status;           // the read status of the options file, or the exporter's refusal
    std::string exporter;    // the exporter id
    bool reviewable = false; // the exporter takes edits; false for single
    bool fromBuild = false;  // the groups come from build/built-plan.json; false when there is none and the
                             // project's own plan was used
    std::vector<ExportGroup> automatic; // what the exporter gives, before edits
    std::vector<ExportGroup> groups;    // after edits (the same as automatic when not reviewable)
    std::vector<std::string> notes;     // edits that were skipped
    ExportOptions options;              // as read
    Plan plan;                          // the plan the groups were made from (the built plan when fromBuild),
                                        // for exportMemberLabel(review.plan, member)
};

// Works out the review: the exporter for the project's export mode, the project with the plan of the last
// successful build when there is one, its groups, and the saved edits applied when the exporter is reviewable.
// runExport uses the same function, so the review and the export always agree.
ExportGroupReview reviewExportGroups(const std::filesystem::path& projectFolder, const Project& project);

}
