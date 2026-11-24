#pragma once

#include "plynth/core/Build.h"
#include "plynth/core/Plan.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace plynth {

// What the last successful build was made from, read from build/built-plan.json.
struct BuiltPlan {
    Plan plan;
    std::string fingerprint;
};

// Reads build/built-plan.json. Empty when the file is missing or unusable.
inline std::optional<BuiltPlan> readBuiltPlan(const std::filesystem::path& projectFolder)
{
    std::ifstream in(buildFolder(projectFolder) / pathFromUtf8(kBuiltPlanFileName), std::ios::binary);
    if (!in) return std::nullopt;
    const nlohmann::ordered_json json = nlohmann::ordered_json::parse(in, nullptr, false);
    if (!json.is_object() || !json.contains("plan") || !json.contains("plan_fingerprint")) return std::nullopt;
    if (!json["plan_fingerprint"].is_string()) return std::nullopt;
    PlanRead read = planFromJson(json["plan"]);
    if (!read.status.ok()) return std::nullopt;
    return BuiltPlan{std::move(read.plan), json["plan_fingerprint"].get<std::string>()};
}

}
