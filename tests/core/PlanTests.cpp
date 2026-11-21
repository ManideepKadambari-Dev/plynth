#include "TestSupport.h"

#include <plynth/core/Plan.h>
#include <plynth/core/Project.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace plynth;
using plynth::test::fullPlan;

TEST_CASE("status names", "[core][status]") {
    REQUIRE(errorCodeName(ErrorCode::None) == "ok");
    REQUIRE(errorCodeName(ErrorCode::InvalidValue) == "invalid_value");
    REQUIRE(errorCodeName(ErrorCode::NotFound) == "not_found");
    REQUIRE(errorCodeName(ErrorCode::AlreadyExists) == "already_exists");
    REQUIRE(errorCodeName(ErrorCode::InUse) == "in_use");
    REQUIRE(errorCodeName(ErrorCode::NotAProject) == "not_a_project");
    REQUIRE(errorCodeName(ErrorCode::CannotRead) == "cannot_read");
    REQUIRE(errorCodeName(ErrorCode::CannotWrite) == "cannot_write");
    REQUIRE(errorCodeName(ErrorCode::BadFile) == "bad_file");
    REQUIRE(errorCodeName(ErrorCode::UnsupportedVersion) == "unsupported_version");
    REQUIRE(errorCodeName(ErrorCode::ImageError) == "image_error");

    const Status s = fail(ErrorCode::NotFound, "gone", "/x");
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.message == "gone");
    REQUIRE(s.where == "/x");
    REQUIRE(Status{}.ok());
}

TEST_CASE("empty plan and new project", "[core][plan]") {
    const Plan plan = makeEmptyPlan();
    REQUIRE(plan.floors.size() == 1);
    REQUIRE(plan.floors[0].id == "f1");
    REQUIRE(plan.floors[0].name == "Floor 1");
    REQUIRE_FALSE(plan.floors[0].image);
    REQUIRE_FALSE(plan.scale.pixelsPerMetre);
    plynth::test::requirePlanValid(plan);

    const Project p = makeNewProject();
    REQUIRE(p.plan == plan);
    REQUIRE(p.settings.name == "Untitled");
    REQUIRE(p.settings.gridSize == kDefaultGridSize);
}

TEST_CASE("names of export modes and opening types", "[core][plan]") {
    REQUIRE(exportModeName(ExportMode::Single) == "single");
    REQUIRE(exportModeName(ExportMode::Grouped) == "grouped");
    REQUIRE(exportModeFromName("grouped") == ExportMode::Grouped);
    REQUIRE_FALSE(exportModeFromName("double"));
    for (auto t : {OpeningType::Door, OpeningType::Window, OpeningType::Plain}) {
        REQUIRE(openingTypeFromName(openingTypeName(t)) == t);
    }
    REQUIRE_FALSE(openingTypeFromName("gate"));
}

TEST_CASE("ids", "[core][plan][id]") {
    REQUIRE(isValidId("p1"));
    REQUIRE(isValidId("a_b-c9"));
    REQUIRE_FALSE(isValidId(""));
    REQUIRE_FALSE(isValidId("1p"));
    REQUIRE_FALSE(isValidId("p 1"));
    REQUIRE_FALSE(isValidId("p/1"));
    REQUIRE_FALSE(isValidId(std::string(65, 'a')));
    REQUIRE(isValidId(std::string(64, 'a')));

    const Plan plan = fullPlan();
    REQUIRE(idInUse(plan, "f1"));
    REQUIRE(idInUse(plan, "p4"));
    REQUIRE(idInUse(plan, "w3"));
    REQUIRE(idInUse(plan, "r2"));
    REQUIRE(idInUse(plan, "o2"));
    REQUIRE_FALSE(idInUse(plan, "p5"));
    REQUIRE_FALSE(idInUse(plan, ""));

    REQUIRE(newId(plan, kPointIdPrefix) == "p5");
    REQUIRE(newId(plan, kWallIdPrefix) == "w4");
    REQUIRE(newId(plan, kRoomIdPrefix) == "r3");
    REQUIRE(newId(plan, kOpeningIdPrefix) == "o3");
    REQUIRE(newId(plan, kFloorIdPrefix) == "f3");
    REQUIRE(newId(makeEmptyPlan(), kPointIdPrefix) == "p1");
}

