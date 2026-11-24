#include "SquareFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Build.h>
#include <plynth/core/EditSession.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Render.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Writes the start of a PNG file: enough for the size check, not a picture anyone can open.
void writePngHead(const fs::path& file, int width, int height)
{
    std::ofstream out(file, std::ios::binary);
    const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.write(reinterpret_cast<const char*>(signature), 8);
    const auto big = [&](unsigned value) {
        const char bytes[4] = {static_cast<char>(value >> 24), static_cast<char>(value >> 16),
                               static_cast<char>(value >> 8), static_cast<char>(value)};
        out.write(bytes, 4);
    };
    big(13);
    out.write("IHDR", 4);
    big(static_cast<unsigned>(width));
    big(static_cast<unsigned>(height));
    const char rest[9] = {8, 6, 0, 0, 0, 0, 0, 0, 0};
    out.write(rest, 9);
}

// The whole file as text.
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Seconds since 1970 now.
long long epochNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A saved and built project with the square on two floors, in a folder whose name has a space and an umlaut.
struct RenderFixture {
    explicit RenderFixture(const std::string& tag) : tmp(tag)
    {
        folder = tmp.path() / fs::path(u8"Mein Projekt ä");
        project = squareProjectWithWalls();
        Floor upper = squareFloorWithWalls();
        upper.id = "f2";
        upper.name = "Floor 2";
        project.plan.floors.push_back(upper);
        requireOk(saveProject(project, folder, {}));
        build(project);
    }

    // Writes build/built-plan.json and a stand in building.fbx for this project.
    void build(const Project& built) const
    {
        fs::create_directories(folder / "build");
        Json json = Json::object();
        json["plan_fingerprint"] = planFingerprint(built);
        json["plan"] = planToJson(built.plan);
        std::ofstream(folder / "build" / "built-plan.json", std::ios::binary) << json.dump(2);
        std::ofstream(folder / "build" / "building.fbx", std::ios::binary) << "FBX";
    }

    fs::path lockFile() const { return folder / "build" / "build.lock"; }

    TempDir tmp;
    fs::path folder;
    Project project;
};

// A renderer that writes a picture of the asked size, and counts its calls.
struct FakeRenderer {
    int calls = 0;
    int widthOverride = 0;
    bool writeNothing = false;
    Status answer;
    const RenderPlan* lastPlan = nullptr;
    RenderOptions lastOptions;
    int lastCut = -2;
    fs::path lastFbx;
    fs::path lastTemporary;

    SceneRenderer get()
    {
        return [this](const RenderShot& shot, std::stop_token) {
            ++calls;
            lastOptions = shot.plan.options;
            lastCut = shot.plan.cutFloorIndex;
            lastFbx = shot.plan.fbx;
            lastTemporary = shot.temporaryPng;
            if (!answer.ok()) return answer;
            if (!writeNothing) {
                writePngHead(shot.temporaryPng, widthOverride > 0 ? widthOverride : shot.plan.options.width,
                             shot.plan.options.height);
            }
            return Status{};
        };
    }
};

// Says whether a folder holds a leftover temporary render file.
bool hasTemporary(const fs::path& folder)
{
    std::error_code ec;
    if (!fs::exists(folder, ec)) return false;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.path().filename().wstring().find(L".rendering-") != std::wstring::npos) return true;
    }
    return false;
}

}

TEST_CASE("a render without a build is not_found and draws nothing", "[render]") {
    TempDir tmp("render-nobuild");
    const fs::path folder = tmp.path() / "P";
    Project project = squareProjectWithWalls();
    requireOk(saveProject(project, folder, {}));
    FakeRenderer fake;
    const RenderReport report = renderBuilding(folder, project, {}, fake.get());
    REQUIRE(report.status.code == ErrorCode::NotFound);
    REQUIRE(report.status.message.find("Build the project first") != std::string::npos);
    REQUIRE(fake.calls == 0);
}

TEST_CASE("a render of a folder without a project is not_a_project", "[render]") {
    TempDir tmp("render-noproject");
    FakeRenderer fake;
    const RenderReport report = renderBuilding(tmp.path(), squareProjectWithWalls(), {}, fake.get());
    REQUIRE(report.status.code == ErrorCode::NotAProject);
    REQUIRE(fake.calls == 0);
}

