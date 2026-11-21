#include "plynth/core/Build.h"

#include "FileIo.h"

#include "plynth/core/Openings.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"
#include "plynth/core/catalog/AssetInfo.h"

#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
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

// Whether an existing lock still counts: younger than kBuildTimeout, by the time it holds and by the file's
// own write time, and, when it names a process, that process is still running. A lock whose content cannot
// be read counts by its write time alone.
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

// Holds build.lock while a build runs and removes it at the end.
class BuildLock {
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

    ~BuildLock()
    {
        if (file_.empty()) return;
        std::error_code ec;
        fs::remove(file_, ec);
    }

private:
    fs::path file_;
};

// 64 bit FNV-1a.
std::uint64_t fnv1a(std::string_view data)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : data) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// The wall, if the id names one of the floor's.
Vec2 middleOfWall(const Floor& floor, const Wall& wall)
{
    const WallCurve curve = wallCurve(floor, wall);
    if (!curve.valid) return {};
    return pointAlong(curve, curve.length / 2.0);
}

// A path in the file form, or null when empty.
Json pathOrNull(const fs::path& path)
{
    return path.empty() ? Json(nullptr) : Json(pathToUtf8(path));
}

// The error code with this stable name. None when unknown.
ErrorCode errorCodeFromName(std::string_view name)
{
    for (int i = 0; i <= static_cast<int>(ErrorCode::DatabaseError); ++i) {
        const ErrorCode code = static_cast<ErrorCode>(i);
        if (errorCodeName(code) == name) return code;
    }
    return ErrorCode::None;
}

// Writes the report of a build to build/build.json.
Status writeReport(const fs::path& folder, const BuildReport& report)
{
    const std::string text = buildReportToJson(report).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return fileio::writeAtomic(folder / pathFromUtf8(kBuildReportFileName), text);
}

// Milliseconds since a start.
long long since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

}

// projectFolder / "build".
fs::path buildFolder(const fs::path& projectFolder)
{
    return projectFolder / pathFromUtf8(kBuildFolderName);
}

// Sixteen hex digits that change when the plan or the project settings change.
std::string planFingerprint(const Project& project)
{
    std::string text = planToJson(project.plan).dump();
    text += '\n';
    text += settingsToJson(project.settings).dump();
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(fnv1a(text)));
    return out;
}

// A BuildAssetSource that asks the catalog.
BuildAssetSource buildAssetSourceFor(const AssetCatalog& catalog)
{
    return [&catalog](std::string_view assetId) -> std::optional<BuildAssetState> {
        const std::optional<AssetRecord> record = catalog.asset(assetId);
        if (!record) return std::nullopt;
        BuildAssetState state;
        state.blendFile = record->folder / pathFromUtf8(kAssetBlendFileName);
        std::error_code ec;
        if (record->state == AssetState::Broken) {
            state.broken = true;
            state.problem = record->problem;
        } else if (!fs::is_regular_file(state.blendFile, ec)) {
            state.broken = true;
            state.problem = "asset.blend is missing.";
        }
        return state;
    };
}

// Where on the plan a problem's object is.
std::optional<Vec2> problemPosition(const Plan& plan, const BuildProblem& problem)
{
    if (problem.object.empty()) return std::nullopt;
    const Floor* floor = findFloorContaining(plan, problem.object);
    if (!floor) return std::nullopt;

    if (const Point* p = findPoint(*floor, problem.object)) return positionOf(*p);
    if (const Wall* w = findWall(*floor, problem.object)) return middleOfWall(*floor, *w);
    if (const Opening* o = findOpening(*floor, problem.object)) {
        const OpeningPlacement placement = openingPlacement(*floor, *o);
        if (!placement.valid) return std::nullopt;
        return placement.centre;
    }
    if (const Item* i = findItem(*floor, problem.object)) return Vec2{i->x, i->y};
    if (const Room* r = findRoom(*floor, problem.object)) {
        const RoomShape shape = roomShape(*floor, *r);
        if (shape.closed) return shape.labelPoint;
        Vec2 sum;
        int n = 0;
        for (const Id& wallId : r->walls) {
            const Wall* w = findWall(*floor, wallId);
            if (!w) continue;
            const Vec2 m = middleOfWall(*floor, *w);
            sum.x += m.x;
            sum.y += m.y;
            ++n;
        }
        if (n == 0) return std::nullopt;
        return Vec2{sum.x / n, sum.y / n};
    }
    return std::nullopt;
}

