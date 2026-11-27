#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Canopies.h>
#include <plynth/core/commands/CanopyCommands.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plynth::cli {

namespace {

struct EditOptions {
    std::string project;
    std::string library;
    std::string id;
    std::string floor;
    std::vector<std::string> points;
    std::optional<double> height;
    std::optional<double> thickness;
    std::string type;
    std::optional<double> pitch;
    std::optional<double> overhang;
    std::optional<double> parapet;
    std::string direction; // degrees or auto
    std::optional<double> secondPitch;
    std::optional<double> breakRatio;
    std::optional<double> ridgeOffset;
    std::optional<double> rise;
    std::string material;          // an id, or none
    std::string undersideMaterial; // an id, or none
    std::string fasciaMaterial;    // an id, or none
};

struct RemoveOptions {
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

// The area an outline encloses, in square metres.
double outlineArea(const std::vector<PlanXY>& outline) {
    double twice = 0.0;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const PlanXY& a = outline[i];
        const PlanXY& b = outline[(i + 1) % outline.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::fabs(twice) / 2.0;
}

// A material name, or null when none is set.
Json optionalJson(const std::optional<std::string>& value) {
    return value ? Json(*value) : Json(nullptr);
}

// The roof of a canopy with every setting, direction null for automatic.
Json roofJson(const RoofSettings& settings) {
    Json json = Json::object();
    json["type"] = settings.type;
    json["pitch"] = settings.pitch;
    json["overhang"] = settings.overhang;
    json["parapet"] = settings.parapet;
    json["direction"] = settings.direction ? Json(*settings.direction) : Json(nullptr);
    json["second_pitch"] = settings.secondPitch;
    json["break_ratio"] = settings.breakRatio;
    json["ridge_offset"] = settings.ridgeOffset;
    json["rise"] = settings.rise;
    return json;
}

// The problems the build would report for the plan, worked out without the library.
std::vector<BuildProblem> planProblems(const Project& project) {
    return makeBuildPlan(project, {}).problems;
}

// The canopy entry: its values, outline, area and the build problems about it.
Json canopyEntry(const Floor& floor, const Canopy& canopy, const std::vector<BuildProblem>& problems) {
    Json entry = Json::object();
    entry["id"] = canopy.id;
    entry["floor"] = floor.id;
    Json outline = Json::array();
    for (const PlanXY& point : canopy.outline) {
        Json pair = Json::array();
        pair.push_back(rounded(point.x));
        pair.push_back(rounded(point.y));
        outline.push_back(std::move(pair));
    }
    entry["outline"] = std::move(outline);
    entry["area"] = rounded(outlineArea(canopy.outline));
    entry["height"] = canopy.height;
    entry["thickness"] = canopy.thickness;
    entry["roof"] = roofJson(canopy.roof);
    entry["material"] = optionalJson(canopy.material);
    entry["underside_material"] = optionalJson(canopy.undersideMaterial);
    entry["fascia_material"] = optionalJson(canopy.fasciaMaterial);
    Json list = Json::array();
    for (const BuildProblem& problem : problems) {
        if (problem.object != canopy.id) {
            continue;
        }
        Json item = Json::object();
        item["code"] = problem.code;
        item["severity"] = problem.severity == BuildSeverity::Error ? "error" : "warning";
        item["message"] = problem.message;
        list.push_back(std::move(item));
    }
    entry["problems"] = std::move(list);
    return entry;
}

// The canopy entry for an id, read from the changed project.
Json entryFor(const Project& project, const Id& id) {
    const Floor* floor = findFloorContaining(project.plan, id);
    const Canopy* canopy = floor == nullptr ? nullptr : findCanopy(*floor, id);
    if (canopy == nullptr) {
        return Json::object();
    }
    return canopyEntry(*floor, *canopy, planProblems(project));
}

// Reads x,y into a point. False when the text is not two numbers.
bool parsePoint(const std::string& text, PlanXY& point) {
    const auto comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    const std::string first = text.substr(0, comma);
    const std::string second = text.substr(comma + 1);
    char* end = nullptr;
    point.x = std::strtod(first.c_str(), &end);
    if (first.empty() || *end != '\0') {
        return false;
    }
    point.y = std::strtod(second.c_str(), &end);
    return !second.empty() && *end == '\0';
}

// Reads the --points values, each holding one or more x,y separated by spaces or semicolons. False when any
// piece is not two numbers.
bool parseOutline(const std::vector<std::string>& values, std::vector<PlanXY>& outline) {
    outline.clear();
    for (const std::string& value : values) {
        std::string piece;
        for (std::size_t i = 0; i <= value.size(); ++i) {
            const char c = i < value.size() ? value[i] : ' ';
            if (c == ' ' || c == ';') {
                if (!piece.empty()) {
                    PlanXY point;
                    if (!parsePoint(piece, point)) {
                        return false;
                    }
                    outline.push_back(point);
                    piece.clear();
                }
            } else {
                piece += c;
            }
        }
    }
    return true;
}

// Checks one material id against the library, as material set does. Nothing to check for none or empty.
bool checkMaterial(const std::string& command, const RunContext& context, const std::string& libraryOption,
                   const std::string& material, Answer& failed) {
    if (material.empty() || material == "none") {
        return true;
    }
    if (!checkIdArgument(command, material, failed)) {
        return false;
    }
    OpenedMaterialCatalog opened;
    if (!openMaterialCatalog(command, context, libraryOption, opened, failed)) {
        return false;
    }
    const MaterialForUse usable = materialForUse(*opened.catalog, material);
    if (!usable.status.ok()) {
        failed = failure(command, usable.status);
        return false;
    }
    return true;
}

// Checks the three material options before anything is opened for writing.
bool checkMaterials(const std::string& command, const RunContext& context, const EditOptions& options, Answer& failed) {
    return checkMaterial(command, context, options.library, options.material, failed) &&
           checkMaterial(command, context, options.library, options.undersideMaterial, failed) &&
           checkMaterial(command, context, options.library, options.fasciaMaterial, failed);
}

// The material an edit leaves: the one given (none clears it), or the current one.
std::optional<std::string> materialAfter(const std::string& option, const std::optional<std::string>& current) {
    if (option == "none") {
        return std::nullopt;
    }
    if (!option.empty()) {
        return option;
    }
    return current;
}

// Says whether any value option was given, for canopy set.
bool anyValue(const EditOptions& options) {
    return !options.points.empty() || options.height || options.thickness || !options.type.empty() ||
           options.pitch || options.overhang || options.parapet || !options.direction.empty() ||
           options.secondPitch || options.breakRatio || options.ridgeOffset || options.rise ||
           !options.material.empty() || !options.undersideMaterial.empty() || !options.fasciaMaterial.empty();
}

// Puts the options that were given into a canopy. False, with failed filled, for points or a direction that
// cannot be read.
bool applyOptions(const std::string& command, const EditOptions& options, Canopy& canopy, Answer& failed) {
    if (!options.points.empty() && !parseOutline(options.points, canopy.outline)) {
        failed = badArguments(command, "bad_arguments",
                              "--points is a list of x,y in metres separated by spaces, such as 6,3 9,3 9,7 6,7.");
        return false;
    }
    if (options.height) {
        canopy.height = *options.height;
    }
    if (options.thickness) {
        canopy.thickness = *options.thickness;
    }
    RoofSettings& roof = canopy.roof;
    if (!options.type.empty()) {
        roof.type = options.type;
    }
    if (options.pitch) {
        roof.pitch = *options.pitch;
    }
    if (options.overhang) {
        roof.overhang = *options.overhang;
    }
    if (options.parapet) {
        roof.parapet = *options.parapet;
    }
    if (options.direction == "auto") {
        roof.direction.reset();
    } else if (!options.direction.empty()) {
        char* end = nullptr;
        const double value = std::strtod(options.direction.c_str(), &end);
        if (*end != '\0') {
            failed = badArguments(command, "bad_arguments", "--direction is degrees or auto.");
            return false;
        }
        roof.direction = value;
    }
    if (options.secondPitch) {
        roof.secondPitch = *options.secondPitch;
    }
    if (options.breakRatio) {
        roof.breakRatio = *options.breakRatio;
    }
    if (options.ridgeOffset) {
        roof.ridgeOffset = *options.ridgeOffset;
    }
    if (options.rise) {
        roof.rise = *options.rise;
    }
    canopy.material = materialAfter(options.material, canopy.material);
    canopy.undersideMaterial = materialAfter(options.undersideMaterial, canopy.undersideMaterial);
    canopy.fasciaMaterial = materialAfter(options.fasciaMaterial, canopy.fasciaMaterial);
    return true;
}

// Adds a canopy over an outline on a floor. Options left out take the canopy defaults.
Answer canopyAdd(const RunContext& context, const EditOptions& options) {
    const std::string command = "canopy add";
    Canopy canopy;
    Answer failed;
    if (!applyOptions(command, options, canopy, failed) || !checkMaterials(command, context, options, failed)) {
        return failed;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        Answer refused;
        const Floor* floor = resolveFloor(command, document.plan(), options.floor, refused);
        if (floor == nullptr) {
            return refused;
        }
        auto added = std::make_unique<AddCanopyCommand>(floor->id, canopy);
        const AddCanopyCommand* raw = added.get();
        if (!runCommand(command, document, std::move(added), refused)) {
            return refused;
        }
        return success(command, entryFor(document.project(), raw->newCanopyId()));
    });
}

// Changes the values of a canopy as one step. What is left out stays.
Answer canopySet(const RunContext& context, const EditOptions& options) {
    const std::string command = "canopy set";
    if (!anyValue(options)) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --points, --height, --thickness, --type, the roof options and the "
                            "material options.");
    }
    Answer failed;
    Canopy probe;
    if (!applyOptions(command, options, probe, failed) || !checkMaterials(command, context, options, failed)) {
        return failed;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        const Floor* floor = findFloorContaining(document.plan(), options.id);
        const Canopy* current = floor == nullptr ? nullptr : findCanopy(*floor, options.id);
        if (current == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no canopy " + options.id + "."));
        }
        Canopy values = *current;
        Answer refused;
        if (!applyOptions(command, options, values, refused) ||
            !runCommand(command, document, std::make_unique<SetCanopyCommand>(options.id, values), refused)) {
            return refused;
        }
        return success(command, entryFor(document.project(), options.id));
    });
}

