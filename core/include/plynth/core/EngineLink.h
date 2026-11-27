#pragma once

#include "Blender.h"
#include "EditSession.h"
#include "Export.h"
#include "ExportGroups.h"
#include "Library.h"
#include "Project.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Engine links (DESIGN.md section 7.16, owner 2026-11-27). Export writes a plain FBX for any use; everything one
// game engine needs lives in its link. Links are registry entries: each is one file in core/src/links/engines/
// with an EngineLinkRegistrar at namespace scope, so adding Unity later never means editing existing code. The
// Unreal Engine link (UnrealLink.h) is the first.
//
// A link run, used by the app's link window, "plynth link <id> send" and the sync after a build:
//   1. find the editor: the link looks for a running editor (EngineLink::findEditors). None, or none whose
//      project is the stored target when one is stored and the run is a sync, ends the run with skipped set.
//   2. prepare: runExport (Export.h) with the link's export profile, force and write from the request, the
//      link's kit and gameplay switches, and outputFolder build/links/<id>/out. A blocked export (a check
//      failed, not forced) ends the run with QualityFailed and nothing is sent.
//   3. deliver: the link hands the prepared files to the editor (EngineLink::deliver).
//   4. record: the target is stored in export-options.json (writeExportOptions) after the first delivery, and
//      the report is written to build/links/<id>/link_report.json. A skipped run, and one refused before the
//      prepare step, write no report, so the last real one stays.
// While it runs it holds build/build.lock like an export. Nothing in plan.json or project.json changes.

inline constexpr std::string_view kLinksFolderName = "links";         // inside build/
inline constexpr std::string_view kLinkOutputFolder = "out";          // inside build/links/<id>/
inline constexpr std::string_view kLinkReportFileName = "link_report.json";
inline constexpr std::string_view kLinkReportFormat = "plynth-link-report";
inline constexpr int kLinkReportVersion = 1;

// projectFolder/build/links/<linkId>.
std::filesystem::path linkFolder(const std::filesystem::path& projectFolder, std::string_view linkId);

// One running editor a link can deliver into.
struct EngineEditor {
    std::string id;          // the link's own handle for it, for example the remote execution node id
    EngineTarget target;     // its project and engine version
    std::string user;        // who runs it, for the picker; empty when unknown
    std::string machine;

    bool operator==(const EngineEditor&) const = default;
};

struct EngineEditorSearch {
    Status status;           // Ok also when none was found; NetworkFailed when the search itself could not run
    std::vector<EngineEditor> editors;
};

// What happened to one asset in the editor. kind: "static_mesh", "texture", "material", "material_instance",
// "file". action: "created", "updated", "unchanged", "removed".
struct EngineAssetResult {
    std::string path; // the editor's path, for example "/Game/Plynth/My_Flat/Meshes/SM_My_Flat"
    std::string kind;
    std::string action;

    bool operator==(const EngineAssetResult&) const = default;
};

// What happened to one actor placed in the open level. action: "spawned", "kept", "removed".
struct EngineActorResult {
    std::string label;
    std::string asset;
    std::string action;

    bool operator==(const EngineActorResult&) const = default;
};

// What the link's deliver step is given.
struct EngineDelivery {
    std::filesystem::path projectFolder;
    Project project;
    EngineLinkOptions options;
    bool grouped = false;            // linkGrouped of the options and the project
    EngineEditor editor;
    const ExportReport* exported = nullptr; // the prepare step's report: files, textures, kit, gameplay
    std::filesystem::path workFolder;       // build/links/<id>, for the link's job and result files
    std::string jobId;
    bool selfCheck = false;                 // from the request
    std::filesystem::path screenshot;
};

// One check the editor side ran after a self check delivery. status "pass", "warning" or "fail".
struct EngineCheckResult {
    std::string id;
    std::string status;
    std::string message;

    bool operator==(const EngineCheckResult&) const = default;
};

enum class EngineLinkStage {
    Finding,    // "finding": looking for the editor
    Preparing,  // "preparing": the export with the link's profile; blender is filled during its Blender stage
    Delivering, // "delivering": the editor imports; message comes from the link
    Done,       // "done"
};

std::string_view engineLinkStageName(EngineLinkStage stage);

struct EngineLinkProgress {
    EngineLinkStage stage = EngineLinkStage::Finding;
    BlenderProgress blender;
    std::string message; // one plain sentence
};

