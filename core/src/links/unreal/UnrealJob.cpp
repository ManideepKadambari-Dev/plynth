#include "plynth/core/UnrealLink.h"

#include "plynth/core/Build.h"
#include "plynth/core/Exporter.h"
#include "plynth/core/Kit.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxAssetName = 128;

// One texture channel: its name in the job and the word the export puts at the end of a texture's name.
struct Channel {
    const char* job;
    const char* word;
};

constexpr std::array<Channel, 6> kChannels{{{"base_color", "BaseColor"},
                                            {"normal", "Normal"},
                                            {"roughness", "Roughness"},
                                            {"metallic", "Metallic"},
                                            {"opacity", "Opacity"},
                                            {"occlusion", "Occlusion"}}};

// Reads an environment variable. Empty when it is not set.
std::wstring environmentText(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), size);
    value.resize(got);
    return value;
}

// Lower case copy of a text, for comparing names without case.
std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// True for a letter or digit of plain ASCII.
bool isAsciiAlnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// The text with every character other than a letter, digit or "_" as "_", the way the export names objects.
std::string sanitized(std::string_view text)
{
    std::string out;
    for (const char c : text) out.push_back(isAsciiAlnum(c) || c == '_' ? c : '_');
    return out;
}

// An id such as "oak_planks" as the export writes it in texture names: "OakPlanks".
std::string pascalCase(std::string_view text)
{
    std::string out;
    bool start = true;
    for (const char c : text) {
        if (!isAsciiAlnum(c)) {
            start = true;
            continue;
        }
        out.push_back(start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        start = false;
    }
    return out;
}

// A material name without the number Blender adds to a repeated name, such as ".001".
std::string withoutBlenderNumber(const std::string& name)
{
    if (name.size() > 4 && name[name.size() - 4] == '.' &&
        std::all_of(name.end() - 3, name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return name.substr(0, name.size() - 4);
    }
    return name;
}

// A name cut to the longest an asset name may be.
std::string assetName(std::string name)
{
    if (name.size() > kMaxAssetName) name.resize(kMaxAssetName);
    return name;
}

// True when a name is a valid asset name of the job: letters, digits, "_" and "-", 1 to 128 characters.
bool isAssetName(std::string_view name)
{
    if (name.empty() || name.size() > kMaxAssetName) return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return isAsciiAlnum(c) || c == '_' || c == '-'; });
}

// True when a file name may stand in the job's data or texture lists: letters, digits, "_", "." and "-".
bool isPlainFileName(std::string_view name)
{
    if (name.empty() || std::all_of(name.begin(), name.end(), [](char c) { return c == '.'; })) return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return isAsciiAlnum(c) || c == '_' || c == '-' || c == '.'; });
}

// A group id as the job's pattern needs it: lower case letters, digits and "_", starting with a letter.
std::string jobGroupId(std::string_view id)
{
    std::string out;
    for (const char c : id) {
        const char low = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        out.push_back((low >= 'a' && low <= 'z') || (low >= '0' && low <= '9') || low == '_' ? low : '_');
    }
    if (out.empty() || out[0] < 'a' || out[0] > 'z') out = "g" + out;
    return out;
}

// A folder in the job's form: absolute, UTF-8, forward slashes, no slash at the end.
std::string folderText(const fs::path& folder)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(folder, ec);
    if (ec) absolute = folder;
    std::string text = pathToUtf8(absolute.lexically_normal());
    while (text.size() > 3 && text.back() == '/') text.pop_back();
    return text;
}

// The job channel a texture's name ends with ("T_Oak_Normal" or "T_Oak_Normal_2" gives "normal"). Empty for none.
std::string channelOfStem(std::string stem)
{
    if (const std::size_t cut = stem.find_last_of('_'); cut != std::string::npos && cut + 1 < stem.size() &&
        std::all_of(stem.begin() + static_cast<std::ptrdiff_t>(cut) + 1, stem.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        stem.resize(cut);
    }
    for (const Channel& channel : kChannels) {
        const std::string tail = std::string("_") + channel.word;
        if (stem.size() > tail.size() && stem.compare(stem.size() - tail.size(), tail.size(), tail) == 0) return channel.job;
    }
    return {};
}

// The source image of one channel of a built material.
const fs::path& channelSource(const BuiltMaterial& material, std::string_view channel)
{
    if (channel == "base_color") return material.baseColor;
    if (channel == "normal") return material.normal;
    if (channel == "roughness") return material.roughness;
    if (channel == "metallic") return material.metallic;
    if (channel == "opacity") return material.opacity;
    return material.occlusion;
}

// The textures the export copied, by lower case file stem: the file name inside Textures/.
std::map<std::string, std::string> texturesByStem(const fs::path& folder)
{
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = pathToUtf8(entry.path().filename());
        if (!isPlainFileName(name)) continue;
        const std::string stem = pathToUtf8(entry.path().stem());
        if (!isAssetName(stem)) continue;
        out.emplace(lowered(stem), name);
    }
    return out;
}

