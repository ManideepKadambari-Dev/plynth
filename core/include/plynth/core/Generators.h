#pragma once

#include "Addons.h"
#include "Items.h"
#include "Plan.h"
#include "Status.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

// Generators (DESIGN.md section 7.10). A generator is one Blender Python file, Library/generators/<id>.py,
// whose module docstring declares it. The docstring is the first thing in the file after any blank lines and
// "#" comment lines, starts with the marker line and holds one JSON object in the form of
// schema/generator.schema.json:
//   """plynth-generator
//   {"format": "plynth-generator", "version": 1, "name": "Column", "description": "...", "addons": [],
//    "size": {"width": {"param": "diameter"}, "depth": {"param": "diameter"}, "height": {"param": "height"}},
//    "params": [{"id": "diameter", "label": "Diameter", "type": "number", "unit": "m", "default": 0.3,
//                "min": 0.05, "max": 2.0}, ...]}
//   """
// Core reads only this declaration and never runs the file. The Blender build runs it (build step
// "generators", see GeneratorBuild.h). Each file is one entry, so adding a generator is adding one file. The
// entries are loaded from the folder into a GeneratorLibrary value rather than a global registry, because the
// library can change while the app runs. Files whose name starts with "_" and subfolders are skipped, so a
// generator may keep helpers there.
//
// A placed generator is an Item with generator set (Plan.h). Its values are full (every declared parameter, no
// other) and its size is generatorSize of them, stored in the item like an asset's measured size, so the
// footprint, fit and overlap work without the library.
//
// Generators may need Blender add-ons ("addons", short module names, see Addons.h). A generator whose add-on is
// known to be off is unavailable: the picker shows it switched off, the CLI refuses to place it, and a placed
// one is left out of the build with the warning generator_unavailable. It is never an error, and nothing else
// depends on an add-on.

inline constexpr std::string_view kGeneratorFormatName = "plynth-generator";
inline constexpr int kGeneratorFormatVersion = 1;
inline constexpr std::string_view kGeneratorFileSuffix = ".py";
inline constexpr std::string_view kGeneratorMarker = "plynth-generator"; // the docstring's first line
inline constexpr std::string_view kCeilingHeightDefault = "ceiling_height"; // a default that means the floor's
inline constexpr std::size_t kMaxGeneratorParams = 32;
inline constexpr std::size_t kMaxGeneratorFileBytes = 1024 * 1024;

enum class GeneratorParamType {
    Number,   // "number": a finite number; stored as a double
    Integer,  // "integer": a whole number; stored as a double with no fraction
    Boolean,  // "boolean"
    Choice,   // "choice": the id of one of its choices
    Material, // "material": a materials catalog id, or empty text for the neutral look
};

std::string_view generatorParamTypeName(GeneratorParamType type);
std::optional<GeneratorParamType> generatorParamTypeFromName(std::string_view name);

// The unit a number is shown with. Values are always metres or degrees, never anything else.
enum class GeneratorUnit {
    None,    // "none"
    Metres,  // "m"
    Degrees, // "deg"
};

std::string_view generatorUnitName(GeneratorUnit unit);

struct GeneratorChoice {
    std::string id;    // a lower case name
    std::string label; // shown in the app

    bool operator==(const GeneratorChoice&) const = default;
};

// One declared parameter.
struct GeneratorParam {
    std::string id;    // a lower case name, unique in the generator
    std::string label; // shown in the app, for example "Diameter"
    std::string help;  // one plain sentence for the tooltip; may be empty
    GeneratorParamType type = GeneratorParamType::Number;
    GeneratorUnit unit = GeneratorUnit::None;     // numbers only
    GeneratorValue defaultValue;                  // of the type's kind; see defaultCeiling
    bool defaultCeiling = false;                  // "default": "ceiling_height" (numbers in metres only)
    std::optional<double> min;                    // numbers and integers, inclusive
    std::optional<double> max;                    // numbers and integers, inclusive, at least min
    double step = 0.0;                            // spin box step; 0 means 0.01 for m, 1 for deg and integers,
                                                  // 0.1 otherwise
    std::vector<GeneratorChoice> choices;         // choices only, at least one

