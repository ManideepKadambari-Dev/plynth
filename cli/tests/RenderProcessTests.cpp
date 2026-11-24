#include "SampleProject.h"

#include <plynth/core/Build.h>
#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ProjectJson.h>
#include <plynth/core/Render.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// These tests run the real plynth.exe, which draws with the graphics card or the software one. They are hidden,
// so a normal test run skips them. The render check of the automatic build runs them with
//   PLYNTH_RENDER_EXE  the plynth.exe of the ready to run copy
// and QT_QPA_PLATFORM=windows, QSG_RHI_BACKEND=d3d11 and QSG_RHI_PREFER_SOFTWARE_RENDERER=1.

using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

// The program to run, from PLYNTH_RENDER_EXE. Empty when it is not set.
std::filesystem::path renderProgram() {
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "PLYNTH_RENDER_EXE") != 0 || value == nullptr) {
        return {};
    }
    const std::string text(value);
    std::free(value);
    return plynth::pathFromUtf8(text);
}

// Writes a box from low to high, in centimetres with Y up, as an ASCII FBX file.
void writeBoxFbx(const std::filesystem::path& file, double x0, double y0, double z0, double x1, double y1, double z1) {
    const double v[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    const int faces[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
    std::ostringstream vertices;
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 3; ++k) {
            vertices << (i == 0 && k == 0 ? "" : ",") << v[i][k];
        }
    }
    std::ostringstream indices;
    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 4; ++k) {
            const int index = k == 3 ? -faces[f][k] - 1 : faces[f][k];
            indices << (f == 0 && k == 0 ? "" : ",") << index;
        }
    }
    std::ofstream out(file, std::ios::binary);
    out << "; FBX 7.4.0 project file\n"
           "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
           "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
           "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
           "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
           "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n"
           "\t}\n}\n"
           "Objects:  {\n"
           "\tGeometry: 1000, \"Geometry::Box\", \"Mesh\" {\n"
           "\t\tVertices: *24 {\n\t\t\ta: "
        << vertices.str()
        << "\n\t\t}\n"
           "\t\tPolygonVertexIndex: *24 {\n\t\t\ta: "
        << indices.str()
        << "\n\t\t}\n"
           "\t\tGeometryVersion: 124\n"
           "\t}\n"
           "\tModel: 2000, \"Model::Box\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t}\n"
           "\t\tShading: T\n\t\tCulling: \"CullingOff\"\n\t}\n"
           "\tMaterial: 3000, \"Material::Grey\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n"
           "\t\tMultiLayer: 0\n\t\tProperties70:  {\n"
           "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.8,0.8\n\t\t}\n\t}\n"
           "}\n"
           "Connections:  {\n\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n\tC: \"OO\",3000,2000\n}\n";
}

// A saved project with a 4 m square room on two floors and a build of it whose model is one box.
std::filesystem::path makeBuiltProject(const std::filesystem::path& parent) {
    const std::filesystem::path folder = parent / std::filesystem::path(u8"Haus ä b");
    plynth::Project project = plynth::makeNewProject();
    plynth::Floor& floor = project.plan.floors[0];
    floor.id = "f1";
    floor.name = "Floor 1";
    floor.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 4.0, 4.0, 0.0}, {"p4", 0.0, 4.0, 0.0}};
    const char* ends[4][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p1"}};
    for (int i = 0; i < 4; ++i) {
        plynth::Wall wall;
        wall.id = "w" + std::to_string(i + 1);
        wall.from = ends[i][0];
        wall.to = ends[i][1];
        wall.thickness = 0.2;
        floor.walls.push_back(wall);
    }
    plynth::Floor upper = floor;
    upper.id = "f2";
    upper.name = "Floor 2";
    project.plan.floors.push_back(upper);
    REQUIRE(plynth::saveProject(project, folder, {}).ok());

    std::filesystem::create_directories(folder / "build");
    plynth::Json built = plynth::Json::object();
    built["plan_fingerprint"] = plynth::planFingerprint(project);
    built["plan"] = plynth::planToJson(project.plan);
    std::ofstream(folder / "build" / "built-plan.json", std::ios::binary) << built.dump(2);
    writeBoxFbx(folder / "build" / "building.fbx", -10, 0, -10, 410, 540, 410);
    return folder;
}

