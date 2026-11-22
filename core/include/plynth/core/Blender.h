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
// Plynth never links against Blender and never installs it.

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

// The install to preselect: the chosen one when it is in the list and usable,
// otherwise the newest Supported one, otherwise the newest Untested one. Empty when none is usable.
std::optional<std::size_t> preferredBlender(const std::vector<BlenderInstall>& installs,
                                            const std::optional<std::filesystem::path>& chosen);

// The folders where the user's own Blender keeps its settings, scripts, extensions and BlenderKit data.
struct BlenderUserFolders {
    std::filesystem::path config;
    std::filesystem::path scripts;
    std::filesystem::path extensions;
    std::filesystem::path blenderKitData;
};

// Works out the user's folders for an install from this process's environment, as absolute paths. Only looks
// whether folders exist: it reads and creates nothing and starts no program. For config, scripts and
// extensions, the first of:
//   1. BLENDER_USER_CONFIG, BLENDER_USER_SCRIPTS or BLENDER_USER_EXTENSIONS, when that folder exists
//   2. config, scripts or extensions in BLENDER_USER_RESOURCES, when the folder the variable names exists
//   3. the same in the folder "portable" next to blender.exe, when that folder exists
//   4. the same in %APPDATA%/Blender Foundation/Blender/<major.minor>
// blenderKitData is blenderkit_data in XDG_DATA_HOME, else in USERPROFILE. A folder that cannot be worked out
// is empty.
BlenderUserFolders blenderUserFolders(const BlenderInstall& install);

// The Blender a job should use.
struct BlenderChoice {
    Status status; // BlenderNotFound when none is usable, naming what was found and why it was not used
    BlenderInstall install;
};

// Picks the Blender for a job: the option when given, else PLYNTH_BLENDER when not empty, else the path in
// settings, else the preferred one of a full search. The option and the variable are binding: when the one
// given is missing or not usable, BlenderNotFound says so and no other Blender is started. An option that is
// an empty path is given and missing, it never counts as no option. A good path in settings is the only one
// inspected, so this is quick.
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

// A step a job may report when its output cannot be trusted, with the only sentence shown for it.
struct BlenderStep {
    std::string step;
    std::string message;
};

struct BlenderRunOptions {
    // True so the user's startup file and add-ons cannot change the result and Blender is given Plynth's
    // private folder as its user folder. The probe task always runs this way, whatever this says. False only
    // for a store fetch (catalog/StoreFetch.h), which needs the user's own add-on and sign in, so Blender
    // starts in the background with the user's settings and runs the enabled add-ons.
    bool factoryStartup = true;
    std::chrono::seconds timeout{600};
    std::filesystem::path scriptsFolder; // empty: blenderScriptsFolder()
    // For a run without factory startup, in order. What such a Blender prints can hold add-on secrets, so none
    // of it is stored, returned or shown. A progress line counts only when its step is listed here. It is then
    // built again from this list: index is the place in the list, count the list's size, message the sentence
    // here. A step that repeats the one before is dropped. Empty: no progress.
    std::vector<BlenderStep> steps;
};

struct BlenderJobRun {
    // Ok when the worker wrote a result file with ok true. BlenderFailed when it wrote ok false (the message
    // is the worker's), when Blender exited without a result file, or when the result file is not valid
    // (with factory startup the message ends with the last lines of the log, without it the sentence stands
    // alone), and when the timeout passed. Cancelled on stop.
    // NotFound when the scripts folder is missing.
    Status status;
    Json result;            // the parsed result file, when there was one
    std::string errorCode;  // the worker's error code, for example "import_failed", when it gave one
    // The last part of what is saved as workFolder/blender.log: Blender's output with factory startup, only
    // the rebuilt step lines without it.
    std::string logTail;
    int exitCode = -1;
};

// Runs one job: writes job to workFolder/job.json, starts
//   <blender> --background [--factory-startup] --disable-autoexec -noaudio --python-exit-code 3
//             --python <scripts>/plynth_worker.py -- --job <workFolder>/job.json
// passes each progress line to onProgress, saves the output as workFolder/blender.log, and reads the
// result file the job names. Without factory startup the output never leaves this function in any way: the
// log and progress hold only the rebuilt lines of options.steps, failures are fixed sentences, and the result
// file is removed once it is read, also when it is broken or the run failed, timed out or was cancelled.
// Scripts inside .blend files never run by themselves, whatever the user's settings say. With factory
// startup, and always for the probe task, the child's five BLENDER_USER_ variables point at Plynth's private
// folder. When that folder cannot be created the status is CannotWrite and Blender is not started. Blocks;
// the app calls it from a worker thread.
BlenderJobRun runBlenderJob(const std::filesystem::path& blender, const Json& job,
                            const std::filesystem::path& workFolder, const BlenderRunOptions& options,
                            const std::function<void(const BlenderProgress&)>& onProgress = {},
                            std::stop_token stop = {});

}
