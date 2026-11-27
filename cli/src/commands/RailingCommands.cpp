#include "../CatalogCommon.h"
#include "../Common.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Railings.h>
#include <plynth/core/commands/RailingCommands.h>

#include <cmath>
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
    std::string points;
    std::string around;
    std::string host;   // a deck id, or none
    std::string closed; // true or false
    std::optional<double> base;
    std::optional<double> height;
    std::string style;
    std::string material;      // an id, or none
    std::string glassMaterial; // an id, or none
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

// An optional text as a string, or null.
Json optionalText(const std::optional<std::string>& text) {
    return text ? Json(*text) : Json(nullptr);
}

// The railing entry: its values, its length along the line, how many straight runs it has and the problems
// the build would report for it.
Json railingEntry(const Plan& plan, const Floor& floor, const Railing& railing) {
    Json entry = Json::object();
    entry["id"] = railing.id;
    entry["floor"] = floor.id;
    Json points = Json::array();
    for (const PlanXY& point : railing.points) {
        Json pair = Json::array();
        pair.push_back(point.x);
        pair.push_back(point.y);
        points.push_back(std::move(pair));
    }
    entry["points"] = std::move(points);
    entry["closed"] = railing.closed;
    entry["host"] = optionalText(railing.host);
    entry["base"] = railing.base;
    entry["height"] = railing.height;
    entry["style"] = railing.style;
    entry["material"] = optionalText(railing.material);
    entry["glass_material"] = optionalText(railing.glassMaterial);
    const std::vector<RailingRun> runs = railingRuns(plan, floor, railing, floorElevations(plan));
    double length = 0.0;
    for (const RailingRun& run : runs) {
        length += std::hypot(run.end.x - run.start.x, run.end.y - run.start.y);
    }
    entry["length"] = rounded(length);
    entry["runs"] = runs.size();
    Json problems = Json::array();
    for (const BuildProblem& problem : railingProblems(floor, railing)) {
        Json item = Json::object();
        item["code"] = problem.code;
        item["severity"] = "warning";
        item["message"] = problem.message;
        problems.push_back(std::move(item));
    }
    entry["problems"] = std::move(problems);
    return entry;
}

// The railing entry of a railing that is in the plan.
Json entryFor(const Plan& plan, const Id& id) {
    const Floor* floor = findFloorContaining(plan, id);
    return railingEntry(plan, *floor, *findRailing(*floor, id));
}

// Reads the numbers of --points, split by commas, semicolons or spaces, two to a point. False when one is not a
// number or the count is odd.
bool parsePoints(const std::string& text, std::vector<PlanXY>& points) {
    std::vector<double> numbers;
    std::string word;
    const auto take = [&]() {
        if (word.empty()) {
            return true;
        }
        char* end = nullptr;
        const double value = std::strtod(word.c_str(), &end);
        if (*end != '\0') {
            return false;
        }
        numbers.push_back(value);
        word.clear();
        return true;
    };
    for (const char c : text) {
        if (c == ',' || c == ';' || c == ' ') {
            if (!take()) {
                return false;
            }
        } else {
            word += c;
        }
    }
    if (!take() || numbers.empty() || numbers.size() % 2 != 0) {
        return false;
    }
    points.clear();
    for (std::size_t i = 0; i < numbers.size(); i += 2) {
        points.push_back({numbers[i], numbers[i + 1]});
    }
    return true;
}

// Checks one material id against the library, as material set does. Not used for none or empty.
bool checkOneMaterial(const std::string& command, const RunContext& context, const std::string& library,
                      const std::string& id, Answer& failed) {
    if (id.empty() || id == "none") {
        return true;
    }
    if (!checkIdArgument(command, id, failed)) {
        return false;
    }
    OpenedMaterialCatalog opened;
    if (!openMaterialCatalog(command, context, library, opened, failed)) {
        return false;
    }
    const MaterialForUse usable = materialForUse(*opened.catalog, id);
    if (!usable.status.ok()) {
        failed = failure(command, usable.status);
        return false;
    }
    return true;
}

// Checks the railing and glass material ids that were given.
bool checkMaterials(const std::string& command, const RunContext& context, const EditOptions& options,
                    Answer& failed) {
    return checkOneMaterial(command, context, options.library, options.material, failed) &&
           checkOneMaterial(command, context, options.library, options.glassMaterial, failed);
}

// Sets or clears an optional text from an option: none clears it, empty leaves it.
void applyText(const std::string& value, std::optional<std::string>& target) {
    if (value == "none") {
        target.reset();
    } else if (!value.empty()) {
        target = value;
    }
}

// Puts the options that were given into a railing, except the points.
void applyOptions(const EditOptions& options, Railing& railing) {
    if (!options.closed.empty()) {
        railing.closed = options.closed == "true";
    }
    applyText(options.host, railing.host);
    if (options.base) {
        railing.base = *options.base;
    }
    if (options.height) {
        railing.height = *options.height;
    }
    if (!options.style.empty()) {
        railing.style = options.style;
    }
    applyText(options.material, railing.material);
    applyText(options.glassMaterial, railing.glassMaterial);
}