TEST_CASE("newId uses the highest number and ignores other ids", "[core][plan][id]") {
    Plan plan = makeEmptyPlan();
    plan.floors[0].points = {{"p3", 0, 0, 0}, {"p10", 1, 0, 0}, {"pa", 2, 0, 0}, {"p7x", 3, 0, 0}};
    REQUIRE(newId(plan, "p") == "p11");
    plan.floors[0].points.pop_back();
    plan.floors[0].points.pop_back();
    plan.floors[0].points.erase(plan.floors[0].points.begin() + 1);
    REQUIRE(newId(plan, "p") == "p4");
}

TEST_CASE("lookups", "[core][plan]") {
    Plan plan = fullPlan();
    REQUIRE(findFloor(plan, "f2") != nullptr);
    REQUIRE(findFloor(plan, "p1") == nullptr);
    Floor* f = findFloorContaining(plan, "w2");
    REQUIRE(f != nullptr);
    REQUIRE(f->id == "f1");
    REQUIRE(findFloorContaining(plan, "w9") == nullptr);
    REQUIRE(findPoint(*f, "p2")->x == 4.0);
    REQUIRE(findPoint(*f, "w1") == nullptr);
    REQUIRE(findWall(*f, "w2")->thickness == 0.3);
    REQUIRE(findRoom(*f, "r1")->walls.size() == 2);
    REQUIRE(findOpening(*f, "o1")->shape == "round_arch");
    const Plan& cp = plan;
    REQUIRE(findFloorContaining(cp, "o2") != nullptr);
    REQUIRE(findPoint(*findFloor(cp, "f1"), "p1") != nullptr);
}

