#pragma once

#include <plynth/core/ProjectFiles.h>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <process.h>

namespace plynth::test {

// A new empty folder below the scratch temp folder, removed again at the end of the test.
class TempFolder {
public:
    TempFolder()
    {
        static std::atomic<int> counter{0};
        const auto base = std::filesystem::temp_directory_path() / "guide";
        for (;;) {
            path_ = base / (std::to_string(_getpid()) + "-" + std::to_string(counter++));
            std::error_code ec;
            std::filesystem::create_directories(base, ec);
            if (std::filesystem::create_directory(path_, ec)) break;
        }
    }
    ~TempFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// Writes text to a file, making the folders above it.
inline void writeFile(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// Reads a whole file. Empty when it cannot be opened.
inline std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// A path from UTF-8 text.
inline std::filesystem::path utf8Path(std::string_view text)
{
    return plynth::pathFromUtf8(text);
}

// Checks JSON against a schema file of schema/. Empty when it passes, otherwise the complaint.
inline std::string schemaProblem(const std::string& schemaFile, const nlohmann::json& instance)
{
    const std::string text = readFile(std::filesystem::path(PLYNTH_SCHEMA_DIR) / schemaFile);
    if (text.empty()) return "could not read " + schemaFile;
    nlohmann::json_schema::json_validator validator;
    try {
        validator.set_root_schema(nlohmann::json::parse(text));
        validator.validate(instance);
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

}
