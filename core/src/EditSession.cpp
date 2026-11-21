#include "plynth/core/EditSession.h"

#include "FileIo.h"

#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxToolCharacters = 64;

// Reads a whole file into text.
Status readText(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(ErrorCode::CannotRead, pathToUtf8(path.filename()) + " could not be opened.");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return fail(ErrorCode::CannotRead, pathToUtf8(path.filename()) + " could not be read.");
    out = buffer.str();
    return {};
}

// The text of a JSON file: 2 space indent and a final line break.
std::string dumpJson(const Json& json)
{
    return json.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

// Deletes a file. Ok when it is not there.
Status removeFile(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) return fail(ErrorCode::CannotWrite, pathToUtf8(path.filename()) + " could not be removed.");
    return {};
}

// Says whether a file or folder exists at a path.
bool pathExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

// Reads a whole number that fits in a long long.
bool readInteger(const Json& value, long long& out)
{
    if (value.is_number_unsigned()) {
        const auto v = value.get<unsigned long long>();
        if (v > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) return false;
        out = static_cast<long long>(v);
        return true;
    }
    if (value.is_number_integer()) {
        out = value.get<long long>();
        return true;
    }
    return false;
}

// Counts the characters of a UTF-8 text, not its bytes.
std::size_t characterCount(std::string_view text)
{
    std::size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
    }
    return count;
}

// Checks that the JSON is an object with exactly these keys, then that format and version are right.
Status checkHeader(const Json& json, std::initializer_list<const char*> keys, std::string_view file,
                   std::string_view format, int version)
{
    const std::string name(file);
    if (!json.is_object()) return fail(ErrorCode::BadFile, name + " is not a JSON object.");
    for (const char* key : keys) {
        if (!json.contains(key)) return fail(ErrorCode::BadFile, name + " has no \"" + key + "\".", std::string("/") + key);
    }
    for (const auto& item : json.items()) {
        const bool known = std::any_of(keys.begin(), keys.end(), [&](const char* k) { return item.key() == k; });
        if (!known) return fail(ErrorCode::BadFile, name + " has an unknown value \"" + item.key() + "\".", "/" + item.key());
    }
    if (!json["format"].is_string() || json["format"].get<std::string>() != format) {
        return fail(ErrorCode::BadFile, name + " is not a " + std::string(format) + " file.", "/format");
    }
    long long fileVersion = 0;
    if (!readInteger(json["version"], fileVersion)) return fail(ErrorCode::BadFile, name + " has a bad version.", "/version");
    if (fileVersion > version) return fail(ErrorCode::UnsupportedVersion, name + " was written by a newer Plynth.", "/version");
    if (fileVersion != version) return fail(ErrorCode::BadFile, name + " has a bad version.", "/version");
    return {};
}

// Reads a time value of a file.
Status readTimeValue(const Json& json, const char* key, std::string_view file, SessionTime& out)
{
    const std::string where = std::string("/") + key;
    if (!json[key].is_string()) return fail(ErrorCode::BadFile, std::string(file) + " has a bad \"" + key + "\".", where);
    const auto time = parseSessionTime(json[key].get<std::string>());
    if (!time) return fail(ErrorCode::BadFile, std::string(file) + " has a bad \"" + key + "\".", where);
    out = *time;
    return {};
}

// Turns the JSON of session.lock into a lock, or says what is wrong.
Status lockFromJson(const Json& json, SessionLock& out)
{
    const std::string file(kSessionFileName);
    if (Status s = checkHeader(json, {"format", "version", "pid", "tool", "started", "last_activity"}, file,
                               kSessionFormat, kSessionFormatVersion); !s.ok()) {
        return s;
    }
    SessionLock lock;
    if (!readInteger(json["pid"], lock.pid) || lock.pid < 0) return fail(ErrorCode::BadFile, file + " has a bad \"pid\".", "/pid");
    if (!json["tool"].is_string()) return fail(ErrorCode::BadFile, file + " has a bad \"tool\".", "/tool");
    lock.tool = json["tool"].get<std::string>();
    const std::size_t chars = characterCount(lock.tool);
    if (chars < 1 || chars > kMaxToolCharacters) return fail(ErrorCode::BadFile, file + " has a bad \"tool\".", "/tool");
    if (Status s = readTimeValue(json, "started", file, lock.started); !s.ok()) return s;
    if (Status s = readTimeValue(json, "last_activity", file, lock.lastActivity); !s.ok()) return s;
    out = std::move(lock);
    return {};
}

