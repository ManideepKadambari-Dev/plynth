#include "plynth/core/catalog/MaterialImport.h"

#include "ImporterChoice.h"
#include "Staging.h"

#include "plynth/core/Process.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/Integrations.h"
#include "plynth/core/catalog/StoreFetch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// A folder that is removed when the object goes away, unless it is told to stay.
class WorkFolder {
public:
    explicit WorkFolder(fs::path path) : path_(std::move(path)) {}
    ~WorkFolder()
    {
        if (keep_) staging::unmarkLive(path_);
        else staging::removeTree(path_);
    }
    WorkFolder(const WorkFolder&) = delete;
    WorkFolder& operator=(const WorkFolder&) = delete;
    // Marks the folder as in use by this process. Call once it exists.
    void start() { staging::markLive(path_); }
    // Keeps the folder for the log, noting which source it was for.
    void keep(const std::string& source = {})
    {
        keep_ = true;
        if (!source.empty()) staging::keepSource(path_, source);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    bool keep_ = false;
};

// The Blender options for material jobs: always the factory startup, so the user's add-ons cannot change results.
BlenderRunOptions materialRunOptions(const ImportContext& context)
{
    BlenderRunOptions run = context.run;
    run.factoryStartup = true;
    return run;
}

// Turns a failed Blender run into the status an import reports, naming the kept work folder.
Status blenderFailure(Status s, const fs::path& kept)
{
    if (s.code == ErrorCode::BlenderFailed && !kept.empty()) {
        s.message += "\nThe work folder was kept for the log: " + pathToUtf8(kept);
    }
    return s;
}

// Says whether a license id is one the catalog has.
bool licenseKnown(const MaterialCatalog& catalog, const std::string& id)
{
    for (const LicenseEntry& l : catalog.licenses()) {
        if (l.id == id) return true;
    }
    return false;
}

// Sorted values without repeats.
std::vector<std::string> sortedUnique(std::vector<std::string> values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

// The same values without repeats, in their order.
std::vector<std::string> withoutRepeats(const std::vector<std::string>& values)
{
    std::vector<std::string> out;
    for (const std::string& v : values) {
        if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    }
    return out;
}

// Says whether a number is a power of two.
bool isPowerOfTwo(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

// The channels that may be given as a single value.
bool takesValue(MapChannel channel)
{
    return channel == MapChannel::Roughness || channel == MapChannel::Metallic || channel == MapChannel::Opacity ||
           channel == MapChannel::Occlusion;
}

// Checks the parts of a request that need no catalog.
Status checkRequest(const MaterialImportRequest& request)
{
    if (request.source.file.empty() && request.source.url.empty()) {
        return fail(ErrorCode::InvalidValue, "No file, folder or address was given to import.");
    }
    if (!request.id.empty()) {
        if (Status s = checkCatalogId(request.id); !s.ok()) return s;
    }
    if (!request.name.empty()) {
        if (Status s = checkCatalogName(request.name); !s.ok()) return s;
    }
    for (const std::string& tag : request.tags) {
        if (Status s = checkTag(tag); !s.ok()) return s;
    }
    for (const std::string& group : request.groups) {
        if (Status s = checkCatalogId(group); !s.ok()) return s;
    }
    if (!request.license.empty()) {
        if (Status s = checkCatalogId(request.license); !s.ok()) return s;
    }
    if (request.tileSize) {
        if (Status s = checkTileSize(*request.tileSize); !s.ok()) return s;
    }
    for (const auto& [channel, value] : request.values) {
        const std::string name(mapChannelName(channel));
        if (!takesValue(channel)) {
            return fail(ErrorCode::InvalidValue, "The " + name + " channel cannot be given as a single value. Use roughness, metallic, opacity or occlusion.");
        }
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            return fail(ErrorCode::InvalidValue, "The " + name + " value must be a number from 0 to 1.");
        }
    }
    std::error_code ec;
    for (const auto& [channel, file] : request.maps) {
        if (!fs::is_regular_file(file, ec)) {
            return fail(ErrorCode::NotFound, "The " + std::string(mapChannelName(channel)) + " map " + pathToUtf8(file) + " was not found.");
        }
    }
    if (request.bakeSize < kMinBakeSize || request.bakeSize > kMaxBakeSize || !isPowerOfTwo(request.bakeSize)) {
        return fail(ErrorCode::InvalidValue, "The bake size must be a power of two from " + std::to_string(kMinBakeSize) + " to " +
                                                 std::to_string(kMaxBakeSize) + " pixels.");
    }
    return {};
}

// Checks that the groups and license of a request are in the catalog.
Status checkAgainstCatalog(const MaterialCatalog& catalog, const MaterialImportRequest& request)
{
    if (!request.groups.empty()) {
        std::set<std::string> known;
        for (const MaterialGroupEntry& g : catalog.groups()) known.insert(g.id);
        for (const std::string& group : request.groups) {
            if (!known.count(group)) return fail(ErrorCode::NotFound, "There is no group \"" + group + "\".");
        }
    }
    if (!request.license.empty()) {
        bool found = false;
        for (const LicenseEntry& l : catalog.licenses()) found = found || l.id == request.license;
        if (!found) return fail(ErrorCode::NotFound, "There is no license \"" + request.license + "\".");
    }
    return {};
}

// The id a folder's material.json asks for, when it has one. Only used to skip an import early.
std::string sourceFileId(const fs::path& folder)
{
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return {};
    std::ifstream in(folder / pathFromUtf8(kMaterialSourceFileName), std::ios::binary);
    if (!in) return {};
    const Json json = Json::parse(in, nullptr, false);
    if (!json.is_object() || !json.contains("id") || !json["id"].is_string()) return {};
    const std::string id = json["id"].get<std::string>();
    return checkCatalogId(id).ok() ? id : std::string();
}

// The first name that passes the name rules.
std::string firstValidName(const std::vector<std::string>& candidates)
{
    for (const std::string& name : candidates) {
        if (!name.empty() && checkCatalogName(name).ok()) return name;
    }
    return {};
}

// A path inside a folder for a moved entry: the same name, or the name with " (2)", " (3)" before the ending.
fs::path freeEntryName(const fs::path& folder, const fs::path& name)
{
    std::error_code ec;
    if (!fs::exists(folder / name, ec)) return folder / name;
    const std::string stem = pathToUtf8(name.stem());
    const std::string extension = pathToUtf8(name.extension());
    for (int n = 2;; ++n) {
        const fs::path candidate = folder / pathFromUtf8(stem + " (" + std::to_string(n) + ")" + extension);
        if (!fs::exists(candidate, ec)) return candidate;
    }
}

// Moves an inbox entry into done/ or failed/. The new path, or an empty path when it could not be moved.
fs::path moveEntry(const fs::path& entry, const fs::path& folder)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path target = freeEntryName(folder, entry.filename());
    fs::rename(entry, target, ec);
    return ec ? fs::path() : target;
}

// Failures that say nothing about the entry itself: it stays in the inbox for another try.
bool leavesEntryInInbox(ErrorCode code)
{
    return code == ErrorCode::Cancelled || code == ErrorCode::BlenderNotFound || code == ErrorCode::NoLibrary ||
           code == ErrorCode::DatabaseError || code == ErrorCode::CannotWrite;
}

// The write time of a file in whole seconds.
SessionTime fileTime(const fs::path& file)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) return {};
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::time_point_cast<std::chrono::seconds>(system);
}

