#pragma once

#include <plynth/core/Geometry.h>
#include <plynth/core/Plan.h>
#include <plynth/core/detection/Detection.h>

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace plynth::test {

// What detection should find in a sample plan, as written in its .expected.json file. Positions are in
// image pixels.
struct SampleExpected {
    struct Wall {
        Vec2 from;
        Vec2 to;
        double thickness = 0.0;
    };
    struct Room {
        Vec2 inside;
        double area = 0.0; // px squared
    };
    struct Opening {
        OpeningType type = OpeningType::Plain;
        Vec2 at;
        double width = 0.0;
    };
    struct Box {
        std::string kind;
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
    };

    std::string image;
    double pixelsPerMetre = 0.0;
    std::vector<Vec2> points;
    std::vector<Wall> walls;
    std::vector<Room> rooms;
    std::vector<Opening> openings;
    std::vector<Box> notWalls;
};

// Reads an expected file. Fails the test when it cannot be read.
inline SampleExpected readSampleExpected(const std::filesystem::path& file)
{
    std::ifstream in(file);
    REQUIRE(in.good());
    const nlohmann::json j = nlohmann::json::parse(in);
    const auto vec = [](const nlohmann::json& a) { return Vec2{a.at(0).get<double>(), a.at(1).get<double>()}; };
    SampleExpected e;
    e.image = j.at("image").get<std::string>();
    e.pixelsPerMetre = j.at("pixels_per_metre").get<double>();
    if (j.contains("points")) {
        for (const auto& p : j.at("points")) e.points.push_back(vec(p));
    }
    for (const auto& w : j.at("walls")) e.walls.push_back({vec(w.at("from")), vec(w.at("to")), w.at("thickness").get<double>()});
    for (const auto& r : j.at("rooms")) e.rooms.push_back({vec(r.at("inside")), r.at("centre_line_area").get<double>()});
    for (const auto& o : j.at("openings")) {
        const auto type = openingTypeFromName(o.at("type").get<std::string>());
        REQUIRE(type.has_value());
        e.openings.push_back({*type, {o.at("x").get<double>(), o.at("y").get<double>()}, o.at("width").get<double>()});
    }
    for (const auto& b : j.at("not_walls")) {
        e.notWalls.push_back({b.at("kind").get<std::string>(), b.at("box").at(0).get<double>(), b.at("box").at(1).get<double>(),
                              b.at("box").at(2).get<double>(), b.at("box").at(3).get<double>()});
    }
    return e;
}

// Says whether a segment touches a box (Liang-Barsky clipping).
inline bool segmentTouchesBox(Vec2 a, Vec2 b, const SampleExpected::Box& box)
{
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x - box.x0, box.x1 - a.x, a.y - box.y0, box.y1 - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
        } else {
            const double r = q[i] / p[i];
            if (p[i] < 0.0) {
                t0 = std::max(t0, r);
            } else {
                t1 = std::min(t1, r);
            }
            if (t0 > t1) return false;
        }
    }
    return true;
}

// Checks a detection result against an expected file: the same walls (ends within positionTol metres,
// thickness within 0.02 m), rooms (centre line area within 0.1 square metres), openings (same type, centre and
// width within 0.1 m), the same number of points, and no wall through a not_walls box.
inline void checkAgainstExpected(const DetectionResult& r, const SampleExpected& e, double positionTol)
{
    const double ppm = e.pixelsPerMetre;
    const auto metres = [&](Vec2 p) { return Vec2{p.x / ppm, p.y / ppm}; };

    REQUIRE(r.status.ok());
    REQUIRE_FALSE(r.cancelled);
    if (!e.points.empty()) REQUIRE(r.points.size() == e.points.size());
    REQUIRE(r.walls.size() == e.walls.size());

    std::vector<int> matches(r.walls.size(), 0);
    for (const SampleExpected::Wall& w : e.walls) {
        const Vec2 a = metres(w.from);
        const Vec2 b = metres(w.to);
        int found = 0;
        for (std::size_t i = 0; i < r.walls.size(); ++i) {
            const Vec2 p = r.points[r.walls[i].from];
            const Vec2 q = r.points[r.walls[i].to];
            const bool forward = distance(p, a) <= positionTol && distance(q, b) <= positionTol;
            const bool backward = distance(p, b) <= positionTol && distance(q, a) <= positionTol;
            if ((forward || backward) && std::abs(r.walls[i].thickness - w.thickness / ppm) <= 0.02) {
                ++found;
                ++matches[i];
            }
        }
        INFO("expected wall (" << w.from.x << "," << w.from.y << ") to (" << w.to.x << "," << w.to.y << ")");
        REQUIRE(found == 1);
    }
    for (int m : matches) REQUIRE(m == 1);

    REQUIRE(r.rooms.size() == e.rooms.size());
    for (const SampleExpected::Room& room : e.rooms) {
        const Vec2 inside = metres(room.inside);
        int found = 0;
        for (const ProposedRoom& p : r.rooms) {
            if (std::abs(p.centreLineArea - room.area / (ppm * ppm)) <= 0.1 && !p.rings.empty()
                && insidePolygon(inside, p.rings[0])) {
                ++found;
            }
        }
        INFO("expected room at (" << room.inside.x << "," << room.inside.y << ")");
        REQUIRE(found == 1);
    }

    REQUIRE(r.openings.size() == e.openings.size());
    std::vector<int> used(r.openings.size(), 0);
    for (const SampleExpected::Opening& o : e.openings) {
        const Vec2 centre = metres(o.at);
        int found = 0;
        for (std::size_t i = 0; i < r.openings.size(); ++i) {
            const ProposedOpening& p = r.openings[i];
            const ProposedWall& w = r.walls[p.wall];
            const Vec2 a = r.points[w.from];
            const Vec2 b = r.points[w.to];
            const double len = distance(a, b);
            const Vec2 at{a.x + (b.x - a.x) * p.offset / len, a.y + (b.y - a.y) * p.offset / len};
            if (p.type == o.type && distance(at, centre) <= 0.1 && std::abs(p.width - o.width / ppm) <= 0.1) {
                ++found;
                ++used[i];
            }
        }
        INFO("expected " << openingTypeName(o.type) << " at (" << o.at.x << "," << o.at.y << ")");
        REQUIRE(found == 1);
    }
    for (int u : used) REQUIRE(u == 1);

    for (const SampleExpected::Box& box : e.notWalls) {
        const SampleExpected::Box scaled{box.kind, box.x0 / ppm, box.y0 / ppm, box.x1 / ppm, box.y1 / ppm};
        for (const ProposedWall& w : r.walls) {
            INFO("a wall runs through the " << box.kind << " box");
            REQUIRE_FALSE(segmentTouchesBox(r.points[w.from], r.points[w.to], scaled));
        }
    }
}

}
