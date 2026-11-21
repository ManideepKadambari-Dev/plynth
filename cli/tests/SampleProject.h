#pragma once

#include "TestSupport.h"

#include <plynth/cli/Cli.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace plynth::cli::testing {

// The whole content of a file.
inline std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A path as UTF-8 text.
inline std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The code of a failed answer.
inline std::string errorCode(const Outcome& outcome) {
    return outcome.answer.at("error").at("code").get<std::string>();
}

// The strings in a JSON array.
inline std::vector<std::string> ids(const nlohmann::json& array) {
    return array.get<std::vector<std::string>>();
}

// A project with the sample image in a new temp folder, with helpers to run commands on it.
struct Sample {
    explicit Sample(const std::string& name) : temp(name) {
        folder = temp.path() / "flat";
        const auto made = runChecked({"project", "new", "--image", PLYNTH_SAMPLE_PLAN, "--project", utf8(folder)});
        REQUIRE(made.exitCode == kExitOk);
        project = utf8(folder);
    }

    // Runs a command on the project.
    Outcome run(std::vector<std::string> args) const {
        args.push_back("--project");
        args.push_back(project);
        return runChecked(args);
    }

    // Runs wall draw with the given end options.
    Outcome draw(const std::vector<std::string>& ends) const {
        std::vector<std::string> args = {"wall", "draw"};
        args.insert(args.end(), ends.begin(), ends.end());
        return run(args);
    }

    // Draws the 4 m by 4 m square p1 p2 p3 p4 from (1, 1) with walls w1 to w4.
    void drawSquare() const {
        REQUIRE(draw({"--from-x", "1", "--from-y", "1", "--to-x", "5", "--to-y", "1"}).exitCode == kExitOk);
        REQUIRE(draw({"--from", "p2", "--to-x", "5", "--to-y", "5"}).exitCode == kExitOk);
        REQUIRE(draw({"--from", "p3", "--to-x", "1", "--to-y", "5"}).exitCode == kExitOk);
        REQUIRE(draw({"--from", "p4", "--to", "p1"}).exitCode == kExitOk);
    }

    // Sets the scale to 100 pixels per metre and draws the square p1 (1, 3), p2 (5, 3), p3 (5, 7), p4 (1, 7)
    // with walls w1 to w4, plus a living room in it of area 14.44.
    void drawRoomSquare() const {
        REQUIRE(run({"scale", "set", "--pixels-per-metre", "100"}).exitCode == kExitOk);
        REQUIRE(draw({"--from-x", "1", "--from-y", "3", "--to-x", "5", "--to-y", "3"}).exitCode == kExitOk);
        REQUIRE(draw({"--from", "p2", "--to-x", "5", "--to-y", "7"}).exitCode == kExitOk);
        REQUIRE(draw({"--from", "p3", "--to-x", "1", "--to-y", "7"}).exitCode == kExitOk);
        REQUIRE(draw({"--from", "p4", "--to", "p1"}).exitCode == kExitOk);
        REQUIRE(run({"room", "add", "--x", "3", "--y", "5", "--type", "living"}).exitCode == kExitOk);
    }

    TempFolder temp;
    std::filesystem::path folder;
    std::string project;
};

}