TEST_CASE("a running build lock is in_use and a stale one does not count", "[render]") {
    RenderFixture f("render-lock");
    FakeRenderer fake;
    {
        const ProcessIdentity me = currentProcess();
        std::ofstream(f.lockFile(), std::ios::binary) << me.pid << " " << me.startKey << " " << epochNow() << "\n";
    }
    REQUIRE(renderBuilding(f.folder, f.project, {}, fake.get()).status.code == ErrorCode::InUse);
    REQUIRE(fake.calls == 0);
    REQUIRE(fs::exists(f.lockFile()));

    std::ofstream(f.lockFile(), std::ios::binary) << 999999 << " " << 1 << " " << epochNow() - 100000 << "\n";
    requireOk(renderBuilding(f.folder, f.project, {}, fake.get()).status);
    REQUIRE(fake.calls == 1);
}

TEST_CASE("a current build gives no warning and a changed plan gives build_out_of_date", "[render]") {
    RenderFixture f("render-current");
    FakeRenderer fake;
    RenderReport report = renderBuilding(f.folder, f.project, {}, fake.get());
    requireOk(report.status);
    REQUIRE(report.current);
    REQUIRE(report.problems.empty());

    Project changed = f.project;
    changed.plan.floors[0].walls[0].thickness = 0.3;
    report = renderBuilding(f.folder, changed, {}, fake.get());
    requireOk(report.status);
    REQUIRE_FALSE(report.current);
    REQUIRE(report.problems.size() == 1);
    REQUIRE(report.problems[0].code == "build_out_of_date");
    REQUIRE(report.problems[0].severity == BuildSeverity::Warning);
    REQUIRE(fs::exists(report.file));
}

TEST_CASE("the cut floor is looked up in the built plan, not the marked one", "[render]") {
    RenderFixture f("render-cut");
    Project marked = f.project;
    marked.plan.floors[1].id = "f7";
    FakeRenderer fake;
    RenderOptions options;
    options.cutFloor = "f2";
    RenderReport report = renderBuilding(f.folder, marked, options, fake.get());
    requireOk(report.status);
    REQUIRE(fake.lastCut == 1);
    REQUIRE(report.cutFloor == "f2");

    options.cutFloor = "f7";
    report = renderBuilding(f.folder, marked, options, fake.get());
    REQUIRE(report.status.code == ErrorCode::NotFound);
    REQUIRE(report.status.message.find("f7") != std::string::npos);

    options.cutFloor.reset();
    requireOk(renderBuilding(f.folder, marked, options, fake.get()).status);
    REQUIRE(fake.lastCut == -1);
}

TEST_CASE("sizes and angles out of range are invalid_value", "[render]") {
    RenderFixture f("render-ranges");
    FakeRenderer fake;
    const auto code = [&](RenderOptions options) { return renderBuilding(f.folder, f.project, options, fake.get()).status.code; };

    RenderOptions o;
    o.width = kMinRenderSide - 1;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o.width = kMaxRenderSide + 1;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o = {};
    o.height = kMinRenderSide - 1;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o = {};
    o.yaw = 360.0;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o.yaw = -0.5;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o.yaw = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o = {};
    o.pitch = 1.9;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    o.pitch = 90.0;
    REQUIRE(code(o) == ErrorCode::InvalidValue);
    REQUIRE(fake.calls == 0);

    o = {};
    o.width = kMinRenderSide;
    o.height = kMaxRenderSide;
    o.yaw = 359.5;
    o.pitch = kMinRenderPitch;
    REQUIRE(code(o) == ErrorCode::None);
    REQUIRE(fake.lastOptions.yaw == 359.5);
    REQUIRE(fake.lastOptions.pitch == kMinRenderPitch);
}

TEST_CASE("each view fills in its own yaw and pitch and names its default file", "[render]") {
    RenderFixture f("render-views");
    FakeRenderer fake;
    RenderOptions o;
    RenderReport report = renderBuilding(f.folder, f.project, o, fake.get());
    requireOk(report.status);
    REQUIRE(*fake.lastOptions.yaw == 45.0);
    REQUIRE(*fake.lastOptions.pitch == 25.0);
    REQUIRE(report.file == f.folder / "renders" / "three-quarter.png");
    REQUIRE(fake.lastFbx == f.folder / "build" / "building.fbx");

    o.view = RenderView::Top;
    report = renderBuilding(f.folder, f.project, o, fake.get());
    requireOk(report.status);
    REQUIRE(*fake.lastOptions.yaw == 0.0);
    REQUIRE(*fake.lastOptions.pitch == kMaxRenderPitch);
    REQUIRE(report.file == f.folder / "renders" / "top.png");
    REQUIRE(fake.lastTemporary.parent_path() == report.file.parent_path());
    REQUIRE(fake.lastTemporary.extension() == ".png");
}

