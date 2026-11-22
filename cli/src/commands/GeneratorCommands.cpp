#include "../CatalogCommon.h"
#include "../Common.h"
#include "../GeneratorCommon.h"

#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>

#include <cmath>
#include <memory>
#include <utility>

namespace plynth::cli {

namespace {

struct ListOptions {
    std::string library;
    std::string blender;
    bool probe = false;
};

struct InfoOptions {
    std::string library;
    std::string id;
};

struct CheckOptions {
    std::string blender;
};

struct SamplesOptions {
    std::string library;
    std::string from;
};

// The spin box step of a parameter: its own, or the default for its unit and type.
double effectiveStep(const GeneratorParam& param) {
    if (param.step > 0.0) {
        return param.step;
    }
    if (param.type == GeneratorParamType::Integer || param.unit == GeneratorUnit::Degrees) {
        return 1.0;
    }
    return param.unit == GeneratorUnit::Metres ? 0.01 : 0.1;
}

// The size entry of a generator at the default ceiling height.
Json sizeJson(const ItemSize& size) {
    Json entry = Json::object();
    entry["width"] = size.width;
    entry["depth"] = size.depth;
    entry["height"] = size.height;
    return entry;
}

// The enabled add-ons of a cache as entries.
Json enabledJson(const AddonCache& cache) {
    Json list = Json::array();
    for (const ProbeAddon& addon : cache.enabled) {
        Json entry = Json::object();
        entry["module"] = addon.module;
        entry["name"] = addon.name;
        entry["version"] = addon.version;
        list.push_back(std::move(entry));
    }
    return list;
}

// The availability of every generator of a library, for check-addons.
Json availabilityJson(const GeneratorEnv& env) {
    Json list = Json::array();
    for (const GeneratorInfo& info : env.library.generators) {
        Json entry = Json::object();
        entry["id"] = info.id;
        entry["availability"] = std::string(generatorAvailabilityName(generatorState(info, env.cache).availability));
        list.push_back(std::move(entry));
    }
    return list;
}

// Picks the Blender for a command that must run it. On error the answer is returned in failed.
bool pickBlender(const std::string& command, const RunContext& context, const MachineSettings& settings,
                 const std::string& option, BlenderInstall& install, Answer& failed) {
    std::optional<std::filesystem::path> path;
    if (!option.empty()) {
        path = userPath(context, option);
    }
    const BlenderChoice choice = chooseBlender(path, settings);
    if (!choice.status.ok()) {
        failed = failure(command, choice.status);
        return false;
    }
    install = choice.install;
    return true;
}

// Lists the generators of the library with their availability and the files that were refused.
Answer generatorList(const RunContext& context, const ListOptions& options) {
    const std::string command = "generator list";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    GeneratorEnv env;
    if (options.probe) {
        BlenderInstall install;
        if (!pickBlender(command, context, opened.settings, options.blender, install, failed)) {
            return failed;
        }
        const AddonProbe probe = probeAddons(install, addonCacheFile());
        if (!probe.status.ok()) {
            return failure(command, probe.status);
        }
        env.library = loadGeneratorFolder(opened.paths.generators);
        env.cache = probe.cache;
    } else {
        std::optional<std::filesystem::path> blender;
        if (!options.blender.empty()) {
            blender = userPath(context, options.blender);
        }
        env = loadGeneratorEnv(opened.paths, opened.settings, blender);
    }

    Json generators = Json::array();
    for (const GeneratorInfo& info : env.library.generators) {
        generators.push_back(generatorEntry(info, env));
    }
    Json refused = Json::array();
    for (const auto& [file, status] : env.library.refused) {
        Json entry = Json::object();
        entry["file"] = pathToUtf8(file.filename());
        entry["code"] = std::string(errorCodeName(status.code));
        entry["message"] = status.message;
        refused.push_back(std::move(entry));
    }
    Json result = Json::object();
    result["generators"] = std::move(generators);
    result["refused"] = std::move(refused);
    result["addons_checked_at"] = env.cache ? Json(formatSessionTime(env.cache->checkedAt)) : Json(nullptr);
    return success(command, std::move(result));
}

// Describes one generator.
Answer generatorInfo(const RunContext& context, const InfoOptions& options) {
    const std::string command = "generator info";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const GeneratorEnv env = loadGeneratorEnv(opened.paths, opened.settings, std::nullopt);
    const GeneratorInfo* info = env.library.find(options.id);
    if (info == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no generator " + options.id + " in the library."));
    }
    Json result = Json::object();
    result["generator"] = generatorEntry(*info, env);
    return success(command, std::move(result));
}

// Asks Blender which add-ons are enabled and keeps the answer.
Answer generatorCheckAddons(const RunContext& context, const CheckOptions& options) {
    const std::string command = "generator check-addons";
    const SettingsLoad loaded = loadSettings();
    if (!loaded.status.ok()) {
        return failure(command, loaded.status);
    }
    BlenderInstall install;
    Answer failed;
    if (!pickBlender(command, context, loaded.settings, options.blender, install, failed)) {
        return failed;
    }
    const AddonProbe probe = probeAddons(install, addonCacheFile());
    if (!probe.status.ok()) {
        return failure(command, probe.status);
    }
    GeneratorEnv env;
    const LibraryChoice choice = chooseLibrary(std::nullopt, loaded.settings);
    if (choice.status.ok()) {
        env.library = loadGeneratorFolder(choice.paths.generators);
    }
    env.cache = probe.cache;

    Json result = Json::object();
    result["blender"] = pathToUtf8(probe.cache.blender);
    result["checked_at"] = formatSessionTime(probe.cache.checkedAt);
    result["enabled"] = enabledJson(probe.cache);
    result["generators"] = availabilityJson(env);
    return success(command, std::move(result));
}

// Copies the sample generators into the library's generators folder, leaving files that are there.
Answer generatorAddSamples(const RunContext& context, const SamplesOptions& options) {
    const std::string command = "library add-sample-generators";
    OpenedCatalog opened;
    Answer failed;
    if (!openCatalog(command, context, options.library, opened, failed)) {
        return failed;
    }
    const SampleGeneratorsCopy copy = addSampleGenerators(opened.paths.generators, userPath(context, options.from));
    if (!copy.status.ok()) {
        return failure(command, copy.status);
    }
    Json result = Json::object();
    result["folder"] = pathToUtf8(opened.paths.generators);
    result["copied"] = copy.copied;
    result["skipped"] = copy.skipped;
    return success(command, std::move(result));
}

}

GeneratorEnv loadGeneratorEnv(const LibraryPaths& paths, const MachineSettings& settings,
                              const std::optional<std::filesystem::path>& blenderOption) {
    GeneratorEnv env;
    env.library = loadGeneratorFolder(paths.generators);
    const AddonCacheRead read = readAddonCache(addonCacheFile());
    if (read.status.ok() && read.cache) {
        const BlenderChoice choice = chooseBlender(blenderOption, settings);
        if (choice.status.ok() && addonCacheMatches(*read.cache, choice.install)) {
            env.cache = read.cache;
        }
    }
    return env;
}

Json generatorValueJson(const GeneratorParam* param, const GeneratorValue& value) {
    if (const double* number = std::get_if<double>(&value)) {
        if (param != nullptr && param->type == GeneratorParamType::Integer) {
            return static_cast<long long>(std::llround(*number));
        }
        return *number;
    }
    if (const bool* flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    return std::get<std::string>(value);
}

Json generatorParamsJson(const GeneratorInfo* info, const std::vector<GeneratorParamValue>& params) {
    Json values = Json::object();
    for (const GeneratorParamValue& entry : params) {
        const GeneratorParam* param = info == nullptr ? nullptr : info->findParam(entry.id);
        values[entry.id] = generatorValueJson(param, entry.value);
    }
    return values;
}

std::string generatorItemState(const GeneratorEnv* env, const GeneratorUse& use) {
    if (env == nullptr) {
        return "unknown";
    }
    const GeneratorInfo* info = env->library.find(use.name);
    if (info == nullptr) {
        return "missing";
    }
    const GeneratorState state = generatorState(*info, env->cache);
    if (state.availability == GeneratorAvailability::NeedsAddon) {
        return "unavailable";
    }
    if (!checkGeneratorParams(*info, use.params).ok()) {
        return "changed";
    }
    return state.availability == GeneratorAvailability::Unknown ? "unknown" : "ok";
}

Json generatorEntry(const GeneratorInfo& info, const GeneratorEnv& env) {
    const GeneratorState state = generatorState(info, env.cache);

    Json addons = Json::array();
    for (const std::string& name : info.addons) {
        Json entry = Json::object();
        entry["name"] = name;
        entry["display_name"] = addonDisplayName(name);
        entry["enabled"] = env.cache ? Json(addonEnabled(*env.cache, name)) : Json(nullptr);
        addons.push_back(std::move(entry));
    }

    const std::vector<GeneratorParamValue> defaults = defaultGeneratorParams(info, kDefaultCeilingHeight);
    Json params = Json::array();
    for (const GeneratorParam& param : info.params) {
        Json entry = Json::object();
        entry["id"] = param.id;
        entry["label"] = param.label;
        entry["help"] = param.help;
        entry["type"] = std::string(generatorParamTypeName(param.type));
        entry["unit"] = std::string(generatorUnitName(param.unit));
        const GeneratorValue* value = findGeneratorValue(defaults, param.id);
        entry["default"] = value == nullptr ? Json(nullptr) : generatorValueJson(&param, *value);
        entry["min"] = param.min ? Json(*param.min) : Json(nullptr);
        entry["max"] = param.max ? Json(*param.max) : Json(nullptr);
        entry["step"] = effectiveStep(param);
        Json choices = Json::array();
        for (const GeneratorChoice& choice : param.choices) {
            Json item = Json::object();
            item["id"] = choice.id;
            item["label"] = choice.label;
            choices.push_back(std::move(item));
        }
        entry["choices"] = std::move(choices);
        params.push_back(std::move(entry));
    }

    const GeneratorPlacement placement = makeGeneratorUse(info, defaults);

    Json entry = Json::object();
    entry["id"] = info.id;
    entry["name"] = info.name;
    entry["description"] = info.description;
    entry["file"] = pathToUtf8(info.file);
    entry["addons"] = std::move(addons);
    entry["availability"] = std::string(generatorAvailabilityName(state.availability));
    entry["message"] = state.message;
    entry["params"] = std::move(params);
    entry["default_size"] = placement.status.ok() ? sizeJson(placement.size) : Json(nullptr);
    return entry;
}

std::vector<CommandSpec> generatorCommands() {
    CommandSpec list;
    list.words = {"generator", "list"};
    list.description = "List the generators of the library, their add-ons and the files that were refused.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addLibraryOption(sub, options->library);
        addBlenderOption(sub, options->blender);
        sub.add_flag("--probe", options->probe, "Ask Blender which add-ons are enabled first.");
        return [options](const RunContext& context) { return generatorList(context, *options); };
    };

    CommandSpec info;
    info.words = {"generator", "info"};
    info.description = "Describe one generator: its parameters, add-ons and default size.";
    info.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<InfoOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the generator.")->type_name("id")->required();
        return [options](const RunContext& context) { return generatorInfo(context, *options); };
    };

    CommandSpec check;
    check.words = {"generator", "check-addons"};
    check.description = "Ask Blender which add-ons are enabled and keep the answer for the generator list.";
    check.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<CheckOptions>();
        addBlenderOption(sub, options->blender);
        return [options](const RunContext& context) { return generatorCheckAddons(context, *options); };
    };

    CommandSpec samples;
    samples.words = {"library", "add-sample-generators"};
    samples.description = "Copy the sample generators into the library. Files already there are kept.";
    samples.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SamplesOptions>();
        addLibraryOption(sub, options->library);
        sub.add_option("--from", options->from, "The folder of sample generators.")->type_name("folder")->required();
        return [options](const RunContext& context) { return generatorAddSamples(context, *options); };
    };

    return {list, info, check, samples};
}

}
