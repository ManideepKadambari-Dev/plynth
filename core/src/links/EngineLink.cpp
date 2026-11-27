#include "plynth/core/EngineLink.h"

#include "BuildLockFiles.h"
#include "FileIo.h"

#include "plynth/core/BlenderJob.h"
#include "plynth/core/Build.h"
#include "plynth/core/ProjectFiles.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The registry: links by id, guarded because registration and lookup may meet on different threads.
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<EngineLink>, std::less<>> links;
};

// The one registry. Made on first use so registrars in any file can use it while the program starts.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedLinkId(std::string_view id)
{
    if (id.empty() || id[0] < 'a' || id[0] > 'z') return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// Holds build.lock while a link hands its files to the editor, and removes it at the end.
class DeliveryLock {
public:
    // Takes the lock. False when a live lock is there.
    bool take(const fs::path& file)
    {
        std::ostringstream text;
        const ProcessIdentity me = currentProcess();
        text << me.pid << " " << me.startKey << " " << lockfile::epochSeconds() << "\n";
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (lockfile::createExclusive(file, text.str())) {
                file_ = file;
                return true;
            }
            std::error_code ec;
            if (!fs::exists(file, ec)) continue;
            if (lockfile::lockIsLive(file)) return false;
            fs::remove(file, ec);
        }
        return false;
    }

    ~DeliveryLock()
    {
        if (file_.empty()) return;
        std::error_code ec;
        fs::remove(file_, ec);
    }

private:
    fs::path file_;
};

// Milliseconds since a start.
long long since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// A folder in a form that compares equal for the same folder, whatever its slashes, case or final slash.
std::string folderKey(const fs::path& folder)
{
    std::string text = pathToUtf8(folder.lexically_normal());
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!text.empty() && text.back() == '/') text.pop_back();
    return text;
}

// True when an editor has the stored project open.
bool sameTarget(const EngineTarget& a, const EngineTarget& b)
{
    return !a.projectRoot.empty() && folderKey(a.projectRoot) == folderKey(b.projectRoot);
}

// A path in the file form, or null when empty.
Json pathOrNull(const fs::path& path)
{
    return path.empty() ? Json(nullptr) : Json(pathToUtf8(path));
}

// The error code with this stable name. None when unknown.
ErrorCode errorCodeFromName(std::string_view name)
{
    for (const ErrorCode code : allErrorCodes()) {
        if (errorCodeName(code) == name) return code;
    }
    return ErrorCode::None;
}

// The editors a link can deliver into: those with a project open.
std::vector<EngineEditor> withProjects(const std::vector<EngineEditor>& editors)
{
    std::vector<EngineEditor> out;
    for (const EngineEditor& e : editors) {
        if (!e.target.projectName.empty() || !e.target.projectRoot.empty()) out.push_back(e);
    }
    return out;
}

// "MyGame (node-1), Castle (node-2)", for a message naming several editors.
std::string editorList(const std::vector<EngineEditor>& editors)
{
    std::string out;
    for (const EngineEditor& e : editors) {
        if (!out.empty()) out += ", ";
        out += (e.target.projectName.empty() ? std::string("a project") : e.target.projectName) + " (" + e.id + ")";
    }
    return out;
}

// Writes build/links/<id>/link_report.json.
Status writeLinkReport(const EngineLinkReport& report)
{
    std::error_code ec;
    fs::create_directories(report.reportFile.parent_path(), ec);
    const std::string text = engineLinkReportToJson(report).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return fileio::writeAtomic(report.reportFile, text);
}

// Stores the editor project a link delivered into, so later syncs go there and nowhere else.
Status storeTarget(const fs::path& projectFolder, std::string_view linkId, const EngineTarget& target)
{
    ExportOptionsRead read = readExportOptions(projectFolder);
    if (!read.status.ok()) return read.status;
    auto it = read.options.links.find(std::string(linkId));
    if (it == read.options.links.end()) it = read.options.links.emplace(std::string(linkId), EngineLinkOptions{}).first;
    if (it->second.target == target) return {};
    it->second.target = target;
    return writeExportOptions(projectFolder, read.options);
}

