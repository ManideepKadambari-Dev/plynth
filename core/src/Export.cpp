#include "plynth/core/Export.h"

#include "FileIo.h"

#include "plynth/core/Build.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Seconds since 1970 now.
long long epochSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Makes the lock file only when it does not exist yet. False when it exists or cannot be made.
bool createExclusive(const fs::path& file, const std::string& text)
{
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(handle);
    return true;
}

// The plan the last build was made from, read from build/job.json. Empty when the file is missing or unusable.
std::optional<Plan> builtPlan(const fs::path& projectFolder)
{
    std::ifstream in(buildFolder(projectFolder) / pathFromUtf8(kJobFileName), std::ios::binary);
    if (!in) return std::nullopt;
    const Json job = Json::parse(in, nullptr, false);
    if (!job.is_object() || !job.contains("params") || !job["params"].is_object()) return std::nullopt;
    if (!job["params"].contains("plan")) return std::nullopt;
    PlanRead read = planFromJson(job["params"]["plan"]);
    if (!read.status.ok()) return std::nullopt;
    return std::move(read.plan);
}

// Whether an existing lock still counts: younger than kBuildTimeout by its time and its write time, and, when it
// names a process, that process is still running. A lock whose content cannot be read counts by its write time.
bool lockIsLive(const fs::path& file)
{
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    if (!ec && fs::file_time_type::clock::now() - written > kBuildTimeout) return false;

    std::ifstream in(file, std::ios::binary);
    ProcessIdentity who;
    long long started = 0;
    if (in >> who.pid >> who.startKey >> started) {
        if (epochSeconds() - started > kBuildTimeout.count()) return false;
        return isProcessRunning(who);
    }
    return true;
}

// Holds build.lock, the build's lock, while an export runs and removes it at the end.
class ExportLock {
public:
    // Takes the lock. False when a live lock is there.
    bool take(const fs::path& file)
    {
        std::ostringstream text;
        const ProcessIdentity me = currentProcess();
        text << me.pid << " " << me.startKey << " " << epochSeconds() << "\n";
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (createExclusive(file, text.str())) {
                file_ = file;
                return true;
            }
            std::error_code ec;
            if (!fs::exists(file, ec)) continue;
            if (lockIsLive(file)) return false;
            fs::remove(file, ec);
        }
        return false;
    }

    ~ExportLock()
    {
        if (file_.empty()) return;
        std::error_code ec;
        fs::remove(file_, ec);
    }

private:
    fs::path file_;
};

// A path in the file form, or null when empty.
Json pathOrNull(const fs::path& path)
{
    return path.empty() ? Json(nullptr) : Json(pathToUtf8(path));
}

// The error code with this stable name. None when unknown.
ErrorCode errorCodeFromName(std::string_view name)
{
    for (int i = 0; i <= static_cast<int>(ErrorCode::QualityFailed); ++i) {
        const ErrorCode code = static_cast<ErrorCode>(i);
        if (errorCodeName(code) == name) return code;
    }
    return ErrorCode::None;
}

// Milliseconds since a start.
long long since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// Writes export/export_report.json.
Status writeReport(const fs::path& folder, const ExportReport& report)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    const std::string text = exportReportToJson(report).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return fileio::writeAtomic(folder / pathFromUtf8(kExportReportFileName), text);
}

// Removes a folder and everything in it, quietly.
void removeTree(const fs::path& folder)
{
    std::error_code ec;
    fs::remove_all(folder, ec);
}

// Moves a file or folder, copying when a rename is not possible.
bool moveInto(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove_all(from, ec);
    return true;
}

// Says which checks failed, for the message of a blocked export.
std::string failedCheckNames(const std::vector<QualityCheckResult>& checks)
{
    std::string names;
    for (const QualityCheckResult& c : checks) {
        if (c.status != QualityStatus::Fail) continue;
        if (!names.empty()) names += ", ";
        names += c.name;
    }
    return names;
}

}

// projectFolder / "export".
fs::path exportFolder(const fs::path& projectFolder)
{
    return projectFolder / pathFromUtf8(kExportFolderName);
}

// "preparing", "blender", "finishing" or "done".
std::string_view exportStageName(ExportStage stage)
{
    switch (stage) {
    case ExportStage::Preparing: return "preparing";
    case ExportStage::Blender: return "blender";
    case ExportStage::Finishing: return "finishing";
    case ExportStage::Done: return "done";
    }
    return "preparing";
}