// "preparing", "blender", "finishing" or "done".
std::string_view buildStageName(BuildStage stage)
{
    switch (stage) {
    case BuildStage::Preparing: return "preparing";
    case BuildStage::Blender: return "blender";
    case BuildStage::Finishing: return "finishing";
    case BuildStage::Done: return "done";
    }
    return "preparing";
}

// How many problems are errors.
int BuildReport::errorCount() const
{
    return static_cast<int>(std::count_if(problems.begin(), problems.end(),
                                          [](const BuildProblem& p) { return p.severity == BuildSeverity::Error; }));
}

// How many problems are warnings.
int BuildReport::warningCount() const
{
    return static_cast<int>(problems.size()) - errorCount();
}

// Builds a project.
BuildReport runBuild(const BuildRequest& request, const std::function<void(const BuildProgress&)>& onProgress,
                     std::stop_token stop)
{
    const auto started = std::chrono::steady_clock::now();
    BuildReport report;
    const auto progress = [&](BuildStage stage, const BlenderProgress& blender = {}) {
        if (!onProgress) return;
        BuildProgress p;
        p.stage = stage;
        p.blender = blender;
        onProgress(p);
    };
    progress(BuildStage::Preparing);

    std::error_code ec;
    if (!fs::is_directory(request.projectFolder, ec)) {
        report.status = fail(ErrorCode::NotFound, "The project folder " + pathToUtf8(request.projectFolder) + " does not exist.");
        return report;
    }
    if (report.status = checkBuildable(request.project.plan); !report.status.ok()) return report;
    if (!request.blender.usable()) {
        report.status = fail(ErrorCode::BlenderNotFound, "There is no usable Blender to build with.");
        return report;
    }

    const fs::path folder = buildFolder(request.projectFolder);
    fs::create_directories(folder, ec);
    if (ec) {
        report.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(folder) + " could not be created.");
        return report;
    }
    BuildLock lock;
    if (!lock.take(folder / pathFromUtf8(kBuildLockFileName))) {
        report.status = fail(ErrorCode::InUse, "Another build of this project is running.");
        return report;
    }

    report.jobId = newJobId("build");
    report.startedAt = sessionNow();
    report.planFingerprint = planFingerprint(request.project);
    report.blenderVersion = request.blender.version ? formatBlenderVersion(*request.blender.version) : std::string();

    std::unique_ptr<AssetCatalog> catalog;
    BuildAssetSource source;
    if (request.library) {
        Status opened;
        catalog = AssetCatalog::open(*request.library, opened);
        if (catalog) source = buildAssetSourceFor(*catalog);
    }
    const auto checkStart = std::chrono::steady_clock::now();
    const BuildPlan plan = makeBuildPlan(request.project, source);
    report.steps.push_back({std::string(kCheckStep), since(checkStart)});
    report.problems = plan.problems;

    const fs::path resultFile = folder / pathFromUtf8(kResultFileName);
    const Json job = makeBuildJob(request.project, plan, request.settings, request.projectFolder, report.jobId, resultFile);

    progress(BuildStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        request.blender.executable, job, folder, request.run, [&](const BlenderProgress& p) { progress(BuildStage::Blender, p); }, stop);
    report.logFile = folder / pathFromUtf8(kBlenderLogFileName);

    progress(BuildStage::Finishing);
    if (!run.status.ok()) {
        report.status = run.status;
        report.errorCode = run.errorCode;
        if (run.status.code == ErrorCode::BlenderFailed) {
            report.status.message += "\nThe log is in " + pathToUtf8(report.logFile);
        }
        report.elapsedMs = since(started);
        if (run.status.code == ErrorCode::Cancelled && stop.stop_requested()) return report;
        writeReport(folder, report);
        return report;
    }

    const BuildWorkerOutput out = readBuildResult(run.result, report.jobId);
    if (!out.header.status.ok()) {
        report.status = fail(ErrorCode::BlenderFailed, out.header.status.message + "\nThe log is in " + pathToUtf8(report.logFile));
        report.errorCode = out.header.errorCode;
        report.elapsedMs = since(started);
        writeReport(folder, report);
        return report;
    }
    report.blenderVersion = out.header.blenderVersion;
    report.blendFile = folder / pathFromUtf8(out.blendFile);
    if (!out.fbxFile.empty()) report.fbxFile = folder / pathFromUtf8(out.fbxFile);
    report.objects = out.objects;
    report.triangles = out.triangles;
    report.counts = out.counts;
    report.steps.insert(report.steps.end(), out.steps.begin(), out.steps.end());
    report.problems.insert(report.problems.end(), out.problems.begin(), out.problems.end());
    report.elapsedMs = since(started);

    if (Status written = writeReport(folder, report); !written.ok()) {
        report.status = written;
        return report;
    }
    progress(BuildStage::Done);
    return report;
}