// Sends one progress report.
void sendProgress(const std::function<void(const ImportProgress&)>& onProgress, ImportStage stage,
                  const BlenderProgress& blender = {})
{
    if (!onProgress) return;
    ImportProgress p;
    p.stage = stage;
    p.blender = blender;
    onProgress(p);
}

// Says whether every map file the result names was written into the folder.
std::string missingMapFile(const fs::path& folder, const MaterialMaps& maps)
{
    std::error_code ec;
    for (const MapChannel channel : allMapChannels()) {
        const MaterialMap& map = materialMap(maps, channel);
        if (!map.file.empty() && !fs::is_regular_file(folder / pathFromUtf8(map.file), ec)) return map.file;
    }
    return {};
}

// Fills the thumbnail part of an info from a job's output.
void setThumbnail(MaterialInfo& info, const MaterialJobOutput& out, bool rendered)
{
    info.thumbnail.rendered = rendered;
    info.thumbnail.engine = rendered ? (out.thumbnailEngine.empty() ? std::string("eevee") : out.thumbnailEngine) : "";
    info.thumbnail.renderedAt = rendered ? sessionNow() : SessionTime{};
    info.thumbnail.error.clear();
    if (!rendered) info.thumbnail.error = out.thumbnailError.empty() ? "The thumbnail was not rendered." : out.thumbnailError;
}

}

