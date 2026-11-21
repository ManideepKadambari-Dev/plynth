#include "plynth/core/SetupCheck.h"

#include "plynth/core/BlenderJob.h"
#include "plynth/core/Library.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/Version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// Lower cases ASCII text.
std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The last dotted part of a module name, lower case: "bl_ext.blender_org.home_builder_5" gives "home_builder_5".
std::string lastPart(const std::string& module)
{
    const std::size_t dot = module.rfind('.');
    return lowered(dot == std::string::npos ? module : module.substr(dot + 1));
}

// The name without a version number at its end: "home_builder_5" gives "home_builder".
std::string withoutVersion(std::string name)
{
    std::size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
    if (end < name.size() && end > 0 && (name[end - 1] == '_' || name[end - 1] == '-')) return name.substr(0, end - 1);
    return name;
}

// Says whether an enabled module is one of the names an add-on may have.
bool moduleMatches(const KnownAddon& addon, const std::string& module)
{
    const std::string part = lastPart(module);
    const std::string bare = withoutVersion(part);
    for (const std::string_view name : addon.modules) {
        if (part == name || bare == name) return true;
    }
    return false;
}

// Whether a folder can be written to. The read only attribute counts, because Windows ignores it for
// creating files inside a folder but a user who set it means the folder is not to be changed.
bool folderWritable(const fs::path& folder)
{
    std::error_code ec;
    const fs::perms perms = fs::status(folder, ec).permissions();
    if (!ec && (perms & fs::perms::owner_write) == fs::perms::none) return false;
    const fs::path probe = folder / ".plynth-write-test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "x";
    }
    fs::remove(probe, ec);
    return true;
}

// "12.5 GB" or "800 MB".
std::string sizeText(std::uintmax_t bytes)
{
    char buffer[32];
    if (bytes >= 1024ull * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu MB", static_cast<unsigned long long>(bytes / (1024 * 1024)));
    }
    return buffer;
}

// Makes an item.
SetupItem item(std::string_view id, std::string name, bool required)
{
    SetupItem i;
    i.id = std::string(id);
    i.name = std::move(name);
    i.required = required;
    return i;
}

// The library folder item.
SetupItem libraryItem(const MachineSettings& settings, std::uintmax_t minFree)
{
    SetupItem i = item(kCheckLibrary, "Library folder", true);
    if (!settings.library) {
        i.state = CheckState::Missing;
        i.detail = "No library folder is chosen.";
        i.action = "Choose a folder for the library. Suggested: " + pathToUtf8(defaultLibraryFolder()) + ".";
        return i;
    }
    const fs::path& root = *settings.library;
    i.path = root;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        i.state = CheckState::Missing;
        i.detail = "The folder " + pathToUtf8(root) + " does not exist.";
        i.action = "Create the folder or choose another one.";
        return i;
    }
    if (!isLibraryFolder(root)) {
        i.state = CheckState::Missing;
        i.detail = "The folder " + pathToUtf8(root) + " is not a library yet.";
        i.action = "Click Create to make it a library, or run \"plynth library init " + pathToUtf8(root) + "\".";
        return i;
    }
    if (!folderWritable(root)) {
        i.state = CheckState::Missing;
        i.detail = "The folder " + pathToUtf8(root) + " cannot be written to.";
        i.action = "Choose a folder you can write to.";
        return i;
    }
    const auto space = fs::space(root, ec);
    if (!ec && space.available < minFree) {
        i.state = CheckState::NeedsAttention;
        i.detail = "Only " + sizeText(space.available) + " is free on the drive of the library.";
        i.action = "Free some space or choose a folder on another drive.";
        return i;
    }
    i.state = CheckState::Found;
    i.detail = "The library is ready.";
    return i;
}

// The projects folder item.
SetupItem projectsItem(const MachineSettings& settings)
{
    SetupItem i = item(kCheckProjects, "Projects folder", true);
    if (!settings.projects) {
        i.state = CheckState::Missing;
        i.detail = "No projects folder is chosen.";
        i.action = "Choose a folder for your projects. Suggested: " + pathToUtf8(defaultProjectsFolder()) + ".";
        return i;
    }
    const fs::path& folder = *settings.projects;
    i.path = folder;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        i.state = CheckState::Missing;
        i.detail = "The folder " + pathToUtf8(folder) + " does not exist.";
        i.action = "Create the folder or choose another one.";
        return i;
    }
    if (!folderWritable(folder)) {
        i.state = CheckState::Missing;
        i.detail = "The folder " + pathToUtf8(folder) + " cannot be written to.";
        i.action = "Choose a folder you can write to.";
        return i;
    }
    i.state = CheckState::Found;
    i.detail = "New projects are saved here.";
    return i;
}