TEST_CASE("the output must be a png outside the project's own folders", "[render]") {
    RenderFixture f("render-paths");
    FakeRenderer fake;
    const auto code = [&](const fs::path& out) {
        RenderOptions o;
        o.out = out;
        return renderBuilding(f.folder, f.project, o, fake.get()).status.code;
    };
    REQUIRE(code("renders/pic.jpg") == ErrorCode::InvalidValue);
    REQUIRE(code("renders/pic") == ErrorCode::InvalidValue);
    REQUIRE(code("build/pic.png") == ErrorCode::InvalidValue);
    REQUIRE(code("Build/pic.png") == ErrorCode::InvalidValue);
    REQUIRE(code("plans/pic.png") == ErrorCode::InvalidValue);
    REQUIRE(code("export/sub/pic.png") == ErrorCode::InvalidValue);
    REQUIRE(code("pack/pic.png") == ErrorCode::InvalidValue);
    REQUIRE(code("renders/../build/pic.png") == ErrorCode::InvalidValue);
    REQUIRE(code(f.folder / "export" / "pic.png") == ErrorCode::InvalidValue);
    fs::create_directories(f.folder / "renders" / "folder.png");
    REQUIRE(code("renders/folder.png") == ErrorCode::InvalidValue);
    REQUIRE(fake.calls == 0);

    REQUIRE(code("builds/pic.PNG") == ErrorCode::None);
    REQUIRE(fs::exists(f.folder / "builds" / "pic.PNG"));
    REQUIRE(code(fs::path(u8"renders/Ansicht ü/Bild 1.png")) == ErrorCode::None);
    REQUIRE(fs::exists(f.folder / fs::path(u8"renders/Ansicht ü/Bild 1.png")));
    const fs::path outside = f.tmp.path() / fs::path(u8"außen/pic.png");
    REQUIRE(code(outside) == ErrorCode::None);
    REQUIRE(fs::exists(outside));
    REQUIRE_FALSE(hasTemporary(f.folder));
    REQUIRE_FALSE(hasTemporary(f.tmp.path()));
}

TEST_CASE("an existing picture is replaced only after a good one", "[render]") {
    RenderFixture f("render-replace");
    const fs::path out = f.folder / "renders" / "three-quarter.png";
    fs::create_directories(out.parent_path());
    std::ofstream(out, std::ios::binary) << "OLD";

    FakeRenderer failing;
    failing.answer = fail(ErrorCode::RenderFailed, "Only the background was drawn.");
    REQUIRE(renderBuilding(f.folder, f.project, {}, failing.get()).status.code == ErrorCode::RenderFailed);
    REQUIRE(readAll(out) == "OLD");

    FakeRenderer wrongSize;
    wrongSize.widthOverride = 100;
    RenderReport report = renderBuilding(f.folder, f.project, {}, wrongSize.get());
    REQUIRE(report.status.code == ErrorCode::RenderFailed);
    REQUIRE(report.status.message.find("100x720") != std::string::npos);
    REQUIRE(readAll(out) == "OLD");

    FakeRenderer empty;
    empty.writeNothing = true;
    REQUIRE(renderBuilding(f.folder, f.project, {}, empty.get()).status.code == ErrorCode::RenderFailed);
    REQUIRE(readAll(out) == "OLD");
    REQUIRE_FALSE(hasTemporary(f.folder));

    FakeRenderer good;
    report = renderBuilding(f.folder, f.project, {}, good.get());
    requireOk(report.status);
    int width = 0;
    int height = 0;
    REQUIRE(readPngSize(out, width, height));
    REQUIRE(width == 1280);
    REQUIRE(height == 720);
    REQUIRE(report.width == 1280);
    REQUIRE(report.height == 720);
    REQUIRE_FALSE(hasTemporary(f.folder));
}