// The report in the file form.
Json buildReportToJson(const BuildReport& report)
{
    Json json = Json::object();
    json["format"] = std::string(kBuildReportFormat);
    json["version"] = kBuildReportVersion;
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
    json["job_id"] = report.jobId;
    json["started_at"] = formatSessionTime(report.startedAt);
    json["elapsed_ms"] = report.elapsedMs;
    json["blender_version"] = report.blenderVersion;
    json["plan_fingerprint"] = report.planFingerprint;
    Json outputs = Json::object();
    outputs["blend_file"] = pathOrNull(report.blendFile);
    outputs["fbx_file"] = pathOrNull(report.fbxFile);
    outputs["log_file"] = pathOrNull(report.logFile);
    json["outputs"] = std::move(outputs);
    json["objects"] = report.objects;
    json["triangles"] = report.triangles;
    Json counts = Json::object();
    counts["wall_solids"] = report.counts.wallSolids;
    counts["openings_cut"] = report.counts.openingsCut;
    counts["fills"] = report.counts.fills;
    counts["rooms"] = report.counts.rooms;
    counts["items"] = report.counts.items;
    json["counts"] = std::move(counts);
    Json steps = Json::array();
    for (const BuildStepTime& s : report.steps) steps.push_back(Json{{"step", s.step}, {"ms", s.ms}});
    json["steps"] = std::move(steps);
    Json problems = Json::array();
    for (const BuildProblem& p : report.problems) problems.push_back(buildProblemToJson(p));
    json["problems"] = std::move(problems);
    json["error_count"] = report.errorCount();
    json["warning_count"] = report.warningCount();
    return json;
}