// Imports one folder, file or zip.
MaterialImportOutcome importMaterial(MaterialCatalog& catalog, const ImportContext& context,
                                     const MaterialImportRequest& request,
                                     const std::function<void(const ImportProgress&)>& onProgress, std::stop_token stop)
{
    MaterialImportOutcome outcome;
    sendProgress(onProgress, ImportStage::Preparing);

    if (outcome.status = checkRequest(request); !outcome.status.ok()) return outcome;
    const ChosenImporter<MaterialImporter> chosen = chooseMaterialImporter(catalog.paths(), request);
    if (!chosen.status.ok()) {
        outcome.status = chosen.status;
        return outcome;
    }
    const MaterialImporter* importer = chosen.importer;
    if (!importer) {
        std::string suffixes;
        for (const std::string& s : importableMaterialSuffixes()) suffixes += (suffixes.empty() ? "" : ", ") + s;
        const std::string what = request.source.file.empty() ? request.source.url : pathToUtf8(request.source.file);
        std::error_code ec;
        if (!request.source.file.empty() && !fs::exists(request.source.file, ec)) {
            outcome.status = fail(ErrorCode::NotFound, "The file " + what + " was not found.");
        } else {
            outcome.status = fail(ErrorCode::InvalidValue, "Plynth cannot import " + what + " as a material. It reads these file endings: " +
                                                               suffixes + ", and folders that hold images.");
        }
        return outcome;
    }
    outcome.importer = std::string(importer->id());
    if (outcome.status = checkAgainstCatalog(catalog, request); !outcome.status.ok()) return outcome;

    if (!request.id.empty() && catalog.idTaken(request.id)) {
        if (request.skipExisting) {
            outcome.skipped = true;
            outcome.materialId = request.id;
            return outcome;
        }
        outcome.status = fail(ErrorCode::AlreadyExists, "The id \"" + request.id + "\" is already used by another material.");
        return outcome;
    }
    if (request.skipExisting && request.id.empty()) {
        const std::string wanted = sourceFileId(request.source.file);
        if (!wanted.empty() && catalog.idTaken(wanted)) {
            outcome.skipped = true;
            outcome.materialId = wanted;
            return outcome;
        }
    }
    if (stop.stop_requested()) {
        outcome.status = fail(ErrorCode::Cancelled, "The import was cancelled.");
        return outcome;
    }

    const LibraryPaths& library = catalog.paths();
    const std::string jobId = newJobId(request.id.empty() ? std::string("material") : request.id);
    WorkFolder work(library.materialStaging / pathFromUtf8(jobId));
    const fs::path itemFolder = work.path() / "item";
    std::error_code ec;
    fs::create_directories(work.path(), ec);
    if (!ec) work.start();
    if (!ec) fs::create_directories(itemFolder, ec);
    if (ec) {
        outcome.status = fail(ErrorCode::CannotWrite, "The work folder " + pathToUtf8(work.path()) + " could not be created.");
        return outcome;
    }
    const std::string sourceText = request.source.file.empty() ? request.source.url : pathToUtf8(request.source.file);

    MaterialImportPlan plan;
    outcome.status = importer->plan(chosen.source, work.path(), plan);
    outcome.details = plan.details;
    if (!outcome.status.ok()) return outcome;

    if (plan.fetch) {
        sendProgress(onProgress, ImportStage::Blender);
        const FetchRun fetched = runImportFetch(*plan.fetch, context.blender.executable, context.run, work.path(),
                                                [&](const BlenderProgress& p) { sendProgress(onProgress, ImportStage::Blender, p); }, stop);
        if (!fetched.status.ok()) {
            const bool blenderProblem = fetched.status.code == ErrorCode::BlenderFailed;
            if (blenderProblem) {
                work.keep(sourceText);
                outcome.keptWorkFolder = work.path();
            }
            outcome.status = blenderFailure(fetched.status, blenderProblem ? work.path() : fs::path());
            return outcome;
        }
        if (stop.stop_requested()) {
            outcome.status = fail(ErrorCode::Cancelled, "The import was cancelled.");
            return outcome;
        }
        outcome.status = applyFetch(fetched, *plan.fetch, plan);
        outcome.details = plan.details;
        if (!outcome.status.ok()) return outcome;
    }

    MaterialCleanupJob cleanup;
    cleanup.workerSource = plan.workerSource;
    cleanup.path = plan.path;
    cleanup.blendMaterial = request.blendMaterial.empty() ? plan.blendMaterial : request.blendMaterial;
    cleanup.maps = request.maps;
    cleanup.values = request.values;
    cleanup.normal = request.normal;
    cleanup.outputFolder = itemFolder;
    cleanup.tileSize = request.tileSize;
    cleanup.bakeSize = request.bakeSize;
    const Json job = makeMaterialCleanupJob(cleanup, jobId, work.path() / pathFromUtf8(kResultFileName));

    sendProgress(onProgress, ImportStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        context.blender.executable, job, work.path(), materialRunOptions(context),
        [&](const BlenderProgress& p) { sendProgress(onProgress, ImportStage::Blender, p); }, stop);
    if (!run.status.ok()) {
        const bool blenderProblem = run.status.code == ErrorCode::BlenderFailed;
        if (blenderProblem) work.keep(sourceText);
        outcome.status = blenderFailure(run.status, blenderProblem ? work.path() : fs::path());
        if (blenderProblem) outcome.keptWorkFolder = work.path();
        return outcome;
    }
    const MaterialJobOutput out = readMaterialCleanupResult(run.result, jobId);
    if (!out.header.status.ok()) {
        work.keep(sourceText);
        outcome.keptWorkFolder = work.path();
        outcome.status = fail(ErrorCode::BlenderFailed, out.header.status.message + "\nThe work folder was kept for the log: " + pathToUtf8(work.path()));
        return outcome;
    }
    if (const std::string missing = missingMapFile(itemFolder, out.maps); !missing.empty()) {
        work.keep(sourceText);
        outcome.keptWorkFolder = work.path();
        outcome.status = fail(ErrorCode::BlenderFailed, "Blender did not write " + missing + ".\nThe work folder was kept for the log: " + pathToUtf8(work.path()));
        return outcome;
    }
    if (stop.stop_requested()) {
        outcome.status = fail(ErrorCode::Cancelled, "The import was cancelled.");
        return outcome;
    }

    sendProgress(onProgress, ImportStage::Saving);
    const MaterialSourceInfo none;
    const MaterialSourceInfo& said = out.sourceInfo ? *out.sourceInfo : none;

    MaterialInfo info;
    info.name = firstValidName({request.name, said.name.value_or(""), plan.defaultName, out.pickedMaterial});
    if (info.name.empty()) info.name = "Material";

    std::string id = request.id;
    if (id.empty()) {
        std::string base = said.id.value_or("");
        if (base.empty() || !checkCatalogId(base).ok()) base = catalogIdFromName(info.name);
        if (request.skipExisting && catalog.idTaken(base)) {
            outcome.skipped = true;
            outcome.materialId = base;
            return outcome;
        }
        id = uniqueCatalogId(base, [&](std::string_view candidate) { return catalog.idTaken(candidate); });
    }
    info.id = id;

    if (!request.groups.empty()) {
        info.groups = withoutRepeats(request.groups);
    } else if (said.groups) {
        for (const std::string& g : *said.groups) {
            if (checkCatalogId(g).ok() && std::find(info.groups.begin(), info.groups.end(), g) == info.groups.end()) info.groups.push_back(g);
        }
    }
    std::vector<std::string> tags = request.tags;
    if (tags.empty() && said.tags) {
        for (const std::string& t : *said.tags) {
            if (checkTag(t).ok()) tags.push_back(t);
        }
    }
    info.tags = sortedUnique(tags);
    if (!request.license.empty()) info.license = request.license;
    else if (said.license && checkCatalogId(*said.license).ok()) info.license = *said.license;
    else if (!plan.license.empty() && licenseKnown(catalog, plan.license)) info.license = plan.license;
    else info.license = std::string(kUnknownLicenseId);

    info.source = {plan.sourceKind, plan.reference, sessionNow()};
    info.tileSize = request.tileSize ? *request.tileSize : out.tileSize;
    info.maps = out.maps;
    info.textureSize = out.textureSize;
    info.files = measureMaterialFiles(itemFolder, info.maps);
    const bool rendered = out.thumbnailRendered && fs::is_regular_file(itemFolder / pathFromUtf8(kMaterialThumbFileName), ec);
    setThumbnail(info, out, rendered);
    info.blenderVersion = out.header.blenderVersion;
    info.warnings = out.header.warnings;

    if (outcome.status = writeMaterialInfo(itemFolder / pathFromUtf8(kMaterialInfoFileName), info); !outcome.status.ok()) return outcome;
    const fs::path target = library.materialItems / pathFromUtf8(id);
    if (fs::exists(target, ec)) {
        outcome.status = fail(ErrorCode::AlreadyExists, "The id \"" + id + "\" was taken while the import ran.");
        return outcome;
    }
    fs::create_directories(library.materialItems, ec);
    fs::rename(itemFolder, target, ec);
    if (ec) {
        outcome.status = fail(ErrorCode::CannotWrite, "The material folder could not be moved to " + pathToUtf8(target) + ".");
        return outcome;
    }
    if (outcome.status = catalog.indexMaterial(info); !outcome.status.ok()) {
        fs::remove_all(target, ec);
        return outcome;
    }

    staging::removeKept(library.materialStaging, sourceText);
    outcome.materialId = id;
    outcome.warnings = info.warnings;
    outcome.thumbnailFailed = !rendered;
    const bool keepOriginal = request.keepOriginal.value_or(context.settings.keepImportedZips);
    if (plan.originalIsZip && !keepOriginal && !request.source.file.empty()) {
        const Status removed = context.removeOriginal ? context.removeOriginal(request.source.file) : moveToRecycleBin(request.source.file);
        outcome.originalRemoved = removed.ok();
    }
    sendProgress(onProgress, ImportStage::Done);
    return outcome;
}

