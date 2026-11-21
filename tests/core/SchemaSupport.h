#pragma once

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace plynth::test {

// Reads a whole file as text. Empty when it cannot be opened.
inline std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// Checks JSON against one of the published schema files in schema/. Returns an empty string when it
// passes, otherwise the validator's complaint.
inline std::string schemaProblem(const std::string& schemaFile, const nlohmann::json& instance)
{
    const std::filesystem::path path = std::filesystem::path(PLYNTH_SCHEMA_DIR) / schemaFile;
    const std::string text = readTextFile(path);
    if (text.empty()) return "could not read " + path.string();
    nlohmann::json_schema::json_validator validator;
    try {
        validator.set_root_schema(nlohmann::json::parse(text));
        validator.validate(instance);
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

// Same as schemaProblem for text, which must hold JSON.
inline std::string schemaProblemForText(const std::string& schemaFile, const std::string& jsonText)
{
    const auto parsed = nlohmann::json::parse(jsonText, nullptr, false);
    if (parsed.is_discarded()) return "not JSON";
    return schemaProblem(schemaFile, parsed);
}

}