// Reads the part of a stored export report that the link report keeps: its outcome, files and counts.
ExportReport exportSummaryFromJson(const Json& json)
{
    ExportReport report;
    if (!json.is_object()) return report;
    const auto text = [&json](const char* key) {
        return json.contains(key) && json[key].is_string() ? json[key].get<std::string>() : std::string();
    };
    const auto flag = [&json](const char* key) {
        return json.contains(key) && json[key].is_boolean() && json[key].get<bool>();
    };
    if (json.contains("ok") && json["ok"].is_boolean() && !json["ok"].get<bool>() && json.contains("error") && json["error"].is_object()) {
        const Json& error = json["error"];
        report.status.code = errorCodeFromName(error.value("code", ""));
        if (report.status.code == ErrorCode::None) report.status.code = ErrorCode::BadFile;
        report.status.message = error.value("message", "");
        if (error.contains("worker_code") && error["worker_code"].is_string()) report.errorCode = error["worker_code"].get<std::string>();
    }
    report.exported = flag("exported");
    report.checkedOnly = flag("checked_only");
    report.forced = flag("forced");
    report.jobId = text("job_id");
    report.blenderVersion = text("blender_version");
    report.exporter = text("exporter");
    if (const auto profile = exportProfileFromName(text("profile"))) report.profile = *profile;
    if (json.contains("outputs") && json["outputs"].is_object()) {
        const Json& outputs = json["outputs"];
        if (outputs.contains("report_file") && outputs["report_file"].is_string()) {
            report.reportFile = pathFromUtf8(outputs["report_file"].get<std::string>());
        }
        if (outputs.contains("textures_folder") && outputs["textures_folder"].is_string()) {
            report.texturesFolder = pathFromUtf8(outputs["textures_folder"].get<std::string>());
        }
        if (outputs.contains("log_file") && outputs["log_file"].is_string()) {
            report.logFile = pathFromUtf8(outputs["log_file"].get<std::string>());
        }
        if (outputs.contains("texture_count") && outputs["texture_count"].is_number_integer()) {
            report.textureCount = outputs["texture_count"].get<int>();
        }
        if (outputs.contains("files") && outputs["files"].is_array()) {
            for (const Json& f : outputs["files"]) {
                if (!f.is_object() || !f.contains("file") || !f["file"].is_string()) continue;
                ExportFileOutput out;
                out.group = f.value("group", "");
                out.name = f.value("name", "");
                out.file = f["file"].get<std::string>();
                out.objects = f.value("objects", 0LL);
                out.triangles = f.value("triangles", 0LL);
                report.fileOutputs.push_back(out);
                if (!report.reportFile.empty()) report.files.push_back(report.reportFile.parent_path() / pathFromUtf8(out.file));
            }
        }
    }
    return report;
}

}

// The link folder of a project.
fs::path linkFolder(const fs::path& projectFolder, std::string_view linkId)
{
    return buildFolder(projectFolder) / pathFromUtf8(kLinksFolderName) / pathFromUtf8(linkId);
}

// Gives the stable name of a link stage.
std::string_view engineLinkStageName(EngineLinkStage stage)
{
    switch (stage) {
    case EngineLinkStage::Finding: return "finding";
    case EngineLinkStage::Preparing: return "preparing";
    case EngineLinkStage::Delivering: return "delivering";
    case EngineLinkStage::Done: return "done";
    }
    return "finding";
}

// Adds a link to the registry.
bool registerEngineLink(std::unique_ptr<EngineLink> link)
{
    if (!link) return false;
    const std::string id(link->id());
    if (!isWellFormedLinkId(id)) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    return r.links.emplace(id, std::move(link)).second;
}

