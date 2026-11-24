#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/SetupCheck.h>
#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/Integrations.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct ImportOptions {
    std::string library;
    BlenderOption blender;
    std::string path;
    std::string id;
    std::string name;
    std::vector<std::string> categories;
    std::vector<std::string> tags;
    std::string license;
    double scale = 1.0;
    int rotate = 0;
    bool keepOriginal = false;
    std::string importer;
    std::vector<std::string> optionPairs;
    bool dryRun = false;
};

struct IntegrationsOptions {
    std::string library;
    BlenderOption blender;
    bool noAddons = false;
};

struct IntegrationOptions {
    std::string library;
    std::string id;
    std::vector<std::string> optionPairs;
};

struct InboxOptions {
    std::string library;
    BlenderOption blender;
};

struct RescanOptions {
    std::string library;
    BlenderOption blender;
    bool noBlender = false;
};

struct ThumbnailOptions {
    std::string library;
    BlenderOption blender;
    std::string id;
};

// The warnings of an import as the answer lists them.
Json warningsJson(const std::vector<BlenderWarning>& warnings) {
    Json list = Json::array();
    for (const BlenderWarning& warning : warnings) {
        Json item = Json::object();
        item["code"] = warning.code;
        item["message"] = warning.message;
        list.push_back(std::move(item));
    }
    return list;
}

// Imports one file, folder, zip or address, or with --dry-run shows what that would do.
Answer assetsImport(const RunContext& context, const ImportOptions& options) {
    const std::string command = "assets import";
    Answer failed;
    if (refuseEmptyBlender(command, options.blender, failed)) {
        return failed;
    }
    if (!options.id.empty()) {
        Answer badId;
        if (!checkIdArgument(command, options.id, badId)) {
            return badId;
        }
    }
    if (options.scale <= 0.0 || options.scale != options.scale) {
        return failure(command, fail(ErrorCode::InvalidValue, "The scale must be above 0."));
    }
    if (options.rotate != 0 && options.rotate != 90 && options.rotate != 180 && options.rotate != 270) {
        return failure(command, fail(ErrorCode::InvalidValue, "The rotation must be 0, 90, 180 or 270 degrees."));
    }
    Json importerOptions;
    if (!parseOptionPairs(command, options.optionPairs, importerOptions, failed)) {
        return failed;
    }
    const bool address = isAddressArgument(context, options.path);
    ImportRequest request;
    if (address) {
        request.source.url = options.path;
    } else {
        request.source.file = userPath(context, options.path);
    }
    request.importer = options.importer;
    request.importerOptions = importerOptions;
    request.id = options.id;
    request.name = options.name;
    request.categories = options.categories;
    request.tags = options.tags;
    request.license = options.license;
    request.scale = options.scale;
    request.rotateZ = options.rotate;
    if (options.keepOriginal) {
        request.keepOriginal = true;
    }
    if (!options.importer.empty() && findImporter(options.importer) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no importer called " + options.importer + "."));
    }
    if (options.dryRun) {
        MachineSettings settings;
        LibraryPaths paths;
        if (!chooseLibraryFolder(command, context, options.library, settings, paths, failed)) {
            return failed;
        }
        const ImportPreview preview = previewAssetImport(paths, request);
        if (!preview.status.ok()) {
            return failure(command, preview.status);
        }
        return success(command, importPreviewToJson(preview));
    }
    OpenedCatalog opened;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    if (options.importer.empty() && !pickImporter(request.source)) {
        if (address) {
            return failure(command, parseBlenderKitAddress(options.path).status);
        }
        std::string endings;
        for (const std::string& suffix : importableSuffixes()) {
            endings += (endings.empty() ? "" : ", ") + suffix;
        }
        return failure(command, fail(ErrorCode::InvalidValue, "Plynth cannot import " + options.path +
                                                                  ". It reads " + endings + " files and folders "
                                                                  "that hold one."));
    }
    ImportContext importContext;
    if (!makeImportContext(command, context, opened, options.blender, importContext, failed)) {
        return failed;
    }
    const ImportOutcome outcome = importAsset(*opened.catalog, importContext, request, {}, stopToken(context));
    if (!outcome.status.ok()) {
        return failure(command, outcome.status);
    }
    Json result = Json::object();
    const auto record = opened.catalog->asset(outcome.assetId);
    result["asset"] = record ? assetEntry(*record) : Json(nullptr);
    result["importer"] = outcome.importer;
    result["thumbnail_failed"] = outcome.thumbnailFailed;
    result["original_removed"] = outcome.originalRemoved;
    Answer answer = success(command, std::move(result));
    for (const BlenderWarning& warning : outcome.warnings) {
        answer.warnings.push_back({warning.code, warning.message});
    }
    return answer;
}

