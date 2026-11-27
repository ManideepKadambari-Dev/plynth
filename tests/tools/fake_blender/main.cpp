// A stand-in for blender.exe in tests. It answers --version and runs the Plynth worker protocol without
// Blender: it reads the job file after "--", acts by the mode in fake_mode.txt next to the program (or the
// FAKE_BLENDER_MODE variable), and writes a result file that follows schema/blender-result.schema.json.
// Modes: ok (default), fail, crash, hang, hang_version, bad_result, no_thumbnail, no_fbx, build_problem,
// several_materials (a cleanup_material job on a .blend with no material named fails with that error),
// quality_fail (an export job reports a failing naming check with a finding on w1), quality_warn (a warning),
// empty_group (the last group of an export job gets no object, so it has no file),
// unreadable (a probe job fails with preferences_unreadable).
// Several modes can be joined with "+", for example write_user+crash. write_user writes marker files into the
// folders BLENDER_USER_CONFIG and BLENDER_USER_EXTENSIONS name, else into config and extensions under
// FAKE_BLENDER_USER_ROOT, and into no folder when neither is set. It never uses a real default folder.
// Its records (the logs below, fake_args.txt, fake_result.json, fake_child.pid) go to the folder that
// FAKE_BLENDER_RECORDS names, else next to the program. The mode and version files are always read next to it.
// Every start also appends its arguments to fake_args.log.
// Every start also appends the BLENDER_USER_* variables it sees to fake_env.log.
// Every job is also appended as one line to fake_jobs.log next to the program.
// Every start also appends its integrity level and fetch variables to fake_starts.log (store_fetch_env.cpp).
// With FAKE_LEAVE_CHILD set, every job start leaves a sleeping child and writes its id to fake_child.pid.
// Every job also appends one JSON line to fake_extensions.log: {"task", "marker", "user_default"}. marker says
// whether the folder BLENDER_USER_EXTENSIONS names holds .plynth-prepared, user_default lists the sorted names
// in that folder's user_default. Without the variable no folder is looked at.
// The version comes from fake_version.txt next to the program, else 5.2.0.
// Probe add-ons come from FAKE_BLENDER_ADDONS (comma separated modules) and FAKE_BLENDER_BLENDERKIT
// (none, installed, enabled or signed_in).
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <crtdbg.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

int runFakeStoreFetch(const Json& job, const std::string& jobPath, const std::vector<std::string>& args);
fs::path recordFolder();
int lingerChild(const std::wstring& lockFile);
void noteStart();
void leaveChildIfAsked();

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

// An environment variable as UTF-8, empty when it is not set.
std::string environment(const char* name)
{
    const std::wstring wideName(name, name + std::strlen(name));
    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(wideName.c_str(), value.data(), size));
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes > 0 ? bytes - 1 : 0), '\0');
    if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

// The version this fake pretends to be.
std::string fakeVersion()
{
    const std::string v = readSmallFile(programFolder() / "fake_version.txt");
    return v.empty() ? "5.2.0" : v;
}

// The modes as written, joined with "+".
std::string fakeModeText()
{
    const std::string m = readSmallFile(programFolder() / "fake_mode.txt");
    if (!m.empty()) return m;
    return environment("FAKE_BLENDER_MODE");
}

// True when write_user is one of the modes.
bool writesUser()
{
    std::stringstream list(fakeModeText());
    for (std::string part; std::getline(list, part, '+');) {
        if (part == "write_user") return true;
    }
    return false;
}