// Registers a link while the program starts.
EngineLinkRegistrar::EngineLinkRegistrar(std::unique_ptr<EngineLink> link)
{
    registerEngineLink(std::move(link));
}

// Looks a link up by id.
const EngineLink* findEngineLink(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.links.find(id);
    return it == r.links.end() ? nullptr : it->second.get();
}

// Lists the links in id order.
std::vector<const EngineLink*> allEngineLinks()
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<const EngineLink*> out;
    for (const auto& entry : r.links) out.push_back(entry.second.get());
    return out;
}

// Finds the editor, prepares the files with the link's export profile, hands them over and records the run.
EngineLinkReport runEngineLink(const EngineLinkRequest& request, const std::function<void(const EngineLinkProgress&)>& onProgress,
                               std::stop_token stop)
{
    const auto started = std::chrono::steady_clock::now();
    EngineLinkReport report;
    report.link = request.link;
    report.startedAt = sessionNow();
    const auto progress = [&](EngineLinkStage stage, std::string message, const BlenderProgress& blender = {}) {
        if (!onProgress) return;
        EngineLinkProgress p;
        p.stage = stage;
        p.blender = blender;
        p.message = std::move(message);
        onProgress(p);
    };
    const auto done = [&]() -> EngineLinkReport {
        progress(EngineLinkStage::Done, report.status.ok() ? "Done." : report.status.message);
        return report;
    };
    const auto finish = [&]() -> EngineLinkReport {
        report.elapsedMs = since(started);
        return done();
    };
    const auto skip = [&](std::string reason) -> EngineLinkReport {
        report.skipped = true;
        report.skippedReason = std::move(reason);
        report.status = {};
        return finish();
    };

    const EngineLink* link = findEngineLink(request.link);
    if (!link) {
        report.status = fail(ErrorCode::NotFound, "There is no engine link named " + request.link + ".");
        return finish();
    }
    const std::string name(link->name());
    std::error_code ec;
    if (!fs::is_directory(request.projectFolder, ec)) {
        report.status = fail(ErrorCode::NotFound, "The project folder " + pathToUtf8(request.projectFolder) + " does not exist.");
        return finish();
    }
    if (!fs::is_regular_file(buildFolder(request.projectFolder) / pathFromUtf8("building.blend"), ec)) {
        report.status = fail(ErrorCode::NotFound, "Build the project first.");
        return finish();
    }
    const ExportOptionsRead read = readExportOptions(request.projectFolder);
    if (!read.status.ok()) {
        report.status = read.status;
        return finish();
    }
    const EngineLinkOptions options = engineLinkOptions(read.options, request.link);
    report.packMaterials = options.packMaterials;
    report.grouped = linkGrouped(options, request.project);
    report.target = options.target;
    if (request.sync && !options.target) return skip("This building is not linked to " + name + " yet, so it was not updated.");

    progress(EngineLinkStage::Finding, "Looking for " + name + ".");
    const EngineEditorSearch search = link->findEditors(request.sync ? kSyncEditorWait : kSendEditorWait, stop);
    if (search.status.code == ErrorCode::Cancelled || stop.stop_requested()) {
        report.status = fail(ErrorCode::Cancelled, "The link was stopped.");
        return finish();
    }
    if (!search.status.ok()) {
        if (request.sync) return skip(name + " could not be reached, so the building was not updated: " + search.status.message);
        report.status = search.status;
        return finish();
    }
    const std::vector<EngineEditor> open = withProjects(search.editors);
    const auto notOpen = name + " is not open, so the building was not updated.";
    std::optional<EngineEditor> chosen;
    if (!request.editor.empty()) {
        for (const EngineEditor& e : open) {
            if (e.id == request.editor) chosen = e;
        }
        if (!chosen) {
            if (request.sync) return skip(notOpen);
            report.status = fail(ErrorCode::EngineNotFound, "The " + name + " editor " + request.editor + " is not open with a project.");
            return finish();
        }
    } else if (options.target) {
        for (const EngineEditor& e : open) {
            if (!chosen && sameTarget(*options.target, e.target)) chosen = e;
        }
        if (!chosen && request.sync) {
            if (open.empty()) return skip(notOpen);
            return skip(name + " has another project open, not " + options.target->projectName + ", so the building was not updated.");
        }
        if (!chosen && open.size() == 1) chosen = open.front();
    } else if (open.size() == 1) {
        chosen = open.front();
    }
    if (!chosen) {
        if (open.empty()) {
            report.status = fail(ErrorCode::EngineNotFound, name + " is not open. Open your project in " + name +
                                                                 " with Python remote execution switched on, then send again.");
        } else {
            report.status = fail(ErrorCode::EngineNotFound, "Several " + name + " editors are open: " + editorList(open) +
                                                                 ". Choose the one to send to.");
        }
        return finish();
    }
    report.target = chosen->target;

    const fs::path work = linkFolder(request.projectFolder, request.link);
    Project project = request.project;
    project.settings.exportMode = report.grouped ? ExportMode::Grouped : ExportMode::Single;

    progress(EngineLinkStage::Preparing, "Preparing the building for " + name + ".");
    ExportRequest prepare;
    prepare.projectFolder = request.projectFolder;
    prepare.project = project;
    prepare.blender = request.blender;
    prepare.run = request.run;
    prepare.settings = request.settings;
    prepare.settings.profile = link->profile();
    prepare.library = request.library;
    prepare.outputFolder = work / pathFromUtf8(kLinkOutputFolder);
    prepare.kit = options.kit;
    prepare.gameplay = options.gameplay;
    report.reportFile = work / pathFromUtf8(kLinkReportFileName);
    report.exported = runExport(prepare, [&](const ExportProgress& p) {
        progress(EngineLinkStage::Preparing, "Preparing the building for " + name + ".", p.blender);
    }, stop);

    const auto record = [&]() -> EngineLinkReport {
        report.elapsedMs = since(started);
        if (const Status written = writeLinkReport(report); !written.ok() && report.status.ok()) report.status = written;
        return done();
    };
    if (!report.exported.status.ok()) {
        report.status = report.exported.status;
        return record();
    }
    if (!report.exported.exported) return record();

    DeliveryLock lock;
    if (!lock.take(buildFolder(request.projectFolder) / pathFromUtf8(kBuildLockFileName))) {
        report.status = fail(ErrorCode::InUse, "A build or an export of this project is running.");
        return record();
    }
    EngineDelivery delivery;
    delivery.projectFolder = request.projectFolder;
    delivery.project = project;
    delivery.options = options;
    delivery.grouped = report.grouped;
    delivery.editor = *chosen;
    delivery.exported = &report.exported;
    delivery.workFolder = work;
    delivery.jobId = newJobId("link");
    delivery.selfCheck = request.selfCheck;
    delivery.screenshot = request.screenshot;
    report.jobId = delivery.jobId;
    progress(EngineLinkStage::Delivering, "Sending the building to " + name + ".");
    const Status delivered = link->deliver(delivery, report, [&](const EngineLinkProgress& p) {
        if (onProgress) onProgress(p);
    }, stop);
    if (!delivered.ok()) {
        report.status = delivered;
        return record();
    }
    report.status = storeTarget(request.projectFolder, request.link, chosen->target);
    return record();
}

