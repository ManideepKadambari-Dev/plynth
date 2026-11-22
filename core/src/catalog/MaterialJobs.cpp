#include "plynth/core/catalog/MaterialJobs.h"

#include "plynth/core/ProjectFiles.h"

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

// A tile size as the job file writes it.
Json tileJson(const TileSize& size)
{
    return Json{{"width", size.width}, {"height", size.height}};
}

// A number field that must be finite.
double number(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_number()) throw Problem{where + "/" + key, "Must be a number."};
    const double d = v.get<double>();
    if (!std::isfinite(d)) throw Problem{where + "/" + key, "Must be a finite number."};
    return d;
}

// A whole number of pixels, at least 1.
int pixels(const Json& object, const char* key, const std::string& where)
{
    const Json& v = field(object, key, where);
    if (!v.is_number_integer() || v.get<long long>() < 1 || v.get<long long>() > 1000000) {
        throw Problem{where + "/" + key, "Must be a whole number of pixels, at least 1."};
    }
    return static_cast<int>(v.get<long long>());
}

// Says whether a map file name is a plain file directly inside textures/.
bool isTexturesFile(const std::string& file)
{
    const std::string prefix = std::string(kMaterialTexturesFolder) + "/";
    if (file.size() <= prefix.size() || file.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string name = file.substr(prefix.size());
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos && name != "." && name != "..";
}

// Reads one map of the result.
MaterialMap readMap(const Json& maps, MapChannel channel)
{
    const std::string name(mapChannelName(channel));
    const std::string where = "/output/maps/" + name;
    const Json& json = field(maps, name.c_str(), "/output/maps");
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
    if (!file.is_string() || !isTexturesFile(file.get<std::string>())) {
        throw Problem{where + "/file", "Must be a file in textures/, for example textures/" + name + ".png."};
    }
    map.file = file.get<std::string>();
    return map;
}

// What material.json said: text fields and lists, each only when present.
MaterialSourceInfo readSourceInfo(const Json& json)
{
    const std::string where = "/output/source_info";
    if (!json.is_object()) throw Problem{where, "Must be an object or null."};
    MaterialSourceInfo info;
    const auto text = [&](const char* key) -> std::optional<std::string> {
        if (!json.contains(key)) return std::nullopt;
        if (!json[key].is_string()) throw Problem{where + "/" + key, "Must be text."};
        return json[key].get<std::string>();
    };
    const auto list = [&](const char* key) -> std::optional<std::vector<std::string>> {
        if (!json.contains(key)) return std::nullopt;
        if (!json[key].is_array()) throw Problem{where + "/" + key, "Must be a list."};
        std::vector<std::string> out;
        for (std::size_t i = 0; i < json[key].size(); ++i) {
            if (!json[key][i].is_string()) throw Problem{where + "/" + key + "/" + std::to_string(i), "Must be text."};
            out.push_back(json[key][i].get<std::string>());
        }
        return out;
    };
    info.id = text("id");
    info.name = text("name");
    info.groups = list("groups");
    info.tags = list("tags");
    info.license = text("license");
    return info;
}

}

// "auto", "opengl" or "directx".
std::string_view normalConventionName(NormalConvention convention)
{
    switch (convention) {
    case NormalConvention::Auto: return "auto";
    case NormalConvention::OpenGl: return "opengl";
    case NormalConvention::DirectX: return "directx";
    }
    return "";
}

// The convention of a name.
std::optional<NormalConvention> normalConventionFromName(std::string_view name)
{
    for (const NormalConvention c : {NormalConvention::Auto, NormalConvention::OpenGl, NormalConvention::DirectX}) {
        if (normalConventionName(c) == name) return c;
    }
    return std::nullopt;
}

// Makes a cleanup_material job.
Json makeMaterialCleanupJob(const MaterialCleanupJob& job, std::string_view jobId, const std::filesystem::path& resultFile)
{
    Json maps = Json::object();
    for (const auto& [channel, file] : job.maps) maps[std::string(mapChannelName(channel))] = pathToUtf8(file);
    Json values = Json::object();
    for (const auto& [channel, value] : job.values) values[std::string(mapChannelName(channel))] = value;

    const bool blend = job.workerSource == "blend";
    Json source = Json::object();
    source["kind"] = job.workerSource;
    source["path"] = pathToUtf8(job.path);
    source["material"] = (blend && !job.blendMaterial.empty()) ? Json(job.blendMaterial) : Json(nullptr);
    source["maps"] = std::move(maps);
    source["values"] = std::move(values);
    source["normal"] = std::string(normalConventionName(job.normal));

    Json params = Json::object();
    params["source"] = std::move(source);
    params["output_folder"] = pathToUtf8(job.outputFolder);
    params["tile_size"] = job.tileSize ? tileJson(*job.tileSize) : Json(nullptr);
    params["bake_size"] = job.bakeSize;
    params["thumbnail"] = thumbnailJson(job.thumbnail);
    return makeBlenderJob(kCleanupMaterialTask, jobId, resultFile, std::move(params));
}

// Reads the result of a cleanup_material job.
MaterialJobOutput readMaterialCleanupResult(const Json& result, std::string_view jobId)
{
    MaterialJobOutput out;
    out.header = readBlenderResultHeader(result, kCleanupMaterialTask, jobId);
    if (!out.header.status.ok()) return out;
    try {
        const Json& output = result["output"];
        const Json& maps = field(output, "maps", "/output");
        for (const MapChannel channel : allMapChannels()) materialMap(out.maps, channel) = readMap(maps, channel);

        const Json& texture = field(output, "texture_size", "/output");
        out.textureSize.width = pixels(texture, "width", "/output/texture_size");
        out.textureSize.height = pixels(texture, "height", "/output/texture_size");

        const Json& tile = field(output, "tile_size", "/output");
        out.tileSize.width = number(tile, "width", "/output/tile_size");
        out.tileSize.height = number(tile, "height", "/output/tile_size");
        if (Status s = checkTileSize(out.tileSize); !s.ok()) throw Problem{"/output/tile_size", s.message};

        const Json& picked = field(output, "picked_material", "/output");
        if (!picked.is_null() && !picked.is_string()) throw Problem{"/output/picked_material", "Must be text or null."};
        out.pickedMaterial = picked.is_string() ? picked.get<std::string>() : "";

        const Json& info = field(output, "source_info", "/output");
        if (!info.is_null()) out.sourceInfo = readSourceInfo(info);

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
    } catch (const Problem& p) {
        out.header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    } catch (const Json::exception& e) {
        out.header.status = fail(ErrorCode::BadFile, std::string("The Blender result is not valid: ") + e.what());
    }
    return out;
}

}
