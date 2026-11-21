#pragma once

#include "Settings.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Finding Blender on this machine, reading its version, and running a Plynth job in it in the background.
// Plynth never links against Blender and never installs it (DESIGN.md sections 2 and 7.19b).

struct BlenderVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const BlenderVersion&) const = default;
};

// The oldest Blender Plynth runs with, and the one it is developed and tested against.
inline constexpr BlenderVersion kMinBlenderVersion{4, 2, 0};
inline constexpr BlenderVersion kTestedBlenderVersion{5, 2, 0};

// "5.2.0".
std::string formatBlenderVersion(BlenderVersion version);

// Reads the version from the first line Blender prints for --version, for example "Blender 5.2.0 LTS" or
// "Blender 4.2.3". A missing patch number is 0. Empty when the text has no version.
std::optional<BlenderVersion> parseBlenderVersion(std::string_view text);

enum class BlenderSupport {
    Supported, // from kMinBlenderVersion up to the tested major version
    TooOld,    // below kMinBlenderVersion: blocks import and build
    Untested,  // a newer major version than kTestedBlenderVersion: allowed, with a warning
};

BlenderSupport blenderSupport(BlenderVersion version);

// "supported", "too_old" or "untested".
std::string_view blenderSupportName(BlenderSupport support);

// Where a Blender was found. A path found in several places lists each.
enum class BlenderSource {
    Settings,      // the path saved in the machine settings
    Environment,   // the PLYNTH_BLENDER environment variable
    ProgramFolder, // Program Files, Program Files (x86) and %LOCALAPPDATA%\Programs, under "Blender Foundation"
    Registry,      // uninstall entries, App Paths, and the .blend file open command
    Store,         // Microsoft Store packages named BlenderFoundation.Blender*
    Steam,         // steamapps/common/Blender in every Steam library
    SystemPath,    // blender.exe on PATH
    Extra,         // a folder given in BlenderSearch::extraFolders (tests)
};

// "settings", "environment", "program_folder", "registry", "store", "steam", "path" or "extra".
std::string_view blenderSourceName(BlenderSource source);

inline constexpr std::string_view kBlenderVariable = "PLYNTH_BLENDER";

// Which places to look in. Tests turn the machine sources off and use extraFolders.
struct BlenderSearch {
    std::optional<std::filesystem::path> chosen; // from the settings
    bool environment = true;
    bool programFolders = true;
    bool registry = true;
    bool store = true;
    bool steam = true;
    bool systemPath = true;
    // Each folder is searched like a program folder: folder/blender.exe and folder/*/blender.exe.
    std::vector<std::filesystem::path> extraFolders;
};

// A blender.exe that exists, before its version is read.
struct BlenderCandidate {
    std::filesystem::path executable; // absolute and canonical, so the same install is listed once
    std::vector<BlenderSource> sources;
};

// Lists every blender.exe in the places the search names. Never runs anything. A chosen path that does
// not exist is left out (the setup check reports it).
std::vector<BlenderCandidate> findBlenderCandidates(const BlenderSearch& search);

// How long reading a version may take before the install counts as broken.
inline constexpr std::chrono::seconds kBlenderVersionTimeout{30};

// One install with what is known about it.
struct BlenderInstall {
    std::filesystem::path executable;
    std::vector<BlenderSource> sources;
    std::optional<BlenderVersion> version; // empty when it could not be read
    std::string versionText;               // the first line Blender printed, for example "Blender 5.2.0 LTS"
    BlenderSupport support = BlenderSupport::TooOld;
    Status status; // BlenderFailed with the reason when the version could not be read

    bool usable() const { return status.ok() && version && support != BlenderSupport::TooOld; }
};

// Runs "blender --background --factory-startup --version" and reads the answer.
BlenderInstall inspectBlender(const std::filesystem::path& executable, std::stop_token stop = {});

// Finds the candidates and inspects each. Sorted newest version first; installs whose version could not
// be read come last, by path.
std::vector<BlenderInstall> findBlenderInstalls(const BlenderSearch& search, std::stop_token stop = {});

// The install to preselect (DESIGN.md section 7.19b): the chosen one when it is in the list and usable,
// otherwise the newest Supported one, otherwise the newest Untested one. Empty when none is usable.
std::optional<std::size_t> preferredBlender(const std::vector<BlenderInstall>& installs,
                                            const std::optional<std::filesystem::path>& chosen);

// The Blender a job should use.
struct BlenderChoice {
    Status status; // BlenderNotFound when none is usable, naming what was found and why it was not used
    BlenderInstall install;
};

// Picks the Blender for a job: the option when given (it must be usable), then PLYNTH_BLENDER, then the
// path in settings, then the preferred one of a full search. Only the first usable one is inspected when an
// explicit path is given, so this is quick when the settings hold a good path.
BlenderChoice chooseBlender(const std::optional<std::filesystem::path>& option, const MachineSettings& settings,
                            std::stop_token stop = {});

// The Plynth scripts Blender runs (the blender/ folder of the repository). The first folder holding
// kWorkerScriptName of: the PLYNTH_BLENDER_SCRIPTS environment variable, the folder "blender" next to the
// running program, and in a developer build the source tree's blender/ folder. Empty when none does.
inline constexpr std::string_view kWorkerScriptName = "plynth_worker.py";
inline constexpr std::string_view kBlenderScriptsVariable = "PLYNTH_BLENDER_SCRIPTS";
std::filesystem::path blenderScriptsFolder();

// A progress line from the worker (see BlenderJob.h for the line format).
struct BlenderProgress {
    std::string step;    // the step id, for example "apply_transforms"
    int index = 0;       // 1 based
    int count = 0;
    std::string message; // one plain sentence for the progress bar
};

struct BlenderRunOptions {
    // True for cleanup and thumbnails, so the user's startup file and add-ons cannot change the result.
    // False for the probe, which must see the user's add-ons.
    bool factoryStartup = true;
    std::chrono::seconds timeout{600};
    std::filesystem::path scriptsFolder; // empty: blenderScriptsFolder()
};

struct BlenderJobRun {
    // Ok when the worker wrote a result file with ok true. BlenderFailed when it wrote ok false (the message
    // is the worker's), when Blender exited without a result file, or when the result file is not valid
    // (the message ends with the last lines of the log). Cancelled on stop or timeout. NotFound when the
    // scripts folder is missing.
    Status status;
    Json result;            // the parsed result file, when there was one
    std::string errorCode;  // the worker's error code, for example "import_failed", when it gave one
    std::string logTail;    // the last part of Blender's output, also saved as workFolder/blender.log
    int exitCode = -1;
};

// Runs one job: writes job to workFolder/job.json, starts
//   <blender> --background [--factory-startup] -noaudio --python-exit-code 3
//             --python <scripts>/plynth_worker.py -- --job <workFolder>/job.json
// passes each progress line to onProgress, saves the output as workFolder/blender.log, and reads the
// result file the job names. Blocks; the app calls it from a worker thread.
BlenderJobRun runBlenderJob(const std::filesystem::path& blender, const Json& job,
                            const std::filesystem::path& workFolder, const BlenderRunOptions& options,
                            const std::function<void(const BlenderProgress&)>& onProgress = {},
                            std::stop_token stop = {});

}