// Updates every linked editor after a good build.
std::vector<EngineLinkReport> syncEngineLinks(const EngineLinkRequest& base, const std::function<void(const EngineLinkProgress&)>& onProgress,
                                              std::stop_token stop)
{
    std::vector<EngineLinkReport> out;
    const ExportOptionsRead read = readExportOptions(base.projectFolder);
    if (!read.status.ok()) return out;
    for (const EngineLink* link : allEngineLinks()) {
        const EngineLinkOptions options = engineLinkOptions(read.options, link->id());
        if (!options.syncAfterBuild || !options.target) continue;
        if (stop.stop_requested()) break;
        EngineLinkRequest request = base;
        request.link = std::string(link->id());
        request.editor.clear();
        request.sync = true;
        out.push_back(runEngineLink(request, onProgress, stop));
    }
    return out;
}

// Forgets the editor project a link delivers into.
Status unlinkEngine(const fs::path& projectFolder, std::string_view linkId)
{
    ExportOptionsRead read = readExportOptions(projectFolder);
    if (!read.status.ok()) return read.status;
    const auto it = read.options.links.find(std::string(linkId));
    if (it == read.options.links.end() || !it->second.target) return {};
    it->second.target.reset();
    if (it->second == EngineLinkOptions{}) read.options.links.erase(it);
    return writeExportOptions(projectFolder, read.options);
}

