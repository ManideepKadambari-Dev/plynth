#include <plynth/core/catalog/StoreFetch.h>

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/BlenderKit.h>

#include <algorithm>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Reads a text field of an object. A null counts as empty when allowNull is set.
bool textField(const Json& object, const char* name, std::string& out, bool allowNull)
{
    if (!object.is_object() || !object.contains(name)) return false;
    const Json& value = object[name];
    if (value.is_null()) return allowNull;
    if (!value.is_string()) return false;
    out = value.get<std::string>();
    return true;
}

// True when a relative file name stays inside its folder.
bool staysInside(const std::string& name)
{
    if (name.empty() || name.find('\\') != std::string::npos) return false;
    const fs::path path = pathFromUtf8(name);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    return std::none_of(path.begin(), path.end(), [](const fs::path& part) { return part == ".."; });
}

// The refusal sentence for an item of the wrong kind.
std::string wrongKindSentence(const ImportFetch& fetch, std::string_view fetchedKind)
{
    const std::string store = fetch.task == kBlenderKitFetchTask ? "BlenderKit item" : "item";
    if (fetchedKind == kFetchKindMaterial) {
        return "This " + store + " is a material. Import it in the Materials tab or with plynth materials import.";
    }
    return "This " + store + " is a model. Import it in the Assets tab or with plynth assets import.";
}

}

// Reads and checks a fetch result.
FetchOutput readFetchResult(const Json& result, std::string_view task, std::string_view jobId)
{
    FetchOutput out;
    out.header = readBlenderResultHeader(result, task, jobId);
    if (!out.header.status.ok()) return out;
    const auto bad = [&](const std::string& where, const std::string& what) {
        out.header.status = fail(ErrorCode::BadFile, what, where);
        return out;
    };
    if (!result.contains("output") || !result["output"].is_object()) return bad("/output", "The fetch result has no output.");
    const Json& output = result["output"];
    if (!textField(output, "file", out.file, false) || !staysInside(out.file)) {
        return bad("/output/file", "The fetched file name is missing or leaves the output folder.");
    }
    if (!textField(output, "format", out.format, false) || out.format.empty()) return bad("/output/format", "Must be text.");
    if (!textField(output, "kind", out.kind, false) || (out.kind != kFetchKindModel && out.kind != kFetchKindMaterial)) {
        return bad("/output/kind", "Must be model or material.");
    }
    const std::pair<const char*, std::string*> optional[] = {
        {"name", &out.name}, {"author", &out.author}, {"license", &out.license}, {"reference", &out.reference}, {"material", &out.material}};
    for (const auto& [name, target] : optional) {
        if (!textField(output, name, *target, true)) return bad(std::string("/output/") + name, "Must be text or null.");
    }
    return out;
}

// Runs a fetch job in the work folder.
FetchRun runImportFetch(const ImportFetch& fetch, const fs::path& blender, const BlenderRunOptions& base,
                        const fs::path& workFolder, const std::function<void(const BlenderProgress&)>& onProgress,
                        std::stop_token stop)
{
    FetchRun run;
    const fs::path outputFolder = workFolder / pathFromUtf8(kFetchOutputFolder);
    std::error_code ec;
    fs::create_directories(outputFolder, ec);
    if (ec) {
        run.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(outputFolder) + " could not be created.");
        return run;
    }

    Json params = fetch.params.is_object() ? fetch.params : Json::object();
    params["output_folder"] = pathToUtf8(outputFolder);
    const std::string jobId = newJobId(fetch.task);
    const Json job = makeBlenderJob(fetch.task, jobId, workFolder / pathFromUtf8(kResultFileName), std::move(params));

    BlenderRunOptions options = base;
    options.factoryStartup = false;
    options.timeout = fetch.timeout;
    options.steps = fetch.steps;
    const BlenderJobRun blenderRun = runBlenderJob(blender, job, workFolder, options, onProgress, stop);
    if (!blenderRun.status.ok()) {
        run.status = blenderRun.status;
        run.errorCode = blenderRun.errorCode;
        return run;
    }

    run.output = readFetchResult(blenderRun.result, fetch.task, jobId);
    if (!run.output.header.status.ok()) {
        run.status = fail(ErrorCode::BlenderFailed, run.output.header.status.message);
        run.errorCode = run.output.header.errorCode;
        return run;
    }
    run.file = outputFolder / pathFromUtf8(run.output.file);
    if (!fs::is_regular_file(run.file, ec)) {
        run.status = fail(ErrorCode::BlenderFailed, "The fetch reported the file " + run.output.file + " but it is not there.");
        return run;
    }
    return run;
}

// Puts a fetched model into an asset plan.
Status applyFetch(const FetchRun& run, const ImportFetch& fetch, ImportPlan& plan)
{
    if (run.output.kind != kFetchKindModel) return fail(ErrorCode::InvalidValue, wrongKindSentence(fetch, run.output.kind));
    plan.file = run.file;
    plan.format = run.output.format;
    if (plan.defaultName.empty()) plan.defaultName = run.output.name;
    if (plan.license.empty()) plan.license = run.output.license;
    if (plan.reference.empty()) plan.reference = run.output.reference;
    return {};
}

// Puts a fetched material into a material plan.
Status applyFetch(const FetchRun& run, const ImportFetch& fetch, MaterialImportPlan& plan)
{
    if (run.output.kind != kFetchKindMaterial) return fail(ErrorCode::InvalidValue, wrongKindSentence(fetch, run.output.kind));
    plan.path = run.file;
    plan.workerSource = run.output.format;
    if (!run.output.material.empty()) plan.blendMaterial = run.output.material;
    if (plan.defaultName.empty()) plan.defaultName = run.output.name;
    if (plan.license.empty()) plan.license = run.output.license;
    if (plan.reference.empty()) plan.reference = run.output.reference;
    return {};
}

}
