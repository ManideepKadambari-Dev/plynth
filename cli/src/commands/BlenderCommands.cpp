#include "../BlenderGet.h"
#include "../Common.h"

#include <plynth/core/Blender.h>
#include <plynth/core/BlenderDownload.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Settings.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace plynth::cli {

namespace {

constexpr const char* kStopped = "Getting Blender was stopped. Nothing was kept.";

struct BlenderGetOptions {
    bool yes = false;
    bool ifMissing = false;
    bool quiet = false;
};

// The source the command uses now.
BlenderGetSource& currentSource() {
    static BlenderGetSource source;
    return source;
}

// Writes one progress line: {"progress": {"stage", "done", "total", "message"}}. total is 0 while unknown.
void writeProgress(std::ostream* out, std::string_view stage, long long done, long long total,
                   const std::string& message) {
    if (out == nullptr) {
        return;
    }
    Json inner = Json::object();
    inner["stage"] = std::string(stage);
    inner["done"] = done;
    inner["total"] = total;
    inner["message"] = message;
    Json line = Json::object();
    line["progress"] = std::move(inner);
    *out << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    out->flush();
}

// Reads blender.org's offer, through the test source when one is set.
BlenderOfferCheck readOffer(std::stop_token stop) {
    const BlenderGetSource& source = currentSource();
    return source.findOffer ? source.findOffer(stop) : findBlenderOffer(stop);
}

// Downloads an offer, through the test source when one is set.
BlenderDownload fetchOffer(const BlenderOffer& offer,
                           const std::function<void(const BlenderDownloadProgress&)>& onProgress,
                           std::stop_token stop) {
    const BlenderGetSource& source = currentSource();
    return source.download ? source.download(offer, onProgress, stop) : downloadBlender(offer, onProgress, stop);
}

// The folder an offer is unpacked into: the zip's name without .zip, in the download folder.
std::filesystem::path targetFolder(const BlenderOffer& offer) {
    std::string name = offer.zipName;
    if (name.size() > 4 && name.ends_with(".zip")) {
        name.resize(name.size() - 4);
    }
    return downloadedBlenderRoot() / pathFromUtf8(name);
}

// The offer's fields as the answer gives them. size is null when blender.org did not list it.
void addOffer(Json& result, const BlenderOffer& offer) {
    result["version"] = formatBlenderVersion(offer.version);
    result["size"] = offer.size > 0 ? Json(offer.size) : Json(nullptr);
    result["source"] = offer.zipUrl;
    result["sha256"] = offer.sha256;
    result["target"] = pathToUtf8(targetFolder(offer));
}

// True when PLYNTH_BLENDER holds a value, which commands use before the Blender in the settings.
bool blenderVariableSet() {
    wchar_t first[2];
    return GetEnvironmentVariableW(L"PLYNTH_BLENDER", first, 2) > 0;
}

// The answer when a usable Blender is already there and --if-missing was given.
Answer skipped(const std::string& command, const BlenderInstall& install) {
    Json found = Json::object();
    found["path"] = pathToUtf8(install.executable);
    found["version"] = install.version ? Json(formatBlenderVersion(*install.version)) : Json(nullptr);
    Json result = Json::object();
    result["state"] = "skipped";
    result["reason"] = "A usable Blender is already on this computer, so nothing was downloaded.";
    result["blender"] = std::move(found);
    return success(command, std::move(result));
}

// The progress sentence for a stage of the download.
std::string stageMessage(BlenderDownloadStage stage, const std::string& name) {
    switch (stage) {
    case BlenderDownloadStage::Downloading:
        return "Downloading " + name + " from blender.org";
    case BlenderDownloadStage::Checking:
        return "Checking the download against the SHA-256 checksum blender.org publishes";
    case BlenderDownloadStage::Unpacking:
        return "Unpacking " + name;
    case BlenderDownloadStage::Inspecting:
        return "Checking that " + name + " runs";
    }
    return name;
}

// Says what blender.org offers, or with --yes downloads, checks, unpacks and chooses it. With --if-missing it
// does nothing when a usable Blender is already found. Asking with --yes is the consent, so nothing is asked.
Answer blenderGet(const RunContext& context, const BlenderGetOptions& options) {
    const std::string command = "blender get";
    const std::stop_token stop = stopToken(context);
    std::ostream* progress = options.quiet ? nullptr : context.progress;

    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }

