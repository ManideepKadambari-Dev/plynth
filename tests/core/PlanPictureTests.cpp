#include "TestSupport.h"

#include <plynth/core/PlanPicture.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/Render.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// The plan of the Blender fixture (from_core.json): two rooms side by side with a divider wall, one curved
// wall, one rounded corner, one opening of each built in shape and a door on the curved wall.
Plan fromCorePlan()
{
    Plan plan = makeEmptyPlan();
    Floor& f = plan.floors[0];
    f.points = {{"p1", 0.0, 0.0, 0.6}, {"p2", 4.0, 0.0, 0.0}, {"p3", 8.0, 0.0, 0.0},
                {"p4", 8.0, 4.0, 0.0}, {"p5", 4.0, 4.0, 0.0}, {"p6", 0.0, 4.0, 0.0}};
    const char* ends[7][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p4"}, {"p4", "p5"}, {"p5", "p6"}, {"p6", "p1"}, {"p2", "p5"}};
    for (int i = 0; i < 7; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.2;
        if (w.id == "w3") w.bulge = -0.5;
        f.walls.push_back(w);
    }
    Room left;
    left.id = "r1";
    left.type = "living";
    left.walls = {"w1", "w7", "w5", "w6"};
    Room right;
    right.id = "r2";
    right.type = "bedroom";
    right.walls = {"w2", "w3", "w4", "w7"};
    f.rooms = {left, right};

    const auto opening = [&](const std::string& id, const std::string& wall, double offset, OpeningType type,
                             const std::string& shape, double width, double height, double sill) {
        Opening o;
        o.id = id;
        o.wall = wall;
        o.offset = offset;
        o.type = type;
        o.shape = shape;
        o.width = width;
        o.height = height;
        o.sill = sill;
        f.openings.push_back(o);
    };
    opening("o1", "w1", 2.0, OpeningType::Door, "rectangle", 0.9, 2.1, 0.0);
    opening("o2", "w5", 2.0, OpeningType::Window, "round_arch", 1.0, 1.4, 0.9);
    opening("o3", "w6", 2.0, OpeningType::Window, "flat_arch", 1.2, 1.2, 0.9);
    opening("o4", "w4", 2.0, OpeningType::Plain, "pointed_arch", 1.0, 2.0, 0.0);
    opening("o5", "w2", 2.0, OpeningType::Window, "circle", 1.0, 1.0, 1.0);
    opening("o6", "w3", 2.1, OpeningType::Door, "rectangle", 0.9, 2.1, 0.0);
    opening("o7", "w7", 2.0, OpeningType::Door, "rectangle", 0.9, 2.1, 0.0);
    return plan;
}

// The pixel of the picture at a plan position.
std::array<std::uint8_t, 3> pixelAt(const PlanPicture& picture, Vec2 position)
{
    const Vec2 p = planToPicture(picture, position);
    const int x = static_cast<int>(std::floor(p.x));
    const int y = static_cast<int>(std::floor(p.y));
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < picture.image.width);
    REQUIRE(y < picture.image.height);
    return picture.image.at(x, y);
}

// The label with this id. Null when there is none.
const PlanLabel* labelOf(const PlanPicture& picture, const std::string& id)
{
    const auto it = std::find_if(picture.labels.begin(), picture.labels.end(), [&](const PlanLabel& l) { return l.id == id; });
    return it == picture.labels.end() ? nullptr : &*it;
}

}

TEST_CASE("the plan picture of the fixture has the asked long side and the plan's proportions", "[core][planpicture]") {
    const Plan plan = fromCorePlan();
    PlanPictureOptions options;
    options.longSide = 1000;
    const PlanPicture picture = drawPlanPicture(plan, plan.floors[0], nullptr, options);
    requireOk(picture.status);
    CHECK(picture.image.width == 1000);
    CHECK(picture.image.height > kMinPlanPictureSide);
    CHECK(picture.image.height < 1000);
    CHECK(picture.image.pixels.size() == static_cast<std::size_t>(picture.image.width) * picture.image.height * 3);
    CHECK(picture.pixelsPerMetre > 0.0);
    CHECK(picture.origin.x < -0.1);
    CHECK(picture.origin.y < -0.1);
    CHECK_FALSE(picture.imageShown);
}

