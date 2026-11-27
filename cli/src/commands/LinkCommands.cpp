#include "../CatalogCommon.h"
#include "../Common.h"
#include "../LinkCommon.h"
#include "../SessionKeeper.h"

#include <plynth/core/Blender.h>
#include <plynth/core/EngineLink.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/ExportJob.h>
#include <plynth/core/Settings.h>

#include <cctype>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct LinkArgs {
    std::string project;
    BlenderOption blender;
    std::string library;
    std::string editor;
    std::string screenshot;
    bool force = false;
    bool selfCheck = false;
    bool quiet = false;
    std::string packMaterials;
    std::string grouped;
    std::string kit;
    std::string gameplay;
    std::string sync;
};

// Writes one progress line of a link run: {"progress": {"stage", "step", "index", "count", "message"}}.
void writeLinkProgress(std::ostream& out, const std::string& stage, const BlenderProgress& step,
                       const std::string& message) {
    Json inner = Json::object();
    inner["stage"] = stage;
    inner["step"] = step.step;
    inner["index"] = step.index;
    inner["count"] = step.count;
    inner["message"] = message;
    Json line = Json::object();
    line["progress"] = std::move(inner);
    out << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    out.flush();
}

// Makes the progress function of a link run. Each stage gets one line, and each Blender step of the prepare
// stage one more. prefix goes in front of the stage names, so the sync after a build can tell its lines apart.
std::function<void(const EngineLinkProgress&)> linkProgress(std::ostream* out, const std::string& prefix) {
    if (out == nullptr) {
        return {};
    }
    auto last = std::make_shared<std::optional<EngineLinkStage>>();
    return [out, prefix, last](const EngineLinkProgress& step) {
        const std::string stage = prefix + std::string(engineLinkStageName(step.stage));
        if (step.stage == EngineLinkStage::Preparing && step.blender.index >= 1) {
            writeLinkProgress(*out, stage, step.blender, step.blender.message);
        } else if (!*last || **last != step.stage) {
            writeLinkProgress(*out, stage, BlenderProgress{}, step.message);
        }
        *last = step.stage;
    };
}

// A folder as text that compares equal for the same folder, whatever its slashes, case or final slash.
std::string folderKey(const std::filesystem::path& folder) {
    std::string text = pathToUtf8(folder.lexically_normal());
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!text.empty() && text.back() == '/') {
        text.pop_back();
    }
    return text;
}

// A stored target as JSON, or null when there is none.
Json targetJson(const std::optional<EngineTarget>& target) {
    if (!target) {
        return nullptr;
    }
    Json json = Json::object();
    json["project_name"] = target->projectName;
    json["project_root"] = pathToUtf8(target->projectRoot);
    json["engine_version"] = target->engineVersion;
    return json;
}

// A link's settings for a project: the two choices, the kit and gameplay switches, the sync and the target.
Json settingsJson(const EngineLinkOptions& options, const Project& project) {
    Json json = Json::object();
    json["pack_materials"] = options.packMaterials;
    json["grouped"] = options.grouped ? Json(*options.grouped) : Json(nullptr);
    json["grouped_now"] = linkGrouped(options, project);
    json["kit"] = options.kit;
    json["gameplay"] = options.gameplay;
    json["sync_after_build"] = options.syncAfterBuild;
    json["target"] = targetJson(options.target);
    return json;
}

// One editor that answered, and whether it has the project this building is linked to open.
Json editorJson(const EngineEditor& editor, const std::optional<EngineTarget>& target) {
    Json json = Json::object();
    json["id"] = editor.id;
    json["project_name"] = editor.target.projectName;
    json["project_root"] = pathToUtf8(editor.target.projectRoot);
    json["engine_version"] = editor.target.engineVersion;
    json["user"] = editor.user;
    json["machine"] = editor.machine;
    json["is_target"] = target && !target->projectRoot.empty() && !editor.target.projectRoot.empty() &&
                        folderKey(target->projectRoot) == folderKey(editor.target.projectRoot);
    return json;
}

// Reads the export options of a project, filling failed when the file cannot be read.
bool readOptions(const std::string& command, const std::filesystem::path& folder, ExportOptions& options,
                 Answer& failed) {
    const ExportOptionsRead read = readExportOptions(folder);
    if (!read.status.ok()) {
        failed = failure(command, read.status);
        return false;
    }
    options = read.options;
    return true;
}

