#include "UpdateInternals.h"

#include "FileIo.h"

#include "plynth/core/ProjectFiles.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace plynth::update_detail {

namespace fs = std::filesystem;

namespace {

using Json = nlohmann::ordered_json;

constexpr std::string_view kStateFormat = "plynth-update-state";
constexpr int kStateVersion = 1;

// A refusal of the state file, pointing at the bad value.
Status badState(const std::string& message, const std::string& where)
{
    return fail(ErrorCode::BadFile, "The update state file is not valid: " + message, where);
}

// Reads a text field that must be there and not be empty.
bool readText(const Json& object, const char* key, const std::string& at, std::string& out, Status& status)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || it->get<std::string>().empty()) {
        status = badState(std::string("\"") + key + "\" must be text.", at + "/" + key);
        return false;
    }
    out = it->get<std::string>();
    return true;
}

}

// Reads update-state.json.
Status readUpdateState(const fs::path& file, UpdateStateFile& out)
{
    out = {};
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        if (ec) return fail(ErrorCode::CannotRead, "The update state file " + pathToUtf8(file) + " could not be checked.");
        return {};
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(ErrorCode::CannotRead, "The update state file " + pathToUtf8(file) + " could not be opened.");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const Json json = Json::parse(buffer.str(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) return badState("it is not a JSON object.", "");
    for (const auto& [key, value] : json.items()) {
        if (key != "format" && key != "version" && key != "highest_signed" && key != "last") {
            return badState("unknown field \"" + key + "\".", "/" + key);
        }
    }
    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() || format->get<std::string>() != kStateFormat) {
        return badState("wrong format.", "/format");
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer()) return badState("no version.", "/version");
    if (version->get<long long>() > kStateVersion) {
        return fail(ErrorCode::UnsupportedVersion, "The update state file was written by a newer Plynth.", "/version");
    }
    if (version->get<long long>() != kStateVersion) return badState("bad version.", "/version");

    UpdateStateFile state;
    if (const auto it = json.find("highest_signed"); it != json.end() && !it->is_null()) {
        const auto parsed = it->is_string() ? parseVersion(it->get<std::string>()) : std::nullopt;
        if (!parsed) return badState("\"highest_signed\" must be a version like 0.2.0.", "/highest_signed");
        state.highestSigned = parsed;
    }
    if (const auto it = json.find("last"); it != json.end() && !it->is_null()) {
        if (!it->is_object()) return badState("\"last\" must be an object.", "/last");
        for (const auto& [key, value] : it->items()) {
            if (key != "from" && key != "to" && key != "started" && key != "log") {
                return badState("unknown field \"" + key + "\".", "/last/" + key);
            }
        }
        UpdateLast last;
        Status status;
        std::string log;
        if (!readText(*it, "from", "/last", last.from, status) || !readText(*it, "to", "/last", last.to, status) ||
            !readText(*it, "started", "/last", last.started, status) || !readText(*it, "log", "/last", log, status)) {
            return status;
        }
        last.log = pathFromUtf8(log);
        state.last = std::move(last);
    }
    out = std::move(state);
    return {};
}

// Writes update-state.json.
Status writeUpdateState(const fs::path& file, const UpdateStateFile& state)
{
    Json json = Json::object();
    json["format"] = std::string(kStateFormat);
    json["version"] = kStateVersion;
    json["highest_signed"] = state.highestSigned ? Json(versionText(*state.highestSigned)) : Json(nullptr);
    if (state.last) {
        Json last = Json::object();
        last["from"] = state.last->from;
        last["to"] = state.last->to;
        last["started"] = state.last->started;
        last["log"] = pathToUtf8(state.last->log);
        json["last"] = std::move(last);
    } else {
        json["last"] = nullptr;
    }
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    if (ec) return fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(file.parent_path()) + " could not be created.");
    return fileio::writeAtomic(file, json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
}

// Raises highest_signed when the version is higher.
Status rememberSigned(const fs::path& file, const Version& version)
{
    UpdateStateFile state;
    if (Status s = readUpdateState(file, state); !s.ok()) return s;
    if (state.highestSigned && *state.highestSigned >= version) return {};
    state.highestSigned = version;
    return writeUpdateState(file, state);
}

}

namespace plynth {

// The stable name of a last update result.
std::string_view lastUpdateResultName(LastUpdateResult result)
{
    switch (result) {
    case LastUpdateResult::None: return "none";
    case LastUpdateResult::Succeeded: return "succeeded";
    case LastUpdateResult::NotFinished: return "not_finished";
    }
    return "none";
}

// Reads the state of updating without any request.
UpdateStatus updateStatus()
{
    using namespace update_detail;
    const UpdateEnvironment& env = environment();
    UpdateStatus status;
    status.installation = env.installation();
    status.checkAtStart = shouldCheckAtStart(status.installation, loadSettings());
    for (const PublicKey& key : env.keys) status.keyIds.push_back(keyIdText(key.keyId));
    UpdateStateFile state;
    if (status.installation.kind == InstallKind::Installed && readUpdateState(stateFile(), state).ok() && state.last) {
        status.from = state.last->from;
        status.to = state.last->to;
        status.log = state.last->log;
        status.last = state.last->to == status.installation.version ? LastUpdateResult::Succeeded
                                                                      : LastUpdateResult::NotFinished;
    }
    return status;
}

}
