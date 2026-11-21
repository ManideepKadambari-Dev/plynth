#include "../Common.h"

#include <plynth/core/Geometry.h>
#include <plynth/core/commands/FloorCommands.h>

#include <memory>

namespace plynth::cli {

namespace {

struct ScaleSetOptions {
    std::string project;
    double pixelsPerMetre = 0.0;
    std::string keep = "image";
};

struct FromPointsOptions {
    std::string project;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double distance = 0.0;
    std::string keep = "image";
};

// What stays put when the scale changes, from the --keep value.
ScaleKeep keepFromName(const std::string& name) {
    return name == "metres" ? ScaleKeep::Metres : ScaleKeep::ImagePositions;
}

// Adds the --keep option shared by both scale commands.
void addKeepOption(CLI::App& sub, std::string& keep) {
    sub.add_option("--keep", keep, "What stays put when a scale already exists. Default: image.")
        ->type_name("image|metres")
        ->check(CLI::IsMember({"image", "metres"}));
}

// Sets the scale of the plan.
Answer scaleSet(const RunContext& context, const ScaleSetOptions& options) {
    return edit(
        "scale set", context, options.project,
        [&](const Plan&) {
            return std::make_unique<SetScaleCommand>(options.pixelsPerMetre, keepFromName(options.keep));
        },
        [&](const Plan& plan, const SetScaleCommand& command) {
            Json result = Json::object();
            result["pixels_per_metre"] = *plan.scale.pixelsPerMetre;
            result["moved_marks"] = command.movedMarks();
            return result;
        });
}

// Measures the scale from two image points and a real distance, then sets it.
Answer scaleFromPoints(const RunContext& context, const FromPointsOptions& options) {
    const std::string name = "scale from-points";
    return editProject(name, context, options.project, [&](Document& document) {
        const ScaleMeasure measure =
            scaleFromImagePoints({options.x1, options.y1}, {options.x2, options.y2}, options.distance);
        if (!measure.status.ok()) {
            return failure(name, measure.status);
        }
        auto command = std::make_unique<SetScaleCommand>(measure.pixelsPerMetre, keepFromName(options.keep));
        const SetScaleCommand* raw = command.get();
        Answer failed;
        if (!runCommand(name, document, std::move(command), failed)) {
            return failed;
        }
        Json result = Json::object();
        result["pixels_per_metre"] = measure.pixelsPerMetre;
        result["pixel_distance"] = measure.pixelDistance;
        result["distance"] = options.distance;
        result["moved_marks"] = raw->movedMarks();
        return success(name, std::move(result));
    });
}

}

std::vector<CommandSpec> scaleCommands() {
    CommandSpec set;
    set.words = {"scale", "set"};
    set.description = "Set how many image pixels make one metre.";
    set.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<ScaleSetOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--pixels-per-metre", options->pixelsPerMetre, "Image pixels in one metre.")
            ->type_name("number")
            ->required();
        addKeepOption(sub, options->keep);
        return [options](const RunContext& context) { return scaleSet(context, *options); };
    };

    CommandSpec fromPoints;
    fromPoints.words = {"scale", "from-points"};
    fromPoints.description = "Set the scale from two points on the plan image and the real distance between them.";
    fromPoints.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<FromPointsOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--x1", options->x1, "First point, pixels from the image's left edge.")
            ->type_name("pixels")
            ->required();
        sub.add_option("--y1", options->y1, "First point, pixels from the image's top edge.")
            ->type_name("pixels")
            ->required();
        sub.add_option("--x2", options->x2, "Second point, pixels from the image's left edge.")
            ->type_name("pixels")
            ->required();
        sub.add_option("--y2", options->y2, "Second point, pixels from the image's top edge.")
            ->type_name("pixels")
            ->required();
        sub.add_option("--distance", options->distance, "Real distance between the two points, in metres.")
            ->type_name("metres")
            ->required();
        addKeepOption(sub, options->keep);
        return [options](const RunContext& context) { return scaleFromPoints(context, *options); };
    };

    return {set, fromPoints};
}

}
