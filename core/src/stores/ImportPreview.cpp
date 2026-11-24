#include <plynth/core/catalog/ImportPreview.h>

#include "catalog/ImporterChoice.h"
#include "plynth/core/BlenderJob.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/ImportFetch.h"

#include <filesystem>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// A text that is empty becomes null.
Json textOrNull(const std::string& text)
{
    return text.empty() ? Json(nullptr) : Json(text);
}

// A path that is empty becomes null.
Json pathOrNull(const fs::path& path)
{
    return path.empty() ? Json(nullptr) : Json(pathToUtf8(path));
}

// The fetch part of a plan.
Json fetchJson(const std::optional<ImportFetch>& fetch)
{
    if (!fetch) return nullptr;
    return Json{{"task", fetch->task},
                {"kind", fetch->expectedKind},
                {"params", fetch->params}};
}

// A work folder for the plan that is removed when the object goes away.
class TempWork {
public:
    explicit TempWork(fs::path path) : path_(std::move(path))
    {
        std::error_code ec;
        fs::create_directories(path_, ec);
        ok_ = !ec;
    }
    ~TempWork()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempWork(const TempWork&) = delete;
    TempWork& operator=(const TempWork&) = delete;
    bool ok() const { return ok_; }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    bool ok_ = false;
};

// The refusal when no importer reads the source.
Status noImporter(const ImportSource& source, bool material)
{
    std::error_code ec;
    if (!source.file.empty() && !fs::exists(source.file, ec)) {
        return fail(ErrorCode::NotFound, "The file " + pathToUtf8(source.file) + " was not found.");
    }
    const std::string what = source.file.empty() ? source.url : pathToUtf8(source.file);
    return fail(ErrorCode::InvalidValue, "Plynth cannot import " + what + (material ? " as a material." : "."));
}

}

// Previews an asset import.
ImportPreview previewAssetImport(const LibraryPaths& library, const ImportRequest& request)
{
    ImportPreview preview;
    if (request.source.file.empty() && request.source.url.empty()) {
        preview.status = fail(ErrorCode::InvalidValue, "No file or address was given to import.");
        return preview;
    }
    const ChosenImporter<Importer> chosen = chooseAssetImporter(library, request);
    if (!chosen.status.ok()) {
        preview.status = chosen.status;
        return preview;
    }
    if (!chosen.importer) {
        preview.status = noImporter(request.source, false);
        return preview;
    }
    preview.importer = std::string(chosen.importer->id());
    if (const Integration* integration = integrationForImporter(preview.importer, false)) {
        preview.integration = std::string(integration->id());
    }
    preview.options = chosen.source.options;

    TempWork work(library.assetStaging / pathFromUtf8(newJobId("preview")));
    if (!work.ok()) {
        preview.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(work.path()) + " could not be created.");
        return preview;
    }
    ImportPlan plan;
    if (preview.status = chosen.importer->plan(chosen.source, work.path(), plan); !preview.status.ok()) {
        if (!plan.details.empty()) preview.plan = Json{{"details", plan.details}};
        return preview;
    }
    preview.plan = Json{{"file", plan.fetch ? Json(nullptr) : pathOrNull(plan.file)},
                        {"format", textOrNull(plan.format)},
                        {"source_kind", plan.sourceKind},
                        {"reference", plan.reference},
                        {"default_name", plan.defaultName},
                        {"license", textOrNull(plan.license)},
                        {"original_is_zip", plan.originalIsZip},
                        {"fetch", fetchJson(plan.fetch)},
                        {"details", plan.details}};
    return preview;
}

// Previews a material import.
ImportPreview previewMaterialImport(const LibraryPaths& library, const MaterialImportRequest& request)
{
    ImportPreview preview;
    if (request.source.file.empty() && request.source.url.empty()) {
        preview.status = fail(ErrorCode::InvalidValue, "No file or address was given to import.");
        return preview;
    }
    const ChosenImporter<MaterialImporter> chosen = chooseMaterialImporter(library, request);
    if (!chosen.status.ok()) {
        preview.status = chosen.status;
        return preview;
    }
    if (!chosen.importer) {
        preview.status = noImporter(request.source, true);
        return preview;
    }
    preview.importer = std::string(chosen.importer->id());
    if (const Integration* integration = integrationForImporter(preview.importer, true)) {
        preview.integration = std::string(integration->id());
    }
    preview.options = chosen.source.options;

    TempWork work(library.materialStaging / pathFromUtf8(newJobId("preview")));
    if (!work.ok()) {
        preview.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(work.path()) + " could not be created.");
        return preview;
    }
    MaterialImportPlan plan;
    if (preview.status = chosen.importer->plan(chosen.source, work.path(), plan); !preview.status.ok()) {
        if (!plan.details.empty()) preview.plan = Json{{"details", plan.details}};
        return preview;
    }
    preview.plan = Json{{"file", plan.fetch ? Json(nullptr) : pathOrNull(plan.path)},
                        {"worker_source", textOrNull(plan.workerSource)},
                        {"source_kind", plan.sourceKind},
                        {"reference", plan.reference},
                        {"default_name", plan.defaultName},
                        {"license", textOrNull(plan.license)},
                        {"original_is_zip", plan.originalIsZip},
                        {"blend_material", textOrNull(plan.blendMaterial)},
                        {"fetch", fetchJson(plan.fetch)},
                        {"details", plan.details}};
    return preview;
}

// The JSON of a preview.
Json importPreviewToJson(const ImportPreview& preview)
{
    return Json{{"importer", preview.importer},
                {"integration", preview.integration},
                {"options", preview.options},
                {"plan", preview.plan}};
}

}
