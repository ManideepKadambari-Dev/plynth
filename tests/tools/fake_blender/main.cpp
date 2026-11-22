// A stand-in for blender.exe in tests. It answers --version and runs the Plynth worker protocol without
// Blender: it reads the job file after "--", acts by the mode in fake_mode.txt next to the program (or the
// FAKE_BLENDER_MODE variable), and writes a result file that follows schema/blender-result.schema.json.
// Modes: ok (default), fail, crash, hang, hang_version, bad_result, no_thumbnail, no_fbx, build_problem,
// several_materials (a cleanup_material job on a .blend with no material named fails with that error).
// Every job is also appended as one line to fake_jobs.log next to the program.
// The version comes from fake_version.txt next to the program, else 5.2.0.
// Probe add-ons come from FAKE_BLENDER_ADDONS (comma separated modules) and FAKE_BLENDER_BLENDERKIT
// (none, installed, enabled or signed_in).
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

// The folder that holds this program.
fs::path programFolder()
{
    wchar_t buffer[MAX_PATH];
    const DWORD got = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return got > 0 ? fs::path(buffer).parent_path() : fs::path();
}

// The text of a small file with spaces at both ends removed. Empty when it is missing.
std::string readSmallFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    std::string s = text.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) ++start;
    return s.substr(start);
}

// An environment variable, empty when it is not set.
std::string environment(const char* name)
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, name);
    std::string out = value ? value : "";
    free(value);
    return out;
}

// The version this fake pretends to be.
std::string fakeVersion()
{
    const std::string v = readSmallFile(programFolder() / "fake_version.txt");
    return v.empty() ? "5.2.0" : v;
}

// What the fake does with a job.
std::string fakeMode()
{
    const std::string m = readSmallFile(programFolder() / "fake_mode.txt");
    if (!m.empty()) return m;
    const std::string e = environment("FAKE_BLENDER_MODE");
    return e.empty() ? "ok" : e;
}

// Waits until the program is killed.
[[noreturn]] void hangForever()
{
    for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// The checksum PNG uses for each chunk.
std::uint32_t crc32(const std::string& data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char c : data) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Four bytes, most significant first.
void put32(std::string& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>((v >> shift) & 0xFF));
}

// One PNG chunk.
void chunk(std::string& png, const char* type, const std::string& data)
{
    put32(png, static_cast<std::uint32_t>(data.size()));
    const std::string body = std::string(type) + data;
    png += body;
    put32(png, crc32(body));
}