// The graphics driver item, a placeholder until the 3D preview exists.
SetupItem graphicsItem()
{
    SetupItem i = item(kCheckGraphics, "Graphics driver", true);
    i.state = CheckState::NotChecked;
    i.detail = "Checked once the 3D preview is added.";
    return i;
}

// An add-on item before anything is known about it.
SetupItem addonItem(const KnownAddon& addon)
{
    SetupItem i = item(addon.checkId, std::string(addon.name) + " add-on", false);
    i.link = std::string(addon.link);
    i.affects.emplace_back(addon.usedFor);
    return i;
}

// Fills the add-on items from what the probe found.
void fillAddons(SetupReport& report, const ProbeResult& probe)
{
    for (const KnownAddon& addon : knownAddons()) {
        SetupItem i = addonItem(addon);
        const ProbeAddon* enabled = nullptr;
        for (const ProbeAddon& a : probe.addons) {
            if (moduleMatches(addon, a.module)) {
                enabled = &a;
                break;
            }
        }
        if (addon.checkId == kCheckBlenderKit) {
            if (enabled) i.version = enabled->version;
            if (probe.blenderKitEnabled && probe.blenderKitSignedIn) {
                i.state = CheckState::Found;
                i.detail = "BlenderKit is enabled and signed in.";
            } else if (probe.blenderKitEnabled) {
                i.state = CheckState::NeedsAttention;
                i.detail = "BlenderKit is enabled but not signed in.";
                i.action = "Sign in to BlenderKit inside Blender, in Edit, Preferences, Add-ons, BlenderKit.";
            } else if (probe.blenderKitInstalled) {
                i.state = CheckState::Missing;
                i.detail = "BlenderKit is installed but not enabled.";
                i.action = "Enable the BlenderKit add-on in Blender's preferences, then sign in.";
            } else {
                i.state = CheckState::Missing;
                i.detail = "BlenderKit is not installed in Blender.";
                i.action = "Install BlenderKit in Blender, then sign in inside Blender.";
            }
        } else if (enabled) {
            i.state = CheckState::Found;
            i.version = enabled->version;
            i.detail = "Enabled in Blender.";
        } else {
            i.state = CheckState::Missing;
            i.detail = "Not installed or not enabled in Blender.";
            i.action = "Install and enable it in Blender to switch these features on.";
        }
        report.items.push_back(std::move(i));
    }
}

// Fills the add-on items with a state that says they were not asked about.
void fillAddonsNotChecked(SetupReport& report, const std::string& detail)
{
    for (const KnownAddon& addon : knownAddons()) {
        SetupItem i = addonItem(addon);
        i.state = CheckState::NotChecked;
        i.detail = detail;
        report.items.push_back(std::move(i));
    }
}

// Sets ready from the items.
void finish(SetupReport& report)
{
    report.ready = std::none_of(report.items.begin(), report.items.end(), [](const SetupItem& i) { return i.blocks(); });
}

// Whether the settings name another Plynth version as the last whose check passed.
bool settingsUpdated(const MachineSettings& settings, bool settingsExist)
{
    return settingsExist && settings.setupCheckedVersion != version();
}

}

// The name used in answers.
std::string_view checkStateName(CheckState state)
{
    switch (state) {
    case CheckState::Found: return "found";
    case CheckState::Missing: return "missing";
    case CheckState::TooOld: return "too_old";
    case CheckState::NeedsAttention: return "needs_attention";
    case CheckState::NotChecked: return "not_checked";
    }
    return "not_checked";
}

// The add-ons Plynth can use.
const std::vector<KnownAddon>& knownAddons()
{
    static const std::vector<KnownAddon> addons = {
        {kCheckBlenderKit, "BlenderKit", {"blenderkit"}, "importing assets from BlenderKit", "https://www.blenderkit.com/", true},
        {kCheckArchimesh, "Archimesh", {"archimesh"}, "stairs, doors, windows and columns generators",
         "https://extensions.blender.org/add-ons/archimesh/", false},
        {kCheckHomeBuilder, "Home Builder", {"home_builder"}, "cabinets, countertops and moldings generators", "", false},
        {kCheckBoolTool, "Bool Tool", {"bool_tool", "object_boolean_tools"}, "cutting openings with boolean tools",
         "https://extensions.blender.org/add-ons/bool-tool/", false},
        {kCheckAssetOptimizer, "Asset Optimizer", {"asset_optimizer"}, "asset cleanup generators", "", false},
        {kCheckBagapie, "Bagapie", {"bagapie"}, "array and scatter generators", "", false},
    };
    return addons;
}

