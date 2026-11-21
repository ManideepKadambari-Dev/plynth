#pragma once

#include "Registry.h"

#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/AssetCatalog.h>

#include <memory>
#include <string>
#include <vector>

namespace plynth::cli {

// An open library: its paths, the settings it came from and its catalog.
struct OpenedCatalog {
    MachineSettings settings;
    LibraryPaths paths;
    std::unique_ptr<AssetCatalog> catalog;
};

// Adds the --library option, which names the library folder. Without it the settings decide.
void addLibraryOption(CLI::App& sub, std::string& value);

// Reads the settings and opens the catalog of the library (the --library value, or the settings). Also
// registers the library's shapes and hands its room types to the core. On error the answer for command is
// returned in failed.
bool openCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                 OpenedCatalog& opened, Answer& failed);

// Registers Library/shapes/ of the library in the settings, when there is one. Does nothing otherwise and
// never fails: a command that only needs shapes still works without a library.
void registerLibraryShapes();

// Makes the room types in use the library's list when the settings name a working library, and the built-in
// list otherwise.
void useSettingsRoomTypes();

// The asset entry shared by the asset commands.
Json assetEntry(const AssetRecord& record);

// The room type entry: id, name, whether it is built in and its category ids.
Json roomTypeEntry(const RoomTypeEntry& entry);

// The category entry: id, name, whether it is built in and how many assets hold it.
Json categoryEntry(const CategoryEntry& entry);

// The license entry: id, name, link and whether it may be redistributed.
Json licenseEntry(const LicenseEntry& entry);

// The rescan entry shared by the rescan commands. refreshed and refreshFailed hold the outcome of the
// Blender refreshes that followed; both are empty when none ran.
Json rescanEntry(const RescanReport& report, const std::vector<std::string>& refreshed,
                 const Json& refreshFailed);

// A comma separated list as a vector. An empty text gives an empty vector.
std::vector<std::string> splitList(const std::string& text);

// An absolute path for text typed by the user, relative to the current folder.
std::filesystem::path userPath(const RunContext& context, const std::string& text);

}
