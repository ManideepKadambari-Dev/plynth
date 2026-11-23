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
};

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
