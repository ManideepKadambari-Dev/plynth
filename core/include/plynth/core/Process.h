#pragma once

#include "Status.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

// Starting another program and waiting for it, used to run Blender in the background. On Windows the child
// gets no console window, is put in a job object so it and anything it starts end when it is stopped, and its
// standard output and standard error are read as one stream of lines.

struct ProcessSpec {
    std::filesystem::path program;
    std::vector<std::string> arguments; // UTF-8, quoted for the Windows command line by runProcess
    std::filesystem::path workingFolder; // empty: the current folder
    std::vector<std::pair<std::string, std::string>> environment; // added to, or replacing, the inherited ones
    std::chrono::milliseconds timeout{0}; // 0: no limit
    // Variables taken out of the inherited ones, by name, ignoring case. Applied before environment.
    std::vector<std::string> removeEnvironment;
    // Start the program, and so everything it starts, at low integrity. Windows then lets it write only to
    // places labelled low, such as a folder made with makeLowIntegrityFolder, and not to the user's other
    // files, folders or registry. Reading stays as before. CannotRead when that cannot be set up; the program
    // is then not started.
    bool lowIntegrity = false;
    // Without it everything the program started still ends when it ends, but runProcess does not wait for
    // that. With it runProcess returns only once every one of them is gone, and when they cannot all be ended
    // within a minute the status is CannotRead "Programs that the program started could not all be ended."
    bool endChildren = false;
};

// Makes a folder, or labels one that exists and is empty, so that a low integrity program may write in it.
// Only for a folder this process made itself. CannotWrite when it fails.
Status makeLowIntegrityFolder(const std::filesystem::path& folder);

// Checks a folder that low integrity programs already used and that is kept: it is a real folder and not a
// link, it is owned by the current user, and its label is low integrity. Never changes the folder. NotFound
// when it is not there, CannotWrite "Plynth's fetch folder is not set up as expected. Delete it and try
// again." when any of the three does not hold.
Status checkLowIntegrityFolder(const std::filesystem::path& folder);

// How much of the output is kept in ProcessResult::outputTail.
inline constexpr std::size_t kProcessOutputTailBytes = 64 * 1024;

struct ProcessResult {
    // Ok when the program ran to its end, whatever its exit code: callers decide what an exit code means.
    // NotFound when the program file does not exist, CannotRead when it could not be started, Cancelled
    // when stop was asked for or the timeout passed.
    Status status;
    int exitCode = -1;
    bool timedOut = false; // the timeout passed and the program was ended; status is Cancelled
    std::string outputTail; // the last kProcessOutputTailBytes of output, UTF-8 as the program wrote it
};

// Runs a program to its end. onLine, when given, gets each output line without its line ending, on the
// calling thread, as soon as it is read. A stop request or the timeout ends the program and everything it
// started. Blocks the calling thread, so the app calls it from a worker thread.
ProcessResult runProcess(const ProcessSpec& spec, const std::function<void(std::string_view line)>& onLine = {},
                         std::stop_token stop = {});

// Moves a file or folder to the Windows Recycle Bin, so the user can still get it back. Used for imported
// zip files. CannotWrite when it fails; NotFound when it is not there.
Status moveToRecycleBin(const std::filesystem::path& path);

}