TEST_CASE("walls are signal blue bands, rooms are tinted and the sheet stays plain", "[core][planpicture]") {
    const Plan plan = fromCorePlan();
    PlanPictureOptions options;
    options.longSide = 1200;
    options.labels = false;
    const PlanPicture picture = drawPlanPicture(plan, plan.floors[0], nullptr, options);
    requireOk(picture.status);

    // The middle of plain wall w5, away from window o2.
    const auto wall = pixelAt(picture, {3.0, 4.0});
    CHECK(wall[2] >= 150);
    CHECK(wall[2] > wall[0] + 80);
    CHECK(wall[1] > wall[0] + 50);

    // Inside room r1, away from every door swing.
    const auto room = pixelAt(picture, {1.5, 2.5});
    CHECK(room[2] > 0x2C + 15);
    CHECK(room[0] < 0x2C);

    // Outside the building, above window o5: the Raised sheet colour of the theme.
    const auto sheet = pixelAt(picture, {6.0, -0.35});
    CHECK(sheet[0] == 0x1B);
    CHECK(sheet[1] == 0x22);
    CHECK(sheet[2] == 0x2C);
}

TEST_CASE("an opening cuts its wall band", "[core][planpicture]") {
    const Plan plan = fromCorePlan();
    PlanPictureOptions options;
    options.longSide = 1200;
    options.labels = false;
    const PlanPicture picture = drawPlanPicture(plan, plan.floors[0], nullptr, options);
    requireOk(picture.status);
    // The middle of window o2 on w5 is a gap, between its two glass lines.
    const auto gap = pixelAt(picture, {2.0, 4.0});
    CHECK(gap[2] < 150);
}

TEST_CASE("labels carry the plan ids, the room type and area", "[core][planpicture]") {
    const Plan plan = fromCorePlan();
    const PlanPicture picture = drawPlanPicture(plan, plan.floors[0], nullptr, PlanPictureOptions{});
    requireOk(picture.status);
    CHECK(picture.image.width == kDefaultPlanPictureSide);

    const PlanLabel* room = labelOf(picture, "r2");
    REQUIRE(room != nullptr);
    CHECK(room->kind == "room");
    CHECK(room->text.find("r2 bedroom") != std::string::npos);
    CHECK(room->text.find(" m2") != std::string::npos);
    for (const std::string id : {"f1", "r1", "w1", "w3", "w7", "o1", "o5", "o6"}) {
        INFO(id);
        CHECK(labelOf(picture, id) != nullptr);
    }
    CHECK(labelOf(picture, "w3")->kind == "wall");
    CHECK(labelOf(picture, "o5")->kind == "opening");
    CHECK(labelOf(picture, "f1")->kind == "floor");
    for (const PlanLabel& label : picture.labels) {
        INFO(label.id);
        CHECK(label.x >= 0);
        CHECK(label.y >= 0);
        CHECK(label.width > 0);
        CHECK(label.height > 0);
        CHECK(label.x + label.width <= picture.image.width);
        CHECK(label.y + label.height <= picture.image.height);
    }

    PlanPictureOptions bare;
    bare.labels = false;
    CHECK(drawPlanPicture(plan, plan.floors[0], nullptr, bare).labels.empty());
}

TEST_CASE("items, stairs and lights get their own labels", "[core][planpicture]") {
    Plan plan = fromCorePlan();
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    plan.floors.push_back(upper);
    Floor& f = plan.floors[0];
    Item sofa;
    sofa.id = "i4";
    sofa.asset = "sofa_01";
    sofa.x = 2.0;
    sofa.y = 2.0;
    sofa.size = {2.0, 0.9, 0.8};
    f.items.push_back(sofa);
    Stair stair;
    stair.id = "s1";
    stair.x = 6.0;
    stair.y = 0.5;
    f.stairs.push_back(stair);
    LightMarker light;
    light.id = "l1";
    light.x = 1.0;
    light.y = 1.0;
    light.z = 2.5;
    f.lights.push_back(light);

    PlanPictureOptions options;
    options.grid = true;
    const PlanPicture picture = drawPlanPicture(plan, f, nullptr, options);
    requireOk(picture.status);
    REQUIRE(labelOf(picture, "i4") != nullptr);
    CHECK(labelOf(picture, "i4")->text == "i4 sofa_01");
    REQUIRE(labelOf(picture, "s1") != nullptr);
    CHECK(labelOf(picture, "s1")->kind == "stair");

    const PlanPicture upperPicture = drawPlanPicture(plan, plan.floors[1], nullptr, options);
    requireOk(upperPicture.status);
}

