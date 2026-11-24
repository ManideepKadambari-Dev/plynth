#include "plynth/core/Render.h"

#include "BuildLockFiles.h"
#include "FileIo.h"

#include "plynth/core/Build.h"
#include "plynth/core/BuildJob.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/ProjectJson.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <fstream>
#include <random>
#include <string>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr double kThreeQuarterYaw = 45.0;
constexpr double kThreeQuarterPitch = 25.0;
constexpr double kTopYaw = 0.0;

// The folders of a project a picture may not be written into.
constexpr std::array<std::wstring_view, 4> kGuardedFolders = {L"plans", L"build", L"export", L"pack"};

// A path in lower case with forward slashes, for comparing paths the way Windows does.
std::wstring folded(const fs::path& path)
{
    std::wstring text = path.lexically_normal().generic_wstring();
    for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
    while (text.size() > 1 && text.back() == L'/') text.pop_back();
    return text;
}

// Says whether path is folder itself or somewhere below it.
bool isInside(const fs::path& path, const fs::path& folder)
{
    const std::wstring p = folded(path);
    const std::wstring f = folded(folder);
    return p == f || (p.size() > f.size() && p.compare(0, f.size(), f) == 0 && p[f.size()] == L'/');
}

// The plan of the last successful build. Empty when the file is missing or cannot be read.
std::optional<std::pair<Plan, std::string>> readBuilt(const fs::path& projectFolder)
{
    std::ifstream in(buildFolder(projectFolder) / pathFromUtf8(kBuiltPlanFileName), std::ios::binary);
    if (!in) return std::nullopt;
    const Json json = Json::parse(in, nullptr, false);
    if (!json.is_object() || !json.contains("plan") || !json.contains("plan_fingerprint")) return std::nullopt;
    if (!json["plan_fingerprint"].is_string()) return std::nullopt;
    PlanRead read = planFromJson(json["plan"]);
    if (!read.status.ok()) return std::nullopt;
    return std::make_pair(std::move(read.plan), json["plan_fingerprint"].get<std::string>());
}

// A file name beside out that no other render uses at the same time.
fs::path temporaryFor(const fs::path& out)
{
    std::random_device device;
    const unsigned value = device();
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%08x", value);
    return out.parent_path() / (L"." + out.stem().wstring() + L".rendering-" + pathFromUtf8(hex).wstring() + L".png");
}

// Number formatting used in messages, without trailing zeros.
std::string number(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

}

// "three-quarter" or "top".
std::string_view renderViewName(RenderView view)
{
    return view == RenderView::Top ? "top" : "three-quarter";
}

// The view for a name, or empty when the name is unknown.
std::optional<RenderView> renderViewFromName(std::string_view name)
{
    if (name == "three-quarter") return RenderView::ThreeQuarter;
    if (name == "top") return RenderView::Top;
    return std::nullopt;
}

// The yaw of the options, or the view's.
double renderYaw(const RenderOptions& options)
{
    if (options.yaw) return *options.yaw;
    return options.view == RenderView::Top ? kTopYaw : kThreeQuarterYaw;
}

// The pitch of the options, or the view's.
double renderPitch(const RenderOptions& options)
{
    if (options.pitch) return *options.pitch;
    return options.view == RenderView::Top ? kMaxRenderPitch : kThreeQuarterPitch;
}