// Lists the engine links, with this project's settings for each when the folder holds a project.
Answer linkList(const RunContext& context, const LinkArgs& args) {
    const std::string command = "link list";
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    std::optional<Project> project;
    ExportOptions options;
    if (!args.project.empty() || isProjectFolder(folder)) {
        Document document;
        Answer failed;
        if (!openProject(command, folder, document, failed, true) || !readOptions(command, folder, options, failed)) {
            return failed;
        }
        project = document.project();
    }
    Json links = Json::array();
    for (const EngineLink* link : allEngineLinks()) {
        Json entry = Json::object();
        entry["id"] = std::string(link->id());
        entry["name"] = std::string(link->name());
        entry["description"] = std::string(link->description());
        entry["profile"] = std::string(exportProfileName(link->profile()));
        entry["settings"] = project ? settingsJson(engineLinkOptions(options, link->id()), *project) : Json(nullptr);
        links.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["links"] = std::move(links);
    return success(command, std::move(result));
}

// Shows the editors that answered, the stored settings and target, and the last report of one link.
Answer linkStatus(const EngineLink& link, const RunContext& context, const LinkArgs& args) {
    const std::string id(link.id());
    const std::string command = "link " + id + " status";
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    Answer failed;
    ExportOptions stored;
    if (!openProject(command, folder, document, failed, true) || !readOptions(command, folder, stored, failed)) {
        return failed;
    }
    const EngineLinkOptions options = engineLinkOptions(stored, id);
    const EngineEditorSearch search = link.findEditors(kSendEditorWait, stopToken(context));
    if (search.status.code == ErrorCode::Cancelled) {
        return failure(command, search.status);
    }
    Json editors = Json::array();
    bool targetOpen = false;
    for (const EngineEditor& editor : search.editors) {
        Json entry = editorJson(editor, options.target);
        targetOpen = targetOpen || entry["is_target"].get<bool>();
        editors.push_back(std::move(entry));
    }
    Json lastReport = nullptr;
    Json reportProblem = nullptr;
    const EngineLinkReportRead read = readEngineLinkReport(folder, id);
    if (read.status.ok()) {
        lastReport = engineLinkReportToJson(read.report);
    } else if (read.status.code != ErrorCode::NotFound) {
        reportProblem = read.status.message;
    }
    Json result = Json::object();
    result["link"] = id;
    result["name"] = std::string(link.name());
    result["settings"] = settingsJson(options, document.project());
    result["editors"] = std::move(editors);
    result["search_problem"] = search.status.ok() ? Json(nullptr) : Json(search.status.message);
    result["target_open"] = targetOpen;
    result["last_report"] = std::move(lastReport);
    result["report_problem"] = std::move(reportProblem);
    return success(command, std::move(result));
}

// Sends the last build to the open editor through the link: the link's export, then the import in the editor.
Answer linkSend(const EngineLink& link, const RunContext& context, const LinkArgs& args) {
    const std::string id(link.id());
    const std::string command = "link " + id + " send";
    if (!args.screenshot.empty() && !args.selfCheck) {
        return badArguments(command, "bad_arguments", "--screenshot needs --self-check.");
    }
    Answer failed;
    if (refuseEmptyBlender(command, args.blender, failed)) {
        return failed;
    }
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    const BlenderChoice choice = chooseBlender(blenderOptionPath(context, args.blender), loaded.settings);
    if (!choice.status.ok()) {
        return failure(command, choice.status);
    }

    EngineLinkRequest request;
    request.projectFolder = folder;
    request.project = document.project();
    request.link = id;
    request.blender = choice.install;
    if (!linkLibrary(command, context, args.library, request.library, failed)) {
        return failed;
    }
    request.settings.force = args.force;
    request.editor = args.editor;
    request.selfCheck = args.selfCheck;
    if (!args.screenshot.empty()) {
        request.screenshot = userPath(context, args.screenshot);
    }

    const SessionKeeper keeper(folder);
    const EngineLinkReport report =
        runEngineLink(request, linkProgress(args.quiet ? nullptr : context.progress, ""), stopToken(context));
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, engineLinkReportToJson(report));
    for (const EngineCheckResult& check : report.checks) {
        if (check.status == "fail") {
            answer.warnings.push_back({"check_failed", "The check '" + check.id + "' failed in " +
                                                           std::string(link.name()) + ": " + check.message});
        }
    }
    return answer;
}

