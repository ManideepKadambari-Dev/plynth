#pragma once

#include <plynth/core/Plan.h>
#include <plynth/core/Project.h>
#include <plynth/core/Status.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace plynth::test {

// A plan that sets every field, with two floors, so round trips cover each value.
inline Plan fullPlan()
{
    Plan plan;
    plan.scale.pixelsPerMetre = 52.4;

    Floor f1;
    f1.id = "f1";
    f1.name = "Ground";
    f1.image = "plans/floor1.png";
    f1.ceilingHeight = 2.7;
    f1.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.15}, {"p3", 4.0, 3.0, 0.0}, {"p4", 0.0, 3.0, 0.0}};
    Wall w1{"w1", "p1", "p2", 0.2, std::nullopt, 0.0, std::nullopt, std::nullopt};
    Wall w2{"w2", "p2", "p3", 0.3, 1.1, -0.4, std::string("plaster_white"), std::string("brick_red")};
    Wall w3{"w3", "p3", "p4", 0.2, std::nullopt, 0.25, std::nullopt, std::nullopt};
    f1.walls = {w1, w2, w3};
    Room r1;
    r1.id = "r1";
    r1.type = "kitchen";
    r1.walls = {"w1", "w2"};
    r1.floorMaterial = "oak_floor";
    r1.ceilingMaterial = "paint_white";
    Room r2;
    r2.id = "r2";
    r2.walls = {};
    f1.rooms = {r1, r2};
    Opening o1;
    o1.id = "o1";
    o1.wall = "w1";
    o1.offset = 1.5;
    o1.type = OpeningType::Window;
    o1.shape = "round_arch";
    o1.width = 1.2;
    o1.height = 1.4;
    o1.sill = 0.9;
    o1.frame = "frame_white";
    Opening o2;
    o2.id = "o2";
    o2.wall = "w3";
    o2.type = OpeningType::Plain;
    f1.openings = {o1, o2};

    Floor f2;
    f2.id = "f2";
    f2.name = "Upstairs";
    plan.floors = {f1, f2};
    return plan;
}

// A small plan with one floor and two points joined by one wall.
inline Plan smallPlan()
{
    Plan plan = makeEmptyPlan();
    Floor& f = plan.floors[0];
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 2.0, 0.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    f.walls = {w};
    return plan;
}

// Fails the test with the message when the status is an error.
inline void requireOk(const Status& s)
{
    INFO(s.message << " at " << s.where);
    REQUIRE(s.ok());
}

// Fails the test unless the plan passes checkPlan.
inline void requirePlanValid(const Plan& plan)
{
    requireOk(checkPlan(plan));
}

// A folder under the system temp folder with a non ASCII name, removed when the object goes away.
class TempDir {
public:
    explicit TempDir(const std::string& tag)
    {
        const std::u8string name = u8"plynth-tests-ü-" + std::u8string(tag.begin(), tag.end());
        path_ = std::filesystem::temp_directory_path() / std::filesystem::path(name);
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}
