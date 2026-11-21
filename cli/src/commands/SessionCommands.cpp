#include "../Common.h"

#include <plynth/core/EditSession.h>

#include <chrono>
#include <memory>

namespace plynth::cli {

namespace {

struct BeginOptions {
    std::string project;
    std::string tool{kDefaultSessionTool};
};

// Adds what the app is doing with the project to a session answer.
void addAppFields(Json& result, const std::filesystem::path& folder) {
    const AppStateRead app = readAppState(folder);
    result["app_open"] = app.open;
    result["app_modified"] = app.open && app.state.modified;
}

// Adds the lock fields, or nulls when there is no readable lock.
void addLockFields(Json& result, const SessionRead& read) {
    const bool known = read.state != SessionState::None && read.status.ok();
    if (known) {
        result["tool"] = read.lock.tool;
        result["pid"] = read.lock.pid;
        result["started"] = formatSessionTime(read.lock.started);
        result["last_activity"] = formatSessionTime(read.lock.lastActivity);
        result["expires_at"] = formatSessionTime(read.expiresAt);
    } else {
        result["tool"] = nullptr;
        result["pid"] = nullptr;
        result["started"] = nullptr;
        result["last_activity"] = nullptr;
        result["expires_at"] = nullptr;
    }
}

// Starts or continues an edit session.
Answer sessionBegin(const RunContext& context, const BeginOptions& options) {
    const std::string command = "session begin";
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);

    SessionLock lock;
    bool already = false;
    const Status status = beginSession(folder, options.tool, currentProcess().pid, sessionNow(), &lock, &already);
    if (!status.ok()) {
        return failure(command, status);
    }

    Json result = Json::object();
    result["state"] = "active";
    result["tool"] = lock.tool;
    result["pid"] = lock.pid;
    result["started"] = formatSessionTime(lock.started);
    result["last_activity"] = formatSessionTime(lock.lastActivity);
    result["expires_at"] = formatSessionTime(lock.lastActivity + std::chrono::seconds(kSessionExpirySeconds));
    result["already_active"] = already;
    addAppFields(result, folder);
    return success(command, std::move(result));
}

// Ends the edit session.
Answer sessionEnd(const RunContext& context, const std::string& projectOption) {
    const std::string command = "session end";
    const std::filesystem::path folder = resolveProjectFolder(context, projectOption);
    bool wasActive = false;
    const Status status = endSession(folder, sessionNow(), &wasActive);
    if (!status.ok()) {
        return failure(command, status);
    }
    Json result = Json::object();
    result["ended"] = wasActive;
    return success(command, std::move(result));
}

// Reports the session without changing it.
Answer sessionStatus(const RunContext& context, const std::string& projectOption) {
    const std::string command = "session status";
    const std::filesystem::path folder = resolveProjectFolder(context, projectOption);
    const SessionRead read = readSession(folder, sessionNow());
    Json result = Json::object();
    result["state"] = std::string(sessionStateName(read.state));
    addLockFields(result, read);
    addAppFields(result, folder);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> sessionCommands() {
    CommandSpec begin;
    begin.words = {"session", "begin"};
    begin.description = "Start an edit session so the app stays read only while commands change the project.";
    begin.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<BeginOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--tool", options->tool, "Name shown in the app while the session is active. Default: plynth.")
            ->type_name("name");
        return [options](const RunContext& context) { return sessionBegin(context, *options); };
    };

    CommandSpec end;
    end.words = {"session", "end"};
    end.description = "End the edit session.";
    end.setup = [](CLI::App& sub) -> Handler {
        auto project = std::make_shared<std::string>();
        addProjectOption(sub, *project);
        return [project](const RunContext& context) { return sessionEnd(context, *project); };
    };

    CommandSpec status;
    status.words = {"session", "status"};
    status.description = "Show whether an edit session is active and whether the app has the project open.";
    status.setup = [](CLI::App& sub) -> Handler {
        auto project = std::make_shared<std::string>();
        addProjectOption(sub, *project);
        return [project](const RunContext& context) { return sessionStatus(context, *project); };
    };

    return {begin, end, status};
}

}