// Checks everything a render needs, in the order a user would fix it: the project, the build, the options.
RenderPlan prepareRender(const fs::path& folder, const Project& marked, const RenderOptions& options)
{
    RenderPlan plan;
    plan.options = options;
    std::error_code ec;
    plan.folder = fs::absolute(folder, ec).lexically_normal();
    if (ec) plan.folder = folder.lexically_normal();

    if (!isProjectFolder(plan.folder)) {
        plan.status = fail(ErrorCode::NotAProject, "The folder " + pathToUtf8(plan.folder) + " holds no project.");
        return plan;
    }

    const fs::path build = buildFolder(plan.folder);
    const fs::path lock = build / pathFromUtf8(kBuildLockFileName);
    if (fs::exists(lock, ec) && lockfile::lockIsLive(lock)) {
        plan.status = fail(ErrorCode::InUse, "A build or an export of this project is running. Render when it ends.");
        return plan;
    }

    plan.fbx = build / pathFromUtf8(kBuildFbxFileName);
    auto built = readBuilt(plan.folder);
    if (!built || !fs::is_regular_file(plan.fbx, ec)) {
        plan.status = fail(ErrorCode::NotFound, "There is no build of this project. Build the project first.");
        return plan;
    }
    plan.built = std::move(built->first);
    plan.current = built->second == planFingerprint(marked);

    if (options.width < kMinRenderSide || options.width > kMaxRenderSide || options.height < kMinRenderSide ||
        options.height > kMaxRenderSide) {
        plan.status = fail(ErrorCode::InvalidValue, "The picture size must be " + std::to_string(kMinRenderSide) +
                                                        " to " + std::to_string(kMaxRenderSide) +
                                                        " pixels on each side, not " + std::to_string(options.width) +
                                                        "x" + std::to_string(options.height) + ".");
        return plan;
    }
    if (options.yaw && (!std::isfinite(*options.yaw) || *options.yaw < 0.0 || *options.yaw >= 360.0)) {
        plan.status = fail(ErrorCode::InvalidValue, "The yaw must be at least 0 and below 360 degrees, not " +
                                                        number(*options.yaw) + ".");
        return plan;
    }
    if (options.pitch &&
        (!std::isfinite(*options.pitch) || *options.pitch < kMinRenderPitch || *options.pitch > kMaxRenderPitch)) {
        plan.status = fail(ErrorCode::InvalidValue, "The pitch must be " + number(kMinRenderPitch) + " to " +
                                                        number(kMaxRenderPitch) + " degrees, not " +
                                                        number(*options.pitch) + ".");
        return plan;
    }
    plan.options.yaw = renderYaw(options);
    plan.options.pitch = renderPitch(options);

    if (options.cutFloor) {
        const auto& floors = plan.built.floors;
        const auto it = std::find_if(floors.begin(), floors.end(), [&](const Floor& f) { return f.id == *options.cutFloor; });
        if (it == floors.end()) {
            plan.status = fail(ErrorCode::NotFound, "The last build has no floor " + *options.cutFloor + ".");
            return plan;
        }
        plan.cutFloorIndex = static_cast<int>(it - floors.begin());
    }

    fs::path out = options.out;
    if (out.empty()) out = fs::path(pathFromUtf8(kRendersFolderName)) / (pathFromUtf8(renderViewName(options.view)).wstring() + L".png");
    if (out.is_relative()) out = plan.folder / out;
    out = out.lexically_normal();
    std::wstring extension = out.extension().wstring();
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    if (out.filename().empty() || out.stem().empty() || extension != L".png") {
        plan.status = fail(ErrorCode::InvalidValue, "The picture file must end in .png: " + pathToUtf8(out) + ".");
        return plan;
    }
    for (std::wstring_view guarded : kGuardedFolders) {
        if (isInside(out, plan.folder / fs::path(guarded))) {
            plan.status = fail(ErrorCode::InvalidValue, "A picture may not be written into the project's " +
                                                            pathToUtf8(fs::path(guarded)) + " folder. Use renders/.");
            return plan;
        }
    }
    if (fs::is_directory(out, ec)) {
        plan.status = fail(ErrorCode::InvalidValue, pathToUtf8(out) + " is a folder, not a picture file.");
        return plan;
    }
    plan.out = out;
    return plan;
}

