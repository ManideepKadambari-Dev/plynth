#include "plynth/core/ExportJob.h"

#include "plynth/core/BuildGeometry.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <limits>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Base color pixels per metre below which a surface gets a warning.
constexpr double kLowTexelDensity = 256.0;

struct Problem {
    std::string where;
    std::string message;
};

// A field that must be there.
const Json& field(const Json& object, const char* key, const std::string& where)
{
    if (!object.is_object() || !object.contains(key)) throw Problem{where + "/" + key, std::string("Missing field \"") + key + "\"."};
    return object[key];
}

// A whole number of at least zero.
long long count(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_number_integer() || v.get<long long>() < 0) throw Problem{where + "/" + key, "Must be a whole number of at least 0."};
    return v.get<long long>();
}

// A required text.
std::string text(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_string()) throw Problem{where + "/" + key, "Must be text."};
    return v.get<std::string>();
}

// Says whether a name is a plain file name, with no folder in it.
bool isPlainFileName(const std::string& name)
{
    return !name.empty() && name.find('/') == std::string::npos && name.find('\\') == std::string::npos && name != "." &&
           name != "..";
}

// A file name with no folder in it.
std::string fileName(const Json& object, const char* key, const std::string& where)
{
    std::string name = text(object, key, where);
    if (!isPlainFileName(name)) throw Problem{where + "/" + key, "Must be a file name without a folder."};
    return name;
}

// Text, or null for an empty text.
std::string optionalText(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_null() && !v.is_string()) throw Problem{where + "/" + key, "Must be text or null."};
    return v.is_string() ? v.get<std::string>() : std::string();
}

// A list.
const Json& list(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_array()) throw Problem{where + "/" + key, "Must be a list."};
    return v;
}

// An absolute path in the file form: UTF-8 with forward slashes.
std::string absolutePath(const fs::path& path)
{
    std::error_code ec;
    const fs::path full = fs::absolute(path, ec);
    return pathToUtf8(ec ? path : full);
}

// A path in the file form, or null when empty.
Json pathOrNull(const fs::path& path)
{
    return path.empty() ? Json(nullptr) : Json(absolutePath(path));
}

// [x, y, z].
Json xyz(const Vec3& p)
{
    return Json::array({p.x, p.y, p.z});
}

// One built material in the form of build/materials.json.
Json materialJson(const BuiltMaterial& m)
{
    Json json = Json::object();
    json["name"] = m.name;
    json["material"] = m.material.empty() ? Json(nullptr) : Json(m.material);
    json["surface"] = m.surface.empty() ? Json(nullptr) : Json(m.surface);
    if (!m.asset.empty()) json["asset"] = m.asset;
    Json maps = Json::object();
    maps["base_color"] = absolutePath(m.baseColor);
    maps["normal"] = absolutePath(m.normal);
    maps["roughness"] = absolutePath(m.roughness);
    maps["metallic"] = absolutePath(m.metallic);
    maps["opacity"] = pathOrNull(m.opacity);
    maps["occlusion"] = pathOrNull(m.occlusion);
    json["maps"] = std::move(maps);
    json["alpha_mode"] = m.alphaMode == AlphaMode::Blend ? "blend" : "opaque";
    return json;
}

}

// "pass", "warning", "fail" or "skipped".
std::string_view qualityStatusName(QualityStatus status)
{
    switch (status) {
    case QualityStatus::Pass: return "pass";
    case QualityStatus::Warning: return "warning";
    case QualityStatus::Fail: return "fail";
    case QualityStatus::Skipped: return "skipped";
    }
    return "pass";
}

// The status with this name. Empty when unknown.
std::optional<QualityStatus> qualityStatusFromName(std::string_view name)
{
    for (const QualityStatus s : {QualityStatus::Pass, QualityStatus::Warning, QualityStatus::Fail, QualityStatus::Skipped}) {
        if (qualityStatusName(s) == name) return s;
    }
    return std::nullopt;
}

// "scene" or "file".
std::string_view qualityPhaseName(QualityPhase phase)
{
    return phase == QualityPhase::File ? "file" : "scene";
}

// The phase with this name. Empty when unknown.
std::optional<QualityPhase> qualityPhaseFromName(std::string_view name)
{
    if (name == "scene") return QualityPhase::Scene;
    if (name == "file") return QualityPhase::File;
    return std::nullopt;
}

// The world bounds of every wall of the project.
std::optional<ExportBounds> expectedWallBounds(const Project& project)
{
    const std::vector<double> levels = floorElevations(project.plan);
    constexpr double inf = std::numeric_limits<double>::infinity();
    ExportBounds bounds{{inf, inf, inf}, {-inf, -inf, -inf}};
    bool any = false;
    for (std::size_t i = 0; i < project.plan.floors.size(); ++i) {
        for (const WallSolid& solid : wallSolids(project.plan.floors[i])) {
            const double low = levels[i];
            const double high = levels[i] + solid.height;
            for (const Vec2& p : solid.outline.points) {
                bounds.min = {std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y), std::min(bounds.min.z, low)};
                bounds.max = {std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y), std::max(bounds.max.z, high)};
                any = true;
            }
        }
    }
    if (!any) return std::nullopt;
    return bounds;
}

