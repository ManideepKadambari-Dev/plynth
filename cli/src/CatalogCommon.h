#pragma once

#include "Registry.h"

#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>
#include <plynth/core/Blender.h>
#include <plynth/core/catalog/AssetCatalog.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/MaterialCatalog.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

// An open library: its paths, the settings it came from and its catalog.
struct OpenedCatalog {
    MachineSettings settings;
    LibraryPaths paths;
    std::unique_ptr<AssetCatalog> catalog;
};

// An open library with its materials catalog.
struct OpenedMaterialCatalog {
    MachineSettings settings;
    LibraryPaths paths;
    std::unique_ptr<MaterialCatalog> catalog;
};

// Adds the --library option, which names the library folder. Without it the settings decide.
void addLibraryOption(CLI::App& sub, std::string& value);

// Reads the settings and opens the catalog of the library (the --library value, or the settings). Also
// registers the library's shapes and hands its room types to the core. On error the answer for command is
// returned in failed.
bool openCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                 OpenedCatalog& opened, Answer& failed);

// Opens the materials catalog of the library (the --library value, or the settings). On error the answer
// for command is returned in failed (no_library and the refusals of MaterialCatalog::open).
bool openMaterialCatalog(const std::string& command, const RunContext& context, const std::string& libraryOption,
                         OpenedMaterialCatalog& opened, Answer& failed);

// Like openMaterialCatalog, but a missing library is not an error: found is false and failed stays empty.
// A --library value that is not a library is still an error.
bool openMaterialCatalogIfAny(const std::string& command, const RunContext& context,
                              const std::string& libraryOption, OpenedMaterialCatalog& opened, bool& found,
                              Answer& failed);

// Registers Library/shapes/ (opening shapes and trim profiles) of the library in the settings, when there is one. Does nothing otherwise and
// never fails: a command that only needs shapes still works without a library.
void registerLibraryShapes();

// Registers the custom opening shapes and trim profiles of one library folder, once per folder in a process.
void registerShapesOf(const LibraryPaths& paths);

// Registers the shapes and trim profiles of the library named by --library, or of the settings' library when the
// option is empty (never failing then). A --library value that is not a library fills failed with no_library.
bool registerShapesFor(const std::string& command, const RunContext& context, const std::string& libraryOption,
                       Answer& failed);

// Makes the room types in use the library's list when the settings name a working library, and the built-in
// list otherwise.
void useSettingsRoomTypes();

// Picks the library folder (the --library value, or the settings) without opening its databases. On error
// the answer for command is returned in failed (no_library).
bool chooseLibraryFolder(const std::string& command, const RunContext& context, const std::string& libraryOption,
                         MachineSettings& settings, LibraryPaths& paths, Answer& failed);

// True when an import argument is an address and not a file. Text that starts with a word of two or more
// letters and a colon, such as https: or asset_base_id:, cannot be a file name on Windows, so it is always an
// address. Other text is an address when an asset or material importer scores it as one and no file or
// folder of that name exists, so a new importer's addresses need no change here.
bool isAddressArgument(const RunContext& context, const std::string& text);

// Reads repeated key=value options into one object. The value is read as JSON when it parses and is text
// otherwise. A dotted key such as maps.*.base_color makes nested objects. On error the answer for command
// is returned in failed (invalid_value).
bool parseOptionPairs(const std::string& command, const std::vector<std::string>& pairs, nlohmann::ordered_json& out,
                      Answer& failed);

// The --blender option: the text typed and whether the option was there at all. --blender "" and
// --blender= are given and name no file, so an empty shell variable never counts as no option.
struct BlenderOption {
    std::string text;
    CLI::Option* option = nullptr;

    // True when the option was on the command line, even with an empty path.
    bool given() const { return option != nullptr && option->count() > 0; }
};

// Adds the --blender option, which names the blender.exe for a command that runs Blender. Without it the
// settings and the search decide. An empty path is refused as bad_arguments before the command runs.
void addBlenderOption(CLI::App& sub, BlenderOption& value,
                      const std::string& help = "The blender.exe to use. Default: the setup check's choice.");

// The same for a command that keeps the path as plain text.
void addBlenderOption(CLI::App& sub, std::string& value);

// The option as core takes it: none when it was not given, an empty path when it was given empty, and
// otherwise the path from the current folder.
std::optional<std::filesystem::path> blenderOptionPath(const RunContext& context, const BlenderOption& option);

// Refuses a given --blender with an empty path, before the command does any work: the option is the only
// Blender allowed and it names none. Fills failed with blender_not_found and core's sentence.
bool refuseEmptyBlender(const std::string& command, const BlenderOption& option, Answer& failed);

// Picks the Blender and fills what importAsset and refreshAsset need. On error the answer for command is
// returned in failed (blender_not_found).
bool makeImportContext(const std::string& command, const RunContext& context, const OpenedCatalog& opened,
                       const BlenderOption& blenderOption, ImportContext& importContext, Answer& failed);

// Rebuilds the index from disk. Unless noBlender, each changed asset is then measured again and given a new
// thumbnail. Returns the rescan entry, with the refresh outcome in it.
Json rescanAndRefresh(const std::string& command, const RunContext& context, OpenedCatalog& opened,
                      const BlenderOption& blenderOption, bool noBlender);

// Checks that an id typed by the user is a plain catalog id (checkCatalogId): no folders, no "..", no
// absolute paths. Fills failed with invalid_value when it is not.
bool checkIdArgument(const std::string& command, const std::string& id, Answer& failed);

// The asset entry shared by the asset commands.
Json assetEntry(const AssetRecord& record);

// The material entry shared by the materials commands.
Json materialEntry(const MaterialRecord& record);

// The group entry of the materials catalog: id, name, whether it is built in and how many materials hold it.
Json materialGroupEntry(const MaterialGroupEntry& entry);

// The room type entry: id, name, whether it is built in and its category ids.
Json roomTypeEntry(const RoomTypeEntry& entry);

// The category entry: id, name, whether it is built in and how many assets hold it.
Json categoryEntry(const CategoryEntry& entry);

// The license entry: id, name, link and whether it may be redistributed.
Json licenseEntry(const LicenseEntry& entry);

// The rescan entry shared by the rescan commands. refreshed and refreshFailed hold the outcome of the
// Blender refreshes that followed; both are empty when none ran. needs_refresh lists the changed assets
// that were not refreshed.
Json rescanEntry(const RescanReport& report, const std::vector<std::string>& refreshed,
                 const Json& refreshFailed);

// A comma separated list as a vector. An empty text gives an empty vector.
std::vector<std::string> splitList(const std::string& text);

// An absolute path for text typed by the user, relative to the current folder.
std::filesystem::path userPath(const RunContext& context, const std::string& text);

}
