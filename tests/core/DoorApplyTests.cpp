#include "SampleExpected.h"
#include "TestSupport.h"

#include <plynth/core/Geometry.h>
#include <plynth/core/WallGeometry.h>
#include <plynth/core/commands/DetectionCommands.h>
#include <plynth/core/detection/Detector.h>
#include <plynth/core/detection/GrayImage.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

// Where a placed door hangs and opens, worked out from the wall it sits in.
struct PlacedDoor {
    Vec2 centre;
    Vec2 jamb;
    Vec2 swingPoint; // a quarter of the width out from the centre on the side the leaf opens toward
    Id wall;
    HingeEnd hinge = HingeEnd::Start;
    WallSide swing = WallSide::Right;
};

PlacedDoor placed(const Floor& floor, const Opening& o)
{
    const Wall* wall = nullptr;
    for (const Wall& w : floor.walls) {
        if (w.id == o.wall) wall = &w;
    }
    REQUIRE(wall != nullptr);
    const WallCurve curve = wallCurve(floor, *wall);
    const Vec2 direction = tangentAlong(curve, o.offset);
    const Vec2 right = rightOf(direction);
    const double side = o.swing == WallSide::Right ? 1.0 : -1.0;
    const double jambOffset = o.hinge == HingeEnd::Start ? o.offset - o.width / 2.0 : o.offset + o.width / 2.0;
    PlacedDoor d;
    d.centre = pointAlong(curve, o.offset);
    d.jamb = pointAlong(curve, jambOffset);
    d.swingPoint = {d.centre.x + side * right.x * o.width / 4.0, d.centre.y + side * right.y * o.width / 4.0};
    d.wall = o.wall;
    d.hinge = o.hinge;
    d.swing = o.swing;
    return d;
}

// Applies detection of a sample to a new project and compares each door with the expected file, in metres.
// Returns one table line for every expected door.
std::vector<std::string> checkSample(const std::string& name, bool strict)
{
    const auto expected = readSampleExpected(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / (name + ".expected.json"));
    const GrayImageLoad load = loadGrayImage(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / expected.image);
    REQUIRE(load.status.ok());
    const double ppm = expected.pixelsPerMetre;
    const DetectionResult result = runDetection("orthogonal", *load.image, ppm, {});

    Project project = makeNewProject();
    ApplyDetectionCommand apply("f1", result);
    requireOk(apply.apply(project));
    const Floor& floor = project.plan.floors[0];

    std::vector<std::string> table;
    for (const SampleExpected::Opening& e : expected.openings) {
        if (!e.hasHinge) continue;
        const Vec2 centre{e.at.x / ppm, e.at.y / ppm};
        const Vec2 hinge{e.hinge.x / ppm, e.hinge.y / ppm};
        const Vec2 swing{e.swingPoint.x / ppm, e.swingPoint.y / ppm};
        const Opening* found = nullptr;
        for (const Opening& o : floor.openings) {
            if (o.type != OpeningType::Door) continue;
            const PlacedDoor d = placed(floor, o);
            if (distance(d.centre, centre) <= 0.1) found = &o;
        }
        REQUIRE(found != nullptr);
        const PlacedDoor d = placed(floor, *found);
        const bool jambOk = distance(d.jamb, hinge) <= 0.1;
        const bool swingOk = distance(d.swingPoint, swing) <= 0.05;
        table.push_back(name + " door at (" + std::to_string(e.at.x) + "," + std::to_string(e.at.y) + ") wall " + d.wall +
                        " hinge " + std::string(hingeEndName(d.hinge)) + " swing " + std::string(wallSideName(d.swing)) +
                        " jamb " + (jambOk ? "ok" : "WRONG") + " swing " + (swingOk ? "ok" : "WRONG"));
        if (strict) {
            INFO(table.back());
            CHECK(jambOk);
            CHECK(swingOk);
        }
    }
    return table;
}

}

TEST_CASE("doors applied from the sample plans hang and swing as drawn", "[core][detection][opening][sample][doors]") {
    for (const char* name : {"apartment-01", "apartment-02", "house-01-ground", "house-01-upper"}) {
        DYNAMIC_SECTION(name) {
            const auto table = checkSample(name, true);
            REQUIRE_FALSE(table.empty());
        }
    }
}

namespace {

// One wall of 4 m from a to b with a door in the middle, as a detection result.
DetectionResult oneDoor(Vec2 a, Vec2 b, HingeEnd hinge, WallSide swing)
{
    DetectionResult r;
    r.points = {a, b};
    ProposedWall w;
    w.from = 0;
    w.to = 1;
    w.thickness = 0.2;
    r.walls.push_back(w);
    ProposedOpening o;
    o.wall = 0;
    o.type = OpeningType::Door;
    o.offset = 2.0;
    o.width = 0.9;
    o.hinge = hinge;
    o.swing = swing;
    r.openings.push_back(o);
    return r;
}

}

TEST_CASE("an applied door keeps its hinge and swing in every wall direction", "[core][detection][opening][doors]") {
    const std::vector<std::pair<Vec2, Vec2>> runs = {{{1, 1}, {5, 1}}, {{5, 1}, {1, 1}}, {{1, 1}, {1, 5}}, {{1, 5}, {1, 1}}};
    for (const bool reversedFirst : {false, true}) {
        for (const auto& [a, b] : runs) {
            for (const HingeEnd hinge : {HingeEnd::Start, HingeEnd::End}) {
                for (const WallSide swing : {WallSide::Left, WallSide::Right}) {
                    INFO("from " << a.x << "," << a.y << " to " << b.x << "," << b.y << " hinge " << hingeEndName(hinge)
                                 << " swing " << wallSideName(swing) << " wall drawn first reversed " << reversedFirst);
                    const DetectionResult proposal = oneDoor(a, b, hinge, swing);
                    Project project = makeNewProject();
                    if (reversedFirst) {
                        DetectionResult wallOnly = oneDoor(b, a, hinge, swing);
                        wallOnly.openings.clear();
                        ApplyDetectionCommand first("f1", wallOnly);
                        requireOk(first.apply(project));
                    }
                    ApplyDetectionCommand apply("f1", proposal);
                    requireOk(apply.apply(project));
                    const Floor& floor = project.plan.floors[0];
                    REQUIRE(floor.openings.size() == 1);
                    const PlacedDoor d = placed(floor, floor.openings[0]);

                    const Vec2 direction{(b.x - a.x) / 4.0, (b.y - a.y) / 4.0};
                    const Vec2 centre{a.x + direction.x * 2.0, a.y + direction.y * 2.0};
                    const double jambAlong = hinge == HingeEnd::Start ? -0.45 : 0.45;
                    const Vec2 jamb{centre.x + direction.x * jambAlong, centre.y + direction.y * jambAlong};
                    const Vec2 right = rightOf(direction);
                    const double side = swing == WallSide::Right ? 1.0 : -1.0;
                    const Vec2 swingPoint{centre.x + side * right.x * 0.225, centre.y + side * right.y * 0.225};
                    CHECK(distance(d.centre, centre) < 1e-9);
                    CHECK(distance(d.jamb, jamb) < 1e-9);
                    CHECK(distance(d.swingPoint, swingPoint) < 1e-9);
                }
            }
        }
    }
}

TEST_CASE("print the door table", "[.doors-table]") {
    for (const char* name : {"apartment-01", "apartment-02", "house-01-ground", "house-01-upper"}) {
        for (const std::string& line : checkSample(name, false)) std::cout << line << "\n";
    }
}
