#pragma once

#include "Registry.h"

#include <plynth/core/EngineLink.h>
#include <plynth/core/Library.h>

#include <optional>
#include <ostream>
#include <stop_token>
#include <string>

namespace plynth::cli {

// Finds the library an engine link's kit takes its material slot names from. An explicit --library must open its
// materials catalog, or the command is refused. Without it the configured folder is passed on as it is. Gives
// false and fills failed only for a refusal. Shared by export and the link commands.
bool linkLibrary(const std::string& command, const RunContext& context, const std::string& libraryOption,
                 std::optional<LibraryPaths>& library, Answer& failed);

// Runs the sync after a good build (syncEngineLinks) and adds what it did to the build's answer: "links" with one
// link report for each link that was run, and a warning for each one that was skipped or failed. Adds nothing when
// no link has a target. Progress lines go to progress when it is not null.
void addLinkSync(Answer& answer, const EngineLinkRequest& base, std::ostream* progress, std::stop_token stop);

}
