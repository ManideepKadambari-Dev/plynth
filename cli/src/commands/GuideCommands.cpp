#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/About.h>
#include <plynth/core/HelpPages.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Resources.h>
#include <plynth/core/Samples.h>
#include <plynth/core/Settings.h>
#include <plynth/core/catalog/MaterialCatalog.h>
#include <plynth/core/catalog/SampleItems.h>

#include <memory>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct DocsOptions {
    std::string id;
    std::string text;
    std::size_t limit = 50;
};

struct SamplesOpenOptions {
    std::string id;
    std::string to;
};

struct SamplesAddOptions {
    std::string library;
    BlenderOption blender;
};

// A path as JSON text, or null when it is empty.
Json pathOrNull(const std::filesystem::path& path) {
    return path.empty() ? Json(nullptr) : Json(pathToUtf8(path));
}

// The docs folder, or a not_found answer in failed.
bool docsFolder(const std::string& command, std::filesystem::path& folder, Answer& failed) {
    folder = resourceFolder(ResourceKind::Docs);
    if (folder.empty()) {
        failed = failure(command, fail(ErrorCode::NotFound, "The help pages were not found next to the program."));
        return false;
    }
    return true;
}

// The samples folder, or a not_found answer in failed.
bool samplesFolder(const std::string& command, std::filesystem::path& folder, Answer& failed) {
    folder = resourceFolder(ResourceKind::Samples);
    if (folder.empty()) {
        failed = failure(command, fail(ErrorCode::NotFound, "The samples were not found next to the program."));
        return false;
    }
    return true;
}

// The version, the three licenses, the copyright line, the notices file and the license texts.
Answer aboutAnswer(const RunContext&) {
    const AboutInfo info = aboutInfo(resourceFolder(ResourceKind::Licenses));
    Json result = Json::object();
    result["version"] = info.version;
    result["copyright"] = info.copyright;
    result["source_license"] = info.sourceLicense;
    result["program_license"] = info.binaryLicense;
    result["sample_license"] = info.sampleLicense;
    result["notices_file"] = pathOrNull(info.noticesFile);
    Json files = Json::array();
    for (const std::filesystem::path& file : info.licenseFiles) files.push_back(pathToUtf8(file));
    result["license_files"] = std::move(files);
    Answer answer = success("about", std::move(result));
    if (!info.status.ok()) answer.warnings.push_back({"not_found", info.status.message});
    return answer;
}

// Every help page row with whether its file is there.
Answer docsList(const RunContext&) {
    const std::string command = "docs list";
    std::filesystem::path docs;
    Answer failed;
    if (!docsFolder(command, docs, failed)) return failed;
    Json pages = Json::array();
    for (const HelpPageEntry& page : helpPages()) {
        if (page.kind != HelpPageKind::File) continue;
        std::error_code ec;
        const bool found = std::filesystem::is_regular_file(docs / pathFromUtf8(page.file), ec);
        pages.push_back({{"id", page.id}, {"title", page.title}, {"file", page.file}, {"listed", page.listed}, {"found", found}});
    }
    Json result = Json::object();
    result["folder"] = pathToUtf8(docs);
    result["pages"] = std::move(pages);
    return success(command, std::move(result));
}

// The Markdown and the headings of one page.
Answer docsShow(const RunContext&, const DocsOptions& options) {
    const std::string command = "docs show";
    std::filesystem::path docs;
    Answer failed;
    if (!docsFolder(command, docs, failed)) return failed;
    const HelpPageRead read = readHelpPage(docs, options.id);
    if (!read.status.ok()) return failure(command, read.status);
    Json headings = Json::array();
    for (const HelpHeading& h : read.headings) {
        headings.push_back({{"text", h.text}, {"anchor", h.anchor}, {"level", h.level}, {"line", h.line}});
    }
    Json result = Json::object();
    result["id"] = options.id;
    result["markdown"] = read.markdown;
    result["headings"] = std::move(headings);
    return success(command, std::move(result));
}

// The places in the help pages that hold every word of the text.
Answer docsSearch(const RunContext&, const DocsOptions& options) {
    const std::string command = "docs search";
    std::filesystem::path docs;
    Answer failed;
    if (!docsFolder(command, docs, failed)) return failed;
    Json hits = Json::array();
    for (const HelpHit& hit : searchHelp(docs, options.text, options.limit)) {
        hits.push_back({{"page", hit.page}, {"title", hit.title}, {"heading", hit.heading}, {"anchor", hit.anchor},
                        {"snippet", hit.snippet}});
    }
    Json result = Json::object();
    result["text"] = options.text;
    result["count"] = hits.size();
    result["hits"] = std::move(hits);
    return success(command, std::move(result));
}

// The sample projects, models and materials and their license.
Answer samplesList(const RunContext&) {
    const std::string command = "samples list";
    std::filesystem::path samples;
    Answer failed;
    if (!samplesFolder(command, samples, failed)) return failed;
    const SamplesRead read = readSamples(samples);
    if (!read.status.ok()) return failure(command, read.status);
    Json projects = Json::array();
    for (const SampleProject& p : read.index.projects) {
        projects.push_back({{"id", p.id}, {"name", p.name}, {"description", p.description},
                            {"folder", pathToUtf8(samples / pathFromUtf8(p.folder))},
                            {"image", pathToUtf8(samples / pathFromUtf8(p.image))}});
    }
    Json assets = Json::array();
    for (const SampleAsset& a : read.index.assets) {
        assets.push_back({{"id", a.id}, {"name", a.name}, {"file", pathToUtf8(samples / pathFromUtf8(a.file))},
                          {"categories", a.categories}, {"tags", a.tags}});
    }
    Json materials = Json::array();
    for (const SampleMaterial& m : read.index.materials) {
        materials.push_back({{"id", m.id}, {"folder", pathToUtf8(samples / pathFromUtf8(m.folder))}});
    }
    Json result = Json::object();
    result["folder"] = pathToUtf8(samples);
    result["license"] = read.index.license;
    result["projects"] = std::move(projects);
    result["assets"] = std::move(assets);
    result["materials"] = std::move(materials);
    return success(command, std::move(result));
}

