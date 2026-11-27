#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/BuildCheck.h>
#include <plynth/core/FinishSettings.h>
#include <plynth/core/VariationCopies.h>
#include <plynth/core/commands/FinishCommands.h>

#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct ShowOptions {
    std::string project;
};

struct SetOptions {
    std::string project;
    std::string seed;
    std::string furniture;
    std::string materials;
};

struct PreviewOptions {
    std::string project;
    std::string library;
    std::string seed;
};

struct CopiesOptions {
    std::string project;
    std::string seeds;
    std::string out;
};

// Reads a seed: a whole number from 0 to 4294967295. Empty when the text is anything else.
std::optional<std::uint32_t> parseSeed(const std::string& text) {
    unsigned long long value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [stop, error] = std::from_chars(begin, end, value);
    if (text.empty() || error != std::errc() || stop != end || value > (std::numeric_limits<std::uint32_t>::max)()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

// The refusal for a seed that is not a whole number in range.
Answer badSeed(const std::string& command, const std::string& text) {
    return failure(command, fail(ErrorCode::InvalidValue,
                                 "The seed '" + text + "' is not a whole number from 0 to 4294967295."));
}

// The variation answer: the seed and the two switches.
Json variationJson(const VariationSettings& variation) {
    Json result = Json::object();
    result["seed"] = variation.seed;
    result["furniture"] = variation.furniture;
    result["materials"] = variation.materials;
    return result;
}

// Shows the variation settings of the project.
Answer variationShow(const RunContext& context, const ShowOptions& options) {
    const std::string command = "variation show";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    return success(command, variationJson(document.project().settings.variation));
}

// Changes the seed or the switches of the variation. What is left out stays.
Answer variationSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "variation set";
    if (options.seed.empty() && options.furniture.empty() && options.materials.empty()) {
        return badArguments(command, "bad_arguments", "Give at least one of --seed, --furniture and --materials.");
    }
    std::optional<std::uint32_t> seed;
    if (!options.seed.empty()) {
        seed = parseSeed(options.seed);
        if (!seed) {
            return badSeed(command, options.seed);
        }
    }
    return editProject(command, context, options.project, [&](Document& document) {
        VariationSettings variation = document.project().settings.variation;
        variation.seed = seed.value_or(variation.seed);
        if (!options.furniture.empty()) {
            variation.furniture = options.furniture == "on";
        }
        if (!options.materials.empty()) {
            variation.materials = options.materials == "on";
        }
        Answer failed;
        if (!runCommand(command, document, std::make_unique<SetVariationCommand>(variation), failed)) {
            return failed;
        }
        return success(command, variationJson(document.project().settings.variation));
    });
}

// Shows what a build would make of the plan with a seed, from the library's choices, as the build resolves it.
// Writes nothing.
Answer variationPreview(const RunContext& context, const PreviewOptions& options) {
    const std::string command = "variation preview";
    std::optional<std::uint32_t> seed;
    if (!options.seed.empty()) {
        seed = parseSeed(options.seed);
        if (!seed) {
            return badSeed(command, options.seed);
        }
    }
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    // The library is opened the way build opens it: a missing or broken one gives no choices, unless --library
    // named it.
    OpenedCatalog opened;
    const bool haveLibrary = openCatalog(command, context, options.library, opened, failed);
    if (!haveLibrary && !options.library.empty()) {
        return failed;
    }
    const std::optional<LibraryPaths> library =
        haveLibrary ? std::optional<LibraryPaths>(opened.paths) : std::nullopt;
    return success(command, variationPreviewToJson(previewVariation(document.project(), seed, library)));
}

// Saves a copy of the project for each seed, in its own folder under --out.
Answer variationCopies(const RunContext& context, const CopiesOptions& options) {
    const std::string command = "variation copies";
    const SeedListRead read = parseSeedList(options.seeds);
    if (!read.status.ok()) {
        return failure(command, read.status);
    }
    const std::vector<std::uint32_t>& seeds = read.seeds;
    const std::filesystem::path folder = resolveProjectFolder(context, options.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const VariationCopies made = makeVariationCopies(document.project(), folder, seeds, userPath(context, options.out));
    if (!made.status.ok()) {
        return failure(command, made.status);
    }
    Json folders = Json::array();
    for (std::size_t i = 0; i < made.folders.size(); ++i) {
        Json entry = Json::object();
        entry["seed"] = seeds[i];
        entry["folder"] = pathToUtf8(made.folders[i]);
        folders.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["folders"] = std::move(folders);
    return success(command, std::move(result));
}

}

std::vector<CommandSpec> variationCommands() {
    CommandSpec show;
    show.words = {"variation", "show"};
    show.description = "Show the variation seed and what it may change.";
    show.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ShowOptions>();
        addProjectOption(sub, options->project);
        return [options](const RunContext& context) { return variationShow(context, *options); };
    };

    CommandSpec set;
    set.words = {"variation", "set"};
    set.description = "Change the variation seed, or whether it may change furniture and materials.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--seed", options->seed, "The seed, a whole number. 0 builds the plan exactly as marked.")
            ->type_name("number");
        sub.add_option("--furniture", options->furniture, "Whether the seed may swap furniture.")
            ->type_name("on|off")
            ->check(CLI::IsMember({"on", "off"}));
        sub.add_option("--materials", options->materials, "Whether the seed may swap materials.")
            ->type_name("on|off")
            ->check(CLI::IsMember({"on", "off"}));
        return [options](const RunContext& context) { return variationSet(context, *options); };
    };

    CommandSpec preview;
    preview.words = {"variation", "preview"};
    preview.description = "Show what a build would change with a seed: furniture and materials, the doors and "
                          "windows it picks and the props in every room. Writes nothing.";
    preview.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<PreviewOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("--seed", options->seed, "The seed to try. Default: the project's seed.")->type_name("number");
        return [options](const RunContext& context) { return variationPreview(context, *options); };
    };

    CommandSpec copies;
    copies.words = {"variation", "copies"};
    copies.description = "Save a copy of the project for each seed, each in its own folder.";
    copies.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<CopiesOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--seeds", options->seeds, "The seeds, separated by commas, with ranges such as 1-5, for example 1-5,9.")
            ->type_name("list")
            ->required();
        sub.add_option("--out", options->out, "The folder the copies go in.")->type_name("folder")->required();
        return [options](const RunContext& context) { return variationCopies(context, *options); };
    };

    return {show, set, preview, copies};
}

}