// The answer listing the railings that were added.
Json addedJson(const Plan& plan, const std::vector<Id>& ids) {
    Json railings = Json::array();
    for (const Id& id : ids) {
        railings.push_back(entryFor(plan, id));
    }
    Json result = Json::object();
    result["railings"] = std::move(railings);
    return result;
}

// Adds a railing along the given points, or one railing for each open stretch of a deck's edge, as one step.
Answer railingAdd(const RunContext& context, const EditOptions& options) {
    const std::string command = "railing add";
    if (options.points.empty() == options.around.empty()) {
        return badArguments(command, "bad_arguments", "Give either --points or --around.");
    }
    if (!options.around.empty() && (!options.host.empty() || !options.closed.empty())) {
        return badArguments(command, "bad_arguments",
                            "--around sets the host and the line itself. Leave out --host and --closed.");
    }
    if (options.host == "none") {
        return badArguments(command, "bad_arguments", "--host none is only for railing set.");
    }
    std::vector<PlanXY> points;
    if (!options.points.empty() && !parsePoints(options.points, points)) {
        return badArguments(command, "bad_arguments", "--points is x,y pairs in metres, for example 1,1,5,1,5,4.");
    }
    Answer refused;
    if (!checkMaterials(command, context, options, refused)) {
        return refused;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        const Plan& plan = document.plan();
        Answer failed;
        const Floor* floor = nullptr;
        if (options.around.empty() || !options.floor.empty()) {
            floor = resolveFloor(command, plan, options.floor, failed);
            if (floor == nullptr) {
                return failed;
            }
        }
        Railing values;
        applyOptions(options, values);
        std::vector<std::vector<PlanXY>> lines;
        if (options.around.empty()) {
            lines.push_back(points);
        } else {
            if (floor == nullptr) {
                floor = findFloorContaining(plan, options.around);
            }
            const Deck* deck = floor == nullptr ? nullptr : findDeck(*floor, options.around);
            if (deck == nullptr) {
                return failure(command, fail(ErrorCode::NotFound, "There is no deck " + options.around +
                                                                      (options.floor.empty() ? std::string(".") : " on " + floor->id + ".")));
            }
            lines = openDeckEdges(*floor, *deck);
            if (lines.empty()) {
                return failure(command, fail(ErrorCode::InvalidValue, "Every edge of deck " + deck->id +
                                                                          " is along a wall, so there is no open edge for a railing."));
            }
            values.host = deck->id;
        }
        const Id floorId = floor->id;
        auto group = std::make_unique<CommandGroup>(lines.size() == 1 ? "Add railing" : "Add railings");
        std::vector<const AddRailingCommand*> adds;
        for (const std::vector<PlanXY>& line : lines) {
            Railing railing = values;
            railing.points = line;
            auto add = std::make_unique<AddRailingCommand>(floorId, railing);
            adds.push_back(add.get());
            group->add(std::move(add));
        }
        if (!runCommand(command, document, std::move(group), failed)) {
            return failed;
        }
        std::vector<Id> ids;
        for (const AddRailingCommand* add : adds) {
            ids.push_back(add->newRailingId());
        }
        return success(command, addedJson(document.plan(), ids));
    });
}

// Changes the values of a railing that were given. What is left out stays.
Answer railingSet(const RunContext& context, const EditOptions& options) {
    const std::string command = "railing set";
    if (options.points.empty() && options.closed.empty() && options.host.empty() && !options.base &&
        !options.height && options.style.empty() && options.material.empty() && options.glassMaterial.empty()) {
        return badArguments(command, "bad_arguments",
                            "Give at least one of --points, --closed, --host, --base, --height, --style, --material "
                            "and --glass-material.");
    }
    std::vector<PlanXY> points;
    if (!options.points.empty() && !parsePoints(options.points, points)) {
        return badArguments(command, "bad_arguments", "--points is x,y pairs in metres, for example 1,1,5,1,5,4.");
    }
    Answer refused;
    if (!checkMaterials(command, context, options, refused)) {
        return refused;
    }
    return editProject(command, context, options.project, [&](Document& document) {
        const Floor* floor = findFloorContaining(document.plan(), options.id);
        const Railing* current = floor == nullptr ? nullptr : findRailing(*floor, options.id);
        if (current == nullptr) {
            return failure(command, fail(ErrorCode::NotFound, "There is no railing " + options.id + "."));
        }
        Railing values = *current;
        applyOptions(options, values);
        if (!options.points.empty()) {
            values.points = points;
        }
        Answer failed;
        if (!runCommand(command, document, std::make_unique<SetRailingCommand>(options.id, values), failed)) {
            return failed;
        }
        return success(command, entryFor(document.plan(), options.id));
    });
}

// Removes a railing.
Answer railingRemove(const RunContext& context, const RemoveOptions& options) {
    return edit(
        "railing remove", context, options.project,
        [&](const Plan&) { return std::make_unique<RemoveRailingCommand>(options.id); },
        [&](const Plan&, const RemoveRailingCommand&) {
            Json result = Json::object();
            result["removed"] = options.id;
            return result;
        });
}