// The inbox entries waiting for import.
std::vector<fs::path> materialInboxEntries(const LibraryPaths& library)
{
    std::vector<fs::path> out;
    const std::vector<std::string> suffixes = importableMaterialSuffixes();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(library.materialInbox, ec)) {
        const std::string name = pathToUtf8(entry.path().filename());
        if (name.empty() || name.front() == '.' || name.front() == '~') continue;
        if (entry.is_directory(ec)) {
            if (name == kLibraryInboxDoneFolder || name == kLibraryInboxFailedFolder) continue;
            out.push_back(entry.path());
        } else if (entry.is_regular_file(ec)) {
            std::string suffix = pathToUtf8(entry.path().extension());
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (std::find(suffixes.begin(), suffixes.end(), suffix) != suffixes.end()) out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return pathToUtf8(a.filename()) < pathToUtf8(b.filename());
    });
    return out;
}

// Imports one inbox entry and moves it out of the inbox.
MaterialImportOutcome importMaterialInboxEntry(MaterialCatalog& catalog, const ImportContext& context, const fs::path& entry,
                                               const std::function<void(const ImportProgress&)>& onProgress,
                                               std::stop_token stop)
{
    const LibraryPaths& library = catalog.paths();
    // A broken integrations.json says nothing about the entry, so the entry stays where it is.
    if (const IntegrationOptionsRead options = readIntegrationOptions(library); !options.status.ok()) {
        MaterialImportOutcome refused;
        refused.status = options.status;
        return refused;
    }
    MaterialImportRequest request;
    request.source.file = entry;
    MaterialImportOutcome outcome = importMaterial(catalog, context, request, onProgress, stop);

    std::error_code ec;
    if (outcome.status.ok()) {
        if (fs::exists(entry, ec)) outcome.movedTo = moveEntry(entry, library.materialInboxDone);
        return outcome;
    }
    if (leavesEntryInInbox(outcome.status.code) || !fs::exists(entry, ec)) return outcome;
    const fs::path moved = moveEntry(entry, library.materialInboxFailed);
    outcome.movedTo = moved;
    if (!moved.empty()) {
        fs::path note = moved;
        note += ".error.txt";
        std::ofstream(note, std::ios::binary | std::ios::trunc) << outcome.status.message << "\n";
    }
    return outcome;
}

