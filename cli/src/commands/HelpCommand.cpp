#include "../ErrorList.h"
#include "../Registry.h"

#include <string>

namespace plynth::cli {

namespace {

// Describes one option for the help answer.
Json optionJson(const CLI::Option& option) {
    Json json = Json::object();
    json["name"] = option.get_name();
    if (option.get_type_size() == 0) {
        json["value"] = nullptr;
    } else {
        json["value"] = option.get_type_name();
    }
    json["positional"] = option.get_positional();
    json["required"] = option.get_required();
    json["description"] = option.get_description();
    return json;
}

// Lists every command below app, going down through the command groups.
void collect(const CLI::App& app, const std::string& prefix, Json& commands) {
    for (const CLI::App* sub : app.get_subcommands(nullptr)) {
        const std::string words = prefix.empty() ? sub->get_name() : prefix + " " + sub->get_name();
        if (!sub->get_subcommands(nullptr).empty()) {
            collect(*sub, words, commands);
            continue;
        }
        Json command = Json::object();
        command["command"] = words;
        command["description"] = sub->get_description();
        Json options = Json::array();
        for (const CLI::Option* option : sub->get_options()) {
            if (option->get_name() == "--help") {
                continue;
            }
            options.push_back(optionJson(*option));
        }
        command["options"] = std::move(options);
        commands.push_back(std::move(command));
    }
}

// Answers with every command and its options, read from the command tree so it never goes stale, and with
// every error and exit code.
Answer helpAnswer(const RunContext& context) {
    Json commands = Json::array();
    if (context.root != nullptr) {
        collect(*context.root, "", commands);
    }
    Json result = Json::object();
    result["commands"] = std::move(commands);
    result["error_codes"] = errorCodesJson();
    result["exit_codes"] = exitCodesJson();
    return success("help", std::move(result));
}

}

std::vector<CommandSpec> helpCommands() {
    CommandSpec spec;
    spec.words = {"help"};
    spec.description = "List every command with its options, and every error and exit code.";
    spec.setup = [](CLI::App&) -> Handler { return helpAnswer; };
    return {spec};
}

}