// Imports everything waiting in the library's inbox. Failures are listed, not refused.
Answer assetsInbox(const RunContext& context, const InboxOptions& options) {
    const std::string command = "assets inbox";
    OpenedCatalog opened;
    Answer failed;
    if (refuseEmptyBlender(command, options.blender, failed)) {
        return failed;
    }
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const std::vector<std::filesystem::path> entries = inboxEntries(opened.paths);
    Json imported = Json::array();
    Json problems = Json::array();
    if (!entries.empty()) {
        ImportContext importContext;
        if (!makeImportContext(command, context, opened, options.blender, importContext, failed)) {
            return failed;
        }
        for (const std::filesystem::path& entry : entries) {
            const ImportOutcome outcome = importInboxEntry(*opened.catalog, importContext, entry, {}, stopToken(context));
            // Ctrl+C ends the whole run, not only this entry.
            if (outcome.status.code == ErrorCode::Cancelled) {
                return failure(command, outcome.status);
            }
            if (outcome.status.ok()) {
                imported.push_back(outcome.assetId);
            } else {
                Json item = Json::object();
                item["entry"] = pathToUtf8(entry.filename());
                item["code"] = std::string(errorCodeName(outcome.status.code));
                item["message"] = outcome.status.message;
                problems.push_back(std::move(item));
            }
        }
    }
    Json result = Json::object();
    result["imported"] = std::move(imported);
    result["failed"] = std::move(problems);
    result["remaining"] = inboxEntries(opened.paths).size();
    return success(command, std::move(result));
}

// Rebuilds the index from disk and measures the assets that changed.
Answer assetsRescan(const RunContext& context, const RescanOptions& options) {
    const std::string command = "assets rescan";
    OpenedCatalog opened;
    Answer failed;
    if (refuseEmptyBlender(command, options.blender, failed)) {
        return failed;
    }
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    return success(command, rescanAndRefresh(command, context, opened, options.blender, options.noBlender));
}

// Measures an asset again and renders a new thumbnail.
Answer assetsThumbnail(const RunContext& context, const ThumbnailOptions& options) {
    const std::string command = "assets thumbnail";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    OpenedCatalog opened;
    Answer failed;
    if (refuseEmptyBlender(command, options.blender, failed)) {
        return failed;
    }
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    if (!opened.catalog->asset(options.id)) {
        return failure(command, fail(ErrorCode::NotFound, "There is no asset " + options.id + "."));
    }
    ImportContext importContext;
    if (!makeImportContext(command, context, opened, options.blender, importContext, failed)) {
        return failed;
    }
    const ImportOutcome outcome = refreshAsset(*opened.catalog, importContext, options.id, {}, stopToken(context));
    if (!outcome.status.ok()) {
        return failure(command, outcome.status);
    }
    return success(command, assetEntry(*opened.catalog->asset(options.id)));
}

// The importers, with what each reads.
Answer assetsImporters() {
    Json list = Json::array();
    for (const Importer* importer : allImporters()) {
        Json item = Json::object();
        item["id"] = std::string(importer->id());
        item["name"] = std::string(importer->name());
        item["description"] = std::string(importer->description());
        item["suffixes"] = importer->fileSuffixes();
        list.push_back(std::move(item));
    }
    Json result = Json::object();
    result["importers"] = std::move(list);
    return success("assets importers", std::move(result));
}

// The library of an integration command. found is false, and failed stays empty, when none is set and
// allowNone is true.
bool integrationLibrary(const std::string& command, const RunContext& context, const std::string& libraryOption,
                        bool allowNone, MachineSettings& settings, LibraryPaths& paths, bool& found,
                        Answer& failed) {
    found = chooseLibraryFolder(command, context, libraryOption, settings, paths, failed);
    if (found) {
        return true;
    }
    if (allowNone && libraryOption.empty() && failed.code == "no_library") {
        failed = Answer();
        return true;
    }
    return false;
}

