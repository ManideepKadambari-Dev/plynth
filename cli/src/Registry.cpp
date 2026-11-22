#include "Registry.h"

#include <utility>

namespace plynth::cli {

const std::vector<CommandSpec>& allCommands() {
    static const std::vector<CommandSpec> commands = [] {
        std::vector<CommandSpec> all;
        for (auto* group : {&versionCommands, &helpCommands, &projectCommands, &scaleCommands, &pointCommands,
                             &wallCommands, &openingCommands, &floorCommands,
                             &roomCommands, &detectCommands, &sessionCommands, &settingsCommands,
                             &libraryCommands, &assetCommands, &doctorCommands, &importCommands, &itemCommands, &buildCommands}) {
            for (auto& spec : group()) {
                all.push_back(std::move(spec));
            }
        }
        return all;
    }();
    return commands;
}

std::string joinWords(const std::vector<std::string>& words) {
    std::string joined;
    for (const auto& word : words) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += word;
    }
    return joined;
}

}
