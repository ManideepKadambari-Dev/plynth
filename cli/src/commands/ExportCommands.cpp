#include "../CatalogCommon.h"
#include "../Common.h"
#include "../LinkCommon.h"
#include "../SessionKeeper.h"

#include <plynth/core/Blender.h>
#include <plynth/core/Export.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/LicensedItems.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct ExportArgs {
    std::vector<std::string> what;
    std::string project;
    BlenderOption blender;
    std::string library;
    std::string name;
    std::string into;
    std::string to;
    bool quiet = false;
    bool check = false;
    bool force = false;
};

// Writes one progress line: {"progress": {"stage", "step", "index", "count", "message"}}.
void writeProgress(std::ostream& out, std::string_view stage, const BlenderProgress& step,
                   const std::string& message) {
    Json inner = Json::object();
    inner["stage"] = std::string(stage);
    inner["step"] = step.step;
    inner["index"] = step.index;
    inner["count"] = step.count;
    inner["message"] = message;
    Json line = Json::object();
    line["progress"] = std::move(inner);
    out << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    out.flush();
}

// The sentence shown for a stage that has no worker step.
std::string stageMessage(ExportStage stage) {
    switch (stage) {
    case ExportStage::Preparing:
        return "Preparing the export";
    case ExportStage::Finishing:
        return "Writing the files and the report";
    case ExportStage::Blender:
    case ExportStage::Done:
        break;
    }
    return "Done";
}

// Answers with the last export report of the project.
Answer exportReport(const std::string& command, const ExportArgs& options, const RunContext& context) {
    if (options.check || options.force) {
        return failure(command, fail(ErrorCode::InvalidValue, "export report takes no --check or --force."));
    }
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const ExportReportRead read = readExportReport(folder);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    return success(command, exportReportToJson(read.report));
}

// Exports the last build, or only runs the checks with --check.
Answer exportProject(const RunContext& context, const ExportArgs& options) {
    const std::string command = "export";
    Answer failed;
    if (refuseEmptyBlender(command, options.blender, failed)) {
        return failed;
    }
    if (!options.name.empty() || !options.into.empty() || !options.to.empty()) {
        return badArguments(command, "bad_arguments", "--name, --into and --to belong to 'export groups'.");
    }
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }

    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    const BlenderChoice choice = chooseBlender(blenderOptionPath(context, options.blender), loaded.settings);
    if (!choice.status.ok()) {
        return failure(command, choice.status);
    }

    ExportRequest request;
    request.projectFolder = folder;
    request.project = document.project();
    request.blender = choice.install;
    if (!linkLibrary(command, context, options.library, request.library, failed)) {
        return failed;
    }
    request.settings.force = options.force;
    request.settings.write = !options.check;

    std::ostream* progress = options.quiet ? nullptr : context.progress;
    std::optional<ExportStage> lastStage;
    const auto onProgress = [&](const ExportProgress& step) {
        if (progress == nullptr) {
            return;
        }
        if (step.stage == ExportStage::Blender) {
            if (step.blender.index < 1) {
                return;
            }
            writeProgress(*progress, exportStageName(step.stage), step.blender, step.blender.message);
        } else if (!lastStage || *lastStage != step.stage) {
            writeProgress(*progress, exportStageName(step.stage), BlenderProgress{}, stageMessage(step.stage));
        }
        lastStage = step.stage;
    };
    const SessionKeeper keeper(folder);
    const ExportReport report = runExport(request, onProgress, stopToken(context));
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, exportReportToJson(report));
    if (options.check) {
        for (const QualityCheckResult& check : report.checks) {
            if (check.status == QualityStatus::Fail) {
                answer.warnings.push_back({"quality_failed", "The check '" + check.id + "' failed."});
            }
        }
    }
    return answer;
}

