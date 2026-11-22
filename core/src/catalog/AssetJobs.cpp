#include "plynth/core/catalog/AssetJobs.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace plynth {

namespace {

// A problem found while reading a result.
struct Problem {
    std::string where;
    std::string message;
};

const Json& field(const Json& object, const char* key, const std::string& where)
{
    if (!object.is_object() || !object.contains(key)) throw Problem{where + "/" + key, std::string("Missing field \"") + key + "\"."};
    return object[key];
}

// The file form of a thumbnail spec.
Json thumbnailJson(const ThumbnailSpec& spec)
{
    Json json = Json::object();
    json["render"] = spec.render;
    json["size"] = spec.size;
    json["background"] = Json::array({spec.background[0], spec.background[1], spec.background[2]});
    return json;
}

// A list of text values.
std::vector<std::string> textList(const Json& list, const std::string& where)
{
    if (!list.is_array()) throw Problem{where, "Must be a list."};
    std::vector<std::string> out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!list[i].is_string()) throw Problem{where + "/" + std::to_string(i), "Must be text."};
        out.push_back(list[i].get<std::string>());
    }
    return out;
}

// A length in metres: finite and not negative.
double length(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_number()) throw Problem{where + "/" + key, "Must be a number."};
    const double d = v.get<double>();
    if (!std::isfinite(d) || d < 0.0) throw Problem{where + "/" + key, "Must be a length of at least 0 metres."};
    return d;
}

// A whole number of at least zero.
long long count(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_number_integer() || v.get<long long>() < 0) throw Problem{where + "/" + key, "Must be a whole number of at least 0."};
    return v.get<long long>();
}

// Says whether a map file is an image under textures/maps/ or textures/preview/ with a plain relative path.
bool isMapFile(const std::string& file)
{
    const std::string maps = std::string(kAssetMapsFolder) + "/";
    const std::string preview = std::string(kAssetPreviewFolder) + "/";
    if (file.rfind(maps, 0) != 0 && file.rfind(preview, 0) != 0) return false;
    for (const char c : file) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                           c == '.' || c == '/' || c == '-';
        if (!plain) return false;
    }
    std::size_t start = 0;
    while (start <= file.size()) {
        std::size_t end = file.find('/', start);
        if (end == std::string::npos) end = file.size();
        const std::string part = file.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    const std::size_t dot = file.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = file.substr(dot + 1);
    return ext == "png" || ext == "jpg" || ext == "jpeg";
}

// Reads one map of a material.
MaterialMap readMap(const Json& maps, MapChannel channel, const std::string& base)
{
    const std::string name(mapChannelName(channel));
    const std::string where = base + "/" + name;
    const Json& json = field(maps, name.c_str(), base);
    const Json& from = field(json, "from", where);
    const Json& file = field(json, "file", where);
    const Json& detail = field(json, "detail", where);
    if (!from.is_string()) throw Problem{where + "/from", "Must be text."};
    const auto origin = mapOriginFromName(from.get<std::string>());
    if (!origin) throw Problem{where + "/from", "Must be file, value, baked or none."};
    if (!detail.is_string()) throw Problem{where + "/detail", "Must be text."};
    const bool required = channel != MapChannel::Opacity && channel != MapChannel::Occlusion;

    MaterialMap map;
    map.origin = *origin;
    map.detail = detail.get<std::string>();
    if (*origin == MapOrigin::None) {
        if (required) throw Problem{where + "/from", "The " + name + " map must have a file."};
        if (!file.is_null()) throw Problem{where + "/file", "A map with origin none has no file."};
        return map;
    }
    if (!file.is_string() || !isMapFile(file.get<std::string>())) {
        throw Problem{where + "/file", "Must be an image in textures/maps/ or textures/preview/."};
    }
    map.file = file.get<std::string>();
    return map;
}

// Reads the material maps of a cleanup output.
std::vector<AssetMaterial> readMaterialMaps(const Json& list)
{
    if (!list.is_array()) throw Problem{"/output/material_maps", "Must be a list."};
    std::vector<AssetMaterial> out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string where = "/output/material_maps/" + std::to_string(i);
        const Json& entry = list[i];
        const Json& name = field(entry, "name", where);
        if (!name.is_string() || name.get<std::string>().empty()) throw Problem{where + "/name", "Must be the material's name."};
        AssetMaterial material;
        material.name = name.get<std::string>();
        const Json& maps = field(entry, "maps", where);
        const Json& preview = field(entry, "preview_maps", where);
        for (const MapChannel channel : allMapChannels()) {
            materialMap(material.maps, channel) = readMap(maps, channel, where + "/maps");
            materialMap(material.previewMaps, channel) = readMap(preview, channel, where + "/preview_maps");
        }
        out.push_back(std::move(material));
    }
    return out;
}