// A valid 8 by 8 RGB PNG with a dark fill.
std::string tinyPng()
{
    std::string raw;
    for (int y = 0; y < 8; ++y) {
        raw.push_back('\0');
        for (int x = 0; x < 8; ++x) {
            raw.push_back(static_cast<char>(14 + x * 8));
            raw.push_back(static_cast<char>(17 + y * 8));
            raw.push_back(static_cast<char>(22));
        }
    }
    std::uint32_t a = 1, b = 0;
    for (const unsigned char c : raw) {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    std::string z = "\x78\x01";
    z.push_back('\x01');
    z.push_back(static_cast<char>(raw.size() & 0xFF));
    z.push_back(static_cast<char>(raw.size() >> 8));
    z.push_back(static_cast<char>(~raw.size() & 0xFF));
    z.push_back(static_cast<char>((~raw.size() >> 8) & 0xFF));
    z += raw;
    put32(z, (b << 16) | a);

    std::string png = "\x89PNG\r\n\x1a\n";
    std::string header;
    put32(header, 8);
    put32(header, 8);
    header += std::string("\x08\x02\x00\x00\x00", 5);
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
    return png;
}

// Writes bytes to a file, making its folder.
void writeFile(const fs::path& file, const std::string& bytes)
{
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// A path from UTF-8 text.
fs::path pathFrom(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

// Prints a progress line the way the worker does.
void progress(const char* step, int index, int count, const char* message)
{
    Json p = Json{{"progress", Json{{"step", step}, {"index", index}, {"count", count}, {"message", message}}}};
    std::cout << "PLYNTH:" << p.dump() << std::endl;
}

// Writes the result through a temporary file and a rename.
void writeResult(const fs::path& file, const Json& result)
{
    fs::path tmp = file;
    tmp += ".tmp";
    writeFile(tmp, result.dump(2));
    std::error_code ec;
    fs::rename(tmp, file, ec);
}

// The size, counts and lists every successful asset result has.
Json measures(bool measured)
{
    if (!measured) return Json{{"size", nullptr}, {"triangles", nullptr}, {"objects", nullptr}, {"materials", nullptr}, {"textures", nullptr}};
    return Json{{"size", Json{{"width", 0.5}, {"depth", 0.5}, {"height", 0.9}}},
                {"triangles", 12},
                {"objects", 1},
                {"materials", Json::array({"Fake"})},
                {"textures", Json::array()}};
}

// The probe output from the environment variables.
Json probeOutput(const std::string& version)
{
    Json addons = Json::array();
    std::vector<std::string> modules;
    std::stringstream list(environment("FAKE_BLENDER_ADDONS"));
    for (std::string m; std::getline(list, m, ',');) {
        if (!m.empty()) modules.push_back(m);
    }
    const std::string kit = environment("FAKE_BLENDER_BLENDERKIT");
    const bool installed = kit == "installed" || kit == "enabled" || kit == "signed_in";
    const bool enabled = kit == "enabled" || kit == "signed_in";
    if (enabled) modules.push_back("bl_ext.user_default.blenderkit");
    for (const std::string& m : modules) addons.push_back(Json{{"module", m}, {"name", m}, {"version", "1.0.0"}});
    return Json{{"blender", Json{{"version", version}, {"version_text", version + " LTS"}, {"python", "3.11.9"}, {"build_date", "2026-07-14"}}},
                {"addons", addons},
                {"blenderkit", Json{{"installed", installed}, {"enabled", enabled}, {"signed_in", kit == "signed_in"}}}};
}


// The material.json of a source folder, or null when there is none.
Json readMaterialSource(const fs::path& folder)
{
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return nullptr;
    std::ifstream in(folder / "material.json", std::ios::binary);
    if (!in) return nullptr;
    const Json json = Json::parse(in, nullptr, false);
    return json.is_object() ? json : Json(nullptr);
}

// The cleanup_material task: four map files (opacity when the job gives a value below 1), thumb.png and the
// fields of material.json that the worker would report.
Json materialOutput(const Json& params, const std::string& mode)
{
    const fs::path folder = pathFrom(params["output_folder"].get<std::string>());
    const Json& source = params["source"];
    const bool wantThumbnail = params["thumbnail"]["render"].get<bool>();

    Json maps = Json::object();
    for (const char* channel : {"base_color", "normal", "roughness", "metallic"}) {
        writeFile(folder / "textures" / (std::string(channel) + ".png"), tinyPng());
        maps[channel] = Json{{"file", std::string("textures/") + channel + ".png"}, {"from", "file"}, {"detail", "fake.png"}};
    }
    const Json& values = source["values"];
    const bool opacity = values.contains("opacity") && values["opacity"].get<double>() < 1.0;
    if (opacity) writeFile(folder / "textures" / "opacity.png", tinyPng());
    maps["opacity"] = opacity ? Json{{"file", "textures/opacity.png"}, {"from", "value"}, {"detail", std::to_string(values["opacity"].get<double>())}}
                              : Json{{"file", nullptr}, {"from", "none"}, {"detail", ""}};
    maps["occlusion"] = Json{{"file", nullptr}, {"from", "none"}, {"detail", ""}};

    const Json sourceFile = source["kind"] == "texture_set" ? readMaterialSource(pathFrom(source["path"].get<std::string>())) : Json(nullptr);
    Json info = nullptr;
    if (sourceFile.is_object()) {
        info = Json::object();
        for (const char* key : {"id", "name", "groups", "tags", "license"}) {
            if (sourceFile.contains(key)) info[key] = sourceFile[key];
        }
    }
    Json tile = Json{{"width", 1.0}, {"height", 1.0}};
    if (params["tile_size"].is_object()) tile = params["tile_size"];
    else if (sourceFile.is_object() && sourceFile.contains("tile_size")) tile = sourceFile["tile_size"];

    Json thumbnail = Json{{"file", nullptr}, {"engine", nullptr}, {"error", nullptr}};
    if (wantThumbnail && mode == "no_thumbnail") {
        thumbnail["error"] = "Fake render failure";
    } else if (wantThumbnail) {
        writeFile(folder / "thumb.png", tinyPng());
        thumbnail = Json{{"file", "thumb.png"}, {"engine", "eevee"}, {"error", nullptr}};
    }
    Json picked = nullptr;
    if (source["kind"] == "blend") picked = source["material"].is_string() ? source["material"] : Json("FakeMaterial");

    Json out = Json::object();
    out["maps"] = maps;
    out["texture_size"] = Json{{"width", 8}, {"height", 8}};
    out["tile_size"] = tile;
    out["picked_material"] = picked;
    out["source_info"] = info;
    out["thumbnail"] = thumbnail;
    return out;
}

// The steps the fake build reports.
constexpr const char* kBuildSteps[] = {"prepare_scene", "walls", "openings", "floors", "assets", "materials", "save_blend", "export_fbx"};

// The sum of one list over all floors of a build job.
int sumOf(const Json& floors, const char* key)
{
    int total = 0;
    for (const Json& floor : floors) total += static_cast<int>(floor[key].size());
    return total;
}

// The materials.json the fake build writes: each library material of the job, and a neutral Wall_Outside
// when a ring edge has no material. Returns how many materials it lists.
int writeBuiltMaterials(const fs::path& folder, const Json& params)
{
    Json materials = Json::array();
    for (const Json& m : params["materials"]) {
        materials.push_back(Json{{"name", m["id"]},
                                 {"material", m["id"]},
                                 {"surface", nullptr},
                                 {"maps", m["maps"]},
                                 {"alpha_mode", m["maps"]["opacity"].is_null() ? "opaque" : "blend"}});
    }
    bool neutral = false;
    for (const Json& floor : params["floors"]) {
        for (const Json& solid : floor["wall_solids"]) {
            for (const Json& edge : solid["outline"]["edges"]) neutral = neutral || edge["material"].is_null();
        }
    }
    if (neutral) {
        const std::u8string utf8 = (folder / "textures" / "Wall_Outside_").generic_u8string();
        const std::string base(utf8.begin(), utf8.end());
        materials.push_back(Json{{"name", "Wall_Outside"},
                                 {"material", nullptr},
                                 {"surface", "Wall_Outside"},
                                 {"maps", Json{{"base_color", base + "base_color.png"},
                                               {"normal", base + "normal.png"},
                                               {"roughness", base + "roughness.png"},
                                               {"metallic", base + "metallic.png"},
                                               {"opacity", nullptr},
                                               {"occlusion", nullptr}}},
                                 {"alpha_mode", "opaque"}});
    }
    const int count = static_cast<int>(materials.size());
    Json slots = Json::array();
    for (const Json& m : materials) slots.push_back(m["name"]);
    const Json noAssets = Json::array();
    for (const Json& asset : params.contains("assets") ? params["assets"] : noAssets) {
        for (const Json& m : asset.contains("materials") ? asset["materials"] : noAssets) {
            materials.push_back(Json{{"name", m["name"]},
                                     {"material", nullptr},
                                     {"surface", nullptr},
                                     {"asset", asset["id"]},
                                     {"maps", m["maps"]},
                                     {"alpha_mode", m["maps"]["opacity"].is_null() ? "opaque" : "blend"}});
        }
    }
    Json objects = Json::array();
    for (const Json& floor : params["floors"]) {
        objects.push_back(Json{{"name", "SM_" + floor["id"].get<std::string>() + "_Walls"}, {"slots", slots}});
    }
    const Json file = Json{{"format", "plynth-build-materials"}, {"version", 1}, {"materials", materials}, {"objects", objects}};
    writeFile(folder / "materials.json", file.dump(2) + "\n");
    return count;
}

// What the fake build task writes and reports.
Json buildOutput(const Json& params, const std::string& mode)
{
    const fs::path folder = pathFrom(params["output_folder"].get<std::string>());
    writeFile(folder / "building.blend", "FAKEBLEND");
    const bool fbx = params["fbx_file"].is_string();
    if (fbx) writeFile(folder / "building.fbx", "FAKEFBX");
    const int materialCount = fbx ? writeBuiltMaterials(folder, params) : static_cast<int>(params["materials"].size());

    const Json& floors = params["floors"];
    const int solids = sumOf(floors, "wall_solids");
    const int openings = sumOf(floors, "openings");
    const int rooms = sumOf(floors, "rooms");
    const int items = sumOf(floors, "items");
    int collision = items;
    for (const Json& floor : floors) {
        if (!floor.contains("collision")) continue;
        for (const Json& shape : floor["collision"]) collision += static_cast<int>(shape["pieces"].size());
    }
    int fills = 0;
    const bool fill = params["settings"]["fill_openings"].get<bool>();
    Json problems = Json::array();
    for (const Json& floor : floors) {
        for (const Json& opening : floor["openings"]) {
            if (fill && opening["type"] != "plain") ++fills;
            if (mode == "build_problem" && problems.empty()) {
                problems.push_back(Json{{"code", "cut_failed"},
                                        {"severity", "error"},
                                        {"message", "The cut for opening " + opening["id"].get<std::string>() + " failed."},
                                        {"floor", floor["id"]},
                                        {"object", opening["id"]},
                                        {"step", "openings"}});
            }
        }
    }
    const int objects = solids + openings + rooms * 2 + items;
    Json steps = Json::array();
    for (const char* step : kBuildSteps) steps.push_back(Json{{"step", step}, {"ms", 3}});

    Json out = Json::object();
    out["blend_file"] = "building.blend";
    out["fbx_file"] = fbx ? Json("building.fbx") : Json(nullptr);
    out["materials_file"] = fbx ? Json("materials.json") : Json(nullptr);
    out["objects"] = objects;
    out["triangles"] = objects * 12;
    out["counts"] = Json{{"wall_solids", solids}, {"openings_cut", openings}, {"fills", fills}, {"rooms", rooms}, {"items", items}, {"materials", materialCount}, {"collision", collision}};
    out["steps"] = steps;
    out["problems"] = problems;
    return out;
}

}

// The command line arguments as UTF-8, read from the wide command line so non ASCII paths survive.
std::vector<std::string> commandLineArguments()
{
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> out;
    for (int i = 1; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
        if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
        out.push_back(text);
    }
    LocalFree(wide);
    return out;
}

int main()
{
    const std::vector<std::string> args = commandLineArguments();
    const std::string mode = fakeMode();
    const std::string version = fakeVersion();

    for (const std::string& a : args) {
        if (a == "--version") {
            if (mode == "hang_version") hangForever();
            std::cout << "Blender " << version << " LTS" << std::endl;
            std::cout << "\tbuild date: 2026-07-14" << std::endl;
            return 0;
        }
    }

    std::string jobPath;
    bool afterDashes = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--") afterDashes = true;
        else if (afterDashes && args[i] == "--job" && i + 1 < args.size()) jobPath = args[i + 1];
    }
    if (jobPath.empty()) {
        std::cerr << "fake blender: no --job given" << std::endl;
        return 3;
    }
    std::ifstream in(pathFrom(jobPath), std::ios::binary);
    if (!in) {
        std::cerr << "fake blender: the job file could not be read" << std::endl;
        return 3;
    }
    const Json job = Json::parse(in, nullptr, false);
    if (job.is_discarded()) return 3;

    {
        std::ofstream log(programFolder() / "fake_jobs.log", std::ios::binary | std::ios::app);
        log << job.dump() << "\n";
    }

    const bool isBuild = job["task"].get<std::string>() == "build";
    if (isBuild) {
        int index = 0;
        for (const char* step : kBuildSteps) progress(step, ++index, static_cast<int>(std::size(kBuildSteps)), "Fake build step");
    } else {
        progress("import_source", 1, 2, "Fake import");
        progress("save_blend", 2, 2, "Fake save");
    }
    std::cout << "fake blender log line" << std::endl;

    if (mode == "hang") hangForever();
    if (mode == "crash") return 9;

    const fs::path resultFile = pathFrom(job["result_file"].get<std::string>());
    if (mode == "bad_result") {
        writeFile(resultFile, "{}");
        return 0;
    }

    const std::string task = job["task"].get<std::string>();
    Json result = Json::object();
    result["format"] = "plynth-blender-result";
    result["version"] = 1;
    result["task"] = task;
    result["job_id"] = job["job_id"];
    result["ok"] = true;
    result["error"] = nullptr;
    result["blender_version"] = version;
    result["warnings"] = Json::array();
    result["elapsed_ms"] = 5;

    const Json& params = job["params"];
    if (mode == "fail" || (mode == "no_thumbnail" && task == "render_thumbnail")) {
        const bool render = mode != "fail";
        result["ok"] = false;
        const bool build = task == "build";
        result["error"] = Json{{"code", render ? "render_failed" : build ? "save_failed" : "import_failed"},
                               {"message", render ? "Fake render failure" : build ? "Fake save failure." : "Fake import failure."}};
        result["output"] = nullptr;
        writeResult(resultFile, result);
        return 0;
    }

    if (mode == "several_materials" && task == "cleanup_material" && params["source"]["kind"] == "blend" &&
        params["source"]["material"].is_null()) {
        result["ok"] = false;
        result["error"] = Json{{"code", "several_materials"}, {"message", "The file has several materials: Brick, Wood. Name the one to take."}};
        result["output"] = nullptr;
        writeResult(resultFile, result);
        return 0;
    }

    if (task == "build") {
        result["output"] = buildOutput(params, mode);
    } else if (task == "probe") {
        result["output"] = probeOutput(version);
    } else if (task == "cleanup_material") {
        result["output"] = materialOutput(params, mode);
    } else if (task == "cleanup_asset") {
        const fs::path folder = pathFrom(params["output_folder"].get<std::string>());
        writeFile(folder / "asset.blend", "FAKEBLEND");
        const bool fbxWritten = mode != "no_fbx";
        if (fbxWritten) writeFile(folder / "asset.fbx", "FAKEFBX");
        else result["warnings"].push_back(Json{{"code", "fbx_failed"}, {"message", "Fake asset.fbx failure"}});
        const bool wantThumbnail = params["thumbnail"]["render"].get<bool>();
        Json thumbnail = Json{{"file", nullptr}, {"engine", nullptr}, {"error", nullptr}};
        if (wantThumbnail && mode == "no_thumbnail") {
            thumbnail["error"] = "Fake render failure";
        } else if (wantThumbnail) {
            writeFile(folder / "thumb.png", tinyPng());
            thumbnail = Json{{"file", "thumb.png"}, {"engine", "eevee"}, {"error", nullptr}};
        }
        const bool zip = params["source"]["format"].get<std::string>() == "zip";
        Json output = measures(true);
        output["blend_file"] = "asset.blend";
        output["fbx_file"] = fbxWritten ? Json("asset.fbx") : Json(nullptr);
        output["picked_file"] = zip ? Json("model.obj") : Json(nullptr);
        output["missing_textures"] = Json::array();
        output["thumbnail"] = thumbnail;
        Json ordered = Json::object();
        for (const char* key : {"blend_file", "fbx_file", "picked_file", "size", "triangles", "objects", "materials", "textures", "missing_textures", "thumbnail"}) {
            ordered[key] = output[key];
        }
        result["output"] = ordered;
    } else if (task == "render_thumbnail") {
        const fs::path file = pathFrom(params["output_file"].get<std::string>());
        writeFile(file, tinyPng());
        Json output = measures(params["measure"].get<bool>());
        Json fbxName = nullptr;
        if (params["fbx_file"].is_string()) {
            if (mode == "no_fbx") {
                result["warnings"].push_back(Json{{"code", "fbx_failed"}, {"message", "Fake asset.fbx failure"}});
            } else {
                const fs::path fbx = pathFrom(params["fbx_file"].get<std::string>());
                writeFile(fbx, "FAKEFBX");
                fbxName = fbx.filename().string();
            }
        }
        output["fbx_file"] = fbxName;
        output["thumbnail"] = Json{{"file", pathFrom(params["output_file"].get<std::string>()).filename().string()},
                                   {"engine", "eevee"},
                                   {"error", nullptr}};
        result["output"] = output;
    } else {
        result["ok"] = false;
        result["error"] = Json{{"code", "unknown_task"}, {"message", "The fake does not know this task."}};
        result["output"] = nullptr;
    }
    writeResult(resultFile, result);
    return 0;
}