// Copies a sample project into the projects folder, or the folder --to names.
Answer samplesOpen(const RunContext& context, const SamplesOpenOptions& options) {
    const std::string command = "samples open";
    Answer failed;
    if (!checkIdArgument(command, options.id, failed)) return failed;
    std::filesystem::path samples;
    if (!samplesFolder(command, samples, failed)) return failed;
    std::filesystem::path into;
    if (!options.to.empty()) {
        into = context.currentFolder / pathFromUtf8(options.to);
    } else {
        const SettingsLoad loaded = loadSettings();
        if (!loaded.status.ok()) return failure(command, loaded.status);
        if (!loaded.settings.projects) {
            return badArguments(command, "bad_arguments", "No projects folder is set. Give --to or set one in the setup check.");
        }
        into = *loaded.settings.projects;
    }
    const SampleCopy copy = copySampleProject(samples, options.id, into.lexically_normal(), agentGuideMaster());
    if (!copy.status.ok()) return failure(command, copy.status);
    Json result = Json::object();
    result["id"] = options.id;
    result["project_folder"] = pathToUtf8(copy.projectFolder);
    return success(command, std::move(result));
}

// Imports the sample models and materials and copies the sample shapes and generators into the library.
Answer samplesAdd(const RunContext& context, const SamplesAddOptions& options) {
    const std::string command = "samples add";
    Answer failed;
    if (refuseEmptyBlender(command, options.blender, failed)) return failed;
    std::filesystem::path samples;
    if (!samplesFolder(command, samples, failed)) return failed;
    OpenedCatalog assets;
    if (!openCatalog(command, context, options.library, assets, failed)) return failed;
    OpenedMaterialCatalog materials;
    if (!openMaterialCatalog(command, context, options.library, materials, failed)) return failed;
    ImportContext importContext;
    if (!makeImportContext(command, context, assets, options.blender, importContext, failed)) return failed;
    const SampleItemsResult added = addSampleItems(*assets.catalog, *materials.catalog, importContext, samples, {},
                                                   stopToken(context));
    if (!added.status.ok()) return failure(command, added.status);
    Json addedList = Json::array();
    Json skippedList = Json::array();
    Json failedList = Json::array();
    for (const SampleItemResult& item : added.items) {
        if (!item.status.ok()) {
            failedList.push_back({{"kind", item.kind}, {"id", item.id}, {"code", std::string(errorCodeName(item.status.code))},
                                  {"message", item.status.message}});
        } else {
            (item.skipped ? skippedList : addedList).push_back({{"kind", item.kind}, {"id", item.id}});
        }
    }
    Json result = Json::object();
    result["added"] = std::move(addedList);
    result["skipped"] = std::move(skippedList);
    result["failed"] = std::move(failedList);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> guideCommands() {
    CommandSpec about;
    about.words = {"about"};
    about.description = "Show the version, the licenses of Plynth and its samples, and where the third party notices are.";
    about.setup = [](CLI::App&) -> Handler { return aboutAnswer; };

    CommandSpec list;
    list.words = {"docs", "list"};
    list.description = "List the help pages and whether each file is there.";
    list.setup = [](CLI::App&) -> Handler { return docsList; };

    CommandSpec show;
    show.words = {"docs", "show"};
    show.description = "Print one help page as Markdown, with its headings and their anchors.";
    show.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<DocsOptions>();
        sub.add_option("id", options->id, "Id of the page, as 'docs list' shows it.")->type_name("id")->required();
        return [options](const RunContext& context) { return docsShow(context, *options); };
    };

    CommandSpec search;
    search.words = {"docs", "search"};
    search.description = "Find every word of a text in the help pages, headings first.";
    search.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<DocsOptions>();
        sub.add_option("text", options->text, "The words to find.")->type_name("text")->required();
        sub.add_option("--limit", options->limit, "Show at most this many hits. Default: 50.")->type_name("number");
        return [options](const RunContext& context) { return docsSearch(context, *options); };
    };

    CommandSpec samplesListSpec;
    samplesListSpec.words = {"samples", "list"};
    samplesListSpec.description = "List the sample projects, models and materials that come with Plynth.";
    samplesListSpec.setup = [](CLI::App&) -> Handler { return samplesList; };

    CommandSpec open;
    open.words = {"samples", "open"};
    open.description = "Copy a sample project into the projects folder, or the folder --to names, so you can change it.";
    open.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SamplesOpenOptions>();
        sub.add_option("id", options->id, "Id of the sample project.")->type_name("id")->required();
        sub.add_option("--to", options->to, "The folder to make the copy in. Default: the projects folder.")->type_name("folder");
        return [options](const RunContext& context) { return samplesOpen(context, *options); };
    };

    CommandSpec add;
    add.words = {"samples", "add"};
    add.description = "Add the sample models and materials with license cc0 and the sample shapes and generators to the "
                      "library. Items it has already are skipped.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SamplesAddOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        return [options](const RunContext& context) { return samplesAdd(context, *options); };
    };

    return {about, list, show, search, samplesListSpec, open, add};
}

}
