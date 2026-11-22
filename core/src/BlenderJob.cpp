#include "plynth/core/BlenderJob.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <random>
#include <system_error>

namespace plynth {

namespace {

// A field of an object that must exist. Throws a status-carrying problem when it does not.
struct Problem {
    std::string where;
    std::string message;
};

const Json& field(const Json& object, const char* key, const std::string& where)
{
    if (!object.is_object() || !object.contains(key)) throw Problem{where + "/" + key, std::string("Missing field \"") + key + "\"."};
    return object[key];
}

}

// Makes a job.
Json makeBlenderJob(std::string_view task, std::string_view jobId, const std::filesystem::path& resultFile, Json params)
{
    Json job = Json::object();
    job["format"] = std::string(kBlenderJobFormat);
    job["version"] = kBlenderJobVersion;
    job["task"] = std::string(task);
    job["job_id"] = std::string(jobId);
    job["result_file"] = pathToUtf8(resultFile);
    job["params"] = std::move(params);
    return job;
}

// A job id: the stem and 8 random hex digits.
std::string newJobId(std::string_view stem)
{
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    static const char digits[] = "0123456789abcdef";
    std::string id(stem);
    id += '-';
    const unsigned long long value = engine();
    for (int i = 0; i < 8; ++i) id += digits[(value >> (i * 4)) & 0xF];
    return id;
}

// Reads and checks the fields every result has.
BlenderResultHeader readBlenderResultHeader(const Json& result, std::string_view task, std::string_view jobId)
{
    BlenderResultHeader header;
    try {
        if (!result.is_object()) throw Problem{"", "The result is not a JSON object."};
        const Json& format = field(result, "format", "");
        if (!format.is_string() || format.get<std::string>() != kBlenderResultFormat) {
            throw Problem{"/format", "This is not a Plynth Blender result."};
        }
        const Json& version = field(result, "version", "");
        if (!version.is_number_integer() || version.get<int>() != kBlenderResultVersion) {
            throw Problem{"/version", "The result version is not supported."};
        }
        const Json& resultTask = field(result, "task", "");
        if (!resultTask.is_string() || resultTask.get<std::string>() != task) {
            throw Problem{"/task", "The result is for another task."};
        }
        const Json& resultJob = field(result, "job_id", "");
        if (!resultJob.is_string() || resultJob.get<std::string>() != jobId) {
            throw Problem{"/job_id", "The result is for another job."};
        }
        const Json& ok = field(result, "ok", "");
        if (!ok.is_boolean()) throw Problem{"/ok", "Must be true or false."};
        const Json& blenderVersion = field(result, "blender_version", "");
        if (!blenderVersion.is_string()) throw Problem{"/blender_version", "Must be text."};
        header.blenderVersion = blenderVersion.get<std::string>();
        const Json& elapsed = field(result, "elapsed_ms", "");
        if (!elapsed.is_number_integer() || elapsed.get<long long>() < 0) throw Problem{"/elapsed_ms", "Must be a whole number of at least 0."};
        header.elapsedMs = elapsed.get<long long>();
        const Json& warnings = field(result, "warnings", "");
        if (!warnings.is_array()) throw Problem{"/warnings", "Must be a list."};
        for (std::size_t i = 0; i < warnings.size(); ++i) {
            const std::string where = "/warnings/" + std::to_string(i);
            const Json& code = field(warnings[i], "code", where);
            const Json& message = field(warnings[i], "message", where);
            if (!code.is_string() || !message.is_string()) throw Problem{where, "Must have text code and message."};
            header.warnings.push_back({code.get<std::string>(), message.get<std::string>()});
        }
        if (!ok.get<bool>()) {
            const Json& error = field(result, "error", "");
            if (!error.is_object()) throw Problem{"/error", "A failed job needs an error."};
            const Json& code = field(error, "code", "/error");
            const Json& message = field(error, "message", "/error");
            if (!code.is_string() || !message.is_string()) throw Problem{"/error", "Must have text code and message."};
            header.errorCode = code.get<std::string>();
            header.status = fail(ErrorCode::BlenderFailed, message.get<std::string>());
        } else {
            const Json& output = field(result, "output", "");
            if (!output.is_object()) throw Problem{"/output", "A finished job needs an output."};
        }
    } catch (const Problem& p) {
        header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    }
    return header;
}

// Reads the probe result.
ProbeResult readProbeResult(const Json& result, std::string_view jobId)
{
    ProbeResult probe;
    probe.header = readBlenderResultHeader(result, kProbeTask, jobId);
    if (!probe.header.status.ok()) return probe;
    try {
        const Json& output = result["output"];
        const Json& blender = field(output, "blender", "/output");
        const Json& version = field(blender, "version", "/output/blender");
        const Json& python = field(blender, "python", "/output/blender");
        if (!version.is_string() || !python.is_string()) throw Problem{"/output/blender", "Must have text version and python."};
        probe.version = version.get<std::string>();
        probe.python = python.get<std::string>();

        const Json& addons = field(output, "addons", "/output");
        if (!addons.is_array()) throw Problem{"/output/addons", "Must be a list."};
        for (std::size_t i = 0; i < addons.size(); ++i) {
            const std::string where = "/output/addons/" + std::to_string(i);
            const Json& module = field(addons[i], "module", where);
            const Json& name = field(addons[i], "name", where);
            const Json& addonVersion = field(addons[i], "version", where);
            if (!module.is_string() || !name.is_string() || !addonVersion.is_string()) throw Problem{where, "Must have text fields."};
            probe.addons.push_back({module.get<std::string>(), name.get<std::string>(), addonVersion.get<std::string>()});
        }

        const Json& kit = field(output, "blenderkit", "/output");
        const Json& installed = field(kit, "installed", "/output/blenderkit");
        const Json& enabled = field(kit, "enabled", "/output/blenderkit");
        const Json& signedIn = field(kit, "signed_in", "/output/blenderkit");
        if (!installed.is_boolean() || !enabled.is_boolean() || !signedIn.is_boolean()) {
            throw Problem{"/output/blenderkit", "Must have true or false fields."};
        }
        probe.blenderKitInstalled = installed.get<bool>();
        probe.blenderKitEnabled = enabled.get<bool>();
        probe.blenderKitSignedIn = signedIn.get<bool>();
    } catch (const Problem& p) {
        probe.header.status = fail(ErrorCode::BadFile, "The Blender result is not valid: " + p.message, p.where);
    }
    return probe;
}

// Makes the probe job with the user's folders.
Json makeProbeJob(const BlenderUserFolders& folders, std::string_view jobId, const std::filesystem::path& resultFile)
{
    Json user = Json::object();
    user["config"] = pathToUtf8(folders.config);
    user["scripts"] = pathToUtf8(folders.scripts);
    user["extensions"] = pathToUtf8(folders.extensions);
    user["blenderkit_data"] = pathToUtf8(folders.blenderKitData);
    Json params = Json::object();
    params["user_folders"] = std::move(user);
    return makeBlenderJob(kProbeTask, jobId, resultFile, std::move(params));
}

// Runs the probe job once, in factory startup, and removes the work folder afterwards.
BlenderProbeRun runBlenderProbe(const BlenderInstall& install, const BlenderRunOptions& options, std::stop_token stop)
{
    BlenderProbeRun out;
    if (!install.usable()) {
        out.status = fail(ErrorCode::BlenderNotFound, "The Blender " + pathToUtf8(install.executable) + " cannot be used.");
        return out;
    }
    const BlenderUserFolders folders = blenderUserFolders(install);
    if (folders.config.empty() || folders.scripts.empty() || folders.extensions.empty() || folders.blenderKitData.empty()) {
        out.status = fail(ErrorCode::NotFound, "The folders of the user's Blender settings could not be worked out.");
        return out;
    }

    std::error_code ec;
    const std::string jobId = newJobId("probe");
    const std::filesystem::path work = std::filesystem::temp_directory_path(ec) / ("plynth-" + jobId);
    const Json job = makeProbeJob(folders, jobId, work / pathFromUtf8(kResultFileName));
    BlenderRunOptions run = options;
    run.factoryStartup = true;
    const BlenderJobRun ran = runBlenderJob(install.executable, job, work, run, {}, stop);
    if (!ran.result.is_null()) out.result = readProbeResult(ran.result, jobId);
    std::filesystem::remove_all(work, ec);

    if (!ran.status.ok()) {
        out.status = ran.status;
        if (out.result.header.errorCode.empty()) out.result.header.errorCode = ran.errorCode;
    } else {
        out.status = out.result.header.status;
    }
    return out;
}

}