// Runs plynth render on a project and returns the answer. Fails the test unless the output is exactly one JSON
// line: Qt's messages and anything else must stay off both streams.
nlohmann::json runRender(const std::filesystem::path& program, const std::filesystem::path& folder,
                         std::vector<std::string> extra) {
    plynth::ProcessSpec spec;
    spec.program = program;
    spec.arguments = {"render", "--project", utf8(folder)};
    spec.arguments.insert(spec.arguments.end(), extra.begin(), extra.end());
    spec.timeout = std::chrono::minutes(4);
    std::vector<std::string> lines;
    const plynth::ProcessResult result = plynth::runProcess(spec, [&](std::string_view line) { lines.emplace_back(line); });
    INFO(result.outputTail);
    REQUIRE(result.status.ok());
    REQUIRE(lines.size() == 1);
    const nlohmann::json answer = nlohmann::json::parse(lines.front());
    INFO(lines.front());
    REQUIRE(answer.at("ok") == true);
    REQUIRE(result.exitCode == 0);
    return answer;
}

// Checks the PNG the answer names has the asked size.
void requireSize(const nlohmann::json& answer, int width, int height) {
    const std::filesystem::path file = plynth::pathFromUtf8(answer.at("result").at("file").get<std::string>());
    int w = 0;
    int h = 0;
    REQUIRE(plynth::readPngSize(file, w, h));
    CHECK(w == width);
    CHECK(h == height);
}

}

TEST_CASE("plynth render draws the build at the exact sizes asked", "[.graphics]") {
    const std::filesystem::path program = renderProgram();
    if (program.empty()) {
        SKIP("PLYNTH_RENDER_EXE is not set.");
    }
    TempFolder temp("render-process-sizes");
    const std::filesystem::path folder = makeBuiltProject(temp.path());
    for (const auto& [text, w, h] : std::vector<std::tuple<std::string, int, int>>{
             {"64x64", 64, 64}, {"1280x720", 1280, 720}, {"4096x4096", 4096, 4096}}) {
        INFO(text);
        const auto answer = runRender(program, folder, {"--size", text, "--out", "renders/size-" + text + ".png"});
        requireSize(answer, w, h);
    }
}

TEST_CASE("plynth render draws both views, a cut and no roofs", "[.graphics]") {
    const std::filesystem::path program = renderProgram();
    if (program.empty()) {
        SKIP("PLYNTH_RENDER_EXE is not set.");
    }
    TempFolder temp("render-process-views");
    const std::filesystem::path folder = makeBuiltProject(temp.path());
    auto answer = runRender(program, folder, {});
    CHECK(answer.at("result").at("view") == "three-quarter");
    requireSize(answer, 1280, 720);
    answer = runRender(program, folder, {"--view", "top"});
    CHECK(answer.at("result").at("view") == "top");
    answer = runRender(program, folder, {"--floor", "f1", "--out", "renders/cut.png"});
    CHECK(answer.at("result").at("floor") == "f1");
    answer = runRender(program, folder, {"--no-roofs", "--yaw", "200", "--pitch", "40", "--out", "renders/back.png"});
    CHECK(answer.at("result").at("roofs") == false);
    CHECK(answer.at("result").at("yaw") == 200.0);
}

TEST_CASE("plynth render draws the committed render fixtures", "[.graphics]") {
    const std::filesystem::path program = renderProgram();
    const std::filesystem::path fixtures = PLYNTH_RENDER_FIXTURES;
    std::error_code ec;
    if (program.empty() || !std::filesystem::is_directory(fixtures, ec)) {
        SKIP("PLYNTH_RENDER_EXE is not set or there are no render fixtures yet.");
    }
    TempFolder temp("render-process-fixtures");
    for (const auto& entry : std::filesystem::directory_iterator(fixtures)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::filesystem::path copy = temp.path() / entry.path().filename();
        std::filesystem::copy(entry.path(), copy, std::filesystem::copy_options::recursive);
        INFO(utf8(copy));
        requireSize(runRender(program, copy, {}), 1280, 720);
        requireSize(runRender(program, copy, {"--view", "top", "--no-roofs"}), 1280, 720);
    }
}
