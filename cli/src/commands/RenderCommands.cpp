#include "../Common.h"

#include <plynth/core/PlanPicture.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Render.h>

#include <charconv>
#include <memory>
#include <string>
#include <string_view>

namespace plynth::cli {

namespace {

struct RenderCliOptions {
    std::string project;
    std::string out;
    std::string size;
    std::string view = "three-quarter";
    double yaw = 0.0;
    double pitch = 0.0;
    std::string floor;
    bool noRoofs = false;
    bool plan = false;
    bool noImage = false;
    bool noLabels = false;
    bool grid = false;
    CLI::Option* viewOption = nullptr;
    CLI::Option* yawOption = nullptr;
    CLI::Option* pitchOption = nullptr;
};

// Reads one whole number. False when the text is not one or does not fit.
bool readNumber(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

// Reads "WxH" into a width and a height.
bool readSize(const std::string& text, int& width, int& height) {
    const auto cross = text.find('x');
    if (cross == std::string::npos) {
        return false;
    }
    return readNumber(std::string_view(text).substr(0, cross), width) &&
           readNumber(std::string_view(text).substr(cross + 1), height);
}

// Says whether an option was given on the command line.
bool given(const CLI::Option* option) {
    return option != nullptr && option->count() > 0;
}

// Saves a picture of one floor's plan with its marks on top. Reads the project only, so it works while a
// session is active, and needs no build.
Answer renderPlan(const RunContext& context, const RenderCliOptions& o) {
    const std::string command = "render";
    if (given(o.viewOption) || given(o.yawOption) || given(o.pitchOption) || o.noRoofs) {
        return badArguments(command, "bad_arguments",
                            "--view, --yaw, --pitch and --no-roofs are for pictures of the build, not with --plan.");
    }
    PlanRenderOptions options;
    if (!o.size.empty() && !readNumber(o.size, options.picture.longSide)) {
        return failure(command, fail(ErrorCode::InvalidValue, "With --plan the size is one whole number, the pixels on "
                                                              "the long side such as 2000, not '" + o.size + "'."));
    }
    options.picture.image = !o.noImage;
    options.picture.labels = !o.noLabels;
    options.picture.grid = o.grid;
    options.floor = o.floor;
    options.out = pathFromUtf8(o.out);

    const std::filesystem::path folder = resolveProjectFolder(context, o.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }
    const PlanRenderReport report = renderPlanPicture(folder, document.project(), options);
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, planRenderReportToJson(report));
    for (const BuildProblem& problem : report.problems) {
        answer.warnings.push_back({problem.code, problem.message});
    }
    return answer;
}

// Saves a picture of the last build, drawn by the program's own 3D view.
Answer renderProject(const RunContext& context, const RenderCliOptions& o) {
    const std::string command = "render";
    if (o.plan) {
        return renderPlan(context, o);
    }
    if (o.noImage || o.noLabels || o.grid) {
        return badArguments(command, "bad_arguments", "--no-image, --no-labels and --grid only go with --plan.");
    }
    const std::filesystem::path folder = resolveProjectFolder(context, o.project);
    Document document;
    Answer failed;
    if (!openProject(command, folder, document, failed, true)) {
        return failed;
    }

    RenderOptions options;
    if (!o.size.empty() && !readSize(o.size, options.width, options.height)) {
        return failure(command, fail(ErrorCode::InvalidValue, "The size must be two whole numbers such as 1280x720, not '" +
                                                                  o.size + "'."));
    }
    options.view = renderViewFromName(o.view).value_or(RenderView::ThreeQuarter);
    if (o.yawOption != nullptr && o.yawOption->count() > 0) {
        options.yaw = o.yaw;
    }
    if (o.pitchOption != nullptr && o.pitchOption->count() > 0) {
        options.pitch = o.pitch;
    }
    if (!o.floor.empty()) {
        options.cutFloor = o.floor;
    }
    options.showRoofs = !o.noRoofs;
    options.out = pathFromUtf8(o.out);

    const SceneRenderer renderer = context.host != nullptr ? context.host->render : SceneRenderer{};
    const RenderReport report = renderBuilding(folder, document.project(), options, renderer, stopToken(context));
    if (!report.status.ok()) {
        return failure(command, report.status);
    }
    Answer answer = success(command, renderReportToJson(report));
    for (const BuildProblem& problem : report.problems) {
        answer.warnings.push_back({problem.code, problem.message});
    }
    return answer;
}

}

std::vector<CommandSpec> renderCommands() {
    CommandSpec render;
    render.words = {"render"};
    render.description = "Save a picture of the last build, drawn by Plynth's own 3D view, or with --plan a picture "
                         "of one floor's plan with its marks on top. Starts no Blender and never builds.";
    render.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RenderCliOptions>();
        addProjectOption(sub, options->project);
        sub.add_flag("--plan", options->plan,
                     "Draw one floor's plan image with the walls, points, openings, rooms, items and stairs on top, "
                     "labelled with their ids. Needs no build and no graphics driver.");
        sub.add_option("--out", options->out,
                       "The PNG file to write. Relative paths are inside the project folder. Default: "
                       "renders/<view>.png, or renders/plan-<floor>.png with --plan.");
        sub.add_option("--size", options->size, "Width and height in pixels, 64 to 4096 each, such as 1280x720 "
                                                "(the default). With --plan one number: the pixels on the long side, "
                                                "64 to 4096 (default 2000).");
        options->viewOption = sub.add_option("--view", options->view, "three-quarter (the default) or top.")
                                  ->check(CLI::IsMember({"three-quarter", "top"}));
        options->yawOption = sub.add_option("--yaw", options->yaw,
                                            "Turn around the building in degrees, 0 to below 360. Default: the view's.");
        options->pitchOption = sub.add_option("--pitch", options->pitch,
                                              "Look down in degrees, 2 to 89.9. Default: the view's.");
        sub.add_option("--floor", options->floor,
                       "Cut the building at this floor: show it and the floors below, without its roofs. With --plan: "
                       "the floor to draw (default: the first).");
        sub.add_flag("--no-roofs", options->noRoofs, "Hide every roof.");
        sub.add_flag("--no-image", options->noImage, "With --plan: leave the plan image out and draw the marks only.");
        sub.add_flag("--no-labels", options->noLabels, "With --plan: leave the id labels out.");
        sub.add_flag("--grid", options->grid, "With --plan: add a 1 m grid and a scale bar.");
        return [options](const RunContext& context) { return renderProject(context, *options); };
    };
    return {render};
}

}
