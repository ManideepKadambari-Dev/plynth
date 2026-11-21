#include "TestSupport.h"

#include <plynth/cli/Cli.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace plynth::cli::testing {

namespace {

// Reads the answer schema once.
nlohmann::json_schema::json_validator& answerValidator() {
    static nlohmann::json_schema::json_validator validator = [] {
        std::ifstream file(std::filesystem::path(PLYNTH_SCHEMA_DIR) / "cli-answer.schema.json");
        nlohmann::json_schema::json_validator made;
        made.set_root_schema(nlohmann::json::parse(file));
        return made;
    }();
    return validator;
}

}

Outcome runChecked(const std::vector<std::string>& args, const std::filesystem::path& currentFolder) {
    Outcome outcome;
    std::ostringstream out;
    outcome.exitCode = run(args, out, currentFolder);
    outcome.text = out.str();

    REQUIRE(!outcome.text.empty());
    REQUIRE(outcome.text.back() == '\n');
    const bool pretty = !args.empty() && std::find(args.begin(), args.end(), "--pretty") != args.end();
    if (!pretty) {
        REQUIRE(outcome.text.find('\n') == outcome.text.size() - 1);
    }
    outcome.answer = nlohmann::json::parse(outcome.text);
    REQUIRE_NOTHROW(answerValidator().validate(outcome.answer));

    const bool ok = outcome.answer.at("ok").get<bool>();
    if (ok) {
        REQUIRE(outcome.exitCode == kExitOk);
    } else {
        REQUIRE(outcome.exitCode != kExitOk);
    }
    return outcome;
}

TempFolder::TempFolder(const std::string& name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string tail = name + "-" + std::to_string(stamp);
    path_ = std::filesystem::temp_directory_path() /
            (std::u8string(u8"plynth-cli-ü-") + std::u8string(tail.begin(), tail.end()));
    std::filesystem::create_directories(path_);
}

TempFolder::~TempFolder() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

}