// The saved options of the library, or none when there is no library. A broken file is refused.
bool savedOptions(const std::string& command, const LibraryPaths& paths, bool found, Json& saved, Answer& failed) {
    saved = Json::object();
    if (!found) {
        return true;
    }
    const IntegrationOptionsRead read = readIntegrationOptions(paths);
    if (!read.status.ok()) {
        failed = failure(command, read.status);
        return false;
    }
    saved = read.saved;
    return true;
}

// One integration's entry: its state from the report and its options with the saved ones on top.
Json integrationEntry(const Integration& integration, const SetupReport& report, const Json& saved) {
    return integrationToJson(integration, integrationStatus(integration, &report),
                             integrationOptions(integration, saved));
}

// The integration with this id, or null and a not_found answer.
const Integration* integrationOrFail(const std::string& command, const std::string& id, Answer& failed) {
    const Integration* integration = findIntegration(id);
    if (integration == nullptr) {
        failed = failure(command, fail(ErrorCode::NotFound, "There is no integration called " + id + "."));
    }
    return integration;
}

// The entry of one integration for show, set and reset. No add-on probe runs.
Answer integrationAnswer(const std::string& command, const Integration& integration, const LibraryPaths& paths,
                         bool found, const MachineSettings& settings) {
    Json saved;
    Answer failed;
    if (!savedOptions(command, paths, found, saved, failed)) {
        return failed;
    }
    return success(command, integrationEntry(integration, quickSetupCheck(settings, true), saved));
}

// Lists the integrations in order with their state and options.
Answer libraryIntegrations(const RunContext& context, const IntegrationsOptions& options) {
    const std::string command = "library integrations";
    MachineSettings settings;
    LibraryPaths paths;
    bool found = false;
    Answer failed;
    if (!integrationLibrary(command, context, options.library, true, settings, paths, found, failed)) {
        return failed;
    }
    Json saved;
    if (!savedOptions(command, paths, found, saved, failed)) {
        return failed;
    }
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    SetupCheckOptions check;
    check.blender = blenderOptionPath(context, options.blender);
    check.probeAddons = !options.noAddons;
    check.settingsExist = loaded.exists;
    const SetupReport report = runSetupCheck(loaded.settings, check);
    Json list = Json::array();
    for (const Integration* integration : allIntegrations()) {
        list.push_back(integrationEntry(*integration, report, saved));
    }
    Json result = Json::object();
    result["integrations"] = std::move(list);
    return success(command, std::move(result));
}

// Shows one integration.
Answer integrationShow(const RunContext& context, const IntegrationOptions& options) {
    const std::string command = "library integration show";
    Answer failed;
    const Integration* integration = integrationOrFail(command, options.id, failed);
    if (integration == nullptr) {
        return failed;
    }
    MachineSettings settings;
    LibraryPaths paths;
    bool found = false;
    if (!integrationLibrary(command, context, options.library, true, settings, paths, found, failed)) {
        return failed;
    }
    return integrationAnswer(command, *integration, paths, found, settings);
}

// Saves options for one integration.
Answer integrationSet(const RunContext& context, const IntegrationOptions& options) {
    const std::string command = "library integration set";
    Answer failed;
    const Integration* integration = integrationOrFail(command, options.id, failed);
    if (integration == nullptr) {
        return failed;
    }
    Json changes;
    if (!parseOptionPairs(command, options.optionPairs, changes, failed)) {
        return failed;
    }
    MachineSettings settings;
    LibraryPaths paths;
    bool found = false;
    if (!integrationLibrary(command, context, options.library, false, settings, paths, found, failed)) {
        return failed;
    }
    const Status status = setIntegrationOptions(paths, options.id, changes);
    if (!status.ok()) {
        return failure(command, status);
    }
    return integrationAnswer(command, *integration, paths, true, settings);
}

// Removes the saved options of one integration.
Answer integrationReset(const RunContext& context, const IntegrationOptions& options) {
    const std::string command = "library integration reset";
    Answer failed;
    const Integration* integration = integrationOrFail(command, options.id, failed);
    if (integration == nullptr) {
        return failed;
    }
    MachineSettings settings;
    LibraryPaths paths;
    bool found = false;
    if (!integrationLibrary(command, context, options.library, false, settings, paths, found, failed)) {
        return failed;
    }
    const Status status = resetIntegrationOptions(paths, options.id);
    if (!status.ok()) {
        return failure(command, status);
    }
    return integrationAnswer(command, *integration, paths, true, settings);
}

}