// The report of one link run. Also the CLI's result and link_report.json (engineLinkReportToJson).
struct EngineLinkReport {
    // Ok when delivered, or when skipped. NotFound for no build ("Build the project first."), InUse when a build or
    // export holds the lock, QualityFailed when the prepare step was blocked, EngineNotFound when no editor was
    // found and the run was asked for by hand, EngineFailed with the editor's message, Cancelled, CannotWrite.
    Status status;
    std::string link;            // "unreal"
    std::string jobId;           // "link-3f9a07c1"
    SessionTime startedAt{};
    long long elapsedMs = 0;
    bool skipped = false;        // a sync that found no target editor; status is Ok
    std::string skippedReason;   // for example "Unreal Engine is not open, so the building was not updated."
    std::optional<EngineTarget> target;
    std::string contentPath;     // for example "/Game/Plynth/My_Flat"
    bool packMaterials = true;
    bool grouped = false;
    ExportReport exported;       // the prepare step
    std::vector<EngineAssetResult> assets;
    std::vector<EngineActorResult> actors;
    std::vector<EngineCheckResult> checks; // only after a self check
    std::filesystem::path screenshot;      // only after a self check that asked for one
    std::filesystem::path reportFile; // build/links/<id>/link_report.json, absolute
    std::filesystem::path logFile;    // the editor's output of this run, build/links/<id>/editor.log
};

class EngineLink {
public:
    virtual ~EngineLink() = default;

    // Stable lower case id, its key in export-options.json "links", for example "unreal".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Unreal Engine".
    virtual std::string_view name() const = 0;
    // One plain sentence on what it does.
    virtual std::string_view description() const = 0;
    // The export profile its prepare step uses (ExportJob.h).
    virtual ExportProfile profile() const = 0;
    // Looks for running editors, waiting at most wait. Blocks.
    virtual EngineEditorSearch findEditors(std::chrono::milliseconds wait, std::stop_token stop) const = 0;
    // Hands the prepared files to the editor and fills assets, actors, contentPath and logFile of the report.
    // EngineFailed with the editor's message when the editor refused or failed. Blocks.
    virtual Status deliver(const EngineDelivery& delivery, EngineLinkReport& report,
                           const std::function<void(const EngineLinkProgress&)>& onProgress,
                           std::stop_token stop) const = 0;
};

// Adds a link. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerEngineLink(std::unique_ptr<EngineLink> link);

// Registers a link while the program starts.
struct EngineLinkRegistrar {
    explicit EngineLinkRegistrar(std::unique_ptr<EngineLink> link);
};

// The link with this id. Null when there is none.
const EngineLink* findEngineLink(std::string_view id);

// Every link, sorted by id.
std::vector<const EngineLink*> allEngineLinks();

// How long a sync after a build waits for an editor to answer before it counts as not open.
inline constexpr std::chrono::milliseconds kSyncEditorWait{1500};
// How long a run by hand waits.
inline constexpr std::chrono::milliseconds kSendEditorWait{3000};

struct EngineLinkRequest {
    std::filesystem::path projectFolder; // must hold build/building.blend
    Project project;                     // as in memory, saved or not
    std::string link;                    // the link id
    BlenderInstall blender;              // for the prepare step
    BlenderRunOptions run{true, kBuildTimeout, {}};
    std::optional<LibraryPaths> library; // for the kit, as ExportRequest::library
    ExportSettings settings;             // force and write; profile is replaced by the link's
    // The editor to deliver into, by EngineEditor::id. Empty: the stored target's editor when it is open, else
    // the only editor found; with several and no stored target, EngineNotFound naming them so the front end can
    // ask which one.
    std::string editor;
    bool sync = false;                   // a sync after a build: skip quietly when the target editor is not open
    // For the headless test ("plynth link <id> send --self-check [--screenshot <file>]"): the editor side checks
    // the import after it (UnrealLink.h UnrealCheck) and saves a picture of the level. Off for users.
    bool selfCheck = false;
    std::filesystem::path screenshot;
};

// Runs one link as the header says. Blocks; the app runs it on a worker thread and passes stop for Cancel.
EngineLinkReport runEngineLink(const EngineLinkRequest& request,
                               const std::function<void(const EngineLinkProgress&)>& onProgress = {},
                               std::stop_token stop = {});

// The sync the app and "plynth build" run after every successful build: runEngineLink with sync set, for every
// link whose options have syncAfterBuild and a target. Links without a target are not run. One report per link
// that was run, in link id order.
std::vector<EngineLinkReport> syncEngineLinks(const EngineLinkRequest& base,
                                              const std::function<void(const EngineLinkProgress&)>& onProgress = {},
                                              std::stop_token stop = {});

// Forgets a link's target ("Unlink"), so later builds no longer update that editor. The assets already in the
// editor are left as they are. Ok when there was none.
Status unlinkEngine(const std::filesystem::path& projectFolder, std::string_view linkId);

// The report in the form of schema/link-report.schema.json. Paths are absolute, UTF-8, forward slashes.
Json engineLinkReportToJson(const EngineLinkReport& report);

struct EngineLinkReportRead {
    Status status; // NotFound when there is none, BadFile when it is not a valid report
    EngineLinkReport report;
};

// Reads build/links/<linkId>/link_report.json, for the app when it opens a project and "plynth link <id> report".
// Of the prepare step's report it gives back the outcome, job id, profile, files, textures and log, not the checks.
EngineLinkReportRead readEngineLinkReport(const std::filesystem::path& projectFolder, std::string_view linkId);

}