// Turns the JSON of app-state.json into a state, or says what is wrong.
Status appStateFromJson(const Json& json, AppState& out)
{
    const std::string file(kAppStateFileName);
    if (Status s = checkHeader(json, {"format", "version", "pid", "process_start", "modified", "updated"}, file,
                               kAppStateFormat, kAppStateFormatVersion); !s.ok()) {
        return s;
    }
    AppState state;
    if (!readInteger(json["pid"], state.process.pid) || state.process.pid < 1) {
        return fail(ErrorCode::BadFile, file + " has a bad \"pid\".", "/pid");
    }
    if (!readInteger(json["process_start"], state.process.startKey) || state.process.startKey < 0) {
        return fail(ErrorCode::BadFile, file + " has a bad \"process_start\".", "/process_start");
    }
    if (!json["modified"].is_boolean()) return fail(ErrorCode::BadFile, file + " has a bad \"modified\".", "/modified");
    state.modified = json["modified"].get<bool>();
    if (Status s = readTimeValue(json, "updated", file, state.updated); !s.ok()) return s;
    out = state;
    return {};
}

// Reads and parses a JSON file in the project folder. CannotRead or BadFile on failure.
Status readJsonFile(const fs::path& path, Json& out)
{
    std::string text;
    if (Status s = readText(path, text); !s.ok()) return s;
    out = Json::parse(text, nullptr, false);
    if (out.is_discarded()) return fail(ErrorCode::BadFile, pathToUtf8(path.filename()) + " is not valid JSON.");
    return {};
}

// A time moved forward by some seconds.
SessionTime plusSeconds(SessionTime time, int seconds)
{
    return time + std::chrono::seconds(seconds);
}

}

// The current time rounded down to whole seconds.
SessionTime sessionNow()
{
    return std::chrono::floor<std::chrono::seconds>(SessionClock::now());
}

// Writes a time as "2026-11-21T10:00:00Z".
std::string formatSessionTime(SessionTime time)
{
    const auto days = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::year_month_day date{days};
    const long long rest = (time - days).count();
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), rest / 3600,
                  (rest / 60) % 60, rest % 60);
    return buffer;
}

// Reads a time written by formatSessionTime.
std::optional<SessionTime> parseSessionTime(std::string_view text)
{
    if (text.size() != 20) return std::nullopt;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char expected = i == 4 || i == 7 ? '-' : i == 10 ? 'T' : i == 13 || i == 16 ? ':' : i == 19 ? 'Z' : '0';
        if (expected == '0' ? (c < '0' || c > '9') : c != expected) return std::nullopt;
    }
    const auto number = [&](std::size_t start, std::size_t length) {
        int value = 0;
        for (std::size_t i = start; i < start + length; ++i) value = value * 10 + (text[i] - '0');
        return value;
    };
    const int hour = number(11, 2);
    const int minute = number(14, 2);
    const int second = number(17, 2);
    if (hour > 23 || minute > 59 || second > 59) return std::nullopt;
    const std::chrono::year_month_day date{std::chrono::year{number(0, 4)},
                                           std::chrono::month{static_cast<unsigned>(number(5, 2))},
                                           std::chrono::day{static_cast<unsigned>(number(8, 2))}};
    if (!date.ok()) return std::nullopt;
    const auto days = std::chrono::sys_days{date}.time_since_epoch();
    return SessionTime{std::chrono::duration_cast<std::chrono::seconds>(days) + std::chrono::hours(hour) +
                       std::chrono::minutes(minute) + std::chrono::seconds(second)};
}

// The stable lower case name of a session state.
std::string_view sessionStateName(SessionState state)
{
    switch (state) {
    case SessionState::None: return "none";
    case SessionState::Active: return "active";
    case SessionState::Expired: return "expired";
    }
    return "none";
}

// Reads folder/session.lock and decides its state at time now.
SessionRead readSession(const fs::path& folder, SessionTime now)
{
    SessionRead result;
    const fs::path path = folder / pathFromUtf8(kSessionFileName);
    if (!pathExists(path)) return result;

    result.state = SessionState::Expired;
    Json json;
    if (Status s = readJsonFile(path, json); !s.ok()) {
        result.status = s;
        return result;
    }
    if (Status s = lockFromJson(json, result.lock); !s.ok()) {
        result.status = s;
        return result;
    }
    result.expiresAt = plusSeconds(result.lock.lastActivity, kSessionExpirySeconds);
    const bool tooOld = result.expiresAt < now;
    const bool inFuture = plusSeconds(now, kSessionExpirySeconds) < result.lock.lastActivity;
    result.state = tooOld || inFuture ? SessionState::Expired : SessionState::Active;
    return result;
}

