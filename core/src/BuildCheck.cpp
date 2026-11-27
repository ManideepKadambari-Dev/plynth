#include "plynth/core/BuildCheck.h"

#include "plynth/core/Build.h"
#include "plynth/core/BuildJob.h"
#include "plynth/core/Generators.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Variation.h"
#include "plynth/core/catalog/AssetCatalog.h"
#include "plynth/core/catalog/BuildChoices.h"
#include "plynth/core/catalog/MaterialCatalog.h"

#include <memory>
#include <utility>

namespace plynth {

namespace {

// The catalogs of a library. A library whose assets catalog does not open counts as no library at all.
struct OpenedLibrary {
    std::unique_ptr<AssetCatalog> assets;
    std::unique_ptr<MaterialCatalog> materials;
};

// Opens both catalogs of a library, when one is given. The materials catalog is opened only after the assets one.
OpenedLibrary openLibraryCatalogs(const std::optional<LibraryPaths>& library)
{
    OpenedLibrary opened;
    if (!library) return opened;
    Status status;
    opened.assets = AssetCatalog::open(*library, status);
    if (!opened.assets) return opened;
    Status materialStatus;
    opened.materials = MaterialCatalog::open(*library, materialStatus);
    return opened;
}

// One prop of the preview: what it is, the rule that placed it, what it stands or hangs on and where.
Json propJson(const DressingPlacement& prop)
{
    Json entry = Json::object();
    entry["asset"] = prop.asset;
    entry["rule"] = prop.rule;
    entry["host"] = prop.host;
    entry["x"] = prop.position.x;
    entry["y"] = prop.position.y;
    entry["z"] = prop.z;
    entry["rotation"] = prop.rotation;
    return entry;
}

}

// Lists what a build would report, without Blender.
BuildCheck checkBuild(const Project& project, const std::optional<LibraryPaths>& library)
{
    BuildCheck check;
    check.planFingerprint = planFingerprint(project);
    check.status = checkBuildable(project.plan);
    if (!check.status.ok()) return check;

    const OpenedLibrary opened = openLibraryCatalogs(library);
    check.libraryOpened = opened.assets != nullptr;
    BuildAssetSource assets;
    BuildMaterialSource materials;
    GeneratorLibrary generators;
    if (opened.assets) {
        assets = buildAssetSourceFor(*opened.assets);
        generators = loadGeneratorFolder(library->generators);
        if (opened.materials) materials = buildMaterialSourceFor(*opened.materials);
    }
    const BuildChoices choices = buildChoicesFrom(opened.assets.get(), opened.materials.get());
    const ResolvedBuild resolved = resolveBuild(project, choices);
    const BuildPlan plan = makeBuildPlan(resolved.project, assets, materials, opened.assets ? &generators : nullptr,
                                         std::nullopt, &choices);
    check.problems = plan.problems;
    for (const BuildProblem& problem : check.problems) {
        (problem.severity == BuildSeverity::Error ? check.errors : check.warnings) += 1;
    }
    return check;
}

// The check in the form "plynth build --check" answers with.
Json buildCheckToJson(const BuildCheck& check)
{
    Json list = Json::array();
    for (const BuildProblem& problem : check.problems) list.push_back(buildProblemToJson(problem));
    Json result = Json::object();
    result["problems"] = std::move(list);
    result["error_count"] = check.errors;
    result["warning_count"] = check.warnings;
    result["plan_fingerprint"] = check.planFingerprint;
    return result;
}

// Works out what a seed would pick, without building.
VariationPreview previewVariation(const Project& project, std::optional<std::uint32_t> seed,
                                  const std::optional<LibraryPaths>& library)
{
    const OpenedLibrary opened = openLibraryCatalogs(library);
    Project tried = project;
    if (seed) tried.settings.variation.seed = *seed;
    VariationPreview preview;
    if (opened.assets) preview.library = library;
    const BuildChoices choices = buildChoicesFrom(opened.assets.get(), opened.materials.get());
    preview.resolved = resolveBuild(tried, choices);
    return preview;
}

// The preview in the form "plynth variation preview" answers with.
Json variationPreviewToJson(const VariationPreview& preview)
{
    const ResolvedBuild& resolved = preview.resolved;
    const VariationSettings& variation = resolved.project.settings.variation;
    Json result = Json::object();
    result["seed"] = variation.seed;
    result["furniture"] = variation.furniture;
    result["materials"] = variation.materials;
    result["library"] = preview.library ? Json(pathToUtf8(preview.library->root)) : Json(nullptr);
    Json changes = Json::array();
    for (const VariationChange& change : resolved.changes) {
        Json entry = Json::object();
        entry["target"] = std::string(variationTargetName(change.target));
        entry["object"] = change.object.empty() ? Json(nullptr) : Json(change.object);
        entry["from"] = change.from;
        entry["to"] = change.to;
        changes.push_back(std::move(entry));
    }
    Json frames = Json::array();
    for (const PickedFrame& frame : resolved.frames) {
        Json entry = Json::object();
        entry["opening"] = frame.opening;
        entry["asset"] = frame.asset;
        frames.push_back(std::move(entry));
    }
    Json rooms = Json::array();
    int count = 0;
    for (const Floor& floor : resolved.project.plan.floors) {
        const FloorDressing* dressing = nullptr;
        for (const FloorDressing& entry : resolved.dressing) {
            if (entry.floor == floor.id) dressing = &entry;
        }
        for (const Room& room : floor.rooms) {
            Json props = Json::array();
            if (dressing != nullptr) {
                for (const DressingPlacement& prop : dressing->props) {
                    if (prop.room == room.id) props.push_back(propJson(prop));
                }
            }
            count += static_cast<int>(props.size());
            Json entry = Json::object();
            entry["floor"] = floor.id;
            entry["room"] = room.id;
            entry["dressing"] = room.dressing;
            entry["props"] = std::move(props);
            rooms.push_back(std::move(entry));
        }
    }
    result["changes"] = std::move(changes);
    result["frames"] = std::move(frames);
    result["rooms"] = std::move(rooms);
    result["prop_count"] = count;
    return result;
}

}