    if (options.ifMissing) {
        writeProgress(progress, "look", 0, 0, "Looking for a usable Blender on this computer");
        const BlenderChoice choice = chooseBlender(std::nullopt, loaded.settings, stop);
        if (choice.status.ok()) {
            return skipped(command, choice.install);
        }
    }
    if (stop.stop_requested()) {
        return failure(command, fail(ErrorCode::Cancelled, kStopped));
    }

    writeProgress(progress, "find", 0, 0, "Looking for the newest portable Blender on blender.org");
    const BlenderOfferCheck check = readOffer(stop);
    if (!check.status.ok()) {
        return failure(command, check.status);
    }
    if (!check.offer) {
        return failure(command, fail(ErrorCode::NetworkFailed, "blender.org offered no portable Blender."));
    }
    const BlenderOffer& offer = *check.offer;

    if (!options.yes) {
        Json result = Json::object();
        result["state"] = "offer";
        addOffer(result, offer);
        result["next"] = options.ifMissing ? "plynth blender get --yes --if-missing" : "plynth blender get --yes";
        result["note"] = "Nothing was downloaded. The command in next downloads this Blender from blender.org, "
                         "checks it against blender.org's SHA-256 checksum, unpacks it into target and chooses it "
                         "in the settings.";
        return success(command, std::move(result));
    }
    if (stop.stop_requested()) {
        return failure(command, fail(ErrorCode::Cancelled, kStopped));
    }

    std::error_code ec;
    const bool reused = std::filesystem::exists(targetFolder(offer) / pathFromUtf8(kDownloadedBlenderMarker), ec);
    const std::string name = "Blender " + formatBlenderVersion(offer.version);
    std::optional<BlenderDownloadStage> lastStage;
    long long lastStep = -1;
    const BlenderDownload download = fetchOffer(
        offer,
        [&](const BlenderDownloadProgress& step) {
            const long long part = step.total > 0 ? step.done * 100 / step.total : step.done / (4LL << 20);
            if (lastStage == step.stage && part == lastStep) {
                return;
            }
            lastStage = step.stage;
            lastStep = part;
            writeProgress(progress, blenderDownloadStageName(step.stage), step.done, step.total,
                          stageMessage(step.stage, name));
        },
        stop);
    if (!download.status.ok()) {
        return failure(command, download.status);
    }
    if (!download.install) {
        return failure(command, fail(ErrorCode::BlenderFailed, "The download gave no Blender."));
    }
    const BlenderInstall& install = *download.install;
    const std::string path = pathToUtf8(install.executable);

    // The settings are read again, since the app may have changed them during the download.
    writeProgress(progress, "select", 0, 0, "Choosing the new Blender in the settings");
    SettingsLoad again = loadSettings();
    if (again.status.ok()) {
        again.settings.blender = install.executable;
        again.status = saveSettings(again.settings);
    }
    if (!again.status.ok()) {
        return failure(command, fail(again.status.code, "Blender was downloaded to " + path +
                                                            ", but it could not be chosen in the settings. " +
                                                            again.status.message));
    }

    Json result = Json::object();
    result["state"] = "installed";
    result["version"] = install.version ? formatBlenderVersion(*install.version) : formatBlenderVersion(offer.version);
    result["path"] = path;
    result["folder"] = pathToUtf8(install.executable.parent_path());
    result["source"] = offer.zipUrl;
    result["sha256"] = offer.sha256;
    result["reused"] = reused;
    result["selected"] = true;
    Answer answer = success(command, std::move(result));
    if (blenderVariableSet()) {
        answer.warnings.push_back({"blender_variable_set",
                                   "PLYNTH_BLENDER is set, and commands use the Blender it names before the one in "
                                   "the settings. Clear it to use the new Blender."});
    }
    return answer;
}

}

void setBlenderGetSource(BlenderGetSource source) {
    currentSource() = std::move(source);
}

std::vector<CommandSpec> blenderCommands() {
    CommandSpec spec;
    spec.words = {"blender", "get"};
    spec.description = "Get the official portable Blender from blender.org. Without --yes it only says what it "
                       "would download.";
    spec.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<BlenderGetOptions>();
        sub.add_flag("--yes", options->yes,
                     "Download it, check its checksum, unpack it and choose it in the settings.");
        sub.add_flag("--if-missing", options->ifMissing, "Do nothing when a usable Blender is already found.");
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        return [options](const RunContext& context) { return blenderGet(context, *options); };
    };
    return {spec};
}

}
