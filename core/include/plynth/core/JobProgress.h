#pragma once

#include "Blender.h"
#include "EditSession.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace plynth {

// While a build or an export runs, in the app or from "plynth build" or "plynth export", it keeps
// build/progress.json up to date, so another program with the project open can show how far it has come.
// The file is replaced in one rename, at most kJobProgressInterval apart unless the stage or the step changes,
// and removed when the job ends, before build/build.lock is removed. Its form:
//   {"format": "plynth-build-progress", "version": 1, "kind": "build" or "export", "job_id": "build-3f9a07c1",
//    "stage": "preparing" | "blender" | "finishing" | "done", "step": the worker's step id or "",
//    "index": 1 based step or 0, "count": steps or 0, "message": one plain sentence, "percent": 0 to 100,
//    "started": the job's start time (formatSessionTime), "pid": the process that holds the lock,
//    "start_key": that process's start key (ProcessIdentity)}
inline constexpr std::string_view kJobProgressFileName = "progress.json";
inline constexpr std::string_view kJobProgressFormat = "plynth-build-progress";
inline constexpr int kJobProgressVersion = 1;
inline constexpr std::chrono::milliseconds kJobProgressInterval{250};

struct JobProgress {
    std::string kind;    // "build" or "export"
    std::string jobId;
    std::string stage;   // buildStageName or exportStageName
    std::string step;
    int index = 0;
    int count = 0;
    std::string message;
    int percent = 0;
    SessionTime started{};
    ProcessIdentity owner;
};

// The percent the progress bar shows for a stage and the worker's step, the same for builds and exports:
// 3 while preparing, 5 to 95 over the worker's steps, 97 while finishing, 100 when done.
int jobPercent(std::string_view stage, const BlenderProgress& blender);

// Reads projectFolder/build/progress.json. Empty when there is none or it is not a valid progress file.
std::optional<JobProgress> readJobProgress(const std::filesystem::path& projectFolder);

// True while a live build/build.lock says a build or an export of the project runs, in any process. A lock left
// behind by a program that stopped, or older than kBuildTimeout, does not count.
bool jobRunning(const std::filesystem::path& projectFolder);

// Keeps build/progress.json of one job. Made after the job took the lock; removes the file when it goes away.
class JobProgressFile {
public:
    JobProgressFile(const std::filesystem::path& buildFolder, std::string kind, std::string jobId, SessionTime started);
    ~JobProgressFile();
    JobProgressFile(const JobProgressFile&) = delete;
    JobProgressFile& operator=(const JobProgressFile&) = delete;

    // Records the job's stage and step. Writes at once when the stage or the step changed, otherwise at most every
    // kJobProgressInterval. A write that fails is dropped; the next one tries again. Safe from several threads.
    void update(std::string_view stage, const BlenderProgress& blender, std::string_view stageMessage);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