// Reads an on or off value. Empty when the option was not given.
std::optional<bool> onOff(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    return value == "on";
}

// Changes the link's settings that were given and answers with all of them.
Answer linkSet(const EngineLink& link, const RunContext& context, const LinkArgs& args) {
    const std::string id(link.id());
    const std::string command = "link " + id + " set";
    if (args.packMaterials.empty() && args.grouped.empty() && args.kit.empty() && args.gameplay.empty() &&
        args.sync.empty()) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --pack-materials, --grouped, --kit, --gameplay or --sync.");
    }
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    Answer failed;
    ExportOptions stored;
    if (!openProject(command, folder, document, failed, false) || !readOptions(command, folder, stored, failed)) {
        return failed;
    }
    const std::vector<std::filesystem::path> kept = backupFiles(folder);
    EngineLinkOptions options = engineLinkOptions(stored, id);
    if (const auto value = onOff(args.packMaterials)) {
        options.packMaterials = *value;
    }
    if (args.grouped == "auto") {
        options.grouped.reset();
    } else if (const auto value = onOff(args.grouped)) {
        options.grouped = *value;
    }
    if (const auto value = onOff(args.kit)) {
        options.kit = *value;
    }
    if (const auto value = onOff(args.gameplay)) {
        options.gameplay = *value;
    }
    if (const auto value = onOff(args.sync)) {
        options.syncAfterBuild = *value;
    }
    stored.links[id] = options;
    if (const Status written = writeExportOptions(folder, stored); !written.ok()) {
        return failure(command, written);
    }
    Json result = Json::object();
    result["link"] = id;
    result["settings"] = settingsJson(options, document.project());
    Answer answer = success(command, std::move(result));
    addNewBackups(answer, folder, kept);
    return answer;
}

// Forgets the editor project the building is linked to, so builds no longer update it.
Answer linkUnlink(const EngineLink& link, const RunContext& context, const LinkArgs& args) {
    const std::string id(link.id());
    const std::string command = "link " + id + " unlink";
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    Answer failed;
    ExportOptions stored;
    if (!openProject(command, folder, document, failed, false) || !readOptions(command, folder, stored, failed)) {
        return failed;
    }
    const std::optional<EngineTarget> before = engineLinkOptions(stored, id).target;
    const std::vector<std::filesystem::path> kept = backupFiles(folder);
    if (const Status status = unlinkEngine(folder, id); !status.ok()) {
        return failure(command, status);
    }
    if (!readOptions(command, folder, stored, failed)) {
        return failed;
    }
    Json result = Json::object();
    result["link"] = id;
    result["unlinked"] = before.has_value();
    result["target"] = targetJson(before);
    result["settings"] = settingsJson(engineLinkOptions(stored, id), document.project());
    Answer answer = success(command, std::move(result));
    addNewBackups(answer, folder, kept);
    return answer;
}

// Answers with the last report of the link, read from build/links/<id>/link_report.json.
Answer linkReport(const EngineLink& link, const RunContext& context, const LinkArgs& args) {
    const std::string id(link.id());
    const std::string command = "link " + id + " report";
    const std::filesystem::path folder = resolveProjectFolder(context, args.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const EngineLinkReportRead read = readEngineLinkReport(folder, id);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    return success(command, engineLinkReportToJson(read.report));
}

using LinkHandler = Answer (*)(const EngineLink&, const RunContext&, const LinkArgs&);

// Makes one command of a link. addOptions adds the options the command takes besides --project.
CommandSpec linkSpec(const EngineLink* link, const std::string& verb, std::string description, LinkHandler handler,
                     std::function<void(CLI::App&, LinkArgs&)> addOptions = {}) {
    CommandSpec spec;
    spec.words = {"link", std::string(link->id()), verb};
    spec.description = std::move(description);
    spec.setup = [link, handler, addOptions](CLI::App& sub) -> Handler {
        auto args = std::make_shared<LinkArgs>();
        addProjectOption(sub, args->project);
        if (addOptions) {
            addOptions(sub, *args);
        }
        return [link, handler, args](const RunContext& context) { return handler(*link, context, *args); };
    };
    return spec;
}

// Adds the options of send.
void addSendOptions(CLI::App& sub, LinkArgs& args) {
    addBlenderOption(sub, args.blender);
    addLibraryOption(sub, args.library);
    sub.add_option("--editor", args.editor,
                   "The editor to send to, by the id 'link status' lists. Default: the linked project's editor, or "
                   "the only one open.")
        ->type_name("id");
    sub.add_flag("--force", args.force, "Send even when a check of the link's export failed.");
    sub.add_flag("--self-check", args.selfCheck, "Let the editor check the import afterwards. For tests.");
    sub.add_option("--screenshot", args.screenshot, "With --self-check: save a picture of the level to this file.")
        ->type_name("file");
    sub.add_flag("--quiet", args.quiet, "Write no progress lines to standard error.");
}

// Adds the options of set.
void addSetOptions(CLI::App& sub, LinkArgs& args) {
    sub.add_option("--pack-materials", args.packMaterials,
                   "on: the materials and textures go over as ready material assets. off: meshes only.")
        ->check(CLI::IsMember({"on", "off"}));
    sub.add_option("--grouped", args.grouped,
                   "on: each export group is its own asset in the level. off: one building asset. auto: follow the "
                   "project's export mode.")
        ->check(CLI::IsMember({"on", "off", "auto"}));
    sub.add_option("--kit", args.kit, "Send the modular kit.")->check(CLI::IsMember({"on", "off"}));
    sub.add_option("--gameplay", args.gameplay, "Send the gameplay data file.")->check(CLI::IsMember({"on", "off"}));
    sub.add_option("--sync", args.sync, "Update the linked editor after every good build.")
        ->check(CLI::IsMember({"on", "off"}));
}

}

