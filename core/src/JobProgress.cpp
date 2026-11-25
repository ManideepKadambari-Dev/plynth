#include "plynth/core/JobProgress.h"

#include "BuildLockFiles.h"
#include "FileIo.h"

#include "plynth/core/Build.h"
#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr int kPreparingPercent = 3;
constexpr int kBlenderStartPercent = 5;
constexpr int kBlenderSpanPercent = 90;
constexpr int kFinishingPercent = 97;

// A text field of the file, or empty when it is missing or not text.
std::string textField(const Json& json, const char* key)
{
    const auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// A whole number field of the file, or 0 when it is missing or not a number.
long long numberField(const Json& json, const char* key)
{
    const auto it = json.find(key);
    return it != json.end() && it->is_number_integer() ? it->get<long long>() : 0;
}

// Writes text next to the file and renames it over the file, trying a few times while a reader holds it open.
void replaceFile(const fs::path& file, const std::string& text)
{
    fs::path tmp;
    if (!fileio::writeTemp(file, text, tmp).ok()) return;
    std::error_code ec;
    for (int attempt = 0; attempt < 3; ++attempt) {
        fs::rename(tmp, file, ec);
        if (!ec) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    fs::remove(tmp, ec);
}

}

struct JobProgressFile::State {
    std::mutex mutex;
    fs::path file;
    JobProgress progress;
    std::chrono::steady_clock::time_point lastWrite{};
    bool written = false;
};

// Turns a stage and the worker's step into the percent of the bar.
int jobPercent(std::string_view stage, const BlenderProgress& blender)
{
    if (stage == "preparing") return kPreparingPercent;
    if (stage == "blender") {
        if (blender.count < 1) return kBlenderStartPercent;
        return kBlenderStartPercent + kBlenderSpanPercent * std::clamp(blender.index - 1, 0, blender.count) / blender.count;
    }
    if (stage == "finishing") return kFinishingPercent;
    return 100;
}

// Reads the progress file of a project.
std::optional<JobProgress> readJobProgress(const fs::path& projectFolder)
{
    std::ifstream in(buildFolder(projectFolder) / pathFromUtf8(kJobProgressFileName), std::ios::binary);
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || textField(json, "format") != kJobProgressFormat ||
        numberField(json, "version") != kJobProgressVersion) {
        return std::nullopt;
    }
    JobProgress p;
    p.kind = textField(json, "kind");
    p.jobId = textField(json, "job_id");
    p.stage = textField(json, "stage");
    p.step = textField(json, "step");
    p.index = static_cast<int>(numberField(json, "index"));
    p.count = static_cast<int>(numberField(json, "count"));
    p.message = textField(json, "message");
    p.percent = static_cast<int>(std::clamp<long long>(numberField(json, "percent"), 0, 100));
    if (const std::optional<SessionTime> started = parseSessionTime(textField(json, "started"))) p.started = *started;
    p.owner.pid = numberField(json, "pid");
    p.owner.startKey = numberField(json, "start_key");
    if (p.kind != "build" && p.kind != "export") return std::nullopt;
    return p;
}

// Says whether a live lock says a job runs.
bool jobRunning(const fs::path& projectFolder)
{
    const fs::path lock = buildFolder(projectFolder) / pathFromUtf8(kBuildLockFileName);
    std::error_code ec;
    return fs::exists(lock, ec) && lockfile::lockIsLive(lock);
}

// Remembers the job; nothing is written before the first update.
JobProgressFile::JobProgressFile(const fs::path& buildFolder, std::string kind, std::string jobId, SessionTime started)
    : state_(std::make_unique<State>())
{
    state_->file = buildFolder / pathFromUtf8(kJobProgressFileName);
    state_->progress.kind = std::move(kind);
    state_->progress.jobId = std::move(jobId);
    state_->progress.started = started;
    state_->progress.owner = currentProcess();
}

// Removes the progress file.
JobProgressFile::~JobProgressFile()
{
    const std::lock_guard<std::mutex> hold(state_->mutex);
    std::error_code ec;
    fs::remove(state_->file, ec);
    fs::path tmp = state_->file;
    tmp += ".tmp";
    fs::remove(tmp, ec);
}

// Records the stage and step and writes the file when it is due.
void JobProgressFile::update(std::string_view stage, const BlenderProgress& blender, std::string_view stageMessage)
{
    const std::lock_guard<std::mutex> hold(state_->mutex);
    JobProgress& p = state_->progress;
    const bool changed = !state_->written || p.stage != stage || p.step != blender.step || p.index != blender.index;
    p.stage = std::string(stage);
    p.step = blender.step;
    p.index = blender.index;
    p.count = blender.count;
    p.message = blender.message.empty() ? std::string(stageMessage) : blender.message;
    p.percent = jobPercent(stage, blender);
    const auto now = std::chrono::steady_clock::now();
    if (!changed && now - state_->lastWrite < kJobProgressInterval) return;

    Json json = Json::object();
    json["format"] = std::string(kJobProgressFormat);
    json["version"] = kJobProgressVersion;
    json["kind"] = p.kind;
    json["job_id"] = p.jobId;
    json["stage"] = p.stage;
    json["step"] = p.step;
    json["index"] = p.index;
    json["count"] = p.count;
    json["message"] = p.message;
    json["percent"] = p.percent;
    json["started"] = formatSessionTime(p.started);
    json["pid"] = p.owner.pid;
    json["start_key"] = p.owner.startKey;
    replaceFile(state_->file, json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
    state_->lastWrite = now;
    state_->written = true;
}

}