// How many checks failed.
int ExportReport::failCount() const
{
    return static_cast<int>(std::count_if(checks.begin(), checks.end(),
                                          [](const QualityCheckResult& c) { return c.status == QualityStatus::Fail; }));
}

// How many checks gave a warning.
int ExportReport::warningCount() const
{
    return static_cast<int>(std::count_if(checks.begin(), checks.end(),
                                          [](const QualityCheckResult& c) { return c.status == QualityStatus::Warning; }));
}

// Says whether no check failed.
bool ExportReport::passed() const
{
    return failCount() == 0;
}

// Exports a project.
ExportReport runExport(const ExportRequest& request, const std::function<void(const ExportProgress&)>& onProgress,
                       std::stop_token stop)
{
    const auto started = std::chrono::steady_clock::now();
    ExportReport report;
    report.forced = request.settings.force;
    report.checkedOnly = !request.settings.write;
    const auto progress = [&](ExportStage stage, const BlenderProgress& blender = {}) {
        if (!onProgress) return;
        ExportProgress p;
        p.stage = stage;
        p.blender = blender;
        onProgress(p);
    };
    progress(ExportStage::Preparing);

    std::error_code ec;
    if (!fs::is_directory(request.projectFolder, ec)) {
        report.status = fail(ErrorCode::NotFound, "The project folder " + pathToUtf8(request.projectFolder) + " does not exist.");
        return report;
    }
    const std::string exporterId(exportModeName(request.project.settings.exportMode));
    report.exporter = exporterId;
    const Exporter* exporter = findExporter(exporterId);
    if (!exporter) {
        const std::string mode = exporterId.empty() ? "This" : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(exporterId[0])))) + exporterId.substr(1);
        report.status = fail(ErrorCode::InvalidValue, mode + " export is not available yet. Set the export mode of the project to single.");
        return report;
    }
    const fs::path blendFile = buildFolder(request.projectFolder) / pathFromUtf8("building.blend");
    if (!fs::is_regular_file(blendFile, ec)) {
        report.status = fail(ErrorCode::NotFound, "Build the project first.");
        return report;
    }
    if (!request.blender.usable()) {
        report.status = fail(ErrorCode::BlenderNotFound, "There is no usable Blender to export with.");
        return report;
    }
    std::vector<ExportGroup> groups;
    if (report.status = exporter->groups(request.project, groups); !report.status.ok()) return report;
    if (groups.empty()) {
        report.status = fail(ErrorCode::InvalidValue, "The exporter has no file to write.");
        return report;
    }

    const fs::path build = buildFolder(request.projectFolder);
    const fs::path exportDir = exportFolder(request.projectFolder);
    const fs::path staging = exportDir / pathFromUtf8(kExportStagingFolder);
    const fs::path work = build / pathFromUtf8(kExportWorkFolder);
    report.reportFile = exportDir / pathFromUtf8(kExportReportFileName);

    ExportLock lock;
    if (!lock.take(build / pathFromUtf8(kBuildLockFileName))) {
        report.status = fail(ErrorCode::InUse, "A build or another export of this project is running.");
        return report;
    }

    report.jobId = newJobId("export");
    report.startedAt = sessionNow();
    report.planFingerprint = planFingerprint(request.project);
    report.blenderVersion = request.blender.version ? formatBlenderVersion(*request.blender.version) : std::string();

    // Leaves the report written and the staging folder gone, whatever went wrong.
    const auto finish = [&](bool writeIt) {
        removeTree(staging);
        report.elapsedMs = since(started);
        if (!writeIt) return;
        if (const Status written = writeReport(exportDir, report); !written.ok() && report.status.ok()) report.status = written;
    };

    const auto prepareStart = std::chrono::steady_clock::now();
    ExportBuildState buildState;
    const BuildReportRead built = readBuildReport(request.projectFolder);
    if (built.status.ok()) {
        buildState.reportFound = true;
        buildState.planChanged = built.report.planFingerprint != report.planFingerprint;
        buildState.problems = built.report.problems;
        report.buildFingerprint = built.report.planFingerprint;
    }
    const BuiltMaterialsRead materials = readBuiltMaterials(request.projectFolder);

    removeTree(staging);
    fs::create_directories(staging, ec);
    fs::create_directories(work, ec);
    if (ec || !fs::is_directory(staging)) {
        report.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(staging) + " could not be created.");
        finish(true);
        return report;
    }
    // The scale check compares the built walls, so the expected bounds come from the plan that was built.
    Project boundsSource = request.project;
    if (const std::optional<Plan> plan = builtPlan(request.projectFolder)) boundsSource.plan = *plan;
    const Json job = makeExportJob(boundsSource, groups, materials.status.ok() ? materials.materials : std::vector<BuiltMaterial>{},
                                   buildState, request.settings, request.projectFolder, staging, report.jobId,
                                   work / pathFromUtf8(kResultFileName));
    report.steps.push_back({"prepare", since(prepareStart)});

    progress(ExportStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        request.blender.executable, job, work, request.run, [&](const BlenderProgress& p) { progress(ExportStage::Blender, p); }, stop);
    report.logFile = work / pathFromUtf8(kBlenderLogFileName);

    progress(ExportStage::Finishing);
    if (!run.status.ok()) {
        report.status = run.status;
        report.errorCode = run.errorCode;
        if (run.status.code == ErrorCode::BlenderFailed) report.status.message += "\nThe log is in " + pathToUtf8(report.logFile);
        finish(!(run.status.code == ErrorCode::Cancelled && stop.stop_requested()));
        return report;
    }

    const ExportWorkerOutput out = readExportResult(run.result, report.jobId);
    if (!out.header.status.ok()) {
        report.status = fail(ErrorCode::BlenderFailed, out.header.status.message + "\nThe log is in " + pathToUtf8(report.logFile));
        report.errorCode = out.header.errorCode;
        finish(true);
        return report;
    }
    report.blenderVersion = out.header.blenderVersion;
    report.objects = out.objects;
    report.triangles = out.triangles;
    report.collisionObjects = out.collisionObjects;
    report.checks = out.checks;
    report.steps.insert(report.steps.end(), out.steps.begin(), out.steps.end());

    std::vector<std::string> failed;
    for (const QualityCheckResult& c : report.checks) {
        if (c.status == QualityStatus::Fail) failed.push_back(c.id);
    }

    if (!request.settings.write) {
        finish(true);
        if (report.status.ok()) progress(ExportStage::Done);
        return report;
    }
    if (!failed.empty() && !request.settings.force) {
        report.status = fail(ErrorCode::QualityFailed, "The export was blocked because these checks failed: " + failedCheckNames(report.checks) +
                                                           ". Nothing in the export folder was changed.");
        finish(true);
        return report;
    }
    if (!out.written || out.files.empty()) {
        report.status = fail(ErrorCode::BlenderFailed, "Blender did not write the export files.\nThe log is in " + pathToUtf8(report.logFile));
        finish(true);
        return report;
    }
    for (const ExportFileOutput& file : out.files) {
        if (!fs::is_regular_file(staging / pathFromUtf8(file.file), ec)) {
            report.status = fail(ErrorCode::BlenderFailed, "Blender did not write " + file.file + ".\nThe log is in " + pathToUtf8(report.logFile));
            finish(true);
            return report;
        }
    }

    // The last good export goes only now: the files its report lists, then the old textures.
    const ExportReportRead previous = readExportReport(request.projectFolder);
    if (previous.status.ok()) {
        for (const fs::path& old : previous.report.files) fs::remove(exportDir / old.filename(), ec);
    }
    const fs::path textures = exportDir / pathFromUtf8(kExportTexturesFolder);
    removeTree(textures);
    bool moved = true;
    for (const ExportFileOutput& file : out.files) {
        const fs::path target = exportDir / pathFromUtf8(file.file);
        fs::remove(target, ec);
        moved = moveInto(staging / pathFromUtf8(file.file), target) && moved;
        report.files.push_back(target);
        report.fileOutputs.push_back(file);
    }
    const fs::path stagedTextures = staging / pathFromUtf8(kExportTexturesFolder);
    if (fs::is_directory(stagedTextures, ec)) {
        moved = moveInto(stagedTextures, textures) && moved;
    } else {
        fs::create_directories(textures, ec);
    }
    if (!moved) {
        report.files.clear();
        report.fileOutputs.clear();
        report.status = fail(ErrorCode::CannotWrite, "The exported files could not be moved into " + pathToUtf8(exportDir) + ".");
        finish(true);
        return report;
    }
    report.exported = true;
    report.texturesFolder = textures;
    report.textureCount = static_cast<int>(out.textures.size());
    if (request.settings.force) report.overridden = failed;

    finish(true);
    if (report.status.ok()) progress(ExportStage::Done);
    return report;
}