// Cleans a material again and renders its thumbnail.
MaterialImportOutcome refreshMaterial(MaterialCatalog& catalog, const ImportContext& context, std::string_view materialId,
                                      const std::function<void(const ImportProgress&)>& onProgress, std::stop_token stop)
{
    MaterialImportOutcome outcome;
    outcome.materialId = std::string(materialId);
    outcome.importer = std::string(kTextureSetImporterId);
    sendProgress(onProgress, ImportStage::Preparing);

    const auto record = catalog.material(materialId);
    if (!record) {
        outcome.status = fail(ErrorCode::NotFound, "There is no material \"" + std::string(materialId) + "\".");
        return outcome;
    }
    const fs::path folder = catalog.paths().materialItems / pathFromUtf8(materialId);
    const fs::path infoFile = folder / pathFromUtf8(kMaterialInfoFileName);
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        outcome.status = fail(ErrorCode::NotFound, "The folder of the material " + std::string(materialId) + " is missing.");
        return outcome;
    }

    MaterialInfo info;
    const bool hasInfo = fs::is_regular_file(infoFile, ec);
    if (hasInfo) {
        MaterialInfoRead read = readMaterialInfo(infoFile, materialId);
        if (!read.status.ok()) {
            outcome.status = read.status;
            return outcome;
        }
        info = std::move(read.info);
    } else {
        info.id = std::string(materialId);
        info.name = record->name.empty() ? catalogNameFromStem(materialId) : record->name;
        info.source = {std::string(kSourceTextureSet), std::string(materialId), sessionNow()};
    }

    const std::string jobId = newJobId(std::string(materialId));
    WorkFolder work(catalog.paths().materialStaging / pathFromUtf8(jobId));
    const fs::path outFolder = work.path() / "item";
    fs::create_directories(outFolder, ec);
    if (ec) {
        outcome.status = fail(ErrorCode::CannotWrite, "The work folder " + pathToUtf8(work.path()) + " could not be created.");
        return outcome;
    }
    work.start();

    MaterialCleanupJob cleanup;
    cleanup.workerSource = std::string(kMaterialSourceTextureSet);
    cleanup.path = folder;
    std::set<MapChannel> named;
    if (hasInfo) {
        for (const MapChannel channel : allMapChannels()) {
            const MaterialMap& map = materialMap(info.maps, channel);
            if (map.file.empty() || !fs::is_regular_file(folder / pathFromUtf8(map.file), ec)) continue;
            cleanup.maps.emplace_back(channel, folder / pathFromUtf8(map.file));
            named.insert(channel);
        }
        cleanup.normal = NormalConvention::OpenGl;
        cleanup.tileSize = info.tileSize;
    }
    cleanup.outputFolder = outFolder;
    const Json job = makeMaterialCleanupJob(cleanup, jobId, work.path() / pathFromUtf8(kResultFileName));

    sendProgress(onProgress, ImportStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        context.blender.executable, job, work.path(), materialRunOptions(context),
        [&](const BlenderProgress& p) { sendProgress(onProgress, ImportStage::Blender, p); }, stop);
    if (!run.status.ok()) {
        const bool blenderProblem = run.status.code == ErrorCode::BlenderFailed;
        if (blenderProblem) {
            work.keep();
            outcome.keptWorkFolder = work.path();
        }
        outcome.status = blenderFailure(run.status, blenderProblem ? work.path() : fs::path());
        return outcome;
    }
    const MaterialJobOutput out = readMaterialCleanupResult(run.result, jobId);
    if (!out.header.status.ok()) {
        work.keep();
        outcome.keptWorkFolder = work.path();
        outcome.status = fail(ErrorCode::BlenderFailed, out.header.status.message + "\nThe work folder was kept for the log: " + pathToUtf8(work.path()));
        return outcome;
    }
    if (const std::string missing = missingMapFile(outFolder, out.maps); !missing.empty()) {
        work.keep();
        outcome.keptWorkFolder = work.path();
        outcome.status = fail(ErrorCode::BlenderFailed, "Blender did not write " + missing + ".\nThe work folder was kept for the log: " + pathToUtf8(work.path()));
        return outcome;
    }
    if (stop.stop_requested()) {
        outcome.status = fail(ErrorCode::Cancelled, "The refresh was cancelled.");
        return outcome;
    }

    sendProgress(onProgress, ImportStage::Saving);
    const MaterialMaps oldMaps = info.maps;
    std::set<std::string> written;
    for (const MapChannel channel : allMapChannels()) {
        const MaterialMap& map = materialMap(out.maps, channel);
        if (map.file.empty()) continue;
        const fs::path target = folder / pathFromUtf8(map.file);
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(outFolder / pathFromUtf8(map.file), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outcome.status = fail(ErrorCode::CannotWrite, "The map " + pathToUtf8(target) + " could not be saved.");
            return outcome;
        }
        written.insert(map.file);
    }
    for (const MapChannel channel : allMapChannels()) {
        const std::string& old = materialMap(oldMaps, channel).file;
        if (!old.empty() && !written.count(old)) fs::remove(folder / pathFromUtf8(old), ec);
    }

    info.maps = out.maps;
    for (const MapChannel channel : named) {
        MaterialMap& map = materialMap(info.maps, channel);
        const MaterialMap& old = materialMap(oldMaps, channel);
        if (map.origin == MapOrigin::File && old.origin != MapOrigin::None) {
            map.origin = old.origin;
            map.detail = old.detail;
        }
    }
    if (!hasInfo) info.tileSize = out.tileSize;
    info.textureSize = out.textureSize;
    const bool rendered = out.thumbnailRendered && fs::is_regular_file(outFolder / pathFromUtf8(kMaterialThumbFileName), ec);
    if (rendered) {
        fs::copy_file(outFolder / pathFromUtf8(kMaterialThumbFileName), folder / pathFromUtf8(kMaterialThumbFileName),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outcome.status = fail(ErrorCode::CannotWrite, "The thumbnail could not be saved in " + pathToUtf8(folder) + ".");
            return outcome;
        }
    } else {
        fs::remove(folder / pathFromUtf8(kMaterialThumbFileName), ec);
    }
    setThumbnail(info, out, rendered);
    info.files = measureMaterialFiles(folder, info.maps);
    info.blenderVersion = out.header.blenderVersion;
    info.warnings = out.header.warnings;

    if (outcome.status = writeMaterialInfo(infoFile, info); !outcome.status.ok()) return outcome;
    if (outcome.status = catalog.indexMaterial(info); !outcome.status.ok()) return outcome;
    outcome.warnings = info.warnings;
    outcome.thumbnailFailed = !rendered;
    if (!rendered) {
        outcome.status = fail(ErrorCode::BlenderFailed, info.thumbnail.error);
        return outcome;
    }
    sendProgress(onProgress, ImportStage::Done);
    return outcome;
}

}