namespace {

// Applies a change to the full plan and expects checkPlan to refuse it with the given code and place.
template <typename Change>
void expectRefused(Change change, ErrorCode code, const std::string& where)
{
    Plan plan = fullPlan();
    change(plan);
    const Status s = checkPlan(plan);
    INFO("expected " << where << " but got " << s.where << ": " << s.message);
    REQUIRE_FALSE(s.ok());
    REQUIRE(s.code == code);
    REQUIRE(s.where == where);
    REQUIRE_FALSE(s.message.empty());
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

}

TEST_CASE("checkPlan accepts the full plan and an empty plan", "[core][plan][check]") {
    plynth::test::requirePlanValid(fullPlan());
    plynth::test::requirePlanValid(makeEmptyPlan());
}

TEST_CASE("checkPlan refuses each broken rule", "[core][plan][check]") {
    SECTION("no floors") {
        expectRefused([](Plan& p) { p.floors.clear(); }, ErrorCode::InvalidValue, "/floors");
    }
    SECTION("scale") {
        expectRefused([](Plan& p) { p.scale.pixelsPerMetre = 0.0; }, ErrorCode::InvalidValue, "/scale/pixels_per_metre");
        expectRefused([](Plan& p) { p.scale.pixelsPerMetre = kNaN; }, ErrorCode::InvalidValue, "/scale/pixels_per_metre");
        expectRefused([](Plan& p) { p.scale.pixelsPerMetre = -3.0; }, ErrorCode::InvalidValue, "/scale/pixels_per_metre");
    }
    SECTION("ids") {
        expectRefused([](Plan& p) { p.floors[0].id = "1f"; }, ErrorCode::InvalidValue, "/floors/0/id");
        expectRefused([](Plan& p) { p.floors[1].id = "f1"; }, ErrorCode::AlreadyExists, "/floors/1/id");
        expectRefused([](Plan& p) { p.floors[0].points[1].id = "p1"; }, ErrorCode::AlreadyExists, "/floors/0/points/1/id");
        expectRefused([](Plan& p) { p.floors[0].walls[2].id = "p1"; }, ErrorCode::AlreadyExists, "/floors/0/walls/2/id");
        expectRefused([](Plan& p) { p.floors[0].rooms[1].id = "w1"; }, ErrorCode::AlreadyExists, "/floors/0/rooms/1/id");
        expectRefused([](Plan& p) { p.floors[0].openings[1].id = "r1"; }, ErrorCode::AlreadyExists, "/floors/0/openings/1/id");
        expectRefused([](Plan& p) { p.floors[0].openings[0].id = ""; }, ErrorCode::InvalidValue, "/floors/0/openings/0/id");
    }
    SECTION("floor") {
        expectRefused([](Plan& p) { p.floors[1].ceilingHeight = 0.0; }, ErrorCode::InvalidValue, "/floors/1/ceiling_height");
        expectRefused([](Plan& p) { p.floors[0].image = ""; }, ErrorCode::InvalidValue, "/floors/0/image");
    }
    SECTION("points") {
        expectRefused([](Plan& p) { p.floors[0].points[2].x = kNaN; }, ErrorCode::InvalidValue, "/floors/0/points/2/x");
        expectRefused([](Plan& p) { p.floors[0].points[3].y = kInf; }, ErrorCode::InvalidValue, "/floors/0/points/3/y");
        expectRefused([](Plan& p) { p.floors[0].points[1].cornerRadius = -0.1; }, ErrorCode::InvalidValue,
                      "/floors/0/points/1/corner_radius");
    }
    SECTION("walls") {
        expectRefused([](Plan& p) { p.floors[0].walls[1].from = "p9"; }, ErrorCode::NotFound, "/floors/0/walls/1/from");
        expectRefused([](Plan& p) { p.floors[0].walls[1].to = "p9"; }, ErrorCode::NotFound, "/floors/0/walls/1/to");
        expectRefused([](Plan& p) { p.floors[0].walls[0].to = "p1"; }, ErrorCode::InvalidValue, "/floors/0/walls/0/to");
        expectRefused([](Plan& p) { p.floors[0].walls[0].thickness = 0.0; }, ErrorCode::InvalidValue, "/floors/0/walls/0/thickness");
        expectRefused([](Plan& p) { p.floors[0].walls[1].height = -1.0; }, ErrorCode::InvalidValue, "/floors/0/walls/1/height");
        expectRefused([](Plan& p) { p.floors[0].walls[2].bulge = kInf; }, ErrorCode::InvalidValue, "/floors/0/walls/2/bulge");
        expectRefused([](Plan& p) { p.floors[0].walls[1].materialInside = "no good"; }, ErrorCode::InvalidValue,
                      "/floors/0/walls/1/material_inside");
        expectRefused([](Plan& p) { p.floors[0].walls[1].materialOutside = ""; }, ErrorCode::InvalidValue,
                      "/floors/0/walls/1/material_outside");
    }
    SECTION("a wall may not use a point of another floor") {
        expectRefused(
            [](Plan& p) {
                p.floors[1].points = {{"p9", 0, 0, 0}};
                p.floors[0].walls[0].to = "p9";
            },
            ErrorCode::NotFound, "/floors/0/walls/0/to");
    }
    SECTION("rooms") {
        expectRefused([](Plan& p) { p.floors[0].rooms[0].walls[1] = "w9"; }, ErrorCode::NotFound, "/floors/0/rooms/0/walls/1");
        expectRefused([](Plan& p) { p.floors[0].rooms[0].type = "bad type"; }, ErrorCode::InvalidValue, "/floors/0/rooms/0/type");
        expectRefused([](Plan& p) { p.floors[0].rooms[0].floorMaterial = "a/b"; }, ErrorCode::InvalidValue,
                      "/floors/0/rooms/0/floor_material");
        expectRefused([](Plan& p) { p.floors[0].rooms[0].ceilingMaterial = "a b"; }, ErrorCode::InvalidValue,
                      "/floors/0/rooms/0/ceiling_material");
    }
    SECTION("openings") {
        expectRefused([](Plan& p) { p.floors[0].openings[0].wall = "w9"; }, ErrorCode::NotFound, "/floors/0/openings/0/wall");
        expectRefused([](Plan& p) { p.floors[0].openings[0].offset = -0.1; }, ErrorCode::InvalidValue, "/floors/0/openings/0/offset");
        expectRefused([](Plan& p) { p.floors[0].openings[0].shape = ""; }, ErrorCode::InvalidValue, "/floors/0/openings/0/shape");
        expectRefused([](Plan& p) { p.floors[0].openings[0].width = 0.0; }, ErrorCode::InvalidValue, "/floors/0/openings/0/width");
        expectRefused([](Plan& p) { p.floors[0].openings[0].height = kNaN; }, ErrorCode::InvalidValue, "/floors/0/openings/0/height");
        expectRefused([](Plan& p) { p.floors[0].openings[0].sill = -1.0; }, ErrorCode::InvalidValue, "/floors/0/openings/0/sill");
        expectRefused([](Plan& p) { p.floors[0].openings[0].frame = "x y"; }, ErrorCode::InvalidValue, "/floors/0/openings/0/frame");
    }
}