TEST_CASE("a stopped render is cancelled and leaves the old picture", "[render]") {
    RenderFixture f("render-cancel");
    const fs::path out = f.folder / "renders" / "three-quarter.png";
    fs::create_directories(out.parent_path());
    std::ofstream(out, std::ios::binary) << "OLD";

    std::stop_source before;
    before.request_stop();
    FakeRenderer fake;
    REQUIRE(renderBuilding(f.folder, f.project, {}, fake.get(), before.get_token()).status.code == ErrorCode::Cancelled);
    REQUIRE(fake.calls == 0);

    std::stop_source during;
    const SceneRenderer stopper = [&](const RenderShot& shot, std::stop_token) {
        writePngHead(shot.temporaryPng, shot.plan.options.width, shot.plan.options.height);
        during.request_stop();
        return Status{};
    };
    REQUIRE(renderBuilding(f.folder, f.project, {}, stopper, during.get_token()).status.code == ErrorCode::Cancelled);
    REQUIRE(readAll(out) == "OLD");
    REQUIRE_FALSE(hasTemporary(f.folder));
}

TEST_CASE("without a renderer a render is graphics_unavailable", "[render]") {
    RenderFixture f("render-norenderer");
    const RenderReport report = renderBuilding(f.folder, f.project, {}, SceneRenderer{});
    REQUIRE(report.status.code == ErrorCode::GraphicsUnavailable);
    REQUIRE_FALSE(fs::exists(f.folder / "renders"));
}

TEST_CASE("a renderer that throws is render_failed", "[render]") {
    RenderFixture f("render-throws");
    const SceneRenderer thrower = [](const RenderShot&, std::stop_token) -> Status { throw std::runtime_error("boom"); };
    const RenderReport report = renderBuilding(f.folder, f.project, {}, thrower);
    REQUIRE(report.status.code == ErrorCode::RenderFailed);
    REQUIRE_FALSE(hasTemporary(f.folder));
}

TEST_CASE("the render answer has every field", "[render]") {
    RenderFixture f("render-json");
    FakeRenderer fake;
    RenderOptions o;
    o.width = 640;
    o.height = 480;
    o.view = RenderView::Top;
    o.cutFloor = "f1";
    o.showRoofs = false;
    const RenderReport report = renderBuilding(f.folder, f.project, o, fake.get());
    requireOk(report.status);
    const Json json = renderReportToJson(report);
    REQUIRE(json.at("file").get<std::string>() == pathToUtf8(f.folder / "renders" / "top.png"));
    REQUIRE(json.at("file").get<std::string>().find('\\') == std::string::npos);
    REQUIRE(json.at("width") == 640);
    REQUIRE(json.at("height") == 480);
    REQUIRE(json.at("view") == "top");
    REQUIRE(json.at("yaw") == 0.0);
    REQUIRE(json.at("pitch") == kMaxRenderPitch);
    REQUIRE(json.at("floor") == "f1");
    REQUIRE(json.at("roofs") == false);
    REQUIRE(json.at("current") == true);

    o.cutFloor.reset();
    REQUIRE(renderReportToJson(renderBuilding(f.folder, f.project, o, fake.get())).at("floor").is_null());
}

TEST_CASE("view names read and write both ways", "[render]") {
    REQUIRE(renderViewName(RenderView::ThreeQuarter) == "three-quarter");
    REQUIRE(renderViewName(RenderView::Top) == "top");
    REQUIRE(renderViewFromName("top") == RenderView::Top);
    REQUIRE(renderViewFromName("three-quarter") == RenderView::ThreeQuarter);
    REQUIRE_FALSE(renderViewFromName("Top").has_value());
    REQUIRE_FALSE(renderViewFromName("").has_value());
}

TEST_CASE("readPngSize refuses a file that is not a png", "[render]") {
    TempDir tmp("render-png");
    int w = 0;
    int h = 0;
    REQUIRE_FALSE(readPngSize(tmp.path() / "missing.png", w, h));
    std::ofstream(tmp.path() / "text.png", std::ios::binary) << "this is not a picture at all";
    REQUIRE_FALSE(readPngSize(tmp.path() / "text.png", w, h));
    writePngHead(tmp.path() / "ok.png", 64, 4096);
    REQUIRE(readPngSize(tmp.path() / "ok.png", w, h));
    REQUIRE(w == 64);
    REQUIRE(h == 4096);
}