// The kit's piece mesh names from its layout file. Empty when the file cannot be read.
std::vector<std::string> kitPieces(const fs::path& layoutFile)
{
    std::vector<std::string> out;
    std::ifstream in(layoutFile, std::ios::binary);
    if (!in) return out;
    const Json layout = Json::parse(in, nullptr, false);
    if (layout.is_discarded() || !layout.is_object() || !layout.contains("pieces") || !layout["pieces"].is_array()) return out;
    for (const Json& piece : layout["pieces"]) {
        if (piece.is_object() && piece.contains("mesh") && piece["mesh"].is_string() && isAssetName(piece["mesh"].get<std::string>())) {
            out.push_back(piece["mesh"].get<std::string>());
        }
    }
    return out;
}

// The file name of an output inside the source folder, when it is there and has a plain name. Empty otherwise.
std::string fileInside(const fs::path& file, const fs::path& folder)
{
    if (file.empty()) return {};
    if (lowered(folderText(file.parent_path())) != lowered(folderText(folder))) return {};
    const std::string name = pathToUtf8(file.filename());
    return isPlainFileName(name) ? name : std::string();
}

// Three numbers as a JSON list.
Json xyz(const Vec3& v)
{
    return Json::array({v.x, v.y, v.z});
}

}

// Finds the folder of the Plynth scripts Unreal runs.
fs::path unrealScriptsFolder()
{
    std::error_code ec;
    const auto holdsScript = [&ec](const fs::path& folder) {
        return !folder.empty() && fs::is_regular_file(folder / pathFromUtf8(kUnrealLinkScript), ec);
    };
    if (const std::wstring env = environmentText(L"PLYNTH_UNREAL_SCRIPTS"); !env.empty()) {
        if (holdsScript(fs::path(env))) return fs::path(env);
    }
    wchar_t program[MAX_PATH];
    const DWORD got = GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program)));
    if (got > 0 && got < std::size(program)) {
        const fs::path beside = fs::path(program).parent_path() / L"unreal";
        if (holdsScript(beside)) return beside;
    }
#ifdef PLYNTH_SOURCE_UNREAL_DIR
    {
        const fs::path source(PLYNTH_SOURCE_UNREAL_DIR);
        if (holdsScript(source)) return source;
    }
#endif
    return {};
}

// Gives the building's folder name in Content/Plynth.
std::string unrealBuildingName(std::string_view projectName)
{
    return exportFileStem(projectName);
}

