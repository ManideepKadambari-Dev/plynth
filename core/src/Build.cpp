#include "plynth/core/Build.h"

#include "BlenderPaths.h"
#include "BuildLockFiles.h"
#include "FileIo.h"

#include "plynth/core/Addons.h"
#include "plynth/core/BlenderJob.h"
#include "plynth/core/GeneratorBuild.h"
#include "plynth/core/Generators.h"
#include "plynth/core/Openings.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"
#include "plynth/core/Rooms.h"
#include "plynth/core/WallGeometry.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/MaterialInfo.h"

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
#include <map>
#include <memory>
#include <sstream>

namespace plynth {

namespace fs = std::filesystem;

namespace {

using lockfile::createExclusive;
using lockfile::epochSeconds;
using lockfile::lockIsLive;

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

// The middle point of the wall.
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

// A warning about add-ons that belongs to the whole build, not to one object.
BuildProblem addonWarning(std::string message)
{
    BuildProblem problem;
    problem.code = std::string(kGeneratorUnavailableProblem);
    problem.severity = BuildSeverity::Warning;
    problem.message = std::move(message);
    problem.step = std::string(kCheckStep);
    return problem;
}

// Removes a file or a folder with everything in it. A link or junction is removed itself, never what it
// points at.
void removeTree(const fs::path& path)
{
    const DWORD attributes = blenderpaths::attributesOf(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    const std::wstring name = blenderpaths::longPath(path);
    const bool link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (!link && (attributes & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(name.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        DeleteFileW(name.c_str());
        return;
    }
    if (!link) {
        std::vector<std::wstring> inside;
        WIN32_FIND_DATAW found{};
        const HANDLE find = FindFirstFileExW(blenderpaths::longPath(path / L"*").c_str(), FindExInfoBasic, &found,
                                             FindExSearchNameMatch, nullptr, 0);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring entry = found.cFileName;
                if (entry != L"." && entry != L"..") inside.push_back(entry);
            } while (FindNextFileW(find, &found));
            FindClose(find);
        }
        for (const std::wstring& entry : inside) removeTree(path / entry);
    }
    RemoveDirectoryW(name.c_str());
}

// Removes the add-on folder of one job when the run ends, however it ends. It only ever removes a folder that
// was not there before this run.
class JobFolderGuard {
public:
    // Remembers the folder when nothing is there yet. Says whether it may be used.
    bool claim(const fs::path& folder)
    {
        if (blenderpaths::attributesOf(folder) != INVALID_FILE_ATTRIBUTES) return false;
        folder_ = folder;
        return true;
    }

    // Removes the folder now.
    void release()
    {
        if (folder_.empty()) return;
        removeTree(folder_);
        folder_.clear();
    }

    ~JobFolderGuard() { release(); }

private:
    fs::path folder_;
};

// Says whether a name is that of a job's add-on folder: plynth-ext- and eight hex digits.
bool isJobFolderName(std::wstring_view name)
{
    constexpr std::wstring_view prefix = L"plynth-ext-";
    if (name.size() != prefix.size() + 8 || !name.starts_with(prefix)) return false;
    return std::all_of(name.begin() + prefix.size(), name.end(),
                       [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); });
}

// Removes the add-on folders of earlier jobs that a killed Plynth or a held file left in the temp folder, once
// they are older than twice the longest build. A temp folder that shares a folder with Blender's user folders
// is not looked at.
void removeOldJobFolders(const BlenderInstall& blender)
{
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    if (ec || temp.empty() || !temp.is_absolute() || blenderpaths::namesBlenderFoundation(temp)) return;
    const BlenderUserFolders folders = blenderUserFolders(blender);
    std::vector<fs::path> kept{folders.config, folders.scripts, folders.extensions, folders.datafiles, folders.blenderKitData};
    if (const std::wstring app = blenderpaths::environmentText(L"APPDATA"); !app.empty()) {
        kept.push_back(fs::path(app) / L"Blender Foundation");
    }
    for (const fs::path& other : kept) {
        if (!other.empty() && blenderpaths::overlaps(temp, other)) return;
    }

    FILETIME nowTime{};
    GetSystemTimeAsFileTime(&nowTime);
    const auto ticks = [](const FILETIME& time) {
        return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    const unsigned long long now = ticks(nowTime);
    const unsigned long long oldest = static_cast<unsigned long long>(kBuildTimeout.count()) * 2ULL * 10000000ULL;

    std::vector<std::wstring> named;
    WIN32_FIND_DATAW found{};
    const HANDLE find = FindFirstFileExW(blenderpaths::longPath(temp / L"plynth-ext-*").c_str(), FindExInfoBasic, &found,
                                         FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = found.cFileName;
        if (isJobFolderName(name)) named.push_back(name);
    } while (FindNextFileW(find, &found));
    FindClose(find);
    // The times are read from the folder itself, because the listing may hold older copies of them.
    for (const std::wstring& name : named) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(blenderpaths::longPath(temp / name).c_str(), GetFileExInfoStandard, &data)) continue;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const unsigned long long written = ticks(data.ftLastWriteTime);
        if (written < now && now - written > oldest) removeTree(temp / name);
    }
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

// The maps of every material inside an asset, as absolute paths. Empty when info.json cannot be read or has no
// material maps yet.
namespace {

std::vector<BuildAssetMaterial> assetMaterialsOf(const fs::path& folder)
{
    std::vector<BuildAssetMaterial> out;
    const AssetInfoRead read = readAssetInfo(folder / pathFromUtf8(kAssetInfoFileName), pathToUtf8(folder.filename()));
    if (!read.status.ok()) return out;
    const auto file = [&folder](const MaterialMap& map) {
        return map.file.empty() ? fs::path() : folder / pathFromUtf8(map.file);
    };
    for (const AssetMaterial& m : read.info.materialMaps) {
        BuildAssetMaterial material;
        material.name = m.name;
        material.baseColor = file(m.maps.baseColor);
        material.normal = file(m.maps.normal);
        material.roughness = file(m.maps.roughness);
        material.metallic = file(m.maps.metallic);
        material.opacity = file(m.maps.opacity);
        material.occlusion = file(m.maps.occlusion);
        out.push_back(std::move(material));
    }
    return out;
}

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
        if (!state.broken) state.materials = assetMaterialsOf(record->folder);
        return state;
    };
}

// A BuildMaterialSource that asks the catalog.
BuildMaterialSource buildMaterialSourceFor(const MaterialCatalog& catalog)
{
    auto cache = std::make_shared<std::map<std::string, std::optional<BuildMaterialState>>>();
    return [&catalog, cache](std::string_view materialId) -> std::optional<BuildMaterialState> {
        const std::string key(materialId);
        if (const auto found = cache->find(key); found != cache->end()) return found->second;
        std::optional<BuildMaterialState> result;
        if (const std::optional<MaterialRecord> record = catalog.material(materialId)) {
            BuildMaterialState state;
            state.material.id = key;
            state.material.tileSize = record->tileSize;
            if (record->state == MaterialState::Broken) {
                state.broken = true;
                state.problem = record->problem;
            } else {
                const MaterialInfoRead read = catalog.info(materialId);
                if (!read.status.ok()) {
                    state.broken = true;
                    state.problem = read.status.message;
                } else {
                    const auto absolute = [&](const MaterialMap& map) {
                        return map.file.empty() ? fs::path() : record->folder / pathFromUtf8(map.file);
                    };
                    state.material.tileSize = read.info.tileSize;
                    state.material.baseColor = absolute(read.info.maps.baseColor);
                    state.material.normal = absolute(read.info.maps.normal);
                    state.material.roughness = absolute(read.info.maps.roughness);
                    state.material.metallic = absolute(read.info.maps.metallic);
                    state.material.opacity = absolute(read.info.maps.opacity);
                    state.material.occlusion = absolute(read.info.maps.occlusion);
                    std::error_code ec;
                    for (const MapChannel channel : allMapChannels()) {
                        const MaterialMap& map = materialMap(read.info.maps, channel);
                        if (!map.file.empty() && !fs::is_regular_file(record->folder / pathFromUtf8(map.file), ec)) {
                            state.broken = true;
                            state.problem = map.file + " is missing.";
                            break;
                        }
                    }
                }
            }
            result = std::move(state);
        }
        cache->emplace(key, result);
        return result;
    };
}

// Where on the plan a problem's object is.
std::optional<Vec2> problemPosition(const Plan& plan, const BuildProblem& problem)
{
    if (problem.object.empty()) return std::nullopt;
    if (const RoofSection* entry = findRoofSection(plan, problem.object)) return Vec2{entry->x, entry->y};
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
    if (const Stair* st = findStair(*floor, problem.object)) return Vec2{st->x, st->y};
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
    // Made before the lock, so it ends after the lock is gone and no cancel marker outlives the job.
    const lockfile::CancelWatch cancelWatch(folder, stop);
    stop = cancelWatch.token();
    BuildLock lock;
    if (!lock.take(folder / pathFromUtf8(kBuildLockFileName))) {
        report.status = fail(ErrorCode::InUse, "Another build of this project is running.");
        return report;
    }

    report.jobId = newJobId("build");
    report.startedAt = sessionNow();
    report.planFingerprint = planFingerprint(request.project);
    report.blenderVersion = request.blender.version ? formatBlenderVersion(*request.blender.version) : std::string();

    removeOldJobFolders(request.blender);

    std::unique_ptr<AssetCatalog> catalog;
    std::unique_ptr<MaterialCatalog> materialCatalog;
    BuildAssetSource source;
    BuildMaterialSource materialSource;
    std::optional<GeneratorLibrary> generators;
    if (request.library) {
        Status opened;
        catalog = AssetCatalog::open(*request.library, opened);
        if (catalog) source = buildAssetSourceFor(*catalog);
        materialCatalog = MaterialCatalog::open(*request.library, opened);
        if (materialCatalog) materialSource = buildMaterialSourceFor(*materialCatalog);
        generators = loadGeneratorFolder(request.library->generators);
    }
    const GeneratorLibrary none{};
    const GeneratorLibrary* library = source ? (generators ? &*generators : &none) : nullptr;

    std::optional<AddonCache> addons;
    if (const AddonCacheRead saved = readAddonCache(addonCacheFile()); saved.status.ok() && saved.cache &&
                                                                       addonCacheMatches(*saved.cache, request.blender)) {
        addons = saved.cache;
    }

    // Only a build that would copy asks Blender, and then the fresh answer alone decides what is built and copied.
    std::vector<BuildProblem> addonProblems;
    bool probed = false;
    const bool wouldCopy = !request.addonSource.empty() && makeGeneratorBuildPlan(request.project.plan, library, addons).needsAddons;
    if (wouldCopy) {
        const fs::path extensions = blenderUserFolders(request.blender).extensions;
        if (!request.addonSource.is_absolute() || extensions.empty() || !blenderpaths::sameText(request.addonSource, extensions)) {
            addonProblems.push_back(addonWarning(
                "Add-ons are copied only from the extensions folder of the Blender that builds, so none was copied for the generators."));
        } else {
            const BlenderRunOptions probeRun{true, std::min<std::chrono::seconds>(kAddonProbeTimeout, request.run.timeout),
                                             request.run.scriptsFolder};
            const AddonProbe probe = probeAddons(request.blender, addonCacheFile(), probeRun, stop);
            if (probe.status.code == ErrorCode::Cancelled) {
                report.status = probe.status;
                report.elapsedMs = since(started);
                return report;
            }
            if (probe.status.ok()) {
                addons = probe.cache;
                probed = true;
            } else {
                addons.reset();
                addonProblems.push_back(addonWarning("No add-on was copied for the generators. " + probe.status.message));
            }
        }
    }

    const auto checkStart = std::chrono::steady_clock::now();
    const BuildPlan plan = makeBuildPlan(request.project, source, materialSource, library, addons);
    report.steps.push_back({std::string(kCheckStep), since(checkStart)});

    BlenderRunOptions runOptions = request.run;
    runOptions.factoryStartup = true;
    runOptions.extensionsFolder.clear();
    JobFolderGuard jobFolder;
    if (probed && plan.generators.needsAddons) {
        std::vector<std::string> wanted;
        for (const BuildGenerator& generator : plan.generators.generators) {
            wanted.insert(wanted.end(), generator.addons.begin(), generator.addons.end());
        }
        std::error_code tempError;
        const fs::path temp = fs::temp_directory_path(tempError);
        const fs::path own = tempError ? fs::path() : temp / pathFromUtf8(newJobId("plynth-ext"));
        AddonFolderCopy copy;
        if (own.empty() || !jobFolder.claim(own)) {
            copy.status = fail(ErrorCode::CannotWrite, "No new folder for the copies could be made in the temp folder.");
        } else {
            copy = prepareAddonFolder(own, request.addonSource, request.blender, *addons, wanted, stop);
        }
        if (copy.status.code == ErrorCode::Cancelled && stop.stop_requested()) {
            jobFolder.release();
            report.status = copy.status;
            report.elapsedMs = since(started);
            return report;
        }
        if (copy.status.ok()) {
            runOptions.extensionsFolder = own;
        } else {
            jobFolder.release();
            addonProblems.push_back(addonWarning("The add-ons the generators need were not copied. " + copy.status.message));
        }
    }
    report.problems = plan.problems;
    report.problems.insert(report.problems.end(), addonProblems.begin(), addonProblems.end());

    const fs::path resultFile = folder / pathFromUtf8(kResultFileName);
    const Json job = makeBuildJob(request.project, plan, request.settings, request.projectFolder, report.jobId, resultFile);

    progress(BuildStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        request.blender.executable, job, folder, runOptions, [&](const BlenderProgress& p) { progress(BuildStage::Blender, p); }, stop);
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
    if (!out.materialsFile.empty()) report.materialsFile = folder / pathFromUtf8(out.materialsFile);
    report.objects = out.objects;
    report.triangles = out.triangles;
    report.counts = out.counts;
    report.steps.insert(report.steps.end(), out.steps.begin(), out.steps.end());
    report.problems.insert(report.problems.end(), out.problems.begin(), out.problems.end());
    report.elapsedMs = since(started);

    Json built = Json::object();
    built["plan_fingerprint"] = report.planFingerprint;
    built["plan"] = planToJson(request.project.plan);
    const std::string planText = built.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    if (Status written = fileio::writeAtomic(folder / pathFromUtf8(kBuiltPlanFileName), planText); !written.ok()) {
        report.status = written;
        return report;
    }
    if (Status written = writeReport(folder, report); !written.ok()) {
        report.status = written;
        return report;
    }
    progress(BuildStage::Done);
    return report;
}

// Writes the cancel marker when a build or an export holds a live lock.
bool requestCancel(const fs::path& projectFolder)
{
    const fs::path folder = buildFolder(projectFolder);
    const fs::path lock = folder / pathFromUtf8(kBuildLockFileName);
    std::error_code ec;
    if (!fs::exists(lock, ec) || !lockIsLive(lock)) return false;
    return fileio::writeAtomic(folder / pathFromUtf8(kBuildCancelFileName), "cancel\n").ok();
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
    outputs["materials_file"] = pathOrNull(report.materialsFile);
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
    counts["materials"] = report.counts.materials;
    counts["slabs"] = report.counts.slabs;
    counts["stairs"] = report.counts.stairs;
    counts["roofs"] = report.counts.roofs;
    counts["collision"] = report.counts.collision;
    counts["generators"] = report.counts.generators;
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
            if (!json.contains("error")) throw std::string("/error");
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
        if (json["outputs"].contains("materials_file")) report.materialsFile = path(json["outputs"], "materials_file", "/outputs");
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
        if (counts.contains("materials")) report.counts.materials = static_cast<int>(whole(counts, "materials", "/counts"));
        if (counts.contains("slabs")) report.counts.slabs = static_cast<int>(whole(counts, "slabs", "/counts"));
        if (counts.contains("stairs")) report.counts.stairs = static_cast<int>(whole(counts, "stairs", "/counts"));
        if (counts.contains("roofs")) report.counts.roofs = static_cast<int>(whole(counts, "roofs", "/counts"));
        if (counts.contains("collision")) report.counts.collision = static_cast<int>(whole(counts, "collision", "/counts"));
        if (counts.contains("generators")) report.counts.generators = static_cast<int>(whole(counts, "generators", "/counts"));
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

// Reads build/materials.json.
BuiltMaterialsRead readBuiltMaterials(const fs::path& projectFolder)
{
    BuiltMaterialsRead result;
    const fs::path file = buildFolder(projectFolder) / pathFromUtf8(kBuildMaterialsFileName);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.status = fail(ErrorCode::NotFound, "There is no " + std::string(kBuildMaterialsFileName) + " in " + pathToUtf8(buildFolder(projectFolder)) + ".");
        return result;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.status = fail(ErrorCode::CannotRead, "The file " + pathToUtf8(file) + " could not be opened.");
        return result;
    }
    const Json json = Json::parse(in, nullptr, false);
    const auto bad = [&](const std::string& where) {
        result.status = fail(ErrorCode::BadFile, "The built materials file is not valid: the field " + where + " is missing or has the wrong type.", where);
        result.materials.clear();
        result.objects.clear();
        return result;
    };
    if (json.is_discarded() || !json.is_object()) return bad("");

    try {
        const auto text = [](const Json& object, const char* key, const std::string& where) -> std::string {
            if (!object.is_object() || !object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty()) {
                throw std::string(where + "/" + key);
            }
            return object[key].get<std::string>();
        };
        const auto optionalText = [](const Json& object, const char* key, const std::string& where) -> std::string {
            if (!object.contains(key)) throw std::string(where + "/" + key);
            if (object[key].is_null()) return {};
            if (!object[key].is_string()) throw std::string(where + "/" + key);
            return object[key].get<std::string>();
        };
        const auto array = [](const Json& object, const char* key) -> const Json& {
            if (!object.contains(key) || !object[key].is_array()) throw std::string(std::string("/") + key);
            return object[key];
        };

        if (text(json, "format", "") != kBuildMaterialsFormat) throw std::string("/format");
        if (!json.contains("version") || json["version"] != kBuildMaterialsVersion) throw std::string("/version");
        const Json& materials = array(json, "materials");
        for (std::size_t i = 0; i < materials.size(); ++i) {
            const std::string where = "/materials/" + std::to_string(i);
            const Json& m = materials[i];
            BuiltMaterial built;
            built.name = text(m, "name", where);
            built.material = optionalText(m, "material", where);
            built.surface = optionalText(m, "surface", where);
            if (!m.contains("maps") || !m["maps"].is_object()) throw std::string(where + "/maps");
            const Json& maps = m["maps"];
            const std::string mapsWhere = where + "/maps";
            built.baseColor = pathFromUtf8(text(maps, "base_color", mapsWhere));
            built.normal = pathFromUtf8(text(maps, "normal", mapsWhere));
            built.roughness = pathFromUtf8(text(maps, "roughness", mapsWhere));
            built.metallic = pathFromUtf8(text(maps, "metallic", mapsWhere));
            built.opacity = pathFromUtf8(optionalText(maps, "opacity", mapsWhere));
            built.occlusion = pathFromUtf8(optionalText(maps, "occlusion", mapsWhere));
            if (m.contains("asset")) built.asset = optionalText(m, "asset", where);
            const std::string mode = text(m, "alpha_mode", where);
            if (mode == "opaque") built.alphaMode = AlphaMode::Opaque;
            else if (mode == "blend") built.alphaMode = AlphaMode::Blend;
            else throw std::string(where + "/alpha_mode");
            result.materials.push_back(std::move(built));
        }
        const Json& objects = array(json, "objects");
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const std::string where = "/objects/" + std::to_string(i);
            BuiltObjectSlots slots;
            slots.object = text(objects[i], "name", where);
            const Json& list = array(objects[i], "slots");
            for (const Json& slot : list) {
                if (!slot.is_string() || slot.get<std::string>().empty()) throw std::string(where + "/slots");
                slots.slotNames.push_back(slot.get<std::string>());
            }
            result.objects.push_back(std::move(slots));
        }
    } catch (const std::string& where) {
        return bad(where);
    }
    return result;
}

}