// Makes the export job.
Json makeExportJob(const Project& project, const std::vector<ExportGroup>& groups,
                   const std::vector<BuiltMaterial>& materials, const ExportBuildState& build,
                   const ExportSettings& settings, const fs::path& projectFolder, const fs::path& stagingFolder,
                   std::string_view jobId, const fs::path& resultFile)
{
    Json params = Json::object();
    Json info = Json::object();
    info["name"] = project.settings.name;
    info["folder"] = absolutePath(projectFolder);
    params["project"] = std::move(info);
    params["blend_file"] = absolutePath(projectFolder / pathFromUtf8("build") / pathFromUtf8("building.blend"));
    params["output_folder"] = absolutePath(stagingFolder);
    params["textures_folder"] = std::string(kExportTexturesFolder);

    Json groupList = Json::array();
    for (const ExportGroup& g : groups) {
        Json group = Json::object();
        group["id"] = g.id;
        group["file"] = g.file;
        group["kinds"] = g.kinds;
        group["ids"] = g.ids;
        groupList.push_back(std::move(group));
    }
    params["groups"] = std::move(groupList);

    Json materialList = Json::array();
    for (const BuiltMaterial& m : materials) materialList.push_back(materialJson(m));
    params["materials"] = std::move(materialList);

    if (const std::optional<ExportBounds> bounds = expectedWallBounds(project)) {
        params["expected_bounds"] = Json{{"min", xyz(bounds->min)}, {"max", xyz(bounds->max)}};
    } else {
        params["expected_bounds"] = nullptr;
    }

    Json problems = Json::array();
    for (const BuildProblem& p : build.problems) problems.push_back(buildProblemToJson(p));
    params["build"] = Json{{"report_found", build.reportFound}, {"plan_changed", build.planChanged}, {"problems", std::move(problems)}};

    params["limits"] = Json{{"object_triangles", kObjectTriangleWarning},
                            {"total_triangles", kTotalTriangleWarning},
                            {"low_texel_density", kLowTexelDensity},
                            {"texel_warning_ratio", kTexelWarningRatio},
                            {"texel_fail_ratio", kTexelFailRatio},
                            {"bounds_tolerance", kBoundsTolerance}};
    params["force"] = settings.force;
    params["write"] = settings.write;
    return makeBlenderJob(kExportTask, jobId, resultFile, std::move(params));
}

// Reads the result of an export job.
ExportWorkerOutput readExportResult(const Json& result, std::string_view jobId)
{
    ExportWorkerOutput out;
    out.header = readBlenderResultHeader(result, kExportTask, jobId);
    if (!out.header.status.ok()) return out;
    try {
        const Json& output = field(result, "output", "");
        if (!output.is_object()) throw Problem{"/output", "Must be an object."};
        const Json& written = field(output, "written", "/output");
        if (!written.is_boolean()) throw Problem{"/output/written", "Must be true or false."};
        out.written = written.get<bool>();

        const Json& files = list(output, "files", "/output");
        for (std::size_t i = 0; i < files.size(); ++i) {
            const std::string where = "/output/files/" + std::to_string(i);
            ExportFileOutput file;
            file.group = text(files[i], "group", where);
            file.file = fileName(files[i], "file", where);
            file.objects = count(files[i], "objects", where);
            file.triangles = count(files[i], "triangles", where);
            out.files.push_back(std::move(file));
        }
        const Json& textures = list(output, "textures", "/output");
        for (std::size_t i = 0; i < textures.size(); ++i) {
            const std::string where = "/output/textures/" + std::to_string(i);
            if (!textures[i].is_string() || !isPlainFileName(textures[i].get<std::string>())) {
                throw Problem{where, "Must be a file name without a folder."};
            }
            out.textures.push_back(textures[i].get<std::string>());
        }
        out.objects = count(output, "objects", "/output");
        out.triangles = count(output, "triangles", "/output");
        out.collisionObjects = count(output, "collision_objects", "/output");

        const Json& checks = list(output, "checks", "/output");
        for (std::size_t i = 0; i < checks.size(); ++i) {
            const std::string where = "/output/checks/" + std::to_string(i);
            QualityCheckResult check;
            check.id = text(checks[i], "id", where);
            check.name = text(checks[i], "name", where);
            const std::optional<QualityPhase> phase = qualityPhaseFromName(text(checks[i], "phase", where));
            if (!phase) throw Problem{where + "/phase", "Must be scene or file."};
            check.phase = *phase;
            const std::optional<QualityStatus> status = qualityStatusFromName(text(checks[i], "status", where));
            if (!status) throw Problem{where + "/status", "Must be pass, warning, fail or skipped."};
            check.status = *status;
            check.message = text(checks[i], "message", where);
            const Json& findings = list(checks[i], "findings", where);
            for (std::size_t k = 0; k < findings.size(); ++k) {
                const std::string at = where + "/findings/" + std::to_string(k);
                QualityFinding finding;
                finding.object = optionalText(findings[k], "object", at);
                finding.planId = optionalText(findings[k], "plan_id", at);
                finding.message = text(findings[k], "message", at);
                check.findings.push_back(std::move(finding));
            }
            out.checks.push_back(std::move(check));
        }

        const Json& steps = list(output, "steps", "/output");
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const std::string where = "/output/steps/" + std::to_string(i);
            out.steps.push_back({text(steps[i], "step", where), count(steps[i], "ms", where)});
        }
    } catch (const Problem& p) {
        const BlenderResultHeader header = out.header;
        out = ExportWorkerOutput{};
        out.header = header;
        out.header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    }
    return out;
}

}