// Makes the job file the script in the editor reads.
Json makeUnrealJob(const EngineDelivery& delivery, const UnrealJobChecks& checks, const fs::path& resultFile)
{
    const std::string building = unrealBuildingName(delivery.project.settings.name);
    const fs::path source = delivery.workFolder / pathFromUtf8(kLinkOutputFolder);
    const bool pack = delivery.options.packMaterials;
    const ExportReport empty{};
    const ExportReport& exported = delivery.exported ? *delivery.exported : empty;

    Json job = Json::object();
    job["format"] = std::string(kUnrealJobFormat);
    job["version"] = kUnrealJobVersion;
    job["job_id"] = delivery.jobId;
    job["plynth_version"] = std::string(version());
    job["building"] = building;
    job["project_folder"] = folderText(delivery.projectFolder);
    job["content_path"] = std::string(kUnrealContentRoot) + "/" + building;
    job["source_folder"] = folderText(source);
    job["pack_materials"] = pack;
    job["grouped"] = delivery.grouped;

    Json meshes = Json::array();
    std::set<std::string> takenAssets;
    for (const ExportFileOutput& file : exported.fileOutputs) {
        const fs::path name = pathFromUtf8(file.file);
        const std::string stem = pathToUtf8(name.stem());
        if (lowered(pathToUtf8(name.extension())) != ".fbx" || !isAssetName(stem)) continue;
        const std::string asset = assetName("SM_" + sanitized(stem));
        if (!takenAssets.insert(lowered(asset)).second) continue;
        meshes.push_back(Json{{"group", jobGroupId(file.group)}, {"file", file.file}, {"asset", asset}, {"actor", assetName(stem)}});
    }
    job["meshes"] = std::move(meshes);

    Json kit = nullptr;
    if (exported.kit.written) {
        const std::string file = fileInside(exported.kit.file, source);
        if (!file.empty()) kit = Json{{"file", file}, {"pieces", kitPieces(exported.kit.layoutFile)}};
    }
    job["kit"] = std::move(kit);

    Json textures = Json::array();
    Json materials = Json::array();
    if (pack) {
        const fs::path texturesFolder = source / pathFromUtf8(kExportTexturesFolder);
        const std::map<std::string, std::string> copied = texturesByStem(texturesFolder);
        std::map<std::string, std::string> channelOf; // lower case stem to job channel
        std::set<std::string> takenSlots;
        const BuiltMaterialsRead built = readBuiltMaterials(delivery.projectFolder);
        for (const BuiltMaterial& m : built.materials) {
            const std::string slot = assetName(m.asset.empty() ? sanitized(m.name)
                                                               : pascalCase(m.asset) + "_" + sanitized(withoutBlenderNumber(m.name)));
            if (!isAssetName(slot) || !takenSlots.insert(lowered(slot)).second) continue;
            const std::string owner = m.asset.empty() && !m.material.empty() ? pascalCase(m.material) : slot;
            const bool glass = lowered(m.surface) == "glass";

            Json maps = Json::object();
            for (const Channel& channel : kChannels) {
                maps[channel.job] = nullptr;
                if (channelSource(m, channel.job).empty()) continue;
                const std::string stem = "T_" + owner + "_" + channel.word;
                if (const auto it = copied.find(lowered(stem)); it != copied.end()) {
                    const std::string texture = pathToUtf8(pathFromUtf8(it->second).stem());
                    maps[channel.job] = texture;
                    channelOf.emplace(lowered(texture), channel.job);
                }
            }
            Json values = Json::object();
            values["base_color"] = glass ? Json::array({0.80, 0.88, 0.92}) : Json::array({0.8, 0.8, 0.8});
            values["roughness"] = glass ? 0.05 : 0.5;
            values["metallic"] = 0.0;
            values["opacity"] = glass ? 0.25 : 1.0;

            const bool opacityMap = !maps["opacity"].is_null();
            std::string blend = "opaque";
            if (glass && (opacityMap || values["opacity"].get<double>() < 1.0)) blend = "translucent";
            else if (opacityMap) blend = "masked";

            materials.push_back(Json{{"slot", slot},
                                     {"asset", assetName("MI_" + slot)},
                                     {"blend", blend},
                                     {"two_sided", glass},
                                     {"maps", std::move(maps)},
                                     {"values", std::move(values)}});
        }
        for (const auto& [stem, file] : copied) {
            std::string channel;
            if (const auto it = channelOf.find(stem); it != channelOf.end()) channel = it->second;
            else channel = channelOfStem(pathToUtf8(pathFromUtf8(file).stem()));
            if (channel.empty()) continue;
            textures.push_back(Json{{"file", std::string(kExportTexturesFolder) + "/" + file},
                                    {"asset", pathToUtf8(pathFromUtf8(file).stem())},
                                    {"channel", channel},
                                    {"srgb", channel == "base_color"}});
        }
    }
    job["textures"] = std::move(textures);
    job["materials"] = std::move(materials);

    Json dataFiles = Json::array();
    for (const fs::path& file : {exported.gameplay.file, exported.licenses.jsonFile, exported.licenses.textFile,
                                 exported.kit.written ? exported.kit.layoutFile : fs::path()}) {
        if (const std::string name = fileInside(file, source); !name.empty()) dataFiles.push_back(name);
    }
    job["data_files"] = std::move(dataFiles);
    job["level"] = Json{{"place_actors", true}, {"tag", "Plynth:" + building}};

    if (checks.enabled) {
        Json bounds = nullptr;
        if (checks.expectedBounds) bounds = Json{{"min", xyz(checks.expectedBounds->min)}, {"max", xyz(checks.expectedBounds->max)}};
        job["checks"] = Json{{"expected_bounds", std::move(bounds)},
                             {"screenshot", checks.screenshot.empty() ? Json(nullptr) : Json(pathToUtf8(fs::absolute(checks.screenshot)))}};
    } else {
        job["checks"] = nullptr;
    }
    job["result_file"] = pathToUtf8(fs::absolute(resultFile));
    return job;
}

