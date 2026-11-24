#include "../Common.h"

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

// Saves a picture of the last build, drawn by the program's own 3D view.
Answer renderProject(const RunContext& context, const RenderCliOptions& o) {
    const std::string command = "render";
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
    const RenderReport report = renderBuilding(folder, document.project(), options, renderer);
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
    render.description = "Save a picture of the last build, drawn by Plynth's own 3D view. Starts no Blender and "
                         "never builds.";
    render.setup = [](CLI::App& sub) -> Handler {
        auto options = std::make_shared<RenderCliOptions>();
        addProjectOption(sub, options->project);
        sub.add_option("--out", options->out,
                       "The PNG file to write. Relative paths are inside the project folder. Default: "
                       "renders/<view>.png.");
        sub.add_option("--size", options->size, "Width and height in pixels, 64 to 4096 each, such as 1280x720 "
                                                "(the default).");
        sub.add_option("--view", options->view, "three-quarter (the default) or top.")
            ->check(CLI::IsMember({"three-quarter", "top"}));
        options->yawOption = sub.add_option("--yaw", options->yaw,
                                            "Turn around the building in degrees, 0 to below 360. Default: the view's.");
        options->pitchOption = sub.add_option("--pitch", options->pitch,
                                              "Look down in degrees, 2 to 89.9. Default: the view's.");
        sub.add_option("--floor", options->floor,
                       "Cut the building at this floor: show it and the floors below, without its roofs.");
        sub.add_flag("--no-roofs", options->noRoofs, "Hide every roof.");
        return [options](const RunContext& context) { return renderProject(context, *options); };
    };
    return {render};
}

}
