#include "plynth/core/catalog/AssetImport.h"

#include "Staging.h"

#include "plynth/core/Process.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetJobs.h"

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

// The write time of a file in whole seconds.
SessionTime fileTime(const fs::path& file)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) return {};
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::time_point_cast<std::chrono::seconds>(system);
}

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

// The part of a source's name that an id and a name are made from.
std::string sourceStem(const ImportSource& source)
{
    if (!source.file.empty()) {
        fs::path p = source.file;
        if (!p.has_filename()) p = p.parent_path();
        std::error_code ec;
        return pathToUtf8(fs::is_directory(source.file, ec) ? p.filename() : p.stem());
    }
    std::string text = source.url;
    const std::size_t query = text.find_first_of("?#");
    if (query != std::string::npos) text.resize(query);
    while (!text.empty() && text.back() == '/') text.pop_back();
    const std::size_t slash = text.rfind('/');
    if (slash != std::string::npos) text = text.substr(slash + 1);
    const std::size_t dot = text.rfind('.');
    if (dot != std::string::npos && dot > 0) text.resize(dot);
    return text;
}

// The settings of the thumbnail the worker renders.
ThumbnailSpec thumbnailSpec(const MachineSettings& settings)
{
    ThumbnailSpec spec;
    spec.engine = settings.thumbnailEngine;
    return spec;
}

// The Blender options for asset jobs: always the factory startup, so the user's add-ons cannot change results.
BlenderRunOptions assetRunOptions(const ImportContext& context)
{
    BlenderRunOptions run = context.run;
    run.factoryStartup = true;
    return run;
}

// Turns a failed Blender run into the status an import reports. The work folder is named in the message when
// it is kept.
Status blenderFailure(const BlenderJobRun& run, const fs::path& kept)
{
    Status s = run.status;
    if (s.code == ErrorCode::BlenderFailed && !kept.empty()) {
        s.message += "\nThe work folder was kept for the log: " + pathToUtf8(kept);
    }
    return s;
}

// Sorted unique values.
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

