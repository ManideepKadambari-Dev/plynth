#include "ErrorList.h"

#include <plynth/cli/Cli.h>
#include <plynth/core/Status.h>

#include <string>

namespace plynth::cli {

namespace {

// One entry of a list.
Json entry(const std::string& code, const std::string& description, const std::string& from) {
    Json json = Json::object();
    json["code"] = code;
    json["description"] = description;
    json["from"] = from;
    return json;
}

}

Json errorCodesJson() {
    Json list = Json::array();
    for (const ErrorCode code : allErrorCodes()) {
        list.push_back(entry(std::string(errorCodeName(code)), std::string(errorCodeDescription(code)), "core"));
    }
    list.push_back(entry("unknown_command", "The first words name no command. Run plynth help to list them.", "cli"));
    list.push_back(entry("bad_arguments", "An option is unknown, missing its value or has a value it does not accept.", "cli"));
    list.push_back(entry("internal_error", "Something went wrong inside Plynth. The message says what.", "cli"));
    return list;
}

Json exitCodesJson() {
    Json list = Json::array();
    const auto add = [&](int code, const std::string& description) {
        Json json = Json::object();
        json["code"] = code;
        json["description"] = description;
        list.push_back(std::move(json));
    };
    add(kExitOk, "The command worked. The answer has ok true.");
    add(kExitFailed, "The command ran and refused. The answer has the error code and the reason.");
    add(kExitBadArguments, "Unknown command or bad options. The answer has unknown_command or bad_arguments.");
    return list;
}

}