TEST_CASE("the plan image is drawn only with a scale and when asked", "[core][planpicture]") {
    Plan plan = fromCorePlan();
    Floor& f = plan.floors[0];
    f.imageOffset = {-3.0, -3.0};
    GrayImage ink;
    ink.width = 100;
    ink.height = 50;
    ink.pixels.assign(100 * 50, 0);

    PlanPictureOptions options;
    options.labels = false;
    const PlanPicture noScale = drawPlanPicture(plan, f, &ink, options);
    requireOk(noScale.status);
    CHECK_FALSE(noScale.imageShown);

    plan.scale.pixelsPerMetre = 50.0;
    const PlanPicture shown = drawPlanPicture(plan, f, &ink, options);
    requireOk(shown.status);
    CHECK(shown.imageShown);
    CHECK(shown.origin.x <= -3.0);
    CHECK(shown.origin.y <= -3.0);
    const auto dark = pixelAt(shown, {-2.0, -2.5});
    CHECK(dark[0] <= 0x12);
    CHECK(dark[2] <= 0x18);

    options.image = false;
    const PlanPicture hidden = drawPlanPicture(plan, f, &ink, options);
    requireOk(hidden.status);
    CHECK_FALSE(hidden.imageShown);
}

TEST_CASE("an empty floor gives a square picture and a size out of range is refused", "[core][planpicture]") {
    const Plan plan = makeEmptyPlan();
    PlanPictureOptions options;
    options.longSide = 300;
    const PlanPicture empty = drawPlanPicture(plan, plan.floors[0], nullptr, options);
    requireOk(empty.status);
    CHECK(empty.image.width == 300);
    CHECK(empty.image.height == 300);

    for (const int side : {kMinPlanPictureSide - 1, kMaxPlanPictureSide + 1}) {
        options.longSide = side;
        CHECK(drawPlanPicture(plan, plan.floors[0], nullptr, options).status.code == ErrorCode::InvalidValue);
    }
}

TEST_CASE("renderPlanPicture writes the PNG and refuses bad floors and paths", "[core][planpicture]") {
    TempDir tmp("plan-picture");
    const fs::path folder = tmp.path() / "project";
    Project project = makeNewProject();
    project.plan = fromCorePlan();
    requireOk(saveProject(project, folder, {}));

    PlanRenderOptions options;
    options.picture.longSide = 640;
    const PlanRenderReport report = renderPlanPicture(folder, project, options);
    requireOk(report.status);
    CHECK(report.file == (folder / "renders" / "plan-f1.png").lexically_normal());
    int width = 0;
    int height = 0;
    REQUIRE(readPngSize(report.file, width, height));
    CHECK(width == 640);
    CHECK(height == report.height);
    CHECK(report.floor == "f1");
    CHECK(report.problems.empty());

    const Json json = planRenderReportToJson(report);
    for (const char* key : {"file", "width", "height", "scale", "origin", "floor", "image", "labels"}) {
        INFO(key);
        CHECK(json.contains(key));
    }
    CHECK(json["width"] == 640);
    CHECK(json["floor"] == "f1");
    CHECK(json["image"] == false);
    CHECK(json["labels"].is_array());

    options.floor = "f9";
    CHECK(renderPlanPicture(folder, project, options).status.code == ErrorCode::NotFound);
    options.floor.clear();
    options.out = "plans/plan.png";
    CHECK(renderPlanPicture(folder, project, options).status.code == ErrorCode::InvalidValue);
    options.out = "renders/plan.jpg";
    CHECK(renderPlanPicture(folder, project, options).status.code == ErrorCode::InvalidValue);
    options.out.clear();
    CHECK(renderPlanPicture(tmp.path() / "nothing", project, options).status.code == ErrorCode::NotAProject);
}

TEST_CASE("a floor image without a scale gives the no_scale warning", "[core][planpicture]") {
    TempDir tmp("plan-picture-scale");
    const fs::path folder = tmp.path() / "project";
    Project project = makeNewProject();
    project.plan = fromCorePlan();
    project.plan.floors[0].image = "plans/missing.png";
    requireOk(saveProject(project, folder, {}));

    const PlanRenderReport noScale = renderPlanPicture(folder, project, {});
    requireOk(noScale.status);
    REQUIRE(noScale.problems.size() == 1);
    CHECK(noScale.problems[0].code == "no_scale");
    CHECK_FALSE(noScale.imageShown);

    project.plan.scale.pixelsPerMetre = 100.0;
    const PlanRenderReport missing = renderPlanPicture(folder, project, {});
    requireOk(missing.status);
    REQUIRE(missing.problems.size() == 1);
    CHECK(missing.problems[0].code == "image_missing");

    PlanRenderOptions bare;
    bare.picture.image = false;
    CHECK(renderPlanPicture(folder, project, bare).problems.empty());
}
