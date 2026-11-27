#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Materials.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/commands/RoofCommands.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

struct SetOptions {
    std::string project;
    std::string library;
    std::string target; // default, a roof section id, or empty with --floor and --at
    std::string floor;
    std::string at;
    std::string type;
    std::optional<double> pitch;
    std::optional<double> overhang;
    std::optional<double> parapet;
    std::string direction; // degrees or auto
    std::string material;  // an id, or none
};

struct ClearOptions {
    std::string project;
    std::string id;
};

struct ListOptions {
    std::string project;
    std::string floor;
};

// Rounds to a tenth of a millimetre so answers do not carry floating point noise.
double rounded(double value) {
    return std::round(value * 1e4) / 1e4;
}

// A plan position as [x, y].
Json pointJson(const Vec2& point) {
    Json json = Json::array();
    json.push_back(rounded(point.x));
    json.push_back(rounded(point.y));
    return json;
}

// Roof settings as {type, pitch, overhang, parapet, direction}; direction is null for automatic.
Json settingsJson(const RoofSettings& settings) {
    Json json = Json::object();
    json["type"] = settings.type;
    json["pitch"] = settings.pitch;
    json["overhang"] = settings.overhang;
    json["parapet"] = settings.parapet;
    if (settings.direction) {
        json["direction"] = *settings.direction;
    } else {
        json["direction"] = nullptr;
    }
    return json;
}

// A resolved material as {material, origin, from}.
Json resolvedJson(const ResolvedMaterial& resolved) {
    Json json = Json::object();
    json["material"] = resolved.material ? Json(*resolved.material) : Json(nullptr);
    json["origin"] = std::string(materialOriginName(resolved.origin));
    json["from"] = resolved.from.empty() ? Json(nullptr) : Json(resolved.from);
    return json;
}

// The roof type the build makes for each section of a floor, by section index.
std::vector<std::string> typesBuilt(const Plan& plan, const Id& floorId) {
    std::vector<std::string> types;
    const auto index = floorIndex(plan, floorId);
    if (!index) {
        return types;
    }
    for (const RoofBuild& roof : floorRoofs(plan, *index, floorElevations(plan)).roofs) {
        if (roof.index >= static_cast<int>(types.size())) {
            types.resize(static_cast<std::size_t>(roof.index) + 1);
        }
        types[static_cast<std::size_t>(roof.index)] = roof.type;
    }
    return types;
}

// The entry shared by roof set and roof list for one derived section.
Json sectionJson(const Project& project, const RoofArea& area, const std::vector<std::string>& built) {
    const std::size_t at = static_cast<std::size_t>(area.index);
    Json entry = Json::object();
    entry["floor"] = area.floor;
    entry["index"] = area.index;
    entry["area"] = rounded(planAreaSize(area.area));
    entry["label"] = pointJson(area.labelPoint);
    entry["section"] = area.section.empty() ? Json(nullptr) : Json(area.section);
    entry["settings"] = settingsJson(area.settings);
    entry["type_built"] = at < built.size() && !built[at].empty() ? built[at] : area.settings.type;
    entry["material"] = resolvedJson(roofPartMaterial(project, area, RoofPart::Covering));
    Json outline = Json::array();
    for (const Vec2& point : area.area.outline) {
        outline.push_back(pointJson(point));
    }
    entry["outline"] = std::move(outline);
    return entry;
}

// The entry of a roof section id: the section it governs, or only its settings when it governs none.
Json entryById(const Project& project, const Id& id) {
    const RoofSection* entry = findRoofSection(project.plan, id);
    if (entry == nullptr) {
        return Json::object();
    }
    for (const RoofArea& area : roofAreasOf(project.plan, entry->floor)) {
        if (area.section == id) {
            return sectionJson(project, area, typesBuilt(project.plan, entry->floor));
        }
    }
    Json json = Json::object();
    json["floor"] = entry->floor;
    json["index"] = nullptr;
    json["section"] = id;
    json["settings"] = settingsJson(entry->settings);
    json["material"] = entry->material ? Json(*entry->material) : Json(nullptr);
    return json;
}

// Reads x,y into a point. False when the text is not two numbers.
bool parsePoint(const std::string& text, double& x, double& y) {
    const auto comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    const std::string first = text.substr(0, comma);
    const std::string second = text.substr(comma + 1);
    char* end = nullptr;
    x = std::strtod(first.c_str(), &end);
    if (first.empty() || *end != '\0') {
        return false;
    }
    y = std::strtod(second.c_str(), &end);
    return !second.empty() && *end == '\0';
}

