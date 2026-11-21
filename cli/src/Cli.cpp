#include <plynth/cli/Cli.h>

#include "Answer.h"
#include "Registry.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <exception>
#include <map>
#include <sstream>
#include <vector>

namespace plynth::cli {

namespace {

// The command whose words start the argument list, or null when none do.
// A group word such as "project" with no known second word gives null and sets groupOnly.
const CommandSpec* matchCommand(const std::vector<std::string>& args, bool& groupOnly) {
    groupOnly = false;
    if (args.empty()) {
        return nullptr;
    }
    bool groupKnown = false;
    for (const CommandSpec& spec : allCommands()) {
        if (spec.words.front() != args.front()) {
            continue;
        }
        groupKnown = true;
        if (spec.words.size() == 1) {
            return &spec;
        }
        if (args.size() > 1 && spec.words[1] == args[1]) {
            return &spec;
        }
    }
    groupOnly = groupKnown;
    return nullptr;
}

// Builds the CLI11 tree for every command and keeps each command's handler by its words.
void buildTree(CLI::App& root, std::map<std::string, Handler>& handlers) {
    std::map<std::string, CLI::App*> groups;
    for (const CommandSpec& spec : allCommands()) {
        CLI::App* parent = &root;
        if (spec.words.size() > 1) {
            auto found = groups.find(spec.words.front());
            if (found == groups.end()) {
                CLI::App* group = root.add_subcommand(spec.words.front());
                group->require_subcommand(1);
                found = groups.emplace(spec.words.front(), group).first;
            }
            parent = found->second;
        }
        CLI::App* sub = parent->add_subcommand(spec.words.back(), spec.description);
        handlers[joinWords(spec.words)] = spec.setup(*sub);
    }
}

// Removes --pretty from the arguments and says whether it was there.
bool takePretty(std::vector<std::string>& args) {
    const auto before = args.size();
    std::erase(args, "--pretty");
    return args.size() != before;
}

// Parses and runs one command and returns its answer.
Answer runCommand(std::vector<std::string> args, const std::filesystem::path& currentFolder) {
    const bool wantsHelp = std::ranges::any_of(args, [](const std::string& arg) { return arg == "--help" || arg == "-h"; });
    if (wantsHelp) {
        args = {"help"};
    }
    bool groupOnly = false;
    const CommandSpec* spec = matchCommand(args, groupOnly);
    if (spec == nullptr) {
        if (args.empty()) {
            return badArguments("", "unknown_command", unknownCommandMessage(args));
        }
        if (groupOnly) {
            return badArguments(args.front(), "unknown_command",
                                "Unknown or missing command after '" + args.front() + "'. Run 'plynth help' to list the commands.");
        }
        return badArguments("", "unknown_command", unknownCommandMessage(args));
    }

    CLI::App root{"plynth", "plynth"};
    root.require_subcommand(1);
    std::map<std::string, Handler> handlers;
    buildTree(root, handlers);

    RunContext context;
    context.currentFolder = currentFolder;
    context.root = &root;

    const std::string words = joinWords(spec->words);
    try {
        std::vector<std::string> reversed(args.rbegin(), args.rend());
        root.parse(std::move(reversed));
    } catch (const CLI::ParseError& error) {
        return badArguments(words, "bad_arguments", error.what());
    }
    return handlers.at(words)(context);
}

// Written once, so every answer ends the same way.
void writeAnswer(const Answer& answer, bool pretty, std::ostream& out) {
    const Json json = toJson(answer);
    out << json.dump(pretty ? 2 : -1, ' ', false, Json::error_handler_t::replace) << '\n';
    out.flush();
}

}

int run(std::span<const std::string> args, std::ostream& out, const std::filesystem::path& currentFolder) {
    std::vector<std::string> words(args.begin(), args.end());
    const bool pretty = takePretty(words);
    Answer answer;
    try {
        answer = runCommand(std::move(words), currentFolder);
    } catch (const std::exception& error) {
        answer = badArguments("", "internal_error", error.what());
        answer.exitCode = kExitFailed;
    }
    writeAnswer(answer, pretty, out);
    return answer.exitCode;
}

}
