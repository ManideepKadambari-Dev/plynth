#pragma once

#include <span>
#include <string>
#include <string_view>

namespace plynth::cli {

// The first words of every CLI command. This list has no dependency on core, so the small plynth.com
// launcher can use it. A test keeps it equal to the commands that help lists.
std::span<const std::string_view> commandWords();

// Says whether a first argument names a CLI command, so plynth.exe answers in JSON instead of opening the window.
bool isCommandWord(std::string_view word);

// The unknown_command message for the arguments left after --pretty is removed: none, or a first word that
// names no command.
std::string unknownCommandMessage(std::span<const std::string> args);

// The whole unknown_command answer line, exactly as the CLI prints it, for arguments that may include --pretty.
std::string unknownCommandAnswer(std::span<const std::string> args);

}