// Reads the result file the script wrote and checks its form.
UnrealResult readUnrealResult(const Json& json, std::string_view jobId)
{
    UnrealResult result;
    const auto bad = [&](const std::string& where) {
        UnrealResult out;
        out.status = fail(ErrorCode::BadFile, "The Unreal result is not valid: the field " + where + " is missing or has the wrong type.", where);
        return out;
    };
    if (!json.is_object()) return bad("");
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
        const auto array = [&](const Json& object, const char* key, const std::string& where) -> const Json& {
            const Json& v = member(object, key, where);
            if (!v.is_array()) throw std::string(where + "/" + key);
            return v;
        };

        if (text(json, "format", "") != kUnrealResultFormat) throw std::string("/format");
        if (member(json, "version", "") != kUnrealJobVersion) throw std::string("/version");
        if (text(json, "job_id", "") != jobId) throw std::string("/job_id");
        const Json& ok = member(json, "ok", "");
        if (!ok.is_boolean()) throw std::string("/ok");
        result.ok = ok.get<bool>();
        const Json& error = member(json, "error", "");
        if (error.is_object()) {
            result.errorCode = text(error, "code", "/error");
            result.errorMessage = text(error, "message", "/error");
        } else if (!error.is_null()) {
            throw std::string("/error");
        }
        if (!result.ok && result.errorCode.empty()) throw std::string("/error");
        result.engineVersion = text(json, "engine_version", "");
        result.projectName = text(json, "project_name", "");
        result.contentPath = text(json, "content_path", "");
        const Json& assets = array(json, "assets", "");
        for (std::size_t i = 0; i < assets.size(); ++i) {
            const std::string where = "/assets/" + std::to_string(i);
            result.assets.push_back({text(assets[i], "path", where), text(assets[i], "kind", where), text(assets[i], "action", where)});
        }
        const Json& actors = array(json, "actors", "");
        for (std::size_t i = 0; i < actors.size(); ++i) {
            const std::string where = "/actors/" + std::to_string(i);
            result.actors.push_back({text(actors[i], "label", where), text(actors[i], "asset", where), text(actors[i], "action", where)});
        }
        const Json& checks = array(json, "checks", "");
        for (std::size_t i = 0; i < checks.size(); ++i) {
            const std::string where = "/checks/" + std::to_string(i);
            UnrealCheck check{text(checks[i], "id", where), text(checks[i], "status", where), text(checks[i], "message", where)};
            if (check.status != "pass" && check.status != "warning" && check.status != "fail") throw std::string(where + "/status");
            result.checks.push_back(std::move(check));
        }
        const Json& screenshot = member(json, "screenshot", "");
        if (screenshot.is_string()) result.screenshot = pathFromUtf8(screenshot.get<std::string>());
        else if (!screenshot.is_null()) throw std::string("/screenshot");
        const Json& ms = member(json, "ms", "");
        if (!ms.is_number_integer() || ms.get<long long>() < 0) throw std::string("/ms");
        result.ms = ms.get<long long>();
    } catch (const std::string& where) {
        return bad(where);
    }
    return result;
}

// Finds the Unreal Editor programs of an install folder.
UnrealInstall unrealInstallAt(const fs::path& root)
{
    UnrealInstall install;
    install.root = root;
    const fs::path binaries = root / L"Engine" / L"Binaries" / L"Win64";
    std::error_code ec;
    if (fs::is_regular_file(binaries / L"UnrealEditor-Cmd.exe", ec)) install.editorCmd = binaries / L"UnrealEditor-Cmd.exe";
    if (fs::is_regular_file(binaries / L"UnrealEditor.exe", ec)) install.editor = binaries / L"UnrealEditor.exe";
    return install;
}

}