// Removes a canopy.
Answer canopyRemove(const RunContext& context, const RemoveOptions& options) {
    return edit(
        "canopy remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveCanopyCommand>(options.id); },
        [&](const Plan&, const RemoveCanopyCommand&) {
            Json result = Json::object();
            result["removed"] = options.id;
            return result;
        });
}

// Lists the canopies of every floor, or of one floor with --floor.
Answer canopyList(const RunContext& context, const ListOptions& options) {
    const std::string command = "canopy list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Plan& plan = document.plan();
    if (!options.floor.empty() && findFloor(plan, options.floor) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no floor " + options.floor + "."));
    }
    bool any = false;
    for (const Floor& floor : plan.floors) {
        any = any || !floor.canopies.empty();
    }
    const std::vector<BuildProblem> problems = any ? planProblems(document.project()) : std::vector<BuildProblem>{};
    Json canopies = Json::array();
    for (const Floor& floor : plan.floors) {
        if (!options.floor.empty() && floor.id != options.floor) {
            continue;
        }
        for (const Canopy& canopy : floor.canopies) {
            canopies.push_back(canopyEntry(floor, canopy, problems));
        }
    }
    Json result = Json::object();
    result["canopies"] = std::move(canopies);
    return success(command, std::move(result));
}

// Adds the value options that canopy add and canopy set share.
void addValueOptions(CLI::App& sub, EditOptions& options, bool adding) {
    auto* points = sub.add_option("--points", options.points,
                                  "The outline on the plan: three or more x,y in metres, separated by spaces.")
                       ->type_name("x,y ...")
                       ->allow_extra_args();
    if (adding) {
        points->required();
    }
    sub.add_option("--height", options.height,
                   adding ? "Height of the underside above the floor, 1 to 20 metres. Default: 2.7."
                          : "Height of the underside above the floor, 1 to 20 metres.")
        ->type_name("metres");
    sub.add_option("--thickness", options.thickness,
                   adding ? "Depth of the slab, 0.05 to 1 metre. Default: 0.25." : "Depth of the slab, 0.05 to 1 metre.")
        ->type_name("metres");
    sub.add_option("--type", options.type, adding ? "Roof type. See roof types. Default: flat." : "Roof type. See roof types.")
        ->type_name("id");
    sub.add_option("--pitch", options.pitch, "Slope in degrees above horizontal.")->type_name("degrees");
    sub.add_option("--overhang", options.overhang, "How far the roof reaches past the outline, metres.")
        ->type_name("metres");
    sub.add_option("--parapet", options.parapet, "Height of the low wall round a flat top, metres. 0 for none.")
        ->type_name("metres");
    sub.add_option("--direction", options.direction, "Ridge or fall direction in degrees, or auto.")
        ->type_name("degrees|auto");
    sub.add_option("--second-pitch", options.secondPitch, "Second slope of the types that use it, degrees.")
        ->type_name("degrees");
    sub.add_option("--break-ratio", options.breakRatio, "Share of the run the lower slope covers, 0.1 to 0.9.")
        ->type_name("ratio");
    sub.add_option("--ridge-offset", options.ridgeOffset, "Moves the ridge across the outline, -0.45 to 0.45.")
        ->type_name("share");
    sub.add_option("--rise", options.rise, "Height of a curved top or glazed band, 0.1 to 10 metres.")
        ->type_name("metres");
    sub.add_option("--material", options.material, "Material id for the covering, or none.")->type_name("id");
    sub.add_option("--underside-material", options.undersideMaterial, "Material id for the soffit, or none.")
        ->type_name("id");
    sub.add_option("--fascia-material", options.fasciaMaterial, "Material id for the edges, or none.")
        ->type_name("id");
}

}

