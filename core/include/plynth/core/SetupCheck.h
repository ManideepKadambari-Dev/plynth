#pragma once

#include "Blender.h"
#include "Settings.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The setup check (DESIGN.md section 7.19b). One core service with three front ends: the app's setup check
// screen, "plynth doctor", and the first run. It only looks: it never downloads or installs anything, and it
// never changes the settings. Creating the library and projects folders is a separate, explicit step
// (initLibrary, std::filesystem::create_directories) taken by the front end when the user asks for it.

enum class CheckState {
    Found,          // "found": present and good
    Missing,        // "missing"
    TooOld,         // "too_old"
    NeedsAttention, // "needs_attention": usable, but the user should look (untested version, low disk space)
    NotChecked,     // "not_checked": not checked here (add-ons without a Blender, the graphics driver in doctor)
};

std::string_view checkStateName(CheckState state);

// Item ids, in the order the screen and doctor list them.
inline constexpr std::string_view kCheckBlender = "blender";
inline constexpr std::string_view kCheckBlenderVersion = "blender_version";
inline constexpr std::string_view kCheckLibrary = "library_folder";
inline constexpr std::string_view kCheckProjects = "projects_folder";
inline constexpr std::string_view kCheckGraphics = "graphics_driver";
inline constexpr std::string_view kCheckBlenderKit = "addon_blenderkit";
inline constexpr std::string_view kCheckArchimesh = "addon_archimesh";
inline constexpr std::string_view kCheckHomeBuilder = "addon_home_builder";
inline constexpr std::string_view kCheckBoolTool = "addon_bool_tool";
inline constexpr std::string_view kCheckAssetOptimizer = "addon_asset_optimizer";
inline constexpr std::string_view kCheckBagapie = "addon_bagapie";

// Below this much free space the library folder needs attention. It never blocks.
inline constexpr std::uintmax_t kMinLibraryFreeBytes = 2ull * 1024 * 1024 * 1024;

inline constexpr std::string_view kBlenderDownloadUrl = "https://www.blender.org/download/";

// One add-on Plynth can use. Matched against the last dotted part of each enabled module name from the
// probe, ignoring case, so "bl_ext.blender_org.archimesh" and "archimesh" both match "archimesh".
struct KnownAddon {
    std::string_view checkId;                  // one of the kCheck ids above
    std::string_view name;                     // "Archimesh"
    std::vector<std::string_view> modules;     // module names it may have
    std::string_view usedFor;                  // "stairs, doors, windows and columns generators"
    std::string_view link;                     // official page, empty when there is no single one
    bool neededForImport = false;              // BlenderKit only
};

// The add-ons of DESIGN.md section 7.10 and BlenderKit, in check order.
const std::vector<KnownAddon>& knownAddons();

struct SetupItem {
    std::string id;
    std::string name;           // "Blender", "Library folder", "BlenderKit add-on"
    bool required = false;      // required items block Build, Export and import until they pass
    CheckState state = CheckState::NotChecked;
    std::string version;        // when found, for example "5.2.0"
    std::filesystem::path path; // when found or chosen
    std::string detail;         // one plain sentence on what was found
    std::string action;         // one plain sentence on what to do; empty when nothing is needed
    std::string link;           // an official page to open; empty when there is none
    std::vector<std::string> affects; // what stays switched off without it, for optional items

    // Missing or TooOld on a required item. NeedsAttention and NotChecked never block.
    bool blocks() const;
};

struct SetupReport {
    std::vector<SetupItem> items;
    std::vector<BlenderInstall> blenderInstalls;  // every Blender found, newest first
    std::optional<std::size_t> selectedBlender;   // index into blenderInstalls (preferredBlender)
    bool ready = false;     // no item blocks
    bool firstRun = false;  // there was no settings file
    bool updated = false;   // the settings name another Plynth version as the last that passed
    bool probed = false;    // the selected Blender was asked about its add-ons

    const SetupItem* find(std::string_view id) const;
};

// What the app found when it started the graphics the 3D preview uses. Core has no graphics code, so the
// app fills this in and passes it to the checks; "plynth doctor" has none.
struct GraphicsProbe {
    bool ok = false;
    std::string api;     // the graphics interface, for example "Direct3D 11"
    std::string device;  // the graphics card, for example "NVIDIA GeForce RTX 3060"
    std::string problem; // one plain sentence when ok is false
};

struct SetupCheckOptions {
    BlenderSearch search;          // chosen is filled from the settings by runSetupCheck when empty
    bool probeAddons = true;       // runs the probe task in the selected Blender (a few seconds)
    std::uintmax_t minLibraryFree = kMinLibraryFreeBytes;
    bool settingsExist = true;     // from SettingsLoad::exists, for firstRun
    std::optional<GraphicsProbe> graphics; // from the app; empty in doctor
};

// Runs every check. Blender: findBlenderInstalls, then the preferred one; missing when none is found,
// too old when only old ones are. Blender version: supported, too old, or needs attention when untested.
// Library folder: set, exists, is a library (isLibraryFolder), writable, free space. Projects folder: set,
// exists, writable. Graphics driver, from options.graphics: Found with version = api and the detail naming
// the device; Missing with the probe's problem as detail and the action "Update the graphics driver. See the
// help page on the 3D preview."; NotChecked with "Checked by the app when it starts the 3D preview." when
// there is no probe. Add-ons (never required): Found or Missing from the probe of the
// selected Blender; NotChecked with "Blender is needed to check add-ons." when no Blender is usable or the
// probe failed, and NotChecked when probeAddons is false. BlenderKit Found but not signed in is
// NeedsAttention.
// "Check again" in the app is calling this again. Blocks; the app runs it on a worker thread.
SetupReport runSetupCheck(const MachineSettings& settings, const SetupCheckOptions& options,
                          std::stop_token stop = {});

// The checks that run no program: folders, whether the saved Blender path still exists (state Found with no
// version, NotChecked for the version and the add-ons), and the graphics driver from graphics as in
// runSetupCheck (NotChecked in the first form). The app runs it at start to decide whether to open the setup
// check screen.
SetupReport quickSetupCheck(const MachineSettings& settings, bool settingsExist);
SetupReport quickSetupCheck(const MachineSettings& settings, bool settingsExist,
                            const std::optional<GraphicsProbe>& graphics);

// Whether the app opens the setup check at start: first run, after an update, or a required item blocks.
bool setupNeeded(const SetupReport& quick);

// The doctor answer's result, in the shape of schema/setup-report.schema.json:
//   {"ready": false, "first_run": false, "updated": false, "probed": true,
//    "items": [{"id": "blender", "name": "Blender", "required": true, "state": "found", "version": "5.2.0",
//               "path": "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe", "detail": "...",
//               "action": "", "link": "", "affects": []}, ...],
//    "blender_installs": [{"path": "...", "version": "5.2.0", "version_text": "Blender 5.2.0 LTS",
//                          "support": "supported", "sources": ["program_folder", "registry"],
//                          "selected": true, "problem": null}]}
// version and path are null when unknown.
Json setupReportToJson(const SetupReport& report);

}
