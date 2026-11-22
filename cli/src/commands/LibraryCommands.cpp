#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/OpeningShapes.h>
#include <plynth/core/ProjectFiles.h>

#include <memory>

namespace plynth::cli {

namespace {

struct InitOptions {
    std::string folder;
    bool use = false;
};

struct InfoOptions {
    std::string library;
};

struct SampleShapesOptions {
    std::string library;
    std::string from;
};

struct RescanOptions {
    std::string library;
    std::string blender;
    bool noBlender = false;
};

// How many folders and files a folder holds directly. Zero when it does not exist.
int countEntries(const std::filesystem::path& folder) {
    std::error_code ignored;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
        const std::string name = pathToUtf8(entry.path().filename());
        if (name.empty() || name.front() == '.' || name.front() == '~') {
            continue;
        }
        ++count;
    }
    return count;
}

// The library entry shared by library init and library info.
Answer libraryInfoAnswer(const std::string& command, const OpenedCatalog& opened) {
    AssetQuery uncategorized;
    uncategorized.uncategorizedOnly = true;
    std::error_code ignored;
    const auto space = std::filesystem::space(opened.paths.root, ignored);

    Json assets = Json::object();
    assets["db"] = pathToUtf8(opened.paths.assetsDb);
    assets["items"] = opened.catalog->assetCount();
    assets["uncategorized"] = opened.catalog->search(uncategorized).size();
    assets["inbox_waiting"] = inboxEntries(opened.paths).size();
    Json materials = Json::object();
    materials["db"] = pathToUtf8(opened.paths.materialsDb);
    materials["items"] = countEntries(opened.paths.materialItems);
    Json shapes = Json::array();
    for (const OpeningShape* shape : allOpeningShapes()) {
        if (!shape->builtIn()) {
            shapes.push_back(std::string(shape->id()));
        }
    }
    Json result = Json::object();
    result["root"] = pathToUtf8(opened.paths.root);
    result["free_bytes"] = static_cast<unsigned long long>(space.available);
    result["assets"] = std::move(assets);
    result["materials"] = std::move(materials);
    result["shapes"] = std::move(shapes);
    return success(command, std::move(result));
}

// Makes a folder into a library, and with --use saves it as the library in the settings.
Answer libraryInit(const RunContext& context, const InitOptions& options) {
    const std::string command = "library init";
    const std::filesystem::path folder = userPath(context, options.folder);
    SettingsLoad loaded;
    if (options.use) {
        loaded = loadSettings();
        if (!loaded.status.ok()) {
            return failure(command, loaded.status);
        }
    }
    const Status made = initLibrary(folder);
    if (!made.ok()) {
        return failure(command, made);
    }
    if (options.use) {
        loaded.settings.library = folder;
        const Status saved = saveSettings(loaded.settings);
        if (!saved.ok()) {
            return failure(command, saved);
        }
    }
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, pathToUtf8(folder), opened, failed)) {
        return failed;
    }
    return libraryInfoAnswer(command, opened);
}

// Describes the library in use.
Answer libraryInfo(const RunContext& context, const InfoOptions& options) {
    const std::string command = "library info";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    return libraryInfoAnswer(command, opened);
}

// Rebuilds the asset index from the folders on disk.
Answer libraryRescan(const RunContext& context, const RescanOptions& options) {
    const std::string command = "library rescan";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    Json result = Json::object();
    result["assets"] = rescanAndRefresh(command, context, opened, options.blender, options.noBlender);
    return success(command, std::move(result));
}

// Copies the sample opening shapes into the library without replacing any file that is already there.
Answer libraryAddSampleShapes(const RunContext& context, const SampleShapesOptions& options) {
    const std::string command = "library add-sample-shapes";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    std::filesystem::path from;
    if (options.from.empty()) {
        from = sampleShapesFolder();
        if (from.empty()) {
            return failure(command, fail(ErrorCode::NotFound,
                                         "The sample shapes were not found. Give the folder with --from, or set "
                                         "PLYNTH_SAMPLES to the samples folder."));
        }
    } else {
        from = userPath(context, options.from);
    }
    const SampleShapesCopy copy = addSampleShapes(opened.paths, from);
    if (!copy.status.ok()) {
        return failure(command, copy.status);
    }
    Json result = Json::object();
    result["copied"] = copy.copied;
    result["skipped"] = copy.skipped;
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> libraryCommands() {
    CommandSpec init;
    init.words = {"library", "init"};
    init.description = "Make a folder into a library, with its catalogs and folders. Never removes anything.";
    init.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<InitOptions>();
        sub.add_option("folder", options->folder, "The library folder.")->type_name("folder")->required();
        sub.add_flag("--use", options->use, "Also save it as the library in the settings.");
        return [options](const RunContext& context) { return libraryInit(context, *options); };
    };

    CommandSpec info;
    info.words = {"library", "info"};
    info.description = "Describe the library: folder, free space, assets, materials and custom shapes.";
    info.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<InfoOptions>();
        addLibraryOption(sub, options->library);
        return [options](const RunContext& context) { return libraryInfo(context, *options); };
    };

    CommandSpec rescan;
    rescan.words = {"library", "rescan"};
    rescan.description = "Rebuild the asset index from the folders on disk and measure assets that changed.";
    rescan.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RescanOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--no-blender", options->noBlender, "List changed assets without measuring them again.");
        return [options](const RunContext& context) { return libraryRescan(context, *options); };
    };

    CommandSpec sampleShapes;
    sampleShapes.words = {"library", "add-sample-shapes"};
    sampleShapes.description =
        "Copy the sample opening shapes into the library. Files already there are left as they are.";
    sampleShapes.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SampleShapesOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("--from", options->from, "A folder holding the *.shape.json files.")->type_name("folder");
        return [options](const RunContext& context) { return libraryAddSampleShapes(context, *options); };
    };

    return {init, info, rescan, sampleShapes};
}

}