    bool operator==(const GeneratorParam&) const = default;
};

// One side of the size: a fixed length, or param * times + plus where param is a number or integer parameter.
struct GeneratorLength {
    double value = 0.0;  // used when param is empty
    std::string param;
    double times = 1.0;
    double plus = 0.0;

    bool operator==(const GeneratorLength&) const = default;
};

// How the size of what a generator makes follows from its values: width along its own x, depth along its own y
// (back to front), height up, as for assets (Plan.h ItemSize).
struct GeneratorSizeRule {
    GeneratorLength width;
    GeneratorLength depth;
    GeneratorLength height;

    bool operator==(const GeneratorSizeRule&) const = default;
};

// A generator as its file declares it.
struct GeneratorInfo {
    std::string id;          // the file stem, a lower case name of at most 64 characters
    std::string name;        // shown in the app, for example "Column"
    std::string description; // one or two plain sentences
    std::vector<std::string> addons; // short module names it needs, lower case, sorted, each once
    std::vector<GeneratorParam> params; // in file order
    GeneratorSizeRule size;
    std::filesystem::path file; // absolute

    // The parameter with this id. Null when there is none.
    const GeneratorParam* findParam(std::string_view paramId) const;

    bool operator==(const GeneratorInfo&) const = default;
};

// What reading one generator file gave.
struct GeneratorFile {
    Status status;
    GeneratorInfo info;
};

// Reads the declaration from a file's text. id is the file stem. BadFile, with a message naming the problem and
// where set to a JSON pointer inside the declaration when it is about a field, when: id is not a well formed
// lower case name; there is no docstring starting with the marker line; the JSON is not valid; format or
// version is wrong; a field is missing, unknown or of the wrong kind; name is empty; an add-on name is not a
// lower case name; there are no parameters or more than kMaxGeneratorParams, or an id is used twice; a label
// is empty; a default does not fit its type, range or choices ("ceiling_height" only for numbers in metres, a
// material default a catalog id or null); min is above max; a step is not above zero; a choice id is used twice;
// a size side names an unknown parameter or one that is not a number or integer, or a fixed side is not above 0.
GeneratorFile parseGeneratorSource(std::string_view source, std::string_view id);

// Reads a file and parses it. CannotRead when it cannot be opened, BadFile when it is larger than
// kMaxGeneratorFileBytes, otherwise as parseGeneratorSource with the file stem as id. info.file is set.
GeneratorFile readGeneratorFile(const std::filesystem::path& file);

// Says whether text is a well formed generator, parameter or choice id: letters a to z, digits and "_",
// starting with a letter, at most 64 characters.
bool isGeneratorId(std::string_view text);

// Every generator of one folder.
struct GeneratorLibrary {
    std::filesystem::path folder;
    std::vector<GeneratorInfo> generators; // the good files, sorted by id
    std::vector<std::pair<std::filesystem::path, Status>> refused; // the bad files, in file name order

    // The generator with this id. Null when there is none.
    const GeneratorInfo* find(std::string_view generatorId) const;
};

// Reads every file ending in kGeneratorFileSuffix in a folder (not its subfolders), skipping names that start
// with "_". A missing folder gives an empty library and no refusals.
GeneratorLibrary loadGeneratorFolder(const std::filesystem::path& folder);

// The default values of a generator, sorted by id. A "ceiling_height" default is the given height.
std::vector<GeneratorParamValue> defaultGeneratorParams(const GeneratorInfo& info, double ceilingHeight);

// Checks a full set of values. InvalidValue, naming the parameter, when one is missing, unknown or used twice,
// not sorted by id, of the wrong kind, outside its range, not whole for an integer, not one of its choices, or a
// material that is neither empty nor a well formed catalog id.
Status checkGeneratorParams(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params);

// Fits stored values to a generator's current declaration: values that fit are kept, unknown ones are dropped,
// missing or wrong ones get their default (a "ceiling_height" default is the given height). changed is true
// when anything was dropped or replaced, and notes says what, one plain sentence each.
struct GeneratorParamsFit {
    std::vector<GeneratorParamValue> params; // sorted by id, passes checkGeneratorParams
    bool changed = false;
    std::vector<std::string> notes;
};

