#include "../Common.h"

#include <plynth/core/commands/FloorCommands.h>
#include <plynth/core/commands/PointCommands.h>
#include <plynth/core/commands/WallCommands.h>

#include <memory>
#include <optional>

namespace plynth::cli {

namespace {

// Loads the project, runs one command through the document and saves. Nothing is saved when the
// command is refused. After a success, done builds the result from the changed document.
template <typename MakeCommand, typename BuildResult>
Answer edit(const std::string& command, const RunContext& context, const std::string& projectOption,
            MakeCommand makeCommand, BuildResult buildResult) {
    const std::filesystem::path folder = resolveProjectFolder(context, projectOption);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed)) {
        return failed;
    }
    auto made = makeCommand(document.plan());
    auto* raw = made.get();
    const Status run = document.execute(std::move(made));
    if (!run.ok()) {
        return failure(command, run);
    }
    const Status saved = saveInPlace(document);
    if (!saved.ok()) {
        return failure(command, saved);
    }
    return success(command, buildResult(document.plan(), *raw));
}

struct ScaleOptions {
    std::string project;
    double pixelsPerMetre = 0.0;
};

struct PointOptions {
    std::string project;
    std::string floor;
    double x = 0.0;
    double y = 0.0;
    double cornerRadius = 0.0;
};

struct WallOptions {
    std::string project;
    std::string from;
    std::string to;
    double thickness = kDefaultWallThickness;
    std::optional<double> height;
};

// Sets the scale of the plan.
Answer scaleSet(const RunContext& context, const ScaleOptions& options) {
    Json result = Json::object();
    return edit(
        "scale set", context, options.project,
        [&](const Plan&) { return std::make_unique<SetScaleCommand>(options.pixelsPerMetre); },
        [&](const Plan& plan, const SetScaleCommand&) {
            result["pixels_per_metre"] = *plan.scale.pixelsPerMetre;
            return result;
        });
}

// Adds a point to a floor.
Answer pointAdd(const RunContext& context, const PointOptions& options) {
    return edit(
        "point add", context, options.project,
        [&](const Plan& plan) {
            const std::string floor = options.floor.empty() ? defaultFloorId(plan) : options.floor;
            return std::make_unique<AddPointCommand>(floor, options.x, options.y, options.cornerRadius);
        },
        [&](const Plan& plan, const AddPointCommand& added) {
            const Floor* floor = findFloorContaining(plan, added.pointId());
            const Point* point = findPoint(*floor, added.pointId());
            Json result = Json::object();
            result["id"] = point->id;
            result["floor"] = floor->id;
            result["x"] = point->x;
            result["y"] = point->y;
            result["corner_radius"] = point->cornerRadius;
            return result;
        });
}

// Adds a wall between two points.
Answer wallAdd(const RunContext& context, const WallOptions& options) {
    return edit(
        "wall add", context, options.project,
        [&](const Plan&) {
            return std::make_unique<AddWallCommand>(options.from, options.to, options.thickness, options.height);
        },
        [&](const Plan& plan, const AddWallCommand& added) {
            const Floor* floor = findFloorContaining(plan, added.wallId());
            const Wall* wall = findWall(*floor, added.wallId());
            Json result = Json::object();
            result["id"] = wall->id;
            result["floor"] = floor->id;
            result["from"] = wall->from;
            result["to"] = wall->to;
            result["thickness"] = wall->thickness;
            if (wall->height) {
                result["height"] = *wall->height;
            } else {
                result["height"] = nullptr;
            }
            return result;
        });
}

}

std::vector<CommandSpec> editCommands() {
    CommandSpec scale;
    scale.words = {"scale", "set"};
    scale.description = "Set how many image pixels make one metre.";
    scale.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ScaleOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--pixels-per-metre", options->pixelsPerMetre, "Image pixels in one metre.")
            ->type_name("number")
            ->required();
        return [options](const RunContext& context) { return scaleSet(context, *options); };
    };

    CommandSpec point;
    point.words = {"point", "add"};
    point.description = "Add a wall corner point to a floor.";
    point.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<PointOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--x", options->x, "Position to the right, in metres.")->type_name("metres")->required();
        sub.add_option("--y", options->y, "Position down, in metres.")->type_name("metres")->required();
        sub.add_option("--floor", options->floor, "Floor id. Default: the first floor.")->type_name("id");
        sub.add_option("--corner-radius", options->cornerRadius, "Corner rounding in metres. Default: 0.")
            ->type_name("metres");
        return [options](const RunContext& context) { return pointAdd(context, *options); };
    };

    CommandSpec wall;
    wall.words = {"wall", "add"};
    wall.description = "Add a straight wall between two points.";
    wall.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<WallOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--from", options->from, "Id of the first point.")->type_name("id")->required();
        sub.add_option("--to", options->to, "Id of the second point.")->type_name("id")->required();
        sub.add_option("--thickness", options->thickness, "Wall thickness in metres. Default: 0.2.")
            ->type_name("metres");
        sub.add_option("--height", options->height, "Wall height in metres. Default: the floor's ceiling height.")
            ->type_name("metres");
        return [options](const RunContext& context) { return wallAdd(context, *options); };
    };

    return {scale, point, wall};
}

}