// The groups answer: the exporter, whether its groups can be edited, where they came from, each group with
// its members, the notes of skipped edits and the kit switch of the Unreal Engine link.
Json groupsJson(const ExportGroupReview& review) {
    Json groups = Json::array();
    for (const ExportGroup& group : review.groups) {
        Json entry = Json::object();
        entry["id"] = group.id;
        entry["name"] = group.name;
        entry["file"] = group.file;
        entry["rest"] = group.rest;
        entry["automatic"] = std::ranges::any_of(
            review.automatic, [&](const ExportGroup& other) { return other.id == group.id; });
        Json members = Json::array();
        for (const ExportMember& member : group.members) {
            Json item = Json::object();
            item["kind"] = member.kind;
            item["id"] = member.id.empty() ? Json(nullptr) : Json(member.id);
            item["label"] = exportMemberLabel(review.plan, member);
            members.push_back(std::move(item));
        }
        entry["members"] = std::move(members);
        groups.push_back(std::move(entry));
    }
    Json notes = Json::array();
    for (const std::string& note : review.notes) {
        notes.push_back(note);
    }
    Json kit = Json::object();
    kit["enabled"] = engineLinkOptions(review.options, kUnrealLinkId).kit;
    Json result = Json::object();
    result["exporter"] = review.exporter;
    result["reviewable"] = review.reviewable;
    result["from_build"] = review.fromBuild;
    result["groups"] = std::move(groups);
    result["notes"] = std::move(notes);
    result["kit"] = std::move(kit);
    return result;
}

// True when an option of the group edits was given.
bool hasEditOptions(const ExportArgs& args) {
    return !args.name.empty() || !args.into.empty() || !args.to.empty();
}

// Lists the groups an export would write, with the saved edits applied.
Answer groupsList(const ExportArgs& args, const RunContext& context) {
    const std::string command = "export groups";
    if (hasEditOptions(args)) {
        return badArguments(command, "bad_arguments", "'export groups' takes no --name, --into or --to.");
    }
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const ExportGroupReview review = reviewExportGroups(folder, document.project());
    if (!review.status.ok()) {
        return failure(command, review.status);
    }
    return success(command, groupsJson(review));
}

// Reads a member written as kind:id, or kind:* for every object of the kind.
bool parseMember(const std::string& command, const std::string& text, ExportMember& member, Answer& failed) {
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == text.size()) {
        failed = failure(command, fail(ErrorCode::InvalidValue,
                                       "A member is written kind:id, for example item:i1, or kind:* for every "
                                       "object of a kind."));
        return false;
    }
    member.kind = text.substr(0, colon);
    const std::string id = text.substr(colon + 1);
    member.id = id == "*" ? std::string() : id;
    return true;
}

// Checks that the options and the argument fit the group edit. Returns false and fills failed when not.
bool checkEditArguments(const std::string& what, const std::string& target, const ExportArgs& args,
                        const std::string& command, Answer& failed) {
    const bool needsName = what == "rename" || what == "new";
    const bool takesTarget = what == "rename" || what == "merge" || what == "move";
    std::string problem;
    if (needsName && args.name.empty()) {
        problem = "'" + command + "' needs --name.";
    } else if (what == "merge" && args.into.empty()) {
        problem = "'" + command + "' needs --into.";
    } else if (what == "move" && args.to.empty()) {
        problem = "'" + command + "' needs --to.";
    } else if ((!needsName && !args.name.empty()) || (what != "merge" && !args.into.empty()) ||
               (what != "move" && !args.to.empty())) {
        problem = "'" + command + "' got an option it does not take.";
    } else if (takesTarget && target.empty()) {
        problem = "'" + command + "' needs " + (what == "move" ? "a member." : "a group.");
    } else if (!takesTarget && !target.empty()) {
        problem = "'" + command + "' takes no argument.";
    }
    if (problem.empty()) {
        return true;
    }
    failed = badArguments(command, "bad_arguments", problem);
    return false;
}