// Prepares, lets the renderer draw into a temporary file, checks it and puts it in place.
RenderReport renderBuilding(const fs::path& folder, const Project& marked, const RenderOptions& options,
                            const SceneRenderer& renderer, std::stop_token stop)
{
    RenderReport report;
    report.options = options;
    const RenderPlan plan = prepareRender(folder, marked, options);
    if (!plan.status.ok()) {
        report.status = plan.status;
        return report;
    }
    report.options = plan.options;
    report.current = plan.current;
    if (plan.cutFloorIndex >= 0) report.cutFloor = plan.built.floors[static_cast<size_t>(plan.cutFloorIndex)].id;
    if (!renderer) {
        report.status = fail(ErrorCode::GraphicsUnavailable, "This program cannot draw the 3D view. Run plynth render from the installed Plynth.");
        return report;
    }
    if (stop.stop_requested()) {
        report.status = fail(ErrorCode::Cancelled, "The render was stopped.");
        return report;
    }

    std::error_code ec;
    fs::create_directories(plan.out.parent_path(), ec);
    if (!fs::is_directory(plan.out.parent_path(), ec)) {
        report.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(plan.out.parent_path()) + " could not be created.");
        return report;
    }

    const RenderShot shot{plan, temporaryFor(plan.out)};
    const auto cleanUp = [&]() {
        std::error_code ignored;
        fs::remove(shot.temporaryPng, ignored);
    };
    Status drawn;
    try {
        drawn = renderer(shot, stop);
    } catch (const std::exception& error) {
        drawn = fail(ErrorCode::RenderFailed, std::string("The 3D view failed: ") + error.what());
    }
    if (drawn.ok() && stop.stop_requested()) drawn = fail(ErrorCode::Cancelled, "The render was stopped.");
    if (!drawn.ok()) {
        cleanUp();
        report.status = drawn;
        return report;
    }
    int width = 0;
    int height = 0;
    if (!readPngSize(shot.temporaryPng, width, height)) {
        cleanUp();
        report.status = fail(ErrorCode::RenderFailed, "The 3D view gave no picture.");
        return report;
    }
    if (width != plan.options.width || height != plan.options.height) {
        cleanUp();
        report.status = fail(ErrorCode::RenderFailed, "The 3D view gave a picture of " + std::to_string(width) + "x" +
                                                          std::to_string(height) + " instead of " +
                                                          std::to_string(plan.options.width) + "x" +
                                                          std::to_string(plan.options.height) + ".");
        return report;
    }
    if (Status moved = fileio::renameOver(shot.temporaryPng, plan.out); !moved.ok()) {
        cleanUp();
        report.status = moved;
        return report;
    }
    report.file = plan.out;
    report.width = width;
    report.height = height;
    if (!plan.current) {
        BuildProblem problem;
        problem.code = "build_out_of_date";
        problem.severity = BuildSeverity::Warning;
        problem.message = "The plan changed since the last build. The picture shows the last build.";
        problem.step = "render";
        report.problems.push_back(std::move(problem));
    }
    return report;
}

// The result object of a render.
Json renderReportToJson(const RenderReport& report)
{
    Json json = Json::object();
    json["file"] = pathToUtf8(report.file);
    json["width"] = report.width;
    json["height"] = report.height;
    json["view"] = std::string(renderViewName(report.options.view));
    json["yaw"] = renderYaw(report.options);
    json["pitch"] = renderPitch(report.options);
    json["floor"] = report.cutFloor.empty() ? Json(nullptr) : Json(report.cutFloor);
    json["roofs"] = report.options.showRoofs;
    json["current"] = report.current;
    return json;
}

// Reads the size from the IHDR chunk that every PNG starts with.
bool readPngSize(const fs::path& file, int& width, int& height)
{
    std::ifstream in(file, std::ios::binary);
    unsigned char head[24] = {};
    if (!in.read(reinterpret_cast<char*>(head), sizeof(head))) return false;
    static constexpr unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!std::equal(std::begin(kSignature), std::end(kSignature), head)) return false;
    if (head[12] != 'I' || head[13] != 'H' || head[14] != 'D' || head[15] != 'R') return false;
    const auto big = [&](int at) {
        return (static_cast<long long>(head[at]) << 24) | (head[at + 1] << 16) | (head[at + 2] << 8) | head[at + 3];
    };
    const long long w = big(16);
    const long long h = big(20);
    if (w <= 0 || h <= 0 || w > 1'000'000 || h > 1'000'000) return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

}