// Gives the report in its file form.
Json engineLinkReportToJson(const EngineLinkReport& report)
{
    Json json = Json::object();
    json["format"] = std::string(kLinkReportFormat);
    json["version"] = kLinkReportVersion;
    json["link"] = report.link;
    json["job_id"] = report.jobId;
    json["status"] = Json{{"code", std::string(errorCodeName(report.status.code))}, {"message", report.status.message}};
    json["started_at"] = formatSessionTime(report.startedAt);
    json["elapsed_ms"] = std::max(0LL, report.elapsedMs);
    json["skipped"] = report.skipped;
    json["skipped_reason"] = report.skippedReason;
    if (report.target) {
        json["target"] = Json{{"project_name", report.target->projectName},
                              {"project_root", pathToUtf8(report.target->projectRoot)},
                              {"engine_version", report.target->engineVersion}};
    } else {
        json["target"] = nullptr;
    }
    json["content_path"] = report.contentPath;
    json["pack_materials"] = report.packMaterials;
    json["grouped"] = report.grouped;
    json["export"] = report.exported.jobId.empty() && report.exported.status.ok() ? Json(nullptr) : exportReportToJson(report.exported);
    Json assets = Json::array();
    for (const EngineAssetResult& a : report.assets) assets.push_back(Json{{"path", a.path}, {"kind", a.kind}, {"action", a.action}});
    json["assets"] = std::move(assets);
    Json actors = Json::array();
    for (const EngineActorResult& a : report.actors) actors.push_back(Json{{"label", a.label}, {"asset", a.asset}, {"action", a.action}});
    json["actors"] = std::move(actors);
    if (!report.checks.empty()) {
        Json checks = Json::array();
        for (const EngineCheckResult& c : report.checks) checks.push_back(Json{{"id", c.id}, {"status", c.status}, {"message", c.message}});
        json["checks"] = std::move(checks);
    }
    json["screenshot"] = pathOrNull(report.screenshot);
    json["report_file"] = pathToUtf8(report.reportFile);
    json["log_file"] = pathOrNull(report.logFile);
    return json;
}