// Writes folder/session.lock through a temporary file and a rename.
Status writeSession(const fs::path& folder, const SessionLock& lock)
{
    Json json = Json::object();
    json["format"] = std::string(kSessionFormat);
    json["version"] = kSessionFormatVersion;
    json["pid"] = lock.pid;
    json["tool"] = lock.tool;
    json["started"] = formatSessionTime(lock.started);
    json["last_activity"] = formatSessionTime(lock.lastActivity);
    return fileio::writeAtomic(folder / pathFromUtf8(kSessionFileName), dumpJson(json));
}

// Deletes folder/session.lock.
Status removeSession(const fs::path& folder)
{
    return removeFile(folder / pathFromUtf8(kSessionFileName));
}

// Reads folder/app-state.json.
AppStateRead readAppState(const fs::path& folder)
{
    AppStateRead result;
    const fs::path path = folder / pathFromUtf8(kAppStateFileName);
    if (!pathExists(path)) return result;

    Json json;
    if (Status s = readJsonFile(path, json); !s.ok()) {
        result.status = s;
        return result;
    }
    if (Status s = appStateFromJson(json, result.state); !s.ok()) {
        result.status = s;
        return result;
    }
    result.open = isProcessRunning(result.state.process);
    return result;
}

// Writes folder/app-state.json through a temporary file and a rename.
Status writeAppState(const fs::path& folder, const AppState& state)
{
    Json json = Json::object();
    json["format"] = std::string(kAppStateFormat);
    json["version"] = kAppStateFormatVersion;
    json["pid"] = state.process.pid;
    json["process_start"] = state.process.startKey;
    json["modified"] = state.modified;
    json["updated"] = formatSessionTime(state.updated);
    return fileio::writeAtomic(folder / pathFromUtf8(kAppStateFileName), dumpJson(json));
}

// Deletes folder/app-state.json when it belongs to the given process.
Status removeAppState(const fs::path& folder, const ProcessIdentity& owner)
{
    const fs::path path = folder / pathFromUtf8(kAppStateFileName);
    if (!pathExists(path)) return {};
    Json json;
    AppState state;
    if (!readJsonFile(path, json).ok() || !appStateFromJson(json, state).ok()) return {};
    if (!(state.process == owner)) return {};
    return removeFile(path);
}

// Starts or continues a session for "plynth session begin".
Status beginSession(const fs::path& folder, std::string_view tool, long long pid, SessionTime now, SessionLock* out,
                    bool* alreadyActive)
{
    if (!isProjectFolder(folder)) {
        return fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(folder) + " is not a Plynth project.");
    }
    std::string name(tool.empty() ? kDefaultSessionTool : tool);
    const std::size_t chars = characterCount(name);
    if (chars > kMaxToolCharacters) {
        return fail(ErrorCode::InvalidValue, "The tool name must be 64 characters or fewer.");
    }
    if (std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
        return fail(ErrorCode::InvalidValue, "The tool name must not contain control characters.");
    }

    const AppStateRead app = readAppState(folder);
    if (app.open && app.state.modified) {
        return fail(ErrorCode::UnsavedChanges,
                    "The project has unsaved changes in the Plynth app. Save or discard them in the app first.");
    }

    const SessionRead current = readSession(folder, now);
    SessionLock lock;
    const bool active = current.state == SessionState::Active;
    if (active) {
        lock = current.lock;
        lock.lastActivity = now;
    } else {
        lock.pid = pid;
        lock.tool = std::move(name);
        lock.started = now;
        lock.lastActivity = now;
    }
    if (Status s = writeSession(folder, lock); !s.ok()) return s;
    if (out) *out = lock;
    if (alreadyActive) *alreadyActive = active;
    return {};
}

// Ends the session for "plynth session end".
Status endSession(const fs::path& folder, SessionTime now, bool* wasActive)
{
    const SessionRead current = readSession(folder, now);
    if (wasActive) *wasActive = current.state == SessionState::Active;
    return removeSession(folder);
}

// Called by every CLI command that opens a project, before it changes anything.
Status allowCliCommand(const fs::path& folder, SessionTime now, bool readOnly)
{
    const SessionRead current = readSession(folder, now);
    if (current.state == SessionState::Active) {
        SessionLock lock = current.lock;
        lock.lastActivity = now;
        return writeSession(folder, lock);
    }
    if (readOnly) return {};
    if (!readAppState(folder).open) return {};
    if (current.state == SessionState::Expired) {
        return fail(ErrorCode::Locked,
                    "The edit session ended after 5 minutes without commands. Run 'plynth session begin' again.");
    }
    return fail(ErrorCode::Locked,
                "The project is open in the Plynth app. Run 'plynth session begin' first so the app does not "
                "overwrite your changes.");
}

}
