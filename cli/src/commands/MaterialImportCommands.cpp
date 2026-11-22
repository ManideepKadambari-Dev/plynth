#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/MaterialImport.h>
#include <plynth/core/catalog/MaterialInfo.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct ImportOptions {
    std::string library;
    std::string blender;
    std::vector<std::string> paths;
    std::string id;
    std::string name;
    std::vector<std::string> groups;
    std::vector<std::string> tags;
    std::string license;
    std::optional<std::string> tile;
    std::string material;
    std::vector<std::string> maps;
    std::vector<std::string> values;
    std::string normal = "auto";
    int bakeSize = kDefaultBakeSize;
    bool keep = false;
    bool skipExisting = false;
    bool quiet = false;
    std::string importer;
    std::vector<std::string> optionPairs;
    bool dryRun = false;
};

// One thing to import: a file or folder, or a BlenderKit address. text is what the user typed.
struct ImportSourceArg {
    std::filesystem::path path;
    std::string url;
    std::string text;
};

struct InboxOptions {
    std::string library;
    std::string blender;
    bool quiet = false;
};

struct ThumbnailOptions {
    std::string library;
    std::string blender;
    std::string id;
    bool quiet = false;
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

// Picks the Blender and fills what the material imports and refreshes need. On error the answer for command
// is returned in failed (blender_not_found).
bool makeMaterialImportContext(const std::string& command, const RunContext& context,
                               const OpenedMaterialCatalog& opened, const std::string& blenderOption,
                               ImportContext& importContext, Answer& failed) {
    std::optional<std::filesystem::path> option;
    if (!blenderOption.empty()) {
        option = userPath(context, blenderOption);
    }
    const BlenderChoice choice = chooseBlender(option, opened.settings);
    if (!choice.status.ok()) {
        failed = failure(command, choice.status);
        return false;
    }
    importContext.library = opened.paths;
    importContext.blender = choice.install;
    importContext.settings = opened.settings;
    return true;
}

// Writes one progress line: {"progress": {"stage", "step", "index", "count", "message", "source"}}.
void writeProgress(std::ostream& out, const ImportProgress& progress, const std::string& source) {
    if (progress.stage == ImportStage::Blender && progress.blender.index < 1) {
        return;
    }
    Json inner = Json::object();
    inner["stage"] = std::string(importStageName(progress.stage));
    inner["step"] = progress.blender.step;
    inner["index"] = progress.blender.index;
    inner["count"] = progress.blender.count;
    inner["message"] = progress.blender.message;
    inner["source"] = source;
    Json line = Json::object();
    line["progress"] = std::move(inner);
    out << line.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    out.flush();
}

// The progress callback for one source, or an empty one when progress is off.
std::function<void(const ImportProgress&)> progressWriter(const RunContext& context, bool quiet,
                                                          const std::string& source) {
    if (quiet || context.progress == nullptr) {
        return {};
    }
    std::ostream* out = context.progress;
    return [out, source](const ImportProgress& progress) { writeProgress(*out, progress, source); };
}

// True when text has a * or ? that matches name, case blind.
bool wildcardMatches(const std::string& pattern, const std::string& name, std::size_t p = 0, std::size_t n = 0) {
    while (p < pattern.size()) {
        if (pattern[p] == '*') {
            for (std::size_t skip = n; skip <= name.size(); ++skip) {
                if (wildcardMatches(pattern, name, p + 1, skip)) {
                    return true;
                }
            }
            return false;
        }
        if (n >= name.size()) {
            return false;
        }
        const auto same = std::tolower(static_cast<unsigned char>(pattern[p])) ==
                          std::tolower(static_cast<unsigned char>(name[n]));
        if (pattern[p] != '?' && !same) {
            return false;
        }
        ++p;
        ++n;
    }
    return n == name.size();
}

// Expands the paths typed by the user. A path whose last part has * or ? becomes the sorted folders in it
// and the files some importer can read, so a README next to the materials is left out. Fails with not_found
// when a pattern matches nothing.
bool expandPaths(const std::string& command, const RunContext& context, const std::vector<std::string>& texts,
                 std::vector<std::filesystem::path>& paths, Answer& failed) {
    for (const std::string& text : texts) {
        const std::filesystem::path path = userPath(context, text);
        const std::string last = pathToUtf8(path.filename());
        if (last.find_first_of("*?") == std::string::npos) {
            paths.push_back(path);
            continue;
        }
        std::vector<std::filesystem::path> found;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path(), error)) {
            const std::string name = pathToUtf8(entry.path().filename());
            if (name.starts_with('.') || name.starts_with('~') || !wildcardMatches(last, name)) {
                continue;
            }
            ImportSource source;
            source.file = entry.path();
            if (entry.is_directory(error) || pickMaterialImporter(source) != nullptr) {
                found.push_back(entry.path());
            }
        }
        std::ranges::sort(found, [](const auto& a, const auto& b) { return pathToUtf8(a) < pathToUtf8(b); });
        if (found.empty()) {
            failed = failure(command, fail(ErrorCode::NotFound, "Nothing to import matches " + text + "."));
            return false;
        }
        paths.insert(paths.end(), found.begin(), found.end());
    }
    return true;
}

