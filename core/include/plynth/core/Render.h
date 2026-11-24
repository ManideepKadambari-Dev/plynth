#pragma once

#include "BuildProblem.h"
#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

namespace plynth {

using Json = nlohmann::ordered_json;

// Pictures of the last build (DESIGN.md section 7.18). Core checks the build, the options and the output path,
// and writes the answer. The drawing itself is a SceneRenderer that the program around core gives it: the app
// draws with its own real-time 3D view, the same scene as the 3D page. Core never starts Blender for a picture.
// The app's "Save 3D picture" and "plynth render" both call renderBuilding.
//
// A render reads build/built-plan.json and build/building.fbx (with build/preview/ when it is there). It never
// builds and writes nothing but the PNG. A plan changed since the build still gives a picture, with the warning
// build_out_of_date.

inline constexpr std::string_view kRendersFolderName = "renders";
inline constexpr int kMinRenderSide = 64;
inline constexpr int kMaxRenderSide = 4096;
inline constexpr double kMinRenderPitch = 2.0;
inline constexpr double kMaxRenderPitch = 89.9;

enum class RenderView {
    ThreeQuarter, // "three-quarter": from the front right, yaw 45, pitch 25
    Top,          // "top": straight down, yaw 0, pitch 89.9
};

std::string_view renderViewName(RenderView view);
std::optional<RenderView> renderViewFromName(std::string_view name);

struct RenderOptions {
    int width = 1280;
    int height = 720;
    RenderView view = RenderView::ThreeQuarter;
    std::optional<double> yaw;   // degrees, 0 to below 360; empty: the view's own
    std::optional<double> pitch; // degrees, kMinRenderPitch to kMaxRenderPitch; empty: the view's own
    std::optional<Id> cutFloor;  // show this floor and the ones below it, without its roofs
    bool showRoofs = true;
    // Empty: renders/<view>.png in the project folder. Relative: inside the project folder. Absolute: as it is.
    std::filesystem::path out;
};

// The yaw and pitch a render uses: the options' own, or the view's.
double renderYaw(const RenderOptions& options);
double renderPitch(const RenderOptions& options);

// Everything a renderer needs, checked and made absolute.
struct RenderPlan {
    // NotFound without a build or for an unknown cut floor, InUse while a build or an export runs, InvalidValue
    // for a size, angle or path out of range, NotAProject when the folder holds no project.
    Status status;
    Plan built;                  // read from build/built-plan.json
    std::filesystem::path folder; // the project folder, absolute
    std::filesystem::path fbx;   // build/building.fbx, absolute
    std::filesystem::path out;   // the PNG to write, absolute
    RenderOptions options;       // with yaw and pitch filled in
    int cutFloorIndex = -1;      // the cut floor's place in built.floors, -1 for none
    bool current = false;        // the build matches the plan as it is now
};

// Checks a render without drawing anything. The output must end in .png and may not be inside the project's
// plans/, build/, export/ or pack/ folders. A stale build lock does not count as running.
RenderPlan prepareRender(const std::filesystem::path& folder, const Project& marked, const RenderOptions& options);

// One picture to draw into temporaryPng, a PNG of exactly the plan's width and height, within the time limit.
struct RenderShot {
    const RenderPlan& plan;
    std::filesystem::path temporaryPng;
    std::chrono::seconds timeout{60};
};

// Draws one shot. Returns GraphicsUnavailable when the 3D view cannot start, RenderFailed when it gives no usable
// picture (only the background), Cancelled when stop was asked for.
using SceneRenderer = std::function<Status(const RenderShot&, std::stop_token)>;

struct RenderReport {
    Status status;
    std::filesystem::path file; // the PNG written, absolute; empty on failure
    int width = 0;
    int height = 0;
    RenderOptions options;      // as used, yaw and pitch filled in
    bool current = false;
    Id cutFloor;                // empty for none
    std::vector<BuildProblem> problems; // warnings such as build_out_of_date
};

// prepareRender, then the renderer draws into a temporary file beside the output, and only a good picture of the
// right size replaces an existing PNG. A failure or a cancel leaves the old one. No renderer: GraphicsUnavailable.
RenderReport renderBuilding(const std::filesystem::path& folder, const Project& marked, const RenderOptions& options,
                            const SceneRenderer& renderer, std::stop_token stop = {});

// The result of "plynth render": file, width, height, view, yaw, pitch, floor (null for none), roofs, current.
// Paths are absolute, UTF-8, forward slashes. Problems are not in it; the CLI gives them as warnings.
Json renderReportToJson(const RenderReport& report);

// Reads the width and height of a PNG file from its header. False when the file is not a PNG.
bool readPngSize(const std::filesystem::path& file, int& width, int& height);

}