// The report in the file form.
Json exportReportToJson(const ExportReport& report)
{
    Json json = Json::object();
    json["format"] = std::string(kExportReportFormat);
    json["version"] = kExportReportVersion;
    json["ok"] = report.status.ok();
    if (report.status.ok()) {
        json["error"] = nullptr;
    } else {
        Json error = Json::object();
        error["code"] = std::string(errorCodeName(report.status.code));
        error["message"] = report.status.message;
        error["worker_code"] = report.errorCode.empty() ? Json(nullptr) : Json(report.errorCode);
        json["error"] = std::move(error);
    }
    json["exported"] = report.exported;
    json["checked_only"] = report.checkedOnly;
    json["forced"] = report.forced;
    json["overridden"] = report.overridden;
    json["job_id"] = report.jobId;
    json["started_at"] = formatSessionTime(report.startedAt);
    json["elapsed_ms"] = report.elapsedMs;
    json["blender_version"] = report.blenderVersion;
    json["exporter"] = report.exporter;
    json["plan_fingerprint"] = report.planFingerprint;
    json["build_fingerprint"] = report.buildFingerprint.empty() ? Json(nullptr) : Json(report.buildFingerprint);

    Json outputs = Json::object();
    Json files = Json::array();
    for (const ExportFileOutput& f : report.fileOutputs) {
        files.push_back(Json{{"group", f.group}, {"file", f.file}, {"objects", f.objects}, {"triangles", f.triangles}});
    }
    outputs["files"] = std::move(files);
    outputs["textures_folder"] = pathOrNull(report.texturesFolder);
    outputs["texture_count"] = report.textureCount;
    outputs["report_file"] = pathToUtf8(report.reportFile);
    outputs["log_file"] = pathOrNull(report.logFile);
    json["outputs"] = std::move(outputs);

    json["objects"] = report.objects;
    json["triangles"] = report.triangles;
    json["collision_objects"] = report.collisionObjects;

    Json checks = Json::array();
    for (const QualityCheckResult& c : report.checks) {
        Json findings = Json::array();
        for (const QualityFinding& f : c.findings) {
            findings.push_back(Json{{"object", f.object.empty() ? Json(nullptr) : Json(f.object)},
                                    {"plan_id", f.planId.empty() ? Json(nullptr) : Json(f.planId)},
                                    {"message", f.message}});
        }
        checks.push_back(Json{{"id", c.id},
                              {"name", c.name},
                              {"phase", std::string(qualityPhaseName(c.phase))},
                              {"status", std::string(qualityStatusName(c.status))},
                              {"message", c.message},
                              {"findings", std::move(findings)}});
    }
    json["quality"] = Json{{"passed", report.passed()},
                           {"fail_count", report.failCount()},
                           {"warning_count", report.warningCount()},
                           {"checks", std::move(checks)}};

    Json steps = Json::array();
    for (const BuildStepTime& s : report.steps) steps.push_back(Json{{"step", s.step}, {"ms", s.ms}});
    json["steps"] = std::move(steps);
    return json;
}