// Applies one group edit to the saved options and answers with the groups after it. what is rename, merge,
// move, new or reset; target is the group or member the edit names.
Answer groupsEdit(const std::string& what, const std::string& target, const ExportArgs& args,
                  const RunContext& context) {
    const std::string command = "export groups " + what;
    Answer failed;
    if (!checkEditArguments(what, target, args, command, failed)) {
        return failed;
    }
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    if (!openProject(command, folder, document, failed, false)) {
        return failed;
    }
    const std::vector<std::filesystem::path> kept = backupFiles(folder);
    std::optional<ExportOptionsRepair> repair;
    if (what == "reset") {
        repair = repairExportOptions(folder);
        if (!repair->status.ok()) {
            return failure(command, repair->status);
        }
    }
    const ExportGroupReview review = reviewExportGroups(folder, document.project());
    if (!review.status.ok()) {
        return failure(command, review.status);
    }
    if (!review.reviewable && repair && repair->repaired) {
        Json result = groupsJson(review);
        result["repaired"] = {{"backup", pathToUtf8(folder / (std::string(kExportOptionsFileName) + ".bad"))}};
        Answer answer = success(command, std::move(result));
        addNewBackups(answer, folder, kept);
        return answer;
    }
    if (!review.reviewable) {
        return failure(command, fail(ErrorCode::InvalidValue,
                                     "Groups can only be edited in grouped export mode. Run plynth project set "
                                     "--export grouped first."));
    }

    ExportOptions options = review.options;
    std::string newId;
    Status status;
    if (what == "rename") {
        status = renameExportGroup(options.groups, review.groups, target, args.name);
    } else if (what == "merge") {
        status = mergeExportGroups(options.groups, review.groups, target, args.into);
    } else if (what == "move") {
        ExportMember member;
        if (!parseMember(command, target, member, failed)) {
            return failed;
        }
        status = moveExportMember(options.groups, review.groups, member, args.to);
    } else if (what == "new") {
        status = addExportGroup(options.groups, review.groups, args.name, newId);
    } else {
        options.groups = {};
    }
    if (!status.ok()) {
        return failure(command, status);
    }
    if (const Status written = writeExportOptions(folder, options); !written.ok()) {
        return failure(command, written);
    }
    const ExportGroupReview after = reviewExportGroups(folder, document.project());
    if (!after.status.ok()) {
        return failure(command, after.status);
    }
    Json result = groupsJson(after);
    if (what == "new") {
        result["new_group"] = newId;
    }
    if (repair && repair->repaired) {
        const std::filesystem::path backup = folder / (std::string(kExportOptionsFileName) + ".bad");
        result["repaired"] = {{"backup", pathToUtf8(backup)}};
    }
    Answer answer = success(command, std::move(result));
    addNewBackups(answer, folder, kept);
    return answer;
}

// The switch of export-options.json that 'export kit', 'export gameplay' or 'export licenses' names. The kit
// and gameplay switches belong to the Unreal Engine link, so they live in links.unreal.
bool& exportSwitch(ExportOptions& options, const std::string& name) {
    if (name == "licenses") {
        return options.licenses.enabled;
    }
    EngineLinkOptions& unreal = options.links[std::string(kUnrealLinkId)];
    return name == "kit" ? unreal.kit : unreal.gameplay;
}

// Says that the kit or gameplay switch now belongs to the Unreal Engine link and is left out of plain export.
std::string linkSwitchNote(const std::string& name) {
    const std::string what = name == "kit" ? "The modular kit" : "The gameplay data file";
    return what + " belongs to the Unreal Engine link. This switch is saved as links.unreal." + name +
           " in export-options.json, and plain export does not write it. 'link unreal set --" + name +
           "' changes the same switch.";
}

// The license report of the last build, looked up in the library when there is one, or null with the reason.
void addLicenseReport(Json& result, const RunContext& context, const std::string& libraryOption,
                      const std::filesystem::path& folder, const std::string& projectName) {
    OpenedCatalog assets;
    OpenedMaterialCatalog materials;
    Answer ignored;
    const bool haveAssets = openCatalog("export licenses", context, libraryOption, assets, ignored);
    const bool haveMaterials = openMaterialCatalog("export licenses", context, libraryOption, materials, ignored);
    const LicenseReportRead report = projectLicenseReport(folder, projectName, haveAssets ? assets.catalog.get() : nullptr,
                                                          haveMaterials ? materials.catalog.get() : nullptr);
    if (report.status.ok()) {
        result["report"] = licenseReportToJson(report.report);
        result["reason"] = nullptr;
    } else {
        result["report"] = nullptr;
        result["reason"] = report.status.message;
    }
}

// Shows the modular kit or the gameplay data switch, or turns it on or off. what[0] is "kit" or "gameplay".
Answer optionSwitch(const std::vector<std::string>& what, const ExportArgs& args, const RunContext& context) {
    const std::string& name = what[0];
    const bool change = what.size() == 2;
    const std::string base = "export " + name;
    const std::string command = change ? base + " " + what[1] : base;
    if (change && what[1] != "on" && what[1] != "off") {
        return badArguments(command, "bad_arguments",
                            "Use '" + base + "', '" + base + " on' or '" + base + " off'.");
    }
    if (hasEditOptions(args)) {
        return badArguments(command, "bad_arguments", "'" + base + "' takes no --name, --into or --to.");
    }
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, !change)) {
        return failed;
    }
    ExportOptionsRead read = readExportOptions(folder);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    const std::vector<std::filesystem::path> kept = backupFiles(folder);
    if (change) {
        exportSwitch(read.options, name) = what[1] == "on";
        if (const Status written = writeExportOptions(folder, read.options); !written.ok()) {
            return failure(command, written);
        }
    }
    Json result = Json::object();
    result["enabled"] = exportSwitch(read.options, name);
    if (name == "licenses" && !change) {
        addLicenseReport(result, context, args.library, folder, document.project().settings.name);
    }
    if (name != "licenses") {
        result["note"] = linkSwitchNote(name);
    }
    Answer answer = success(command, std::move(result));
    addNewBackups(answer, folder, kept);
    return answer;
}

