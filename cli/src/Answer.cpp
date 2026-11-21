#include "Answer.h"

#include <utility>

namespace plynth::cli {

Answer success(std::string command, Json result) {
    Answer answer;
    answer.command = std::move(command);
    answer.result = std::move(result);
    return answer;
}

Answer failure(std::string command, const Status& status) {
    Answer answer;
    answer.ok = false;
    answer.command = std::move(command);
    answer.code = std::string(errorCodeName(status.code));
    answer.message = status.message;
    answer.where = status.where;
    answer.exitCode = 1;
    return answer;
}

Answer badArguments(std::string command, std::string code, std::string message) {
    Answer answer;
    answer.ok = false;
    answer.command = std::move(command);
    answer.code = std::move(code);
    answer.message = std::move(message);
    answer.exitCode = 2;
    return answer;
}

Json toJson(const Answer& answer) {
    Json json = Json::object();
    json["ok"] = answer.ok;
    json["command"] = answer.command;
    if (answer.ok) {
        json["result"] = answer.result;
        return json;
    }
    Json error = Json::object();
    error["code"] = answer.code;
    error["message"] = answer.message;
    if (!answer.where.empty()) {
        error["where"] = answer.where;
    }
    json["error"] = std::move(error);
    return json;
}

}
