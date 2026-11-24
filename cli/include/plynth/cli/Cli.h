#pragma once

#include "Words.h"

#include <plynth/core/Render.h>

#include <filesystem>
#include <ostream>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace plynth::cli {

inline constexpr int kExitOk = 0;
inline constexpr int kExitFailed = 1;       // the command ran and refused, the answer has the error
inline constexpr int kExitBadArguments = 2; // unknown command or bad options, the answer has the error

// What the program around the CLI can do for it. An empty member means "not available here".
struct CliHost {
    plynth::SceneRenderer render; // draws the 3D view for "plynth render"
    std::stop_token stop;         // asks a running command to stop, for Ctrl+C; a long command then answers cancelled
};

// Runs one command. args are the words after the program name, in UTF-8. Writes exactly one JSON answer
// and a line break to out, using currentFolder where the user gave no --project. Returns the exit code.
// A command that takes long writes one JSON progress line at a time to progress when it is not null.
// host gives what only the program around the CLI can do, such as drawing the 3D view.
int run(std::span<const std::string> args, std::ostream& out, const std::filesystem::path& currentFolder,
        std::ostream* progress = nullptr, const CliHost& host = {});

// Checks that arguments name a command and that its options parse, without running it. Empty when they do,
// otherwise the reason. The guide test uses it to keep every example in docs/CLI_GUIDE.md true.
std::string parseProblem(std::span<const std::string> args);

// Entry point for main: connects to the console or pipe it was started from, reads the arguments as
// UTF-8 from the Windows command line and runs the command in the process's current folder. When plynth.com
// started it, the event named in PLYNTH_CANCEL_EVENT stops the running command (Ctrl+C in the terminal).
int runFromProcess(int argc, char* argv[], const CliHost& host = {});

}