// Checks a material id against the library, as material set does. Not used for none.
bool checkMaterial(const std::string& command, const RunContext& context, const SetOptions& options, Answer& failed) {
    if (options.material.empty() || options.material == "none") {
        return true;
    }
    if (!checkIdArgument(command, options.material, failed)) {
        return false;
    }
    OpenedMaterialCatalog opened;
    if (!openMaterialCatalog(command, context, options.library, opened, failed)) {
        return false;
    }
    const MaterialForUse usable = materialForUse(*opened.catalog, options.material);
    if (!usable.status.ok()) {
        failed = failure(command, usable.status);
        return false;
    }
    return true;
}

// Puts the options that were given into roof settings. False, with failed filled, for a direction that is
// neither a number nor auto.
bool applySettings(const std::string& command, const SetOptions& options, RoofSettings& settings, Answer& failed) {
    if (!options.type.empty()) {
        settings.type = options.type;
    }
    if (options.pitch) {
        settings.pitch = *options.pitch;
    }
    if (options.overhang) {
        settings.overhang = *options.overhang;
    }
    if (options.parapet) {
        settings.parapet = *options.parapet;
    }
    if (options.direction == "auto") {
        settings.direction.reset();
    } else if (!options.direction.empty()) {
        char* end = nullptr;
        const double value = std::strtod(options.direction.c_str(), &end);
        if (*end != '\0') {
            failed = badArguments(command, "bad_arguments", "--direction is degrees or auto.");
            return false;
        }
        settings.direction = value;
    }
    return true;
}

// The material an edit leaves: the one given (none clears it), or the current one.
std::optional<std::string> materialAfter(const SetOptions& options, const std::optional<std::string>& current) {
    if (options.material == "none") {
        return std::nullopt;
    }
    if (!options.material.empty()) {
        return options.material;
    }
    return current;
}

// Sets the roof default, one roof section entry, or the entry of the section at a point (added when it has none).
Answer roofSet(const RunContext& context, const SetOptions& options) {
    const std::string command = "roof set";
    const bool isDefault = options.target == "default";
    const bool anyValue = !options.type.empty() || options.pitch || options.overhang || options.parapet ||
                          !options.direction.empty() || !options.material.empty();
    if (!anyValue) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --type, --pitch, --overhang, --parapet, --direction and --material.");
    }
    if (isDefault && (!options.material.empty() || !options.floor.empty() || !options.at.empty())) {
        return badArguments(command, "bad_arguments", "The roof default takes no --material, --floor or --at.");
    }
    if (options.target.empty() && options.at.empty()) {
        return badArguments(command, "bad_arguments", "Name a roof section id, default, or give --floor and --at.");
    }
    if (!options.target.empty() && !isDefault && !options.at.empty()) {
        return badArguments(command, "bad_arguments", "Give a roof section id or --at, not both.");
    }
    double atX = 0.0;
    double atY = 0.0;
    if (!options.at.empty() && !parsePoint(options.at, atX, atY)) {
        return badArguments(command, "bad_arguments", "--at is x,y in metres.");
    }
    Answer refused;
    if (!checkMaterial(command, context, options, refused)) {
        return refused;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer failed;
        if (isDefault) {
            RoofSettings settings = document.plan().roof;
            if (!applySettings(command, options, settings, failed) ||
                !runCommand(command, document, std::make_unique<SetRoofDefaultCommand>(settings), failed)) {
                return failed;
            }
            return success(command, settingsJson(document.plan().roof));
        }

        Id entryId = options.target;
        if (entryId.empty()) {
            const Floor* floor = resolveFloor(command, document.plan(), options.floor, failed);
            if (floor == nullptr) {
                return failed;
            }
            const std::optional<RoofArea> area = roofAreaAt(document.plan(), floor->id, Vec2{atX, atY});
            if (!area || area->section.empty()) {
                RoofSettings settings = area ? area->settings : document.plan().roof;
                if (!applySettings(command, options, settings, failed)) {
                    return failed;
                }
                auto add = std::make_unique<AddRoofSectionCommand>(floor->id, atX, atY, settings,
                                                                   materialAfter(options, std::nullopt));
                const AddRoofSectionCommand* raw = add.get();
                if (!runCommand(command, document, std::move(add), failed)) {
                    return failed;
                }
                return success(command, entryById(document.project(), raw->newSectionId()));
            }
            entryId = area->section;
        }

        const RoofSection* current = findRoofSection(document.plan(), entryId);
        if (current == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no roof section " + entryId + "."));
        }
        RoofSettings settings = current->settings;
        const std::optional<std::string> material = materialAfter(options, current->material);
        if (!applySettings(command, options, settings, failed) ||
            !runCommand(command, document, std::make_unique<SetRoofSectionCommand>(entryId, settings, material),
                        failed)) {
            return failed;
        }
        return success(command, entryById(document.project(), entryId));
    });
}