// Splits "channel=value" into its two halves. False when there is no "=" or a half is empty.
bool splitPair(const std::string& text, std::string& key, std::string& value) {
    const std::size_t equals = text.find('=');
    if (equals == std::string::npos || equals == 0 || equals + 1 == text.size()) {
        return false;
    }
    key = text.substr(0, equals);
    value = text.substr(equals + 1);
    return true;
}

// Reads --map and --value into the request. Fails with invalid_value for a bad channel, file or number.
bool readMapOptions(const std::string& command, const RunContext& context, const ImportOptions& options,
                    MaterialImportRequest& request, Answer& failed) {
    for (const std::string& text : options.maps) {
        std::string key;
        std::string file;
        const auto channel = splitPair(text, key, file) ? mapChannelFromName(key) : std::nullopt;
        if (!channel) {
            failed = failure(command, fail(ErrorCode::InvalidValue, "--map takes channel=file, for example "
                                                                    "base_color=wood.jpg. Got \"" + text + "\"."));
            return false;
        }
        request.maps.emplace_back(*channel, userPath(context, file));
    }
    for (const std::string& text : options.values) {
        std::string key;
        std::string number;
        const auto channel = splitPair(text, key, number) ? mapChannelFromName(key) : std::nullopt;
        double value = 0.0;
        const char* begin = number.data();
        const auto [end, error] = std::from_chars(begin, begin + number.size(), value);
        if (!channel || error != std::errc() || end != begin + number.size()) {
            failed = failure(command, fail(ErrorCode::InvalidValue, "--value takes channel=number, for example "
                                                                    "metallic=1. Got \"" + text + "\"."));
            return false;
        }
        request.values.emplace_back(*channel, value);
    }
    return true;
}

