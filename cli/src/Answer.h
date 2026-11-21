#pragma once

#include <plynth/core/Status.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace plynth::cli {

using Json = nlohmann::ordered_json;

// Something a command noticed without refusing.
struct AnswerWarning {
    std::string code;
    std::string message;
};

// One command's outcome, before it is written out as JSON.
struct Answer {
    bool ok = true;
    std::string command;
    Json result = Json::object();
    std::string code;
    std::string message;
    std::string where;
    int exitCode = 0;
    std::vector<AnswerWarning> warnings; // only written for a successful answer
};

// Makes a successful answer.
Answer success(std::string command, Json result);

// Makes a failed answer from a core status. Exit code 1.
Answer failure(std::string command, const Status& status);

// Makes a failed answer with a CLI code such as "bad_arguments". Exit code 2.
Answer badArguments(std::string command, std::string code, std::string message);

// Turns an answer into the JSON object that is printed.
Json toJson(const Answer& answer);

}
