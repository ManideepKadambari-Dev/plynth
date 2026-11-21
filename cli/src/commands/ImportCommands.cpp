#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/Importer.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct ImportOptions {
    std::string library;
    std::string blender;
    std::string path;
    std::string id;
    std::string name;
    std::vector<std::string> categories;
    std::vector<std::string> tags;
    std::string license;
    double scale = 1.0;
    int rotate = 0;
    bool keepOriginal = false;
};

struct InboxOptions {
    std::string library;
    std::string blender;
};

struct RescanOptions {
    std::string library;
    std::string blender;
    bool noBlender = false;
};

struct ThumbnailOptions {
    std::string library;
    std::string blender;
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

// Imports one file, folder or zip.
Answer assetsImport(const RunContext& context, const ImportOptions& options) {
    const std::string command = "assets import";
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
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    ImportRequest request;
    request.source.file = userPath(context, options.path);
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
    if (!pickImporter(request.source)) {
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
    const ImportOutcome outcome = importAsset(*opened.catalog, importContext, request);
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
            const ImportOutcome outcome = importInboxEntry(*opened.catalog, importContext, entry);
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
    const ImportOutcome outcome = refreshAsset(*opened.catalog, importContext, options.id);
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

}

std::vector<CommandSpec> importCommands() {
    CommandSpec import;
    import.words = {"assets", "import"};
    import.description = "Import a model file, a zip or a folder into the library.";
    import.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ImportOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_option("path", options->path, "The file, zip or folder to import.")->type_name("path")->required();
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

    return {import, inbox, rescan, thumbnail, importers};
}

}