std::vector<CommandSpec> importCommands() {
    CommandSpec import;
    import.words = {"assets", "import"};
    import.description = "Import a model file, a zip, a folder or a BlenderKit address into the library.";
    import.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ImportOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_option("path", options->path, "The file, zip or folder to import, or a BlenderKit address.")->type_name("path")->required();
        sub.add_option("--id", options->id, "Id for the asset. Default: made from the file name.")->type_name("id");
        sub.add_option("--name", options->name, "Name for the asset. Default: made from the file name.")
            ->type_name("text");
        sub.add_option("--category", options->categories, "A category id. Repeatable.")->type_name("id");
        sub.add_option("--tag", options->tags, "A tag. Repeatable.")->type_name("tag");
        sub.add_option("--license", options->license, "License id. Default: unknown.")->type_name("id");
        sub.add_option("--scale", options->scale, "Multiply the size of the model. Default: 1.")->type_name("number");
        sub.add_option("--rotate", options->rotate, "Turn the model around the vertical axis. Default: 0.")
            ->type_name("0|90|180|270");
        sub.add_flag("--keep-original", options->keepOriginal, "Keep a zip after it was imported.");
        sub.add_option("--importer", options->importer, "Use this importer instead of the best match.")
            ->type_name("id");
        sub.add_option("--option", options->optionPairs,
                       "An option for the importer, key=value. A dotted key makes nested options. Repeatable.")
            ->type_name("key=value");
        sub.add_flag("--dry-run", options->dryRun, "Show the importer, options and plan. Imports nothing.");
        return [options](const RunContext& context) { return assetsImport(context, *options); };
    };

    CommandSpec inbox;
    inbox.words = {"assets", "inbox"};
    inbox.description = "Import everything waiting in the library's inbox folder.";
    inbox.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<InboxOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        return [options](const RunContext& context) { return assetsInbox(context, *options); };
    };

    CommandSpec rescan;
    rescan.words = {"assets", "rescan"};
    rescan.description = "Rebuild the asset index from the folders on disk and measure assets that changed.";
    rescan.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RescanOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--no-blender", options->noBlender, "List changed assets without measuring them again.");
        return [options](const RunContext& context) { return assetsRescan(context, *options); };
    };

    CommandSpec thumbnail;
    thumbnail.words = {"assets", "thumbnail"};
    thumbnail.description = "Measure an asset again and render a new thumbnail.";
    thumbnail.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ThumbnailOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_option("id", options->id, "Id of the asset.")->type_name("id")->required();
        return [options](const RunContext& context) { return assetsThumbnail(context, *options); };
    };

    CommandSpec importers;
    importers.words = {"assets", "importers"};
    importers.description = "List the ways an asset can be imported.";
    importers.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return assetsImporters(); };
    };

    CommandSpec integrations;
    integrations.words = {"library", "integrations"};
    integrations.description = "List the sources assets and materials come from, with their state and options.";
    integrations.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IntegrationsOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--no-addons", options->noAddons, "Do not ask Blender about its add-ons.");
        return [options](const RunContext& context) { return libraryIntegrations(context, *options); };
    };

    CommandSpec show;
    show.words = {"library", "integration", "show"};
    show.description = "Show one integration with its options.";
    show.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IntegrationOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the integration.")->type_name("id")->required();
        return [options](const RunContext& context) { return integrationShow(context, *options); };
    };

    CommandSpec set;
    set.words = {"library", "integration", "set"};
    set.description = "Save options for an integration. They apply to every import from it.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IntegrationOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the integration.")->type_name("id")->required();
        sub.add_option("--option", options->optionPairs, "An option to save, key=value. Repeatable.")
            ->type_name("key=value")
            ->required();
        return [options](const RunContext& context) { return integrationSet(context, *options); };
    };

    CommandSpec reset;
    reset.words = {"library", "integration", "reset"};
    reset.description = "Remove the saved options of an integration, so its defaults apply.";
    reset.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<IntegrationOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the integration.")->type_name("id")->required();
        return [options](const RunContext& context) { return integrationReset(context, *options); };
    };

    return {import, inbox, rescan, thumbnail, importers, integrations, show, set, reset};
}

}
