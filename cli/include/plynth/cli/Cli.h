#pragma once

#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace plynth::cli {

inline constexpr int kExitOk = 0;
inline constexpr int kExitFailed = 1;       // the command ran and refused, the answer has the error
inline constexpr int kExitBadArguments = 2; // unknown command or bad options, the answer has the error

// Says whether a first argument names a CLI command, so plynth.exe answers in JSON instead of opening the window.
bool isCommandWord(std::string_view word);

// Runs one command. args are the words after the program name, in UTF-8. Writes exactly one JSON answer
// and a line break to out, using currentFolder where the user gave no --project. Returns the exit code.
int run(std::span<const std::string> args, std::ostream& out, const std::filesystem::path& currentFolder);

// Entry point for main: connects to the console or pipe it was started from, reads the arguments as
// UTF-8 from the Windows command line and runs the command in the process's current folder.
int runFromProcess(int argc, char* argv[]);

}