// Reads the last link report of a project.
EngineLinkReportRead readEngineLinkReport(const fs::path& projectFolder, std::string_view linkId)
{
    EngineLinkReportRead result;
    const fs::path file = linkFolder(projectFolder, linkId) / pathFromUtf8(kLinkReportFileName);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.status = fail(ErrorCode::NotFound, "There is no link report in " + pathToUtf8(file.parent_path()) + ".");
        return result;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The link report " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    const Json json = Json::parse(in, nullptr, false);
    const auto bad = [&](const std::string& where) {
        EngineLinkReportRead out;
        out.status = fail(ErrorCode::BadFile, "The link report is not valid: the field " + where + " is missing or has the wrong type.", where);
        return out;
    };
    if (json.is_discarded() || !json.is_object()) return bad("");

    try {
        const auto member = [](const Json& object, const char* key, const std::string& where) -> const Json& {
            if (!object.is_object() || !object.contains(key)) throw std::string(where + "/" + key);
            return object[key];
        };
        const auto text = [&](const Json& object, const char* key, const std::string& where) -> std::string {
            const Json& v = member(object, key, where);
            if (!v.is_string()) throw std::string(where + "/" + key);
            return v.get<std::string>();
        };
        const auto optionalPath = [&](const Json& object, const char* key) -> fs::path {
            if (!object.contains(key) || object[key].is_null()) return {};
            if (!object[key].is_string()) throw std::string(std::string("/") + key);
            return pathFromUtf8(object[key].get<std::string>());
        };
        const auto flag = [&](const Json& object, const char* key) -> bool {
            const Json& v = member(object, key, "");
            if (!v.is_boolean()) throw std::string(std::string("/") + key);
            return v.get<bool>();
        };
        const auto array = [&](const Json& object, const char* key) -> const Json& {
            const Json& v = member(object, key, "");
            if (!v.is_array()) throw std::string(std::string("/") + key);
            return v;
        };

        if (text(json, "format", "") != kLinkReportFormat) throw std::string("/format");
        if (member(json, "version", "") != kLinkReportVersion) throw std::string("/version");
        EngineLinkReport& report = result.report;
        report.link = text(json, "link", "");
        report.jobId = text(json, "job_id", "");
        const Json& status = member(json, "status", "");
        const std::string code = text(status, "code", "/status");
        if (code != "ok") {
            report.status.code = errorCodeFromName(code);
            if (report.status.code == ErrorCode::None) throw std::string("/status/code");
        }
        report.status.message = text(status, "message", "/status");
        const std::optional<SessionTime> when = parseSessionTime(text(json, "started_at", ""));
        if (!when) throw std::string("/started_at");
        report.startedAt = *when;
        const Json& elapsed = member(json, "elapsed_ms", "");
        if (!elapsed.is_number_integer() || elapsed.get<long long>() < 0) throw std::string("/elapsed_ms");
        report.elapsedMs = elapsed.get<long long>();
        report.skipped = flag(json, "skipped");
        report.skippedReason = text(json, "skipped_reason", "");
        const Json& target = member(json, "target", "");
        if (target.is_object()) {
            EngineTarget t;
            t.projectName = text(target, "project_name", "/target");
            t.projectRoot = pathFromUtf8(text(target, "project_root", "/target"));
            t.engineVersion = text(target, "engine_version", "/target");
            report.target = t;
        } else if (!target.is_null()) {
            throw std::string("/target");
        }
        report.contentPath = text(json, "content_path", "");
        report.packMaterials = flag(json, "pack_materials");
        report.grouped = flag(json, "grouped");
        const Json& exported = member(json, "export", "");
        if (!exported.is_null() && !exported.is_object()) throw std::string("/export");
        report.exported = exportSummaryFromJson(exported);
        const Json& assets = array(json, "assets");
        for (std::size_t i = 0; i < assets.size(); ++i) {
            const std::string where = "/assets/" + std::to_string(i);
            report.assets.push_back({text(assets[i], "path", where), text(assets[i], "kind", where), text(assets[i], "action", where)});
        }
        const Json& actors = array(json, "actors");
        for (std::size_t i = 0; i < actors.size(); ++i) {
            const std::string where = "/actors/" + std::to_string(i);
            report.actors.push_back({text(actors[i], "label", where), text(actors[i], "asset", where), text(actors[i], "action", where)});
        }
        if (json.contains("checks")) {
            const Json& checks = array(json, "checks");
            for (std::size_t i = 0; i < checks.size(); ++i) {
                const std::string where = "/checks/" + std::to_string(i);
                report.checks.push_back({text(checks[i], "id", where), text(checks[i], "status", where), text(checks[i], "message", where)});
            }
        }
        report.screenshot = optionalPath(json, "screenshot");
        report.reportFile = pathFromUtf8(text(json, "report_file", ""));
        report.logFile = optionalPath(json, "log_file");
    } catch (const std::string& where) {
        return bad(where);
    }
    return result;
}

}
