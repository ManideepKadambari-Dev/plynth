#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace plynth::cli::testing {

// A path as UTF-8 text.
inline std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// What one run of the CLI produced.
struct Outcome {
    int exitCode = -1;
    std::string text;      // everything written to the output stream
    std::string progress;  // everything written to the progress stream
    nlohmann::json answer; // the parsed answer
};

// Runs the CLI in a folder, checks that the output is one line of JSON that matches
// schema/cli-answer.schema.json, and returns it. Fails the current test when it is not.
Outcome runChecked(const std::vector<std::string>& args,
                   const std::filesystem::path& currentFolder = std::filesystem::current_path());

// A new empty folder under the temp folder, with a non ASCII name, removed when this object goes away.
class TempFolder {
public:
    explicit TempFolder(const std::string& name);
    ~TempFolder();
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}