GeneratorParamsFit fitGeneratorParams(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& stored,
                                      double ceilingHeight);

// Reads a value typed by a person (the CLI's --param id=value, a text field): numbers in metres or degrees with
// no unit written, integers, true or false (also yes, no, 1, 0), a choice id, a material id or "none" for the
// neutral look. InvalidValue naming the parameter when it does not fit the type (range is checked by
// checkGeneratorParams).
struct GeneratorValueRead {
    Status status;
    GeneratorValue value;
};

GeneratorValueRead parseGeneratorValue(const GeneratorParam& param, std::string_view text);

// A value as text: numbers with up to 6 decimals and no trailing zeros, true or false, the text itself, or
// "none" for an empty material.
std::string generatorValueText(const GeneratorValue& value);

// Sets one value in a full set, keeping it sorted. InvalidValue when the id is unknown or the value does not fit.
Status setGeneratorParam(const GeneratorInfo& info, std::vector<GeneratorParamValue>& params,
                         std::string_view paramId, const GeneratorValue& value);

// The value with this id in a set. Null when there is none.
const GeneratorValue* findGeneratorValue(const std::vector<GeneratorParamValue>& params, std::string_view paramId);

// The size a generator makes with these values, by its size rule. InvalidValue when the values do not pass
// checkGeneratorParams or a side does not come out above zero.
struct GeneratorSize {
    Status status;
    ItemSize size;
};

GeneratorSize generatorSize(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params);

// Everything an item needs to run a generator with these values: the use (name and sorted values) and its size.
// As checkGeneratorParams and generatorSize.
struct GeneratorPlacement {
    Status status;
    GeneratorUse use;
    ItemSize size;
};

GeneratorPlacement makeGeneratorUse(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params);

// The ids of the material parameters of a generator, in declaration order.
std::vector<std::string> generatorMaterialParams(const GeneratorInfo& info);

enum class GeneratorAvailability {
    Available,  // "available": needs no add-on, or every add-on it needs is enabled
    NeedsAddon, // "needs_addon": the cache says an add-on it needs is not enabled
    Unknown,    // "unknown": it needs an add-on and there is no cache for the Blender in use
};

std::string_view generatorAvailabilityName(GeneratorAvailability availability);

struct GeneratorState {
    GeneratorAvailability availability = GeneratorAvailability::Available;
    std::vector<std::string> missingAddons; // short names, sorted; only for NeedsAddon
    // One plain sentence for NeedsAddon and Unknown, for example "Needs the Archimesh add-on, which is not
    // enabled in Blender." or "Needs the Archimesh add-on. Check the add-ons to see if it is enabled.". Empty
    // for Available.
    std::string message;
};

// Works out whether a generator can run, from the add-on cache. An empty cache gives Unknown for a generator
// that needs an add-on. The caller passes a cache only when addonCacheMatches the Blender in use.
GeneratorState generatorState(const GeneratorInfo& info, const std::optional<AddonCache>& addons);

// A lookup for checkItem and itemProblems (Items.h): empty when the generator is not in the library; otherwise
// available from generatorState (Unknown counts as available), valuesFit from checkGeneratorParams, and size
// from generatorSize. The library must outlive the lookup.
GeneratorItemLookup generatorItemLookupFor(const GeneratorLibrary& library, const std::optional<AddonCache>& addons);

// What copying the sample generators did.
struct SampleGeneratorsCopy {
    Status status;                    // NotFound when from is not a folder; CannotWrite when a copy fails
    std::vector<std::string> copied;  // file names, in name order
    std::vector<std::string> skipped; // file names already in the generators folder, left as they are
};

// Copies every generator file of from (the samples/generators folder) into a library's generators folder,
// creating it when missing, never overwriting a file that is there.
SampleGeneratorsCopy addSampleGenerators(const std::filesystem::path& generatorsFolder,
                                         const std::filesystem::path& from);

}
