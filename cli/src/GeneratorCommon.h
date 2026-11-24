#pragma once

#include "Registry.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Generators.h>
#include <plynth/core/Library.h>
#include <plynth/core/Settings.h>

#include <optional>
#include <string>

namespace plynth::cli {

// A library's generators with the add-on cache that applies to the chosen Blender, for the commands that
// show or check generators.
struct GeneratorEnv {
    GeneratorLibrary library;
    std::optional<AddonCache> cache;

    // The lookup for checkItem. It refers to this object, which must outlive it.
    GeneratorItemLookup lookup() const { return generatorItemLookupFor(library, cache); }
};

// Loads the generators folder of a library and the add-on cache. The cache is kept only when it was made by
// the Blender that would be used (the option, then PLYNTH_BLENDER, then the settings and the search); with no
// usable Blender or no matching cache it stays empty.
GeneratorEnv loadGeneratorEnv(const LibraryPaths& paths, const MachineSettings& settings,
                              const std::optional<std::filesystem::path>& blenderOption);

// The generator entry shared by the generator commands and place.
Json generatorEntry(const GeneratorInfo& info, const GeneratorEnv& env);

// A parameter value as JSON: a number (a whole number for an integer parameter), true or false, or text.
Json generatorValueJson(const GeneratorParam* param, const GeneratorValue& value);

// A set of values as an object keyed by parameter id.
Json generatorParamsJson(const GeneratorInfo* info, const std::vector<GeneratorParamValue>& params);

// What an item's generator state is called in an item entry: ok, missing, unavailable, changed or unknown.
std::string generatorItemState(const GeneratorEnv* env, const GeneratorUse& use);

}
