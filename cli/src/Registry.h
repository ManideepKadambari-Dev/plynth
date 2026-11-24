#pragma once

#include "Answer.h"

#include <plynth/cli/Cli.h>

#include <CLI/CLI.hpp>

#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace plynth::cli {

// What a command handler is given when it runs.
struct RunContext {
    std::filesystem::path currentFolder;
    const CLI::App* root = nullptr; // the whole command tree, for help
    std::ostream* progress = nullptr; // where long commands write progress lines, null for none
    const CliHost* host = nullptr;    // what the program around the CLI can do, null for nothing
};

using Handler = std::function<Answer(const RunContext&)>;

// One command: its words, a description, and a function that adds its options to a CLI11 subcommand
// and returns the handler to run once the options are parsed.
struct CommandSpec {
    std::vector<std::string> words;
    std::string description;
    std::function<Handler(CLI::App&)> setup;
};

// Each command group lives in its own file and lists its commands here.
std::vector<CommandSpec> versionCommands();
std::vector<CommandSpec> helpCommands();
std::vector<CommandSpec> projectCommands();
std::vector<CommandSpec> scaleCommands();
std::vector<CommandSpec> pointCommands();
std::vector<CommandSpec> wallCommands();
std::vector<CommandSpec> openingCommands();
std::vector<CommandSpec> floorCommands();
std::vector<CommandSpec> stairCommands();
std::vector<CommandSpec> roofCommands();
std::vector<CommandSpec> roomCommands();
std::vector<CommandSpec> detectCommands();
std::vector<CommandSpec> sessionCommands();
std::vector<CommandSpec> settingsCommands();
std::vector<CommandSpec> libraryCommands();
std::vector<CommandSpec> assetCommands();
std::vector<CommandSpec> doctorCommands();
std::vector<CommandSpec> importCommands();
std::vector<CommandSpec> itemCommands();
std::vector<CommandSpec> buildCommands();
std::vector<CommandSpec> exportCommands();
std::vector<CommandSpec> materialCommands();
std::vector<CommandSpec> materialCatalogCommands();
std::vector<CommandSpec> materialImportCommands();
std::vector<CommandSpec> generatorCommands();
std::vector<CommandSpec> renderCommands();

// All commands in the order help lists them.
const std::vector<CommandSpec>& allCommands();

// The command words joined with spaces, for example "project new".
std::string joinWords(const std::vector<std::string>& words);

}