// Reads export/export_report.json.
ExportReportRead readExportReport(const fs::path& projectFolder)
{
    ExportReportRead result;
    const fs::path file = exportFolder(projectFolder) / pathFromUtf8(kExportReportFileName);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.status = fail(ErrorCode::NotFound, "There is no export report in " + pathToUtf8(exportFolder(projectFolder)) + ".");
        return result;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The export report " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    const Json json = Json::parse(in, nullptr, false);
    const auto bad = [&](const std::string& why, const std::string& where) {
        result.status = fail(ErrorCode::BadFile, "The export report is not valid: " + why, where);
        result.report = {};
        return result;
    };
    if (json.is_discarded() || !json.is_object()) return bad("It is not a JSON object.", "");

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
        const auto optionalText = [&](const Json& object, const char* key, const std::string& where) -> std::string {
            const Json& v = member(object, key, where);
            if (v.is_null()) return {};
            if (!v.is_string()) throw std::string(where + "/" + key);
            return v.get<std::string>();
        };
        const auto whole = [&](const Json& object, const char* key, const std::string& where) -> long long {
            const Json& v = member(object, key, where);
            if (!v.is_number_integer() || v.get<long long>() < 0) throw std::string(where + "/" + key);
            return v.get<long long>();
        };
        const auto flag = [&](const Json& object, const char* key, const std::string& where) -> bool {
            const Json& v = member(object, key, where);
            if (!v.is_boolean()) throw std::string(where + "/" + key);
            return v.get<bool>();
        };
        const auto array = [&](const Json& object, const char* key, const std::string& where) -> const Json& {
            const Json& v = member(object, key, where);
            if (!v.is_array()) throw std::string(where + "/" + key);
            return v;
        };

        if (text(json, "format", "") != kExportReportFormat) throw std::string("/format");
        if (!json.contains("version") || json["version"] != kExportReportVersion) throw std::string("/version");
        ExportReport& report = result.report;
        if (!flag(json, "ok", "")) {
            const Json& error = member(json, "error", "");
            if (!error.is_object()) throw std::string("/error");
            report.status.code = errorCodeFromName(text(error, "code", "/error"));
            if (report.status.code == ErrorCode::None) throw std::string("/error/code");
            report.status.message = text(error, "message", "/error");
            report.errorCode = optionalText(error, "worker_code", "/error");
        }
        report.exported = flag(json, "exported", "");
        report.checkedOnly = flag(json, "checked_only", "");
        report.forced = flag(json, "forced", "");
        for (const Json& id : array(json, "overridden", "")) {
            if (!id.is_string()) throw std::string("/overridden");
            report.overridden.push_back(id.get<std::string>());
        }
        report.jobId = text(json, "job_id", "");
        const auto time = parseSessionTime(text(json, "started_at", ""));
        if (!time) throw std::string("/started_at");
        report.startedAt = *time;
        report.elapsedMs = whole(json, "elapsed_ms", "");
        report.blenderVersion = text(json, "blender_version", "");
        report.exporter = text(json, "exporter", "");
        report.planFingerprint = text(json, "plan_fingerprint", "");
        report.buildFingerprint = optionalText(json, "build_fingerprint", "");

        const Json& outputs = member(json, "outputs", "");
        if (!outputs.is_object()) throw std::string("/outputs");
        const Json& files = array(outputs, "files", "/outputs");
        const fs::path folder = exportFolder(projectFolder);
        for (std::size_t i = 0; i < files.size(); ++i) {
            const std::string where = "/outputs/files/" + std::to_string(i);
            ExportFileOutput f;
            f.group = text(files[i], "group", where);
            f.file = text(files[i], "file", where);
            f.objects = whole(files[i], "objects", where);
            f.triangles = whole(files[i], "triangles", where);
            report.files.push_back(folder / pathFromUtf8(f.file));
            report.fileOutputs.push_back(std::move(f));
        }
        const std::string textures = optionalText(outputs, "textures_folder", "/outputs");
        if (!textures.empty()) report.texturesFolder = pathFromUtf8(textures);
        report.textureCount = static_cast<int>(whole(outputs, "texture_count", "/outputs"));
        report.reportFile = pathFromUtf8(text(outputs, "report_file", "/outputs"));
        const std::string log = optionalText(outputs, "log_file", "/outputs");
        if (!log.empty()) report.logFile = pathFromUtf8(log);

        report.objects = whole(json, "objects", "");
        report.triangles = whole(json, "triangles", "");
        report.collisionObjects = whole(json, "collision_objects", "");

        const Json& quality = member(json, "quality", "");
        if (!quality.is_object()) throw std::string("/quality");
        const Json& checks = array(quality, "checks", "/quality");
        for (std::size_t i = 0; i < checks.size(); ++i) {
            const std::string where = "/quality/checks/" + std::to_string(i);
            QualityCheckResult c;
            c.id = text(checks[i], "id", where);
            c.name = text(checks[i], "name", where);
            const auto phase = qualityPhaseFromName(text(checks[i], "phase", where));
            if (!phase) throw std::string(where + "/phase");
            c.phase = *phase;
            const auto status = qualityStatusFromName(text(checks[i], "status", where));
            if (!status) throw std::string(where + "/status");
            c.status = *status;
            c.message = text(checks[i], "message", where);
            const Json& findings = array(checks[i], "findings", where);
            for (std::size_t k = 0; k < findings.size(); ++k) {
                const std::string at = where + "/findings/" + std::to_string(k);
                QualityFinding f;
                f.object = optionalText(findings[k], "object", at);
                f.planId = optionalText(findings[k], "plan_id", at);
                f.message = text(findings[k], "message", at);
                c.findings.push_back(std::move(f));
            }
            report.checks.push_back(std::move(c));
        }
        const Json& steps = array(json, "steps", "");
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const std::string where = "/steps/" + std::to_string(i);
            report.steps.push_back({text(steps[i], "step", where), whole(steps[i], "ms", where)});
        }
    } catch (const std::string& where) {
        return bad("The field " + where + " is missing or has the wrong type.", where);
    }
    return result;
}

}