// What the fake does with a job: the first mode that is not write_user.
std::string fakeMode()
{
    std::stringstream list(fakeModeText());
    for (std::string part; std::getline(list, part, '+');) {
        if (!part.empty() && part != "write_user") return part;
    }
    return "ok";
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

// Writes a marker file into the user's config and extensions folders, as a Blender that loads the user's
// settings would. The folders come from the environment, else from FAKE_BLENDER_USER_ROOT, never from a
// real default.
void writeUserMarkers()
{
    const std::string root = environment("FAKE_BLENDER_USER_ROOT");
    for (const char* kind : {"config", "extensions"}) {
        const std::string name = std::string("BLENDER_USER_") + (std::string(kind) == "config" ? "CONFIG" : "EXTENSIONS");
        const std::string given = environment(name.c_str());
        fs::path folder;
        if (!given.empty()) folder = pathFrom(given);
        else if (!root.empty()) folder = pathFrom(root) / kind;
        if (folder.empty()) continue;
        writeFile(folder / "fake_marker.txt", "touched by the fake\n");
    }
}

// What the extensions folder given to this job holds. Reads no file and writes nothing there.
Json extensionsSeen(const std::string& task)
{
    bool marker = false;
    std::vector<std::string> names;
    const std::string given = environment("BLENDER_USER_EXTENSIONS");
    if (!given.empty()) {
        const fs::path folder = pathFrom(given);
        std::error_code ec;
        marker = fs::is_regular_file(folder / ".plynth-prepared", ec);
        ec.clear();
        for (fs::directory_iterator it(folder / "user_default", ec), end; !ec && it != end; it.increment(ec)) {
            const std::u8string name = it->path().filename().u8string();
            names.emplace_back(name.begin(), name.end());
        }
        std::sort(names.begin(), names.end());
    }
    return Json{{"task", task}, {"marker", marker}, {"user_default", names}};
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

// The sum of one list over all floors of a build job. A floor without the list adds nothing.
int sumOf(const Json& floors, const char* key)
{
    int total = 0;
    for (const Json& floor : floors) total += floor.contains(key) ? static_cast<int>(floor[key].size()) : 0;
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

// The preview parts the fake build writes: one file per floor, one more for each floor with slab polygons and with roofs, and
// parts.json listing them.
void writePreviewParts(const fs::path& folder, const Json& floors)
{
    Json parts = Json::array();
    for (const Json& floor : floors) {
        const std::string id = floor["id"].get<std::string>();
        writeFile(folder / (id + ".fbx"), "FAKEFBX");
        parts.push_back(Json{{"floor", id}, {"kind", "storey"}, {"file", id + ".fbx"}});
        if (!floor["slab"]["polygons"].empty()) {
            writeFile(folder / (id + "_slab.fbx"), "FAKEFBX");
            parts.push_back(Json{{"floor", id}, {"kind", "slab"}, {"file", id + "_slab.fbx"}});
        }
        if (floor["roofs"].empty()) continue;
        writeFile(folder / (id + "_roof.fbx"), "FAKEFBX");
        parts.push_back(Json{{"floor", id}, {"kind", "roof"}, {"file", id + "_roof.fbx"}});
    }
    const Json file = Json{{"format", "plynth-build-preview"}, {"version", 1}, {"parts", parts}};
    writeFile(folder / "parts.json", file.dump(2) + "\n");
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
    const int stairs = sumOf(floors, "stairs");
    const int roofs = sumOf(floors, "roofs");
    int slabs = 0;
    for (const Json& floor : floors) slabs += floor["slab"]["polygons"].empty() ? 0 : 1;
    if (fbx && params["preview_folder"].is_string()) writePreviewParts(folder / params["preview_folder"].get<std::string>(), floors);
    const int generated = sumOf(floors, "generator_items");
    int collision = items + generated;
    for (const Json& floor : floors) {
        if (!floor.contains("collision")) continue;
        for (const Json& shape : floor["collision"]) collision += static_cast<int>(shape["pieces"].size() + (shape.contains("hulls") ? shape["hulls"].size() : 0));
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
    const int objects = solids + openings + rooms * 2 + items + generated;
    Json steps = Json::array();
    for (const char* step : kBuildSteps) steps.push_back(Json{{"step", step}, {"ms", 3}});

    Json out = Json::object();
    out["blend_file"] = "building.blend";
    out["fbx_file"] = fbx ? Json("building.fbx") : Json(nullptr);
    out["materials_file"] = fbx ? Json("materials.json") : Json(nullptr);
    out["objects"] = objects;
    out["triangles"] = objects * 12;
    out["counts"] = Json{{"wall_solids", solids}, {"openings_cut", openings}, {"fills", fills}, {"rooms", rooms}, {"items", items},
                       {"materials", materialCount}, {"slabs", slabs}, {"stairs", stairs}, {"roofs", roofs}, {"collision", collision}};
    if (generated > 0) out["counts"]["generators"] = generated;
    out["steps"] = steps;
    out["problems"] = problems;
    return out;
}

// The steps the fake export reports.
constexpr const char* kExportSteps[] = {"open_blend", "prepare",   "name_objects", "textures",
                                        "checks",     "finish_objects", "write_fbx",    "file_checks"};

// One check result of the fake export.
Json fakeCheck(const char* id, const char* name, const char* phase, const char* status, const char* message, Json findings = Json::array())
{
    return Json{{"id", id}, {"name", name}, {"phase", phase}, {"status", status}, {"message", message}, {"findings", std::move(findings)}};
}

// What the fake export task writes and reports: every group's file and Textures/T_Fake_BaseColor.png when the job
// writes and no scene check fails (or it is forced).
Json exportOutput(const Json& params, const std::string& mode)
{
    const fs::path folder = pathFrom(params["output_folder"].get<std::string>());
    const bool failing = mode == "quality_fail";
    const bool warning = mode == "quality_warn";
    const bool write = params["write"].get<bool>() && (!failing || params["force"].get<bool>());

    Json files = Json::array();
    Json textures = Json::array();
    Json emptyGroups = Json::array();
    Json kitOutput = nullptr;
    if (write) {
        const std::size_t groupCount = params["groups"].size();
        std::size_t position = 0;
        for (const Json& group : params["groups"]) {
            ++position;
            // Mode empty_group: the last group gets no object, so no file is written for it.
            if (mode == "empty_group" && position == groupCount) {
                emptyGroups.push_back(group["id"]);
                continue;
            }
            writeFile(folder / pathFrom(group["file"].get<std::string>()), "FAKEFBX");
            files.push_back(Json{{"group", group["id"]}, {"file", group["file"]}, {"objects", 12}, {"triangles", 144}});
        }
        if (params.contains("kit") && params["kit"].is_object()) {
            const Json& kitJob = params["kit"];
            const int pieces = static_cast<int>(kitJob["pieces"].size());
            writeFile(folder / pathFrom(kitJob["file"].get<std::string>()), "FAKEKIT");
            kitOutput = Json{{"file", kitJob["file"]}, {"pieces", pieces}, {"objects", pieces * 2}, {"triangles", 100 * pieces}};
        }
        writeFile(folder / pathFrom(params["textures_folder"].get<std::string>()) / "T_Fake_BaseColor.png", tinyPng());
        textures.push_back("T_Fake_BaseColor.png");
    }

    Json naming = fakeCheck("naming", "Naming", "scene", "pass", "Every name follows the rules.");
    if (failing) {
        naming = fakeCheck("naming", "Naming", "scene", "fail", "One object has a bad name.",
                           Json::array({Json{{"object", "SM_Bad Name"}, {"plan_id", "w1"}, {"message", "The name has a space in it."}}}));
    } else if (warning) {
        naming = fakeCheck("naming", "Naming", "scene", "warning", "One name is unusual.");
    }
    Json checks = Json::array();
    checks.push_back(fakeCheck("build_current", "Build is up to date", "scene", "pass", "The build matches the plan."));
    checks.push_back(naming);
    checks.push_back(write ? fakeCheck("fbx_scale", "File scale", "file", "pass", "The files have the right size.")
                           : fakeCheck("fbx_scale", "File scale", "file", "skipped", "Nothing was written."));

    Json steps = Json::array();
    for (const char* step : kExportSteps) steps.push_back(Json{{"step", step}, {"ms", 2}});

    const int groups = static_cast<int>(files.size());
    Json out = Json::object();
    out["written"] = write;
    out["files"] = files;
    out["textures"] = textures;
    out["objects"] = 12 * groups;
    out["triangles"] = 144 * groups;
    out["collision_objects"] = 3 * groups;
    out["empty_groups"] = emptyGroups;
    out["kit"] = kitOutput;
    out["checks"] = checks;
    out["steps"] = steps;
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

// Sends failure reports of the C runtime and Windows to stderr instead of dialogs, so a failing run of this
// tool ends instead of waiting for someone to close a window.
void quietFailures()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
}

int main()
{
    quietFailures();
    const std::vector<std::string> args = commandLineArguments();
    if (args.size() == 2 && args[0] == "--linger") return lingerChild(pathFrom(args[1]).wstring());
    {
        std::ofstream log(recordFolder() / "fake_args.log", std::ios::binary | std::ios::app);
        for (std::size_t i = 0; i < args.size(); ++i) log << (i ? "|" : "") << args[i];
        log << "\n";
    }
    noteStart();
    if (std::find(args.begin(), args.end(), "--version") == args.end()) leaveChildIfAsked();
    if (writesUser()) writeUserMarkers();
    {
        std::ofstream log(recordFolder() / "fake_env.log", std::ios::binary | std::ios::app);
        log << (std::find(args.begin(), args.end(), "--version") != args.end() ? "version" : "job");
        for (const char* name : {"BLENDER_USER_RESOURCES", "BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS", "BLENDER_USER_EXTENSIONS", "BLENDER_USER_DATAFILES"})
            log << "|" << name << "=" << environment(name);
        log << "\n";
    }
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
        std::ofstream log(recordFolder() / "fake_jobs.log", std::ios::binary | std::ios::app);
        log << job.dump() << "\n";
    }
    {
        std::ofstream log(recordFolder() / "fake_extensions.log", std::ios::binary | std::ios::app);
        log << extensionsSeen(job["task"].get<std::string>()).dump() << "\n";
    }

    if (job["task"] == "blenderkit_fetch") return runFakeStoreFetch(job, jobPath, args);
    if (job["task"].get<std::string>() == "probe") {
        const Json& given = job["params"];
        const Json folders = given.is_object() && given.contains("user_folders") ? given["user_folders"] : Json();
        bool complete = folders.is_object();
        for (const char* key : {"config", "scripts", "extensions", "blenderkit_data"}) {
            complete = complete && folders.contains(key) && folders[key].is_string() && !folders[key].get<std::string>().empty();
        }
        if (!complete) {
            Json bad = Json::object();
            bad["format"] = "plynth-blender-result";
            bad["version"] = 1;
            bad["task"] = "probe";
            bad["job_id"] = job["job_id"];
            bad["ok"] = false;
            bad["error"] = Json{{"code", "bad_job"}, {"message", "The probe job has no user folders."}};
            bad["blender_version"] = version;
            bad["warnings"] = Json::array();
            bad["elapsed_ms"] = 1;
            bad["output"] = nullptr;
            writeResult(pathFrom(job["result_file"].get<std::string>()), bad);
            return 0;
        }
    }

    const bool isBuild = job["task"].get<std::string>() == "build";
    if (isBuild) {
        int index = 0;
        for (const char* step : kBuildSteps) progress(step, ++index, static_cast<int>(std::size(kBuildSteps)), "Fake build step");
    } else if (job["task"].get<std::string>() == "export") {
        int index = 0;
        for (const char* step : kExportSteps) progress(step, ++index, static_cast<int>(std::size(kExportSteps)), "Fake export step");
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
        const bool exporting = task == "export";
        result["error"] = Json{{"code", render ? "render_failed" : build ? "save_failed" : exporting ? "export_failed" : "import_failed"},
                               {"message", render ? "Fake render failure" : build ? "Fake save failure." : exporting ? "Fake export failure." : "Fake import failure."}};
        result["output"] = nullptr;
        writeResult(resultFile, result);
        return 0;
    }

    if (mode == "unreadable" && task == "probe") {
        result["ok"] = false;
        result["error"] = Json{{"code", "preferences_unreadable"}, {"message", "The fake preferences cannot be read."}};
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
    } else if (task == "export") {
        result["output"] = exportOutput(params, mode);
    } else if (task == "probe") {
        result["output"] = probeOutput(version);
    } else if (task == "cleanup_material") {
        result["output"] = materialOutput(params, mode);
    } else if (task == "cleanup_asset") {
        const fs::path folder = pathFrom(params["output_folder"].get<std::string>());
        const bool fromBlend = params["source"]["format"].get<std::string>() == "blend";
        writeFile(folder / "asset.blend", fromBlend ? "CLEANED:" + params["source"]["file"].get<std::string>() : "FAKEBLEND");
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
        Json textures = Json::array();
        Json maps = Json::object();
        for (const char* channel : {"base_color", "normal", "roughness", "metallic"}) {
            const std::string file = std::string("textures/maps/fake/") + channel + ".png";
            writeFile(folder / pathFrom(file), tinyPng());
            textures.push_back(file);
            maps[channel] = Json{{"file", file}, {"from", "file"}, {"detail", "fake.png"}};
        }
        for (const char* channel : {"opacity", "occlusion"}) maps[channel] = Json{{"file", nullptr}, {"from", "none"}, {"detail", ""}};
        output["textures"] = textures;
        output["material_maps"] = Json::array({Json{{"name", "Fake"}, {"maps", maps}, {"preview_maps", maps}}});
        output["blend_file"] = "asset.blend";
        output["fbx_file"] = fbxWritten ? Json("asset.fbx") : Json(nullptr);
        output["picked_file"] = zip ? Json("model.obj") : Json(nullptr);
        output["missing_textures"] = Json::array();
        output["thumbnail"] = thumbnail;
        output["supports"] = Json::array();
        Json ordered = Json::object();
        for (const char* key : {"blend_file", "fbx_file", "picked_file", "size", "triangles", "objects", "materials", "textures", "missing_textures", "material_maps", "thumbnail", "supports"}) {
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