// Imports files, folders and zips in order. The first failure stops the run.
Answer materialsImport(const RunContext& context, const ImportOptions& options) {
    const std::string command = "materials import";
    if (!options.id.empty()) {
        Answer badId;
        if (!checkIdArgument(command, options.id, badId)) {
            return badId;
        }
    }
    MaterialImportRequest base;
    base.id = options.id;
    base.name = options.name;
    base.groups = options.groups;
    base.tags = options.tags;
    base.license = options.license;
    base.blendMaterial = options.material;
    base.bakeSize = options.bakeSize;
    base.skipExisting = options.skipExisting;
    if (options.keep) {
        base.keepOriginal = true;
    }
    if (options.tile) {
        TileSize tile;
        const Status parsed = parseTileSize(*options.tile, tile);
        if (!parsed.ok()) {
            return failure(command, parsed);
        }
        base.tileSize = tile;
    }
    const auto normal = normalConventionFromName(options.normal);
    if (!normal) {
        return failure(command, fail(ErrorCode::InvalidValue, "--normal is auto, opengl or directx."));
    }
    base.normal = *normal;
    Answer failed;
    if (!readMapOptions(command, context, options, base, failed)) {
        return failed;
    }
    base.importer = options.importer;
    if (!options.importer.empty() && findMaterialImporter(options.importer) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no importer called " + options.importer + "."));
    }
    if (!parseOptionPairs(command, options.optionPairs, base.importerOptions, failed)) {
        return failed;
    }
    std::vector<ImportSourceArg> sources;
    for (const std::string& text : options.paths) {
        if (isAddressArgument(context, text)) {
            sources.push_back({{}, text, text});
            continue;
        }
        std::vector<std::filesystem::path> paths;
        if (!expandPaths(command, context, {text}, paths, failed)) {
            return failed;
        }
        for (const std::filesystem::path& path : paths) {
            sources.push_back({path, {}, pathToUtf8(path.filename())});
        }
    }
    if (!options.id.empty() && sources.size() > 1) {
        return badArguments(command, "bad_arguments", "--id names one material, so it needs exactly one path.");
    }
    if (options.dryRun) {
        MachineSettings settings;
        LibraryPaths library;
        if (!chooseLibraryFolder(command, context, options.library, settings, library, failed)) {
            return failed;
        }
        Json previews = Json::array();
        for (const ImportSourceArg& source : sources) {
            MaterialImportRequest request = base;
            request.source.file = source.path;
            request.source.url = source.url;
            const ImportPreview preview = previewMaterialImport(library, request);
            if (!preview.status.ok()) {
                return failure(command, preview.status);
            }
            Json item = importPreviewToJson(preview);
            if (sources.size() > 1) {
                item["source"] = source.text;
            }
            previews.push_back(std::move(item));
        }
        if (previews.size() == 1) {
            return success(command, std::move(previews.at(0)));
        }
        Json result = Json::object();
        result["previews"] = std::move(previews);
        return success(command, std::move(result));
    }
    OpenedMaterialCatalog opened;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    ImportContext importContext;
    if (!makeMaterialImportContext(command, context, opened, options.blender, importContext, failed)) {
        return failed;
    }
    Json imported = Json::array();
    std::vector<AnswerWarning> warnings;
    for (const ImportSourceArg& source : sources) {
        MaterialImportRequest request = base;
        request.source.file = source.path;
        request.source.url = source.url;
        const std::string& name = source.text;
        if (options.importer.empty() && !source.url.empty() && pickMaterialImporter(request.source) == nullptr) {
            return failure(command, parseBlenderKitAddress(source.url).status);
        }
        const MaterialImportOutcome outcome = importMaterial(*opened.catalog, importContext, request,
                                                             progressWriter(context, options.quiet, name));
        if (!outcome.status.ok()) {
            std::string message = outcome.status.message;
            if (!imported.empty()) {
                message += " " + std::to_string(imported.size()) +
                           (imported.size() == 1 ? " material was" : " materials were") + " imported before it.";
            }
            Status status = outcome.status;
            status.message = message;
            return failure(command, status);
        }
        Json item = Json::object();
        item["id"] = outcome.materialId;
        item["importer"] = outcome.importer;
        item["skipped"] = outcome.skipped;
        item["warnings"] = warningsJson(outcome.warnings);
        item["thumbnail_failed"] = outcome.thumbnailFailed;
        item["original_removed"] = outcome.originalRemoved;
        imported.push_back(std::move(item));
        for (const BlenderWarning& warning : outcome.warnings) {
            warnings.push_back({warning.code, name + ": " + warning.message});
        }
    }
    Json result = Json::object();
    result["imported"] = std::move(imported);
    Answer answer = success(command, std::move(result));
    answer.warnings = std::move(warnings);
    return answer;
}

// Imports everything waiting in the library's materials inbox. Failures are listed, not refused.
Answer materialsInbox(const RunContext& context, const InboxOptions& options) {
    const std::string command = "materials inbox";
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const std::vector<std::filesystem::path> entries = materialInboxEntries(opened.paths);
    Json imported = Json::array();
    Json problems = Json::array();
    if (!entries.empty()) {
        ImportContext importContext;
        if (!makeMaterialImportContext(command, context, opened, options.blender, importContext, failed)) {
            return failed;
        }
        for (const std::filesystem::path& entry : entries) {
            const std::string name = pathToUtf8(entry.filename());
            const MaterialImportOutcome outcome = importMaterialInboxEntry(
                *opened.catalog, importContext, entry, progressWriter(context, options.quiet, name));
            if (outcome.status.ok()) {
                imported.push_back(outcome.materialId);
            } else {
                Json item = Json::object();
                item["entry"] = name;
                item["code"] = std::string(errorCodeName(outcome.status.code));
                item["message"] = outcome.status.message;
                problems.push_back(std::move(item));
            }
        }
    }
    Json result = Json::object();
    result["imported"] = std::move(imported);
    result["failed"] = std::move(problems);
    result["remaining"] = materialInboxEntries(opened.paths).size();
    return success(command, std::move(result));
}