// Reads build/build.json.
BuildReportRead readBuildReport(const fs::path& projectFolder)
{
    BuildReportRead result;
    const fs::path file = buildFolder(projectFolder) / pathFromUtf8(kBuildReportFileName);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.status = fail(ErrorCode::NotFound, "There is no build report in " + pathToUtf8(buildFolder(projectFolder)) + ".");
        return result;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The build report " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    const Json json = Json::parse(in, nullptr, false);
    const auto bad = [&](const std::string& why, const std::string& where) {
        result.status = fail(ErrorCode::BadFile, "The build report is not valid: " + why, where);
        result.report = {};
        return result;
    };
    if (json.is_discarded() || !json.is_object()) return bad("It is not a JSON object.", "");

    try {
        const auto text = [&](const Json& object, const char* key, const std::string& where) -> std::string {
            if (!object.contains(key) || !object[key].is_string()) throw std::string(where + "/" + key);
            return object[key].get<std::string>();
        };
        const auto whole = [&](const Json& object, const char* key, const std::string& where) -> long long {
            if (!object.contains(key) || !object[key].is_number_integer() || object[key].get<long long>() < 0) {
                throw std::string(where + "/" + key);
            }
            return object[key].get<long long>();
        };
        const auto path = [&](const Json& object, const char* key, const std::string& where) -> fs::path {
            if (!object.contains(key)) throw std::string(where + "/" + key);
            if (object[key].is_null()) return {};
            if (!object[key].is_string()) throw std::string(where + "/" + key);
            return pathFromUtf8(object[key].get<std::string>());
        };
        const auto optionalText = [&](const Json& object, const char* key, const std::string& where) -> std::string {
            if (!object.contains(key)) throw std::string(where + "/" + key);
            if (object[key].is_null()) return {};
            if (!object[key].is_string()) throw std::string(where + "/" + key);
            return object[key].get<std::string>();
        };

        if (text(json, "format", "") != kBuildReportFormat) throw std::string("/format");
        if (!json.contains("version") || json["version"] != kBuildReportVersion) throw std::string("/version");
        BuildReport& report = result.report;
        if (!json.contains("ok") || !json["ok"].is_boolean()) throw std::string("/ok");
        if (!json["ok"].get<bool>()) {
            const Json& error = json["error"];
            if (!error.is_object()) throw std::string("/error");
            report.status.code = errorCodeFromName(text(error, "code", "/error"));
            if (report.status.code == ErrorCode::None) throw std::string("/error/code");
            report.status.message = text(error, "message", "/error");
            report.errorCode = optionalText(error, "worker_code", "/error");
        }
        report.jobId = text(json, "job_id", "");
        const auto time = parseSessionTime(text(json, "started_at", ""));
        if (!time) throw std::string("/started_at");
        report.startedAt = *time;
        report.elapsedMs = whole(json, "elapsed_ms", "");
        report.blenderVersion = text(json, "blender_version", "");
        report.planFingerprint = text(json, "plan_fingerprint", "");
        if (!json.contains("outputs") || !json["outputs"].is_object()) throw std::string("/outputs");
        report.blendFile = path(json["outputs"], "blend_file", "/outputs");
        report.fbxFile = path(json["outputs"], "fbx_file", "/outputs");
        report.logFile = path(json["outputs"], "log_file", "/outputs");
        report.objects = whole(json, "objects", "");
        report.triangles = whole(json, "triangles", "");
        if (!json.contains("counts") || !json["counts"].is_object()) throw std::string("/counts");
        const Json& counts = json["counts"];
        report.counts.wallSolids = static_cast<int>(whole(counts, "wall_solids", "/counts"));
        report.counts.openingsCut = static_cast<int>(whole(counts, "openings_cut", "/counts"));
        report.counts.fills = static_cast<int>(whole(counts, "fills", "/counts"));
        report.counts.rooms = static_cast<int>(whole(counts, "rooms", "/counts"));
        report.counts.items = static_cast<int>(whole(counts, "items", "/counts"));
        if (!json.contains("steps") || !json["steps"].is_array()) throw std::string("/steps");
        for (std::size_t i = 0; i < json["steps"].size(); ++i) {
            const std::string where = "/steps/" + std::to_string(i);
            report.steps.push_back({text(json["steps"][i], "step", where), whole(json["steps"][i], "ms", where)});
        }
        if (!json.contains("problems") || !json["problems"].is_array()) throw std::string("/problems");
        for (std::size_t i = 0; i < json["problems"].size(); ++i) {
            const std::string where = "/problems/" + std::to_string(i);
            const Json& p = json["problems"][i];
            BuildProblem problem;
            problem.code = text(p, "code", where);
            const auto severity = buildSeverityFromName(text(p, "severity", where));
            if (!severity) throw std::string(where + "/severity");
            problem.severity = *severity;
            problem.message = text(p, "message", where);
            problem.floor = optionalText(p, "floor", where);
            problem.object = optionalText(p, "object", where);
            problem.step = text(p, "step", where);
            report.problems.push_back(std::move(problem));
        }
    } catch (const std::string& where) {
        return bad("The field " + where + " is missing or has the wrong type.", where);
    }
    return result;
}

}
