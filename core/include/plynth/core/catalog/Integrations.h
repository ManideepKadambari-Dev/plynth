#pragma once

#include "../Library.h"
#include "../SetupCheck.h"
#include "../Status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// The Integrations tab: one entry per source assets and materials come from.
// Integrations are registry entries like importers: each lives in the importer file of its source with
// an IntegrationRegistrar at namespace scope, so a new source is new files only. Built in now:
//   "local"       Own files: asset importer local_file, material importers texture_set and blend_material
//                 (core/src/catalog/importers/LocalIntegration.cpp)
//   "blenderkit"  BlenderKit: asset and material importer blenderkit (BlenderKit.h)
//   "fab"         Fab: asset and material importer fab_zip (FabZip.h)
// Integration options live in the library, Library/integrations.json, schema/integrations.schema.json, so
// the machine settings file does not change. Only what the user changed is saved; defaults come from code.

// How an option is used: saved as the default for every import, or given for one import only.
enum class OptionUse { Saved, Import };

class Integration {
public:
    virtual ~Integration() = default;

    // Stable lower case id, for example "fab".
    virtual std::string_view id() const = 0;
    // The entry's title in the tab, for example "Fab".
    virtual std::string_view name() const = 0;
    // One or two plain sentences on what it does and what it needs.
    virtual std::string_view description() const = 0;
    // Place in the tab, lowest first: local 10, blenderkit 20, fab 30.
    virtual int order() const = 0;
    // The importer ids it brings, in each registry. Either list may be empty.
    virtual std::vector<std::string> assetImporters() const = 0;
    virtual std::vector<std::string> materialImporters() const = 0;
    // True when it takes web addresses (BlenderKit), false when it takes files.
    virtual bool takesAddresses() const = 0;
    // The setup check item it depends on, for example kCheckBlenderKit; empty for none.
    virtual std::string_view setupItem() const = 0;
    // The source's official site, opened by the tab's link; empty for none. Never fetched by Plynth.
    virtual std::string_view site() const = 0;
    // One plain sentence the front ends show with the entry (a warning or a reminder); empty for none.
    virtual std::string_view note() const { return {}; }
    // Every saved option with its default value, in display order. An empty object for none.
    virtual Json defaultOptions() const = 0;
    // InvalidValue naming the option for an unknown key or a bad value; per import keys are refused for
    // OptionUse::Saved. Keys missing from options are fine.
    virtual Status checkOptions(const Json& options, OptionUse use) const = 0;
};

// Adds an integration. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerIntegration(std::unique_ptr<Integration> integration);

// Registers an integration while the program starts.
struct IntegrationRegistrar {
    explicit IntegrationRegistrar(std::unique_ptr<Integration> integration);
};

// The integration with this id. Null when there is none.
const Integration* findIntegration(std::string_view id);

// Every integration, by order, then id.
std::vector<const Integration*> allIntegrations();

// The integration that lists this importer id among its asset importers (material false) or material
// importers (material true). Null when none does.
const Integration* integrationForImporter(std::string_view importerId, bool material);

// True for source kinds of store items (kSourceBlenderKit, kSourceFabZip). Such items are never copied into
// packs or bundled; a pack stores their reference and each user gets their own copy.
bool isStoreSource(std::string_view sourceKind);

inline constexpr std::string_view kIntegrationsFileName = "integrations.json";
inline constexpr std::string_view kIntegrationsFormat = "plynth-integrations";
inline constexpr int kIntegrationsVersion = 1;

// library.root / kIntegrationsFileName.
std::filesystem::path integrationsFile(const LibraryPaths& library);

// The file: {"format": "plynth-integrations", "version": 1, "integrations": {"fab": {"quality": "high"}}}.
struct IntegrationOptionsRead {
    Status status; // BadFile with a JSON pointer for a broken file or an option its integration refuses;
                   // UnsupportedVersion when newer; CannotRead when it cannot be opened
    bool exists = false;
    Json saved = Json::object(); // the "integrations" object; entries of unknown integrations are kept as
                                 // they are, so a newer Plynth's options survive
};

// Reads integrations.json. A missing file is ok with exists false and nothing saved.
IntegrationOptionsRead readIntegrationOptions(const LibraryPaths& library);

// An integration's options: defaultOptions with its entry from saved on top. Saved keys that are not in
// defaultOptions (for example the old blender_window) are left out. saved is the whole "integrations"
// object (IntegrationOptionsRead::saved), not the one entry.
Json integrationOptions(const Integration& integration, const Json& saved);

// Saves changes to one integration's options: checked with OptionUse::Saved, merged into the saved entry and
// written through a temporary file and a rename. A value equal to the default is still saved. NotFound for an
// unknown integration, InvalidValue for a refused option (nothing written), the read's failure for a file that
// cannot be read, CannotWrite when it cannot be replaced, on Windows also while another process holds it open.
Status setIntegrationOptions(const LibraryPaths& library, std::string_view id, const Json& changes);

// Removes an integration's saved entry, so its defaults apply again. Ok without writing when none is saved.
// NotFound for an unknown integration; the file's failures as for setIntegrationOptions.
Status resetIntegrationOptions(const LibraryPaths& library, std::string_view id);

// The options an importer's plan gets (ImportSource::options): its integration's options with request on top,
// request checked with OptionUse::Import. An empty object, and request must be empty, for an importer that no
// integration lists. InvalidValue for a refused request option, readIntegrationOptions's failure for an
// integrations.json that cannot be used.
struct ImporterOptions {
    Status status;
    Json options = Json::object();
};
ImporterOptions optionsForImporter(const LibraryPaths& library, std::string_view importerId, bool material,
                                   const Json& request);

enum class IntegrationState {
    Ready,      // "ready"
    NeedsSetup, // "needs_setup": its setup item is missing or needs attention (BlenderKit not signed in)
    NotChecked, // "not_checked": it has a setup item and no report has it checked yet
};

std::string_view integrationStateName(IntegrationState state);

struct IntegrationStatus {
    IntegrationState state = IntegrationState::NotChecked;
    std::string detail; // the setup item's detail, or "Ready." for an integration without a setup item
    std::string action; // the setup item's action; empty when nothing is needed
    std::string link;   // the setup item's link
};

// The state from a setup report: Ready when there is no setup item or it is Found; NeedsSetup when it is
// Missing, TooOld or NeedsAttention; NotChecked when report is null or the item is NotChecked.
IntegrationStatus integrationStatus(const Integration& integration, const SetupReport* report);

// One entry as the CLI lists it ("library integrations"):
//   {"id": "fab", "name": "Fab", "description": "...", "order": 30, "takes_addresses": false,
//    "suffixes": [".zip"], "asset_importers": ["fab_zip"], "material_importers": ["fab_zip"],
//    "note": "", "site": "https://www.fab.com/", "state": "ready", "detail": "Ready.", "action": "", "link": "",
//    "options": {...}, "defaults": {...}}
// suffixes are the file endings of its importers, sorted, without repeats.
Json integrationToJson(const Integration& integration, const IntegrationStatus& status, const Json& options);

}