// Reads the thumbnail part of an output.
void readThumbnailPart(const Json& output, AssetJobOutput& out)
{
    const Json& thumb = field(output, "thumbnail", "/output");
    const Json& file = field(thumb, "file", "/output/thumbnail");
    const Json& engine = field(thumb, "engine", "/output/thumbnail");
    const Json& error = field(thumb, "error", "/output/thumbnail");
    if (!file.is_null() && !file.is_string()) throw Problem{"/output/thumbnail/file", "Must be text or null."};
    if (!engine.is_null() && !engine.is_string()) throw Problem{"/output/thumbnail/engine", "Must be text or null."};
    if (!error.is_null() && !error.is_string()) throw Problem{"/output/thumbnail/error", "Must be text or null."};
    out.thumbnailRendered = file.is_string();
    out.thumbnailEngine = engine.is_string() ? engine.get<std::string>() : "";
    out.thumbnailError = error.is_string() ? error.get<std::string>() : "";
}

// Reads the fbx_file field of an output: a name, or null when none was written.
void readFbxPart(const Json& output, AssetJobOutput& out)
{
    const Json& fbx = field(output, "fbx_file", "/output");
    if (!fbx.is_null() && !fbx.is_string()) throw Problem{"/output/fbx_file", "Must be text or null."};
    out.fbxFile = fbx.is_string() ? fbx.get<std::string>() : "";
}

// Reads the measures of an output.
void readMeasures(const Json& output, AssetJobOutput& out)
{
    const Json& size = field(output, "size", "/output");
    out.size.width = length(size, "width", "/output/size");
    out.size.depth = length(size, "depth", "/output/size");
    out.size.height = length(size, "height", "/output/size");
    out.triangles = count(output, "triangles", "/output");
    out.objects = static_cast<int>(count(output, "objects", "/output"));
    out.materials = textList(field(output, "materials", "/output"), "/output/materials");
    out.textures = textList(field(output, "textures", "/output"), "/output/textures");
}

}

// Makes a cleanup_asset job.
Json makeCleanupJob(const CleanupJob& job, std::string_view jobId, const std::filesystem::path& resultFile)
{
    Json params = Json::object();
    params["source"] = Json{{"file", pathToUtf8(job.sourceFile)}, {"format", job.format}};
    params["asset_id"] = job.assetId;
    params["output_folder"] = pathToUtf8(job.outputFolder);
    params["scale"] = job.scale;
    params["rotate_z"] = job.rotateZ;
    params["triangle_warning"] = job.triangleWarning;
    params["bake_size"] = job.bakeSize;
    params["thumbnail"] = thumbnailJson(job.thumbnail);
    return makeBlenderJob(kCleanupAssetTask, jobId, resultFile, std::move(params));
}

// Makes a render_thumbnail job.
Json makeThumbnailJob(const ThumbnailJob& job, std::string_view jobId, const std::filesystem::path& resultFile)
{
    Json params = Json::object();
    params["blend_file"] = pathToUtf8(job.blendFile);
    params["output_file"] = pathToUtf8(job.outputFile);
    params["measure"] = job.measure;
    params["fbx_file"] = job.fbxFile.empty() ? Json(nullptr) : Json(pathToUtf8(job.fbxFile));
    params["thumbnail"] = thumbnailJson(job.thumbnail);
    return makeBlenderJob(kRenderThumbnailTask, jobId, resultFile, std::move(params));
}

// Reads the result of a cleanup_asset job.
AssetJobOutput readCleanupResult(const Json& result, std::string_view jobId)
{
    AssetJobOutput out;
    out.header = readBlenderResultHeader(result, kCleanupAssetTask, jobId);
    if (!out.header.status.ok()) return out;
    try {
        const Json& output = result["output"];
        const Json& blend = field(output, "blend_file", "/output");
        if (!blend.is_string()) throw Problem{"/output/blend_file", "Must be text."};
        out.blendFile = blend.get<std::string>();
        readFbxPart(output, out);
        const Json& picked = field(output, "picked_file", "/output");
        if (!picked.is_null() && !picked.is_string()) throw Problem{"/output/picked_file", "Must be text or null."};
        out.pickedFile = picked.is_string() ? picked.get<std::string>() : "";
        readMeasures(output, out);
        out.measured = true;
        out.missingTextures = textList(field(output, "missing_textures", "/output"), "/output/missing_textures");
        out.materialMaps = readMaterialMaps(field(output, "material_maps", "/output"));
        readThumbnailPart(output, out);
    } catch (const Problem& p) {
        out.header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    }
    return out;
}

// Reads the result of a render_thumbnail job.
AssetJobOutput readThumbnailResult(const Json& result, std::string_view jobId)
{
    AssetJobOutput out;
    out.header = readBlenderResultHeader(result, kRenderThumbnailTask, jobId);
    if (!out.header.status.ok()) return out;
    try {
        const Json& output = result["output"];
        readFbxPart(output, out);
        const Json& size = field(output, "size", "/output");
        if (!size.is_null()) {
            readMeasures(output, out);
            out.measured = true;
        }
        readThumbnailPart(output, out);
    } catch (const Problem& p) {
        out.header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    }
    return out;
}

}