// Checks the parts of a request that need no catalog.
Status checkRequest(const ImportRequest& request)
{
    if (request.source.file.empty() && request.source.url.empty()) {
        return fail(ErrorCode::InvalidValue, "No file or address was given to import.");
    }
    if (!std::isfinite(request.scale) || request.scale <= 0.0) {
        return fail(ErrorCode::InvalidValue, "The scale must be a number above zero.");
    }
    if (request.rotateZ != 0 && request.rotateZ != 90 && request.rotateZ != 180 && request.rotateZ != 270) {
        return fail(ErrorCode::InvalidValue, "The rotation must be 0, 90, 180 or 270 degrees.");
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
    for (const std::string& category : request.categories) {
        if (Status s = checkCatalogId(category); !s.ok()) return s;
    }
    if (!request.license.empty()) {
        if (Status s = checkCatalogId(request.license); !s.ok()) return s;
    }
    return {};
}

// Checks that the categories and license of a request are in the catalog.
Status checkAgainstCatalog(const AssetCatalog& catalog, const ImportRequest& request)
{
    if (!request.categories.empty()) {
        std::set<std::string> known;
        for (const CategoryEntry& c : catalog.categories()) known.insert(c.id);
        for (const std::string& category : request.categories) {
            if (!known.count(category)) return fail(ErrorCode::NotFound, "There is no category \"" + category + "\".");
        }
    }
    if (!request.license.empty()) {
        bool found = false;
        for (const LicenseEntry& l : catalog.licenses()) found = found || l.id == request.license;
        if (!found) return fail(ErrorCode::NotFound, "There is no license \"" + request.license + "\".");
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

}

// The name used in progress lines.
std::string_view importStageName(ImportStage stage)
{
    switch (stage) {
    case ImportStage::Preparing: return "preparing";
    case ImportStage::Blender: return "blender";
    case ImportStage::Saving: return "saving";
    case ImportStage::Done: return "done";
    }
    return "preparing";
}

// Imports one file, folder or address.
ImportOutcome importAsset(AssetCatalog& catalog, const ImportContext& context, const ImportRequest& request,
                          const std::function<void(const ImportProgress&)>& onProgress, std::stop_token stop)
{
    ImportOutcome outcome;
    const auto report = [&](ImportStage stage, const BlenderProgress& blender = {}) {
        if (!onProgress) return;
        ImportProgress p;
        p.stage = stage;
        p.blender = blender;
        onProgress(p);
    };
    report(ImportStage::Preparing);

    if (outcome.status = checkRequest(request); !outcome.status.ok()) return outcome;
    const Importer* importer = pickImporter(request.source);
    if (!importer) {
        std::string suffixes;
        for (const std::string& s : importableSuffixes()) suffixes += (suffixes.empty() ? "" : ", ") + s;
        const std::string what = request.source.file.empty() ? request.source.url : pathToUtf8(request.source.file);
        std::error_code ec;
        if (!request.source.file.empty() && !fs::exists(request.source.file, ec)) {
            outcome.status = fail(ErrorCode::NotFound, "The file " + what + " was not found.");
        } else {
            outcome.status = fail(ErrorCode::InvalidValue,
                                  "Plynth cannot import " + what + ". It reads these file endings: " + suffixes +
                                      ", and folders that hold a model.");
        }
        return outcome;
    }
    outcome.importer = std::string(importer->id());
    if (outcome.status = checkAgainstCatalog(catalog, request); !outcome.status.ok()) return outcome;

    std::string id = request.id;
    if (!id.empty()) {
        if (catalog.idTaken(id)) {
            outcome.status = fail(ErrorCode::AlreadyExists, "The id \"" + id + "\" is already used by another asset.");
            return outcome;
        }
    } else {
        id = uniqueCatalogId(catalogIdFromName(sourceStem(request.source)),
                             [&](std::string_view candidate) { return catalog.idTaken(candidate); });
    }
    if (stop.stop_requested()) {
        outcome.status = fail(ErrorCode::Cancelled, "The import was cancelled.");
        return outcome;
    }

    const LibraryPaths& library = catalog.paths();
    const std::string jobId = newJobId(id);
    WorkFolder work(library.assetStaging / pathFromUtf8(jobId));
    const fs::path itemFolder = work.path() / "item";
    std::error_code ec;
    // The folder is marked as in use before anything else is put in it, so a library init in another process
    // never sees it unmarked for longer than it takes to write the marker.
    fs::create_directories(work.path(), ec);
    if (!ec) work.start();
    if (!ec) fs::create_directories(itemFolder, ec);
    if (ec) {
        outcome.status = fail(ErrorCode::CannotWrite, "The work folder " + pathToUtf8(work.path()) + " could not be created.");
        return outcome;
    }
    const std::string sourceText = request.source.file.empty() ? request.source.url : pathToUtf8(request.source.file);

    ImportPlan plan;
    if (outcome.status = importer->plan(request.source, work.path(), plan); !outcome.status.ok()) return outcome;

    CleanupJob cleanup;
    cleanup.sourceFile = plan.file;
    cleanup.format = plan.format;
    cleanup.assetId = id;
    cleanup.outputFolder = itemFolder;
    cleanup.scale = request.scale;
    cleanup.rotateZ = request.rotateZ;
    cleanup.triangleWarning = context.settings.triangleWarning;
    cleanup.thumbnail = thumbnailSpec(context.settings);
    const Json job = makeCleanupJob(cleanup, jobId, work.path() / pathFromUtf8(kResultFileName));

    report(ImportStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        context.blender.executable, job, work.path(), assetRunOptions(context),
        [&](const BlenderProgress& p) { report(ImportStage::Blender, p); }, stop);
    if (!run.status.ok()) {
        const bool blenderProblem = run.status.code == ErrorCode::BlenderFailed;
        if (blenderProblem) work.keep(sourceText);
        outcome.status = blenderFailure(run, blenderProblem ? work.path() : fs::path());
        if (blenderProblem) outcome.keptWorkFolder = work.path();
        return outcome;
    }
    const AssetJobOutput out = readCleanupResult(run.result, jobId);
    if (!out.header.status.ok()) {
        work.keep(sourceText);
        outcome.keptWorkFolder = work.path();
        outcome.status = fail(ErrorCode::BlenderFailed, out.header.status.message + "\nThe work folder was kept for the log: " + pathToUtf8(work.path()));
        return outcome;
    }
    if (stop.stop_requested()) {
        outcome.status = fail(ErrorCode::Cancelled, "The import was cancelled.");
        return outcome;
    }

    report(ImportStage::Saving);
    AssetInfo info;
    info.id = id;
    info.name = request.name.empty() ? plan.defaultName : request.name;
    if (info.name.empty()) info.name = catalogNameFromStem(id);
    info.categories = withoutRepeats(request.categories);
    info.tags = sortedUnique(request.tags);
    info.license = !request.license.empty() ? request.license : !plan.license.empty() ? plan.license : std::string(kUnknownLicenseId);
    info.source = {plan.sourceKind, plan.reference, sessionNow()};
    info.size = out.size;
    info.triangles = out.triangles;
    info.objects = out.objects;
    info.materials = out.materials;
    info.textures = out.textures;
    const fs::path blend = itemFolder / pathFromUtf8(kAssetBlendFileName);
    if (!fs::is_regular_file(blend, ec)) {
        outcome.status = fail(ErrorCode::BlenderFailed, "Blender did not save asset.blend.");
        work.keep(sourceText);
        outcome.keptWorkFolder = work.path();
        return outcome;
    }
    info.blendBytes = static_cast<long long>(fs::file_size(blend, ec));
    info.blendModified = fileTime(blend);
    const bool rendered = out.thumbnailRendered && fs::is_regular_file(itemFolder / pathFromUtf8(kAssetThumbFileName), ec);
    info.thumbnail.rendered = rendered;
    info.thumbnail.engine = rendered ? out.thumbnailEngine : "";
    info.thumbnail.renderedAt = rendered ? sessionNow() : SessionTime{};
    if (!rendered) {
        info.thumbnail.error = out.thumbnailError.empty() ? "The thumbnail was not rendered." : out.thumbnailError;
    }
    info.blenderVersion = out.header.blenderVersion;
    info.warnings = out.header.warnings;

    if (outcome.status = writeAssetInfo(itemFolder / pathFromUtf8(kAssetInfoFileName), info); !outcome.status.ok()) return outcome;
    const fs::path target = library.assetItems / pathFromUtf8(id);
    if (fs::exists(target, ec)) {
        outcome.status = fail(ErrorCode::AlreadyExists, "The id \"" + id + "\" was taken while the import ran.");
        return outcome;
    }
    fs::create_directories(library.assetItems, ec);
    fs::rename(itemFolder, target, ec);
    if (ec) {
        outcome.status = fail(ErrorCode::CannotWrite, "The asset folder could not be moved to " + pathToUtf8(target) + ".");
        return outcome;
    }
    if (outcome.status = catalog.indexAsset(info); !outcome.status.ok()) {
        fs::remove_all(target, ec);
        return outcome;
    }

    staging::removeKept(library.assetStaging, sourceText);
    outcome.assetId = id;
    outcome.warnings = info.warnings;
    outcome.thumbnailFailed = !rendered;
    const bool keepOriginal = request.keepOriginal.value_or(context.settings.keepImportedZips);
    if (plan.originalIsZip && !keepOriginal && !request.source.file.empty()) {
        const Status removed = context.removeOriginal ? context.removeOriginal(request.source.file) : moveToRecycleBin(request.source.file);
        outcome.originalRemoved = removed.ok();
    }
    report(ImportStage::Done);
    return outcome;
}

// The inbox entries waiting for import.
std::vector<fs::path> inboxEntries(const LibraryPaths& library)
{
    std::vector<fs::path> out;
    const std::vector<std::string> suffixes = importableSuffixes();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(library.assetInbox, ec)) {
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
        const std::string x = pathToUtf8(a.filename());
        const std::string y = pathToUtf8(b.filename());
        return x < y;
    });
    return out;
}