// Sends the words after export to the report, the groups, the kit or gameplay switch or the export itself.
Answer exportDispatch(const RunContext& context, const ExportArgs& args) {
    const std::vector<std::string>& what = args.what;
    if (what.empty()) {
        return exportProject(context, args);
    }
    const bool plain = !args.check && !args.force;
    if (what[0] == "report" && what.size() == 1) {
        return exportReport("export", args, context);
    }
    if (what[0] == "cancel" && what.size() == 1 && plain) {
        return cancelRunning("export", context, args.project);
    }
    if (what[0] == "groups" && plain) {
        if (what.size() == 1) {
            return groupsList(args, context);
        }
        static const std::vector<std::string> edits = {"rename", "merge", "move", "new", "reset"};
        if (std::ranges::find(edits, what[1]) != edits.end() && what.size() <= 3) {
            return groupsEdit(what[1], what.size() == 3 ? what[2] : std::string(), args, context);
        }
    }
    if ((what[0] == "kit" || what[0] == "gameplay" || what[0] == "licenses") && plain && what.size() <= 2) {
        return optionSwitch(what, args, context);
    }
    return badArguments("export", "bad_arguments",
                        "Unknown form of 'export'. Use: export, export report, export cancel, export groups, export groups "
                        "rename|merge|move|new|reset, export kit, export kit on|off, export gameplay, export "
                        "gameplay on|off, export licenses, export licenses on|off.");
}

}

std::vector<CommandSpec> exportCommands() {
    CommandSpec spec;
    spec.words = {"export"};
    spec.description =
        "Export the last build as plain FBX files for any tool behind the quality checks. "
        "With --check, only run the checks. With --force, export past failed checks. "
        "'export report' shows the last export report and 'export cancel' stops a running export. "
        "'export groups' lists the files a grouped export writes. "
        "'export groups rename <group> --name <text>', 'export groups merge <group> --into <group>', "
        "'export groups move <kind>:<id> --to <group>' (<kind>:* for every object of a kind), "
        "'export groups new --name <text>' and 'export groups reset' edit them. "
        "'export kit' shows the modular kit switch of the Unreal Engine link; 'export kit on' and 'export kit off' "
        "change it. 'export gameplay' shows the gameplay data switch of the Unreal Engine link; 'export gameplay on' "
        "and 'export gameplay off' change it. Plain export writes neither. "
        "'export licenses' shows the license report switch and the report of the last build; 'export licenses on' and "
        "'export licenses off' change the switch.";
    spec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ExportArgs>();
        sub.add_option("what", options->what,
                       "Nothing to export; 'report'; 'cancel'; 'groups' with rename, merge, move, new or reset and its "
                       "group or member; 'kit', 'gameplay' or 'licenses' with on or off.")
            ->expected(0, 3);
        addProjectOption(sub, options->project);
        addBlenderOption(sub, options->blender);
        addLibraryOption(sub, options->library);
        sub.add_option("--name", options->name, "The group name, for 'export groups rename' and 'new'. Allowed: A-Z, a-z, 0-9, "
                       "spaces, hyphens and underscores, 1 to 64 characters, at least one letter or digit.")
            ->type_name("text");
        sub.add_option("--into", options->into, "The group to merge into, for 'export groups merge'.")
            ->type_name("group");
        sub.add_option("--to", options->to, "The group to move a member to, for 'export groups move'.")
            ->type_name("group");
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        sub.add_flag("--check", options->check, "Run the checks without writing any export files.");
        sub.add_flag("--force", options->force, "Export even when a check failed.");
        return [options](const RunContext& context) { return exportDispatch(context, *options); };
    };
    return {spec};
}

}