// Lists the railings of one floor or of every floor.
Answer railingList(const RunContext& context, const ListOptions& options) {
    const std::string command = "railing list";
    Document document;
    Answer failed;
    if (!openProject(command, resolveProjectFolder(context, options.project), document, failed, true)) {
        return failed;
    }
    const Plan& plan = document.plan();
    if (!options.floor.empty() && findFloor(plan, options.floor) == nullptr) {
        return failure(command, fail(ErrorCode::NotFound, "There is no floor " + options.floor + "."));
    }
    Json railings = Json::array();
    for (const Floor& floor : plan.floors) {
        if (!options.floor.empty() && floor.id != options.floor) {
            continue;
        }
        for (const Railing& railing : floor.railings) {
            railings.push_back(railingEntry(plan, floor, railing));
        }
    }
    Json result = Json::object();
    result["railings"] = std::move(railings);
    return success(command, std::move(result));
}

// Lists the railing styles with their names and the heights each can be built at.
Answer railingStyles() {
    Json styles = Json::array();
    for (const RailingStyle* style : allRailingStyles()) {
        Json item = Json::object();
        item["id"] = std::string(style->id());
        item["name"] = std::string(style->name());
        item["min_height"] = style->minHeight();
        item["max_height"] = style->maxHeight();
        styles.push_back(std::move(item));
    }
    Json result = Json::object();
    result["styles"] = std::move(styles);
    return success("railing styles", std::move(result));
}

// The options railing add and railing set share.
void addEditOptions(CLI::App& sub, EditOptions& options) {
    addProjectOption(sub, options.project);
    addLibraryOption(sub, options.library);
    sub.add_option("--base", options.base, "Height of the foot above the floor's level when no deck holds it, metres.")
        ->type_name("metres");
    sub.add_option("--height", options.height, "Height of the railing above its foot, metres.")->type_name("metres");
    sub.add_option("--material", options.material, "Material id for the posts, rails and infill, or none.")
        ->type_name("id");
    sub.add_option("--glass-material", options.glassMaterial, "Material id for glass panels, or none.")
        ->type_name("id");
}

}

std::vector<CommandSpec> railingCommands() {
    CommandSpec add;
    add.words = {"railing", "add"};
    add.description = "Add a railing along a line, or round the open edges of a deck.";
    add.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<EditOptions>();
        sub.add_option("--floor", options->floor, "Floor of the railing. Default: the first floor, or the deck's.")
            ->type_name("id");
        sub.add_option("--points", options->points, "The line as x,y pairs in metres, for example 1,1,5,1,5,4.")
            ->type_name("x,y,...");
        sub.add_option("--around", options->around, "A deck id: one railing along each open stretch of its edge.")
            ->type_name("id");
        sub.add_option("--host", options->host, "A deck the railing stands on, with --points.")->type_name("id");
        sub.add_option("--closed", options->closed, "Whether the line joins its last point back to its first.")
            ->type_name("true|false")
            ->check(CLI::IsMember({"true", "false"}));
        sub.add_option("--style", options->style, "Railing style. See railing styles.")->type_name("id")->required();
        addEditOptions(sub, *options);
        return [options](const RunContext& context) { return railingAdd(context, *options); };
    };
    CommandSpec set;
    set.words = {"railing", "set"};
    set.description = "Change the line, style, height, host or materials of a railing.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<EditOptions>();
        sub.add_option("id", options->id, "Railing id.")->type_name("id")->required();
        sub.add_option("--points", options->points, "A new line as x,y pairs in metres.")->type_name("x,y,...");
        sub.add_option("--host", options->host, "A deck the railing stands on, or none.")->type_name("id|none");
        sub.add_option("--closed", options->closed, "Whether the line joins its last point back to its first.")
            ->type_name("true|false")
            ->check(CLI::IsMember({"true", "false"}));
        sub.add_option("--style", options->style, "Railing style. See railing styles.")->type_name("id");
        addEditOptions(sub, *options);
        return [options](const RunContext& context) { return railingSet(context, *options); };
    };
    CommandSpec remove;
    remove.words = {"railing", "remove"};
    remove.description = "Remove a railing.";
    remove.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RemoveOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("id", options->id, "Railing id.")->type_name("id")->required();
        return [options](const RunContext& context) { return railingRemove(context, *options); };
    };
    CommandSpec list;
    list.words = {"railing", "list"};
    list.description = "List the railings with their line, length and problems.";
    list.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ListOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--floor", options->floor, "Only the railings of this floor.")->type_name("id");
        return [options](const RunContext& context) { return railingList(context, *options); };
    };
    CommandSpec styles;
    styles.words = {"railing", "styles"};
    styles.description = "List the railing styles and the heights each can be built at.";
    styles.setup = [](CLI::App&) -> Handler {
        return [](const RunContext&) { return railingStyles(); };
    };

    return {add, set, remove, list, styles};
}

}