bool linkLibrary(const std::string& command, const RunContext& context, const std::string& libraryOption,
                 std::optional<LibraryPaths>& library, Answer& failed) {
    if (!libraryOption.empty()) {
        OpenedMaterialCatalog opened;
        if (!openMaterialCatalog(command, context, libraryOption, opened, failed)) {
            return false;
        }
        library = opened.paths;
        registerShapesOf(*library);
        return true;
    }
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        failed = failure(command, loaded.status);
        return false;
    }
    if (loaded.settings.library) {
        library = libraryPaths(*loaded.settings.library);
        registerShapesOf(*library);
    }
    return true;
}

void addLinkSync(Answer& answer, const EngineLinkRequest& base, std::ostream* progress, std::stop_token stop) {
    const std::vector<EngineLinkReport> reports = syncEngineLinks(base, linkProgress(progress, "link_"), stop);
    if (reports.empty()) {
        return;
    }
    Json links = Json::array();
    for (const EngineLinkReport& report : reports) {
        const EngineLink* link = findEngineLink(report.link);
        const std::string name = link != nullptr ? std::string(link->name()) : report.link;
        if (report.skipped) {
            answer.warnings.push_back({"link_skipped", report.skippedReason});
        } else if (!report.status.ok()) {
            answer.warnings.push_back({"link_failed", name + " was not updated: " + report.status.message});
        }
        links.push_back(engineLinkReportToJson(report));
    }
    answer.result["links"] = std::move(links);
}

std::vector<CommandSpec> linkCommands() {
    std::vector<CommandSpec> specs;
    CommandSpec list;
    list.words = {"link", "list"};
    list.description = "List the game engine links, with this project's settings for each when the folder holds a project.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto args = std::make_shared<LinkArgs>();
        addProjectOption(sub, args->project);
        return [args](const RunContext& context) { return linkList(context, *args); };
    };
    specs.push_back(std::move(list));

    for (const EngineLink* link : allEngineLinks()) {
        const std::string name(link->name());
        specs.push_back(linkSpec(link, "status",
                                 "Show the " + name + " editors that are open, the link's settings and the last report.",
                                 &linkStatus));
        specs.push_back(linkSpec(link, "send",
                                 "Send the last build to the project open in " + name +
                                     ". The first send links the building to that project.",
                                 &linkSend, &addSendOptions));
        specs.push_back(linkSpec(link, "set",
                                 "Change the " + name + " link's settings: --pack-materials, --grouped, --kit, "
                                 "--gameplay and --sync.",
                                 &linkSet, &addSetOptions));
        specs.push_back(linkSpec(link, "unlink",
                                 "Forget the " + name + " project this building is linked to, so builds no longer "
                                 "update it. What is already in the editor stays.",
                                 &linkUnlink));
        specs.push_back(linkSpec(link, "report", "Show the last report of the " + name + " link.", &linkReport));
    }
    return specs;
}

}