// Removes a roof section entry so its section follows the default again.
Answer roofClear(const RunContext& context, const ClearOptions& options) {
    return edit(
        "roof clear", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveRoofSectionCommand>(options.id); },
        [&](const Plan&, const RemoveRoofSectionCommand&) {
            Json result = Json::object();
            result["removed"] = options.id;
            return result;
        });
}

// Lists the roof default and every roof section, with the entries that match no section.
Answer roofList(const RunContext& context, const ListOptions& options) {
    const std::string command = "roof list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Plan& plan = document.plan();
    if (!options.floor.empty() && findFloor(plan, options.floor) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no floor " + options.floor + "."));
    }
    Json sections = Json::array();
    for (const Floor& floor : plan.floors) {
        if (!options.floor.empty() && floor.id != options.floor) {
            continue;
        }
        const auto built = typesBuilt(plan, floor.id);
        for (const RoofArea& area : roofAreasOf(plan, floor.id)) {
            sections.push_back(sectionJson(document.project(), area, built));
        }
    }
    Json unused = Json::array();
    for (const Id& id : unusedRoofSections(plan)) {
        unused.push_back(id);
    }
    Json result = Json::object();
    result["default"] = settingsJson(plan.roof);
    result["sections"] = std::move(sections);
    result["unused"] = std::move(unused);
    return success(command, std::move(result));
}

// Lists the roof types and the settings each uses.
Answer roofTypes() {
    Json types = Json::array();
    for (const RoofType* type : allRoofTypes()) {
        Json uses = Json::array();
        if (type->usesPitch()) {
            uses.push_back("pitch");
        }
        if (type->usesDirection()) {
            uses.push_back("direction");
        }
        if (type->usesOverhang()) {
            uses.push_back("overhang");
        }
        if (type->usesParapet()) {
            uses.push_back("parapet");
        }
        if (type->usesSecondPitch()) {
            uses.push_back("second_pitch");
        }
        if (type->usesBreakRatio()) {
            uses.push_back("break_ratio");
        }
        if (type->usesRidgeOffset()) {
            uses.push_back("ridge_offset");
        }
        if (type->usesRise()) {
            uses.push_back("rise");
        }
        Json item = Json::object();
        item["id"] = std::string(type->id());
        item["name"] = std::string(type->name());
        item["uses"] = std::move(uses);
        types.push_back(std::move(item));
    }
    Json result = Json::object();
    result["types"] = std::move(types);
    return success("roof types", std::move(result));
}

}

std::vector<CommandSpec> roofCommands() {
    CommandSpec set;
    set.words = {"roof", "set"};
    set.description = "Change the roof default (roof set default), a roof section entry (roof set rf1) or the "
                      "section at a point (--floor and --at, added when it has no entry).";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<SetOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("target", options->target, "default, or a roof section id such as rf1.")->type_name("id");
        sub.add_option("--floor", options->floor, "Floor of the section for --at. Default: the first floor.")
            ->type_name("id");
        sub.add_option("--at", options->at, "A point x,y in metres inside the section.")->type_name("x,y");
        sub.add_option("--type", options->type, "Roof type. See roof types.")->type_name("id");
        sub.add_option("--pitch", options->pitch, "Slope in degrees above horizontal.")->type_name("degrees");
        sub.add_option("--overhang", options->overhang, "How far the roof reaches past the walls, metres.")
            ->type_name("metres");
        sub.add_option("--parapet", options->parapet, "Height of the wall round a flat roof, metres.")
            ->type_name("metres");
        sub.add_option("--direction", options->direction, "Ridge or fall direction in degrees, or auto.")
            ->type_name("degrees|auto");
        sub.add_option("--material", options->material, "Material id for the covering, or none. Not for default.")
            ->type_name("id");
        return [options](const RunContext& context) { return roofSet(context, *options); };
    };
    CommandSpec clear;
    clear.words = {"roof", "clear"};
    clear.description = "Remove a roof section entry so its section follows the default.";
    clear.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ClearOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Roof section id.")->type_name("id")->required();
        return [options](const RunContext& context) { return roofClear(context, *options); };
    };
    CommandSpec list;
    list.words = {"roof", "list"};
    list.description = "List the roof default and the roof sections with their settings, material and outline.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Only the sections of this floor.")->type_name("id");
        return [options](const RunContext& context) { return roofList(context, *options); };
    };
    CommandSpec types;
    types.words = {"roof", "types"};
    types.description = "List the roof types and the settings each uses.";
    types.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return roofTypes(); };
    };
    return {set, clear, list, types};
}

}
