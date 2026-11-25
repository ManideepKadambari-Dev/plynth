#include "../Registry.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>
#include <plynth/core/Update.h>
#include <plynth/core/Version.h>

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace plynth::cli {

namespace {

constexpr const char* kReleasesAddress = "https://github.com/ManideepKadambari-Dev/plynth/releases";

struct UpdateOptions {
    bool check = false;
    bool status = false;
};

// Writes one progress line: {"progress": {"stage", "done", "total", "message"}}. total is 0 while unknown.
void writeProgress(std::ostream* out, const std::string& stage, long long done, long long total,
                   const std::string& message) {
    if (out == nullptr) {
        return;
    }
    Json inner = Json::object();
    inner["stage"] = stage;
    inner["done"] = done;
    inner["total"] = total;
    inner["message"] = message;
    Json line = Json::object();
    line["progress"] = std::move(inner);
    *out << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    out->flush();
}

// The refusal for a copy that is not an installed release. It is made before anything is sent.
Answer notInstalled(const std::string& command) {
    return failure(command,
                   fail(ErrorCode::UpdateNotInstalled,
                        std::string("This copy of Plynth was not installed, so it cannot update itself. Build it "
                                    "again from the source, or install Plynth with PlynthSetup.exe from the "
                                    "Releases page. ") +
                            kReleasesAddress));
}

// The name used for the last update in update --status.
const char* lastName(LastUpdateResult last) {
    return last == LastUpdateResult::Succeeded ? "succeeded" : "not_finished";
}

// A path as text, or null when there is none.
Json optionalPath(const std::filesystem::path& path) {
    return path.empty() ? Json(nullptr) : Json(pathToUtf8(path));
}

// What update --status prints. It makes no request.
Answer updateShowStatus() {
    const UpdateStatus status = updateStatus();
    Json result = Json::object();
    result["install"] = status.installation.kind == InstallKind::Installed ? "installed" : "not_installed";
    result["version"] = std::string(version());
    result["check_at_start"] = status.checkAtStart;
    if (status.last == LastUpdateResult::None) {
        result["last"] = nullptr;
    } else {
        Json last = Json::object();
        last["result"] = lastName(status.last);
        last["from"] = status.from;
        last["to"] = status.to;
        last["log"] = optionalPath(status.log);
        result["last"] = std::move(last);
    }
    Json ids = Json::array();
    for (const std::string& id : status.keyIds) {
        ids.push_back(id);
    }
    result["key_ids"] = std::move(ids);
    return success("update", std::move(result));
}

// Adds the warnings of a check to an answer, so a bad signature that was skipped is not silent.
void addWarnings(Answer& answer, const UpdateCheck& check) {
    for (const UpdateWarning& warning : check.warnings) {
        answer.warnings.push_back({warning.code, warning.message});
    }
}

// The result of a check: a newer release, or nothing newer.
Json checkJson(const UpdateCheck& check) {
    Json result = Json::object();
    result["current"] = check.current;
    if (check.offer) {
        result["state"] = "available";
        result["latest"] = check.offer->version;
        result["early_preview"] = check.offer->earlyPreview;
        result["release_url"] = check.offer->releaseUrl;
    } else {
        result["state"] = "up_to_date";
        result["latest"] = check.current;
        result["early_preview"] = false;
        result["release_url"] = nullptr;
    }
    return result;
}

// The stop request of the program around the CLI, or none.
std::stop_token stopOf(const RunContext& context) {
    return context.host != nullptr ? context.host->stop : std::stop_token{};
}

// Asks for the newest signed release and says what it found. Nothing is downloaded.
Answer updateCheckOnly(const RunContext& context) {
    const std::string command = "update";
    if (currentInstallation().kind != InstallKind::Installed) {
        return notInstalled(command);
    }
    const UpdateCheck check = checkForUpdate(stopOf(context));
    if (!check.status.ok()) {
        return failure(command, check.status);
    }
    Answer answer = success(command, checkJson(check));
    addWarnings(answer, check);
    return answer;
}

// Checks, downloads, verifies and starts the installer. The command then ends so the installer can replace
// the files. Asking for the command is the consent, so nothing is asked.
Answer updateInstall(const RunContext& context) {
    const std::string command = "update";
    const Installation installation = currentInstallation();
    if (installation.kind != InstallKind::Installed) {
        return notInstalled(command);
    }
    const std::stop_token stop = stopOf(context);

    writeProgress(context.progress, "check", 0, 0, "Looking for a newer release");
    const UpdateCheck check = checkForUpdate(stop);
    if (!check.status.ok()) {
        return failure(command, check.status);
    }
    if (!check.offer) {
        Answer answer = success(command, checkJson(check));
        addWarnings(answer, check);
        return answer;
    }

    const std::vector<UpdateBlocker> blockers = updateBlockers(installation, context.currentFolder);
    if (!blockers.empty()) {
        std::string reasons;
        for (const UpdateBlocker& blocker : blockers) {
            if (!reasons.empty()) {
                reasons += ' ';
            }
            reasons += blocker.message;
        }
        return failure(command, fail(ErrorCode::UpdateBlocked, reasons));
    }

    const std::string target = check.offer->version;
    std::ostream* progress = context.progress;
    long long lastStep = -1;
    UpdateDownload download = downloadUpdate(
        *check.offer,
        [&](long long done, long long total) {
            const long long step = total > 0 ? done * 100 / total : done / (4LL << 20);
            if (step == lastStep) {
                return;
            }
            lastStep = step;
            writeProgress(progress, "download", done, total, "Downloading Plynth " + target);
        },
        stop);
    if (!download.status.ok()) {
        return failure(command, download.status);
    }
    if (!download.installer) {
        return failure(command, fail(ErrorCode::NetworkFailed, "The download gave no installer."));
    }

    writeProgress(progress, "start", 0, 0, "Starting the installer. This command ends now.");
    const Status launched = launchUpdate(std::move(*download.installer), context.currentFolder, false);
    if (!launched.ok()) {
        return failure(command, launched);
    }

    Json result = Json::object();
    result["state"] = "started";
    result["from"] = check.current;
    result["to"] = target;
    result["log"] = optionalPath(updateStatus().log);
    Answer answer = success(command, std::move(result));
    addWarnings(answer, check);
    return answer;
}

}

std::vector<CommandSpec> updateCommands() {
    CommandSpec spec;
    spec.words = {"update"};
    spec.description = "Update Plynth from the signed release, or with --check or --status only look.";
    spec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<UpdateOptions>();
        auto* check = sub.add_flag("--check", options->check, "Only look for a newer release. Nothing is downloaded.");
        auto* status = sub.add_flag("--status", options->status,
                                    "Show the install kind, the switch, the last update and the keys. Sends nothing.");
        check->excludes(status);
        status->excludes(check);
        return [options](const RunContext& context) {
            if (options->status) {
                return updateShowStatus();
            }
            return options->check ? updateCheckOnly(context) : updateInstall(context);
        };
    };
    return {spec};
}

}