// Cleans a material again and renders a new thumbnail.
Answer materialsThumbnail(const RunContext& context, const ThumbnailOptions& options) {
    const std::string command = "materials thumbnail";
    Answer badId;
    if (!checkIdArgument(command, options.id, badId)) {
        return badId;
    }
    OpenedMaterialCatalog opened;
    Answer failed;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    if (!opened.catalog->material(options.id)) {
        return failure(command, fail(ErrorCode::NotFound, "There is no material " + options.id + "."));
    }
    ImportContext importContext;
    if (!makeMaterialImportContext(command, context, opened, options.blender, importContext, failed)) {
        return failed;
    }
    const MaterialImportOutcome outcome = refreshMaterial(*opened.catalog, importContext, options.id,
                                                          progressWriter(context, options.quiet, options.id));
    if (!outcome.status.ok()) {
        return failure(command, outcome.status);
    }
    return success(command, materialEntry(*opened.catalog->material(options.id)));
}

// The material importers, with what each reads.
Answer materialsImporters() {
    Json list = Json::array();
    for (const MaterialImporter* importer : allMaterialImporters()) {
        Json item = Json::object();
        item["id"] = std::string(importer->id());
        item["name"] = std::string(importer->name());
        item["description"] = std::string(importer->description());
        item["suffixes"] = importer->fileSuffixes();
        list.push_back(std::move(item));
    }
    Json result = Json::object();
    result["importers"] = std::move(list);
    return success("materials importers", std::move(result));
}

}

std::vector<CommandSpec> materialImportCommands() {
    CommandSpec import;
    import.words = {"materials", "import"};
    import.description = "Import texture folders, zips, image files or .blend files as materials.";
    import.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ImportOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_option("path", options->paths,
                       "A folder, zip, image, .blend or BlenderKit address. A last part with * or ? is expanded.")
            ->type_name("path")
            ->required();
        sub.add_option("--id", options->id, "Id for the material. One path only. Default: material.json or the name.")
            ->type_name("id");
        sub.add_option("--name", options->name, "Name for the material.")->type_name("text");
        sub.add_option("--group", options->groups, "A group id. Repeatable.")->type_name("id");
        sub.add_option("--tag", options->tags, "A tag. Repeatable.")->type_name("tag");
        sub.add_option("--license", options->license, "License id.")->type_name("id");
        sub.add_option("--tile", options->tile, "Real world size of one tile in metres, 1.2x0.6 or 2.")
            ->type_name("WxH");
        sub.add_option("--material", options->material, "The material to take from a .blend file.")
            ->type_name("name");
        sub.add_option("--map", options->maps, "Use this file for a channel, channel=file. Repeatable.")
            ->type_name("channel=file");
        sub.add_option("--value", options->values, "Use a number from 0 to 1 for a channel, channel=number. Repeatable.")
            ->type_name("channel=number");
        sub.add_option("--normal", options->normal, "How the normal map is built. Default: auto.")
            ->type_name("auto|opengl|directx");
        sub.add_option("--bake-size", options->bakeSize, "Size in pixels of maps made from a .blend file.")
            ->type_name("pixels");
        sub.add_flag("--keep", options->keep, "Keep a zip after it was imported.");
        sub.add_flag("--skip-existing", options->skipExisting,
                     "Skip a material whose id is taken instead of making a new id.");
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        sub.add_option("--importer", options->importer, "Use this importer instead of the best match.")
            ->type_name("id");
        sub.add_option("--option", options->optionPairs,
                       "An option for the importer, key=value. A dotted key makes nested options. Repeatable.")
            ->type_name("key=value");
        sub.add_flag("--dry-run", options->dryRun, "Show the importer, options and plan. Imports nothing.");
        return [options](const RunContext& context) { return materialsImport(context, *options); };
    };

    CommandSpec inbox;
    inbox.words = {"materials", "inbox"};
    inbox.description = "Import everything waiting in the library's materials inbox folder.";
    inbox.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<InboxOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        return [options](const RunContext& context) { return materialsInbox(context, *options); };
    };

    CommandSpec thumbnail;
    thumbnail.words = {"materials", "thumbnail"};
    thumbnail.description = "Clean a material again and render a new thumbnail.";
    thumbnail.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ThumbnailOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_option("id", options->id, "Id of the material.")->type_name("id")->required();
        sub.add_flag("--quiet", options->quiet, "Write no progress lines to standard error.");
        return [options](const RunContext& context) { return materialsThumbnail(context, *options); };
    };

    CommandSpec importers;
    importers.words = {"materials", "importers"};
    importers.description = "List the ways a material can be imported.";
    importers.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return materialsImporters(); };
    };

    return {import, inbox, thumbnail, importers};
}

}