std::vector<CommandSpec> canopyCommands() {
    CommandSpec add;
    add.words = {"canopy", "add"};
    add.description = "Add a canopy: a roof over an open area such as a porch, veranda or carport. An upper floor "
                      "that overhangs needs no canopy: draw its walls past the walls of the floor below.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<EditOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        addValueOptions(sub, *options, true);
        return [options](const RunContext& context) { return canopyAdd(context, *options); };
    };

    CommandSpec set;
    set.words = {"canopy", "set"};
    set.description = "Change the outline, heights, roof or materials of a canopy.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<EditOptions>();
        addProjectOption(sub, options->project);
        addLibraryOption(sub, options->library);
        sub.add_option("id", options->id, "Id of the canopy.")->type_name("id")->required();
        addValueOptions(sub, *options, false);
        return [options](const RunContext& context) { return canopySet(context, *options); };
    };

    CommandSpec remove;
    remove.words = {"canopy", "remove"};
    remove.description = "Remove a canopy.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Id of the canopy.")->type_name("id")->required();
        return [options](const RunContext& context) { return canopyRemove(context, *options); };
    };

    CommandSpec list;
    list.words = {"canopy", "list"};
    list.description = "List the canopies with their values and the problems the build would report.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Only the canopies of this floor.")->type_name("id");
        return [options](const RunContext& context) { return canopyList(context, *options); };
    };

    return {add, set, remove, list};
}

}
