// The fake store tasks of the fake Blender. blenderkit_fetch acts by the FAKE_BLENDERKIT variable: unset (the
// item is the kind the job asks for), ok (the item is a model), material, hdr, not_signed_in, addon_missing,
// download_failed, timeout, mislabel (succeeds with the other kind than asked), missing_file (reports a file
// it did not write). It writes fake_item.blend and textures/fake.png into the job's output_folder, its whole
// command line to fake_args.txt next to the job file and next to the program, and a copy of each result to
// fake_result.json next to the program. store_fetch_env.cpp records each run and its side effects.
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

void keepResultCopy(const Json& result);

namespace {

// A path from UTF-8 text.
fs::path pathFrom(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

// Writes bytes to a file, making its folder.
void writeFile(const fs::path& file, const std::string& bytes)
{
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// Prints a progress line the way the worker does.
void progress(const char* step, int index, const char* message)
{
    const Json p = Json{{"progress", Json{{"step", step}, {"index", index}, {"count", 3}, {"message", message}}}};
    std::cout << "PLYNTH:" << p.dump() << std::endl;
}

// What the fake does, from FAKE_BLENDERKIT.
std::string fakeKitMode()
{
    char* value = nullptr;
    std::size_t size = 0;
    _dupenv_s(&value, &size, "FAKE_BLENDERKIT");
    std::string text = value ? value : "";
    std::free(value);
    return text.empty() ? "follow" : text;
}

// Writes the result through a temporary file and a rename.
void writeResult(const fs::path& file, const Json& result)
{
    fs::path tmp = file;
    tmp += ".tmp";
    writeFile(tmp, result.dump(2));
    keepResultCopy(result);
    std::error_code ec;
    fs::rename(tmp, file, ec);
}

}

void recordFetchRun(const Json& job, const std::string& jobPath, const std::string& line);
int fetchSideEffects(const Json& job);

// Runs the fake blenderkit_fetch job. Returns the process exit code.
int runFakeStoreFetch(const Json& job, const std::string& jobPath, const std::vector<std::string>& args)
{
    const fs::path jobFile = pathFrom(jobPath);
    std::string line;
    for (const std::string& a : args) line += (line.empty() ? "" : " ") + a;
    writeFile(jobFile.parent_path() / "fake_args.txt", line + "\n");
    recordFetchRun(job, jobPath, line);
    if (const int code = fetchSideEffects(job); code >= 0) return code;

    const Json& params = job["params"];
    const fs::path folder = pathFrom(params["output_folder"].get<std::string>());
    const std::string id = params["asset_base_id"].get<std::string>();
    const std::string how = fakeKitMode();
    const fs::path resultFile = pathFrom(job["result_file"].get<std::string>());

    progress("check_addon", 1, "Checking the BlenderKit add-on");
    Json result = Json::object();
    result["format"] = "plynth-blender-result";
    result["version"] = 1;
    result["task"] = job["task"];
    result["job_id"] = job["job_id"];
    result["blender_version"] = "5.2.0";
    result["warnings"] = Json::array();
    result["elapsed_ms"] = 5;

    const auto failure = [&](const char* code, const char* message) {
        result["ok"] = false;
        result["error"] = Json{{"code", code}, {"message", message}};
        result["output"] = nullptr;
        writeResult(resultFile, result);
        return 0;
    };
    if (how == "addon_missing") {
        return failure("addon_missing", "The BlenderKit add-on is not installed or not turned on in Blender.");
    }
    if (how == "not_signed_in") {
        return failure("not_signed_in",
                       "Sign in to BlenderKit inside Blender first: open Blender, go to Preferences, Add-ons, BlenderKit, and log in.");
    }
    progress("download", 2, "Downloading from BlenderKit");
    if (how == "download_failed") return failure("download_failed", "The BlenderKit add-on reported an error while downloading.");
    if (how == "timeout") return failure("download_timeout", "BlenderKit did not finish the download in time.");
    if (how == "hdr") {
        return failure("wrong_asset_kind", "This BlenderKit item is an HDR. Only models and materials can be imported.");
    }

    const bool wantsMaterial = params["kind"].get<std::string>() == "material";
    const bool material = how == "mislabel" ? !wantsMaterial : how == "follow" ? wantsMaterial : how == "material";
    if (how != "mislabel" && material != wantsMaterial) {
        return failure("wrong_asset_kind", material
            ? "This BlenderKit item is a material. Import it in the Materials window or with plynth materials import."
            : "This BlenderKit item is a model. Import it in the Assets window or with plynth assets import.");
    }

    progress("copy", 3, "Copying the download");
    if (how != "missing_file") writeFile(folder / "fake_item.blend", "FAKEBLEND");
    writeFile(folder / "textures" / "fake.png", "FAKEPNG");
    result["ok"] = true;
    result["error"] = nullptr;
    result["output"] = Json{{"file", "fake_item.blend"},
                            {"format", "blend"},
                            {"kind", material ? "material" : "model"},
                            {"name", "Fake chair"},
                            {"author", "Fake Author"},
                            {"license", "royalty_free"},
                            {"reference", "https://www.blendkit.com/asset-gallery-detail/" + id + "/"},
                            {"material", material ? Json("Fake material") : Json(nullptr)}};
    writeResult(resultFile, result);
    return 0;
}