// Imports one inbox entry and moves it out of the inbox.
ImportOutcome importInboxEntry(AssetCatalog& catalog, const ImportContext& context, const fs::path& entry,
                               const std::function<void(const ImportProgress&)>& onProgress, std::stop_token stop)
{
    const LibraryPaths& library = catalog.paths();
    ImportRequest request;
    request.source.file = entry;
    ImportOutcome outcome = importAsset(catalog, context, request, onProgress, stop);

    std::error_code ec;
    if (outcome.status.ok()) {
        if (fs::exists(entry, ec)) moveEntry(entry, library.assetInboxDone);
        return outcome;
    }
    if (leavesEntryInInbox(outcome.status.code) || !fs::exists(entry, ec)) return outcome;
    const fs::path moved = moveEntry(entry, library.assetInboxFailed);
    if (!moved.empty()) {
        fs::path note = moved;
        note += ".error.txt";
        std::ofstream(note, std::ios::binary | std::ios::trunc) << outcome.status.message << "\n";
    }
    return outcome;
}

// Measures an asset again and renders its thumbnail.
ImportOutcome refreshAsset(AssetCatalog& catalog, const ImportContext& context, std::string_view assetId,
                           const std::function<void(const ImportProgress&)>& onProgress, std::stop_token stop)
{
    ImportOutcome outcome;
    outcome.assetId = std::string(assetId);
    const auto report = [&](ImportStage stage, const BlenderProgress& blender = {}) {
        if (!onProgress) return;
        ImportProgress p;
        p.stage = stage;
        p.blender = blender;
        onProgress(p);
    };
    report(ImportStage::Preparing);

    const auto record = catalog.asset(assetId);
    if (!record) {
        outcome.status = fail(ErrorCode::NotFound, "There is no asset \"" + std::string(assetId) + "\".");
        return outcome;
    }
    const fs::path folder = catalog.paths().assetItems / pathFromUtf8(assetId);
    const fs::path blend = folder / pathFromUtf8(kAssetBlendFileName);
    std::error_code ec;
    if (!fs::is_regular_file(blend, ec)) {
        outcome.status = fail(ErrorCode::NotFound, "The asset " + std::string(assetId) + " has no asset.blend.");
        return outcome;
    }
    AssetInfoRead read = readAssetInfo(folder / pathFromUtf8(kAssetInfoFileName), assetId);
    if (!read.status.ok()) {
        outcome.status = read.status;
        return outcome;
    }
    AssetInfo info = std::move(read.info);

    const std::string jobId = newJobId(std::string(assetId));
    WorkFolder work(catalog.paths().assetStaging / pathFromUtf8(jobId));
    fs::create_directories(work.path(), ec);
    if (ec) {
        outcome.status = fail(ErrorCode::CannotWrite, "The work folder " + pathToUtf8(work.path()) + " could not be created.");
        return outcome;
    }
    work.start();

    ThumbnailJob thumb;
    thumb.blendFile = blend;
    thumb.outputFile = work.path() / pathFromUtf8(kAssetThumbFileName);
    thumb.measure = true;
    thumb.thumbnail = thumbnailSpec(context.settings);
    const Json job = makeThumbnailJob(thumb, jobId, work.path() / pathFromUtf8(kResultFileName));

    report(ImportStage::Blender);
    const BlenderJobRun run = runBlenderJob(
        context.blender.executable, job, work.path(), assetRunOptions(context),
        [&](const BlenderProgress& p) { report(ImportStage::Blender, p); }, stop);

    report(ImportStage::Saving);
    if (!run.status.ok()) {
        if (run.status.code == ErrorCode::BlenderFailed && run.errorCode == "render_failed") {
            info.thumbnail.rendered = false;
            info.thumbnail.error = run.status.message;
            info.thumbnail.engine.clear();
            fs::remove(folder / pathFromUtf8(kAssetThumbFileName), ec);
            if (Status s = writeAssetInfo(folder / pathFromUtf8(kAssetInfoFileName), info); s.ok()) catalog.indexAsset(info);
            outcome.thumbnailFailed = true;
            outcome.status = run.status;
            return outcome;
        }
        const bool blenderProblem = run.status.code == ErrorCode::BlenderFailed;
        if (blenderProblem) {
            work.keep();
            outcome.keptWorkFolder = work.path();
        }
        outcome.status = blenderFailure(run, blenderProblem ? work.path() : fs::path());
        return outcome;
    }
    const AssetJobOutput out = readThumbnailResult(run.result, jobId);
    if (!out.header.status.ok()) {
        work.keep();
        outcome.keptWorkFolder = work.path();
        outcome.status = fail(ErrorCode::BlenderFailed, out.header.status.message + "\nThe work folder was kept for the log: " + pathToUtf8(work.path()));
        return outcome;
    }

    if (out.measured) {
        info.size = out.size;
        info.triangles = out.triangles;
        info.objects = out.objects;
        info.materials = out.materials;
        info.textures = out.textures;
    }
    info.blendBytes = static_cast<long long>(fs::file_size(blend, ec));
    info.blendModified = fileTime(blend);
    const bool rendered = out.thumbnailRendered && fs::is_regular_file(thumb.outputFile, ec);
    if (rendered) {
        fs::copy_file(thumb.outputFile, folder / pathFromUtf8(kAssetThumbFileName), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outcome.status = fail(ErrorCode::CannotWrite, "The thumbnail could not be saved in " + pathToUtf8(folder) + ".");
            return outcome;
        }
        info.thumbnail.rendered = true;
        info.thumbnail.engine = out.thumbnailEngine;
        info.thumbnail.renderedAt = sessionNow();
        info.thumbnail.error.clear();
    } else {
        info.thumbnail.rendered = false;
        info.thumbnail.engine.clear();
        info.thumbnail.error = out.thumbnailError.empty() ? "The thumbnail was not rendered." : out.thumbnailError;
    }
    info.blenderVersion = out.header.blenderVersion;
    info.warnings = out.header.warnings;
    if (outcome.status = writeAssetInfo(folder / pathFromUtf8(kAssetInfoFileName), info); !outcome.status.ok()) return outcome;
    if (outcome.status = catalog.indexAsset(info); !outcome.status.ok()) return outcome;
    outcome.warnings = info.warnings;
    outcome.thumbnailFailed = !rendered;
    report(ImportStage::Done);
    return outcome;
}

}