// Missing or too old on a required item.
bool SetupItem::blocks() const
{
    return required && (state == CheckState::Missing || state == CheckState::TooOld);
}

// Finds an item by id.
const SetupItem* SetupReport::find(std::string_view id) const
{
    for (const SetupItem& i : items) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

// Runs every check.
SetupReport runSetupCheck(const MachineSettings& settings, const SetupCheckOptions& options, std::stop_token stop)
{
    SetupReport report;
    report.firstRun = !options.settingsExist;
    report.updated = settingsUpdated(settings, options.settingsExist);

    BlenderSearch search = options.search;
    if (!search.chosen) search.chosen = settings.blender;
    report.blenderInstalls = findBlenderInstalls(search, stop);
    report.selectedBlender = preferredBlender(report.blenderInstalls, settings.blender);

    SetupItem blender = item(kCheckBlender, "Blender", true);
    SetupItem blenderVersion = item(kCheckBlenderVersion, "Blender version", true);
    const BlenderInstall* shown = nullptr;
    if (report.selectedBlender) {
        shown = &report.blenderInstalls[*report.selectedBlender];
    } else if (!report.blenderInstalls.empty() && report.blenderInstalls.front().version) {
        shown = &report.blenderInstalls.front();
    }
    if (shown) {
        blender.state = CheckState::Found;
        blender.path = shown->executable;
        blender.version = formatBlenderVersion(*shown->version);
        blender.detail = shown->versionText + " found.";
        blenderVersion.version = blender.version;
        blenderVersion.path = shown->executable;
        const std::string supported = "Plynth needs Blender " + formatBlenderVersion(kMinBlenderVersion) + " or newer.";
        switch (shown->support) {
        case BlenderSupport::Supported:
            blenderVersion.state = CheckState::Found;
            blenderVersion.detail = "Blender " + blender.version + " is supported.";
            break;
        case BlenderSupport::Untested:
            blenderVersion.state = CheckState::NeedsAttention;
            blenderVersion.detail = "Blender " + blender.version + " is newer than " + formatBlenderVersion(kTestedBlenderVersion) +
                                    ", the version Plynth is tested with. It is allowed.";
            blenderVersion.action = "If something goes wrong, try Blender " + formatBlenderVersion(kTestedBlenderVersion) + ".";
            break;
        case BlenderSupport::TooOld:
            blenderVersion.state = CheckState::TooOld;
            blenderVersion.detail = "Blender " + blender.version + " is too old. " + supported;
            blenderVersion.action = "Install a newer Blender from blender.org.";
            blenderVersion.link = std::string(kBlenderDownloadUrl);
            break;
        }
    } else {
        blender.state = CheckState::Missing;
        blender.link = std::string(kBlenderDownloadUrl);
        if (report.blenderInstalls.empty()) {
            blender.detail = settings.blender ? "The Blender " + pathToUtf8(*settings.blender) + " does not exist and no other Blender was found."
                                              : "No Blender was found on this computer.";
            blender.action = "Install Blender " + formatBlenderVersion(kMinBlenderVersion) +
                             " or newer from blender.org, or use Browse to pick blender.exe.";
        } else {
            blender.detail = "Blender was found at " + pathToUtf8(report.blenderInstalls.front().executable) +
                             " but it did not report its version: " + report.blenderInstalls.front().status.message;
            blender.action = "Reinstall Blender from blender.org, or use Browse to pick another blender.exe.";
        }
        blenderVersion.state = CheckState::NotChecked;
        blenderVersion.detail = "Blender is needed to read its version.";
    }
    report.items.push_back(std::move(blender));
    report.items.push_back(std::move(blenderVersion));
    report.items.push_back(libraryItem(settings, options.minLibraryFree));
    report.items.push_back(projectsItem(settings));
    report.items.push_back(graphicsItem());

    const BlenderInstall* usable = report.selectedBlender ? &report.blenderInstalls[*report.selectedBlender] : nullptr;
    if (!options.probeAddons) {
        fillAddonsNotChecked(report, "Add-ons were not checked this time.");
    } else if (!usable || stop.stop_requested()) {
        fillAddonsNotChecked(report, "Blender is needed to check add-ons.");
    } else {
        std::error_code ec;
        const std::string jobId = newJobId("probe");
        const fs::path work = fs::temp_directory_path(ec) / ("plynth-" + jobId);
        const fs::path resultFile = work / pathFromUtf8(kResultFileName);
        const Json job = makeBlenderJob(kProbeTask, jobId, resultFile, Json::object());
        BlenderRunOptions run;
        run.factoryStartup = false;
        run.timeout = std::chrono::seconds(120);
        const BlenderJobRun ran = runBlenderJob(usable->executable, job, work, run, {}, stop);
        ProbeResult probe;
        if (ran.status.ok()) probe = readProbeResult(ran.result, jobId);
        fs::remove_all(work, ec);
        if (ran.status.ok() && probe.header.status.ok()) {
            report.probed = true;
            fillAddons(report, probe);
        } else {
            fillAddonsNotChecked(report, "Blender is needed to check add-ons.");
        }
    }
    finish(report);
    return report;
}

// The checks that run no program.
SetupReport quickSetupCheck(const MachineSettings& settings, bool settingsExist)
{
    SetupReport report;
    report.firstRun = !settingsExist;
    report.updated = settingsUpdated(settings, settingsExist);

    SetupItem blender = item(kCheckBlender, "Blender", true);
    std::error_code ec;
    if (settings.blender && fs::is_regular_file(*settings.blender, ec)) {
        blender.state = CheckState::Found;
        blender.path = *settings.blender;
        blender.detail = "The Blender chosen in the settings exists. Its version is checked in the setup check.";
    } else {
        BlenderSearch search;
        search.chosen = settings.blender;
        const std::vector<BlenderCandidate> candidates = findBlenderCandidates(search);
        if (!candidates.empty()) {
            blender.state = CheckState::Found;
            blender.path = candidates.front().executable;
            blender.detail = "A Blender was found. Its version is checked in the setup check.";
        } else {
            blender.state = CheckState::Missing;
            blender.link = std::string(kBlenderDownloadUrl);
            blender.detail = settings.blender ? "The Blender " + pathToUtf8(*settings.blender) + " does not exist."
                                              : "No Blender was found on this computer.";
            blender.action = "Install Blender " + formatBlenderVersion(kMinBlenderVersion) +
                             " or newer from blender.org, or use Browse to pick blender.exe.";
        }
    }
    report.items.push_back(std::move(blender));

    SetupItem blenderVersion = item(kCheckBlenderVersion, "Blender version", true);
    blenderVersion.state = CheckState::NotChecked;
    blenderVersion.detail = "Checked in the setup check.";
    report.items.push_back(std::move(blenderVersion));
    report.items.push_back(libraryItem(settings, kMinLibraryFreeBytes));
    report.items.push_back(projectsItem(settings));
    report.items.push_back(graphicsItem());
    fillAddonsNotChecked(report, "Checked in the setup check.");
    finish(report);
    return report;
}

// Whether the app opens the setup check at start.
bool setupNeeded(const SetupReport& quick)
{
    return quick.firstRun || quick.updated || !quick.ready;
}

// The doctor answer's result.
Json setupReportToJson(const SetupReport& report)
{
    Json json = Json::object();
    json["ready"] = report.ready;
    json["first_run"] = report.firstRun;
    json["updated"] = report.updated;
    json["probed"] = report.probed;
    Json items = Json::array();
    for (const SetupItem& i : report.items) {
        Json j = Json::object();
        j["id"] = i.id;
        j["name"] = i.name;
        j["required"] = i.required;
        j["state"] = std::string(checkStateName(i.state));
        j["version"] = i.version.empty() ? Json(nullptr) : Json(i.version);
        j["path"] = i.path.empty() ? Json(nullptr) : Json(pathToUtf8(i.path));
        j["detail"] = i.detail;
        j["action"] = i.action;
        j["link"] = i.link;
        j["affects"] = i.affects;
        items.push_back(std::move(j));
    }
    json["items"] = std::move(items);
    Json installs = Json::array();
    for (std::size_t n = 0; n < report.blenderInstalls.size(); ++n) {
        const BlenderInstall& b = report.blenderInstalls[n];
        Json j = Json::object();
        j["path"] = pathToUtf8(b.executable);
        j["version"] = b.version ? Json(formatBlenderVersion(*b.version)) : Json(nullptr);
        j["version_text"] = b.versionText;
        j["support"] = std::string(blenderSupportName(b.support));
        Json sources = Json::array();
        for (const BlenderSource s : b.sources) sources.push_back(std::string(blenderSourceName(s)));
        j["sources"] = std::move(sources);
        j["selected"] = report.selectedBlender && *report.selectedBlender == n;
        j["problem"] = b.status.ok() ? Json(nullptr) : Json(b.status.message);
        installs.push_back(std::move(j));
    }
    json["blender_installs"] = std::move(installs);
    return json;
}

}
