#pragma once

#include <plynth/core/Status.h>

#include <filesystem>

namespace plynth::cli {

inline constexpr const char* kAgentGuideFileName = "AGENTS.md";

enum class AgentGuideResult { Written, Kept };

struct AgentGuideWrite {
    Status status;
    AgentGuideResult result = AgentGuideResult::Kept;
};

// Copies the master instructions file into a project folder. An existing file is kept unless overwrite is true.
AgentGuideWrite writeAgentGuide(const std::filesystem::path& projectFolder, bool overwrite);

}
