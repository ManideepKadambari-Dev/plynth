#include "SampleExpected.h"
#include "TestImages.h"
#include "TestSupport.h"

#include <plynth/core/Openings.h>
#include <plynth/core/commands/DetectionCommands.h>
#include <plynth/core/detection/Detector.h>
#include <plynth/core/detection/GrayImage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <numbers>
#include <stop_token>
#include <thread>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// Loads a sample plan once and keeps it.
std::shared_ptr<const GrayImage> sampleImage(const std::string& name)
{
    static std::map<std::string, std::shared_ptr<const GrayImage>> cache;
    auto& slot = cache[name];
    if (!slot) {
        const GrayImageLoad load = loadGrayImage(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / name);
        REQUIRE(load.status.ok());
        slot = load.image;
    }
    return slot;
}

// Runs the orthogonal detector at 100 pixels per metre with default settings unless given others.
DetectionResult run(const GrayImage& img, const DetectionParams& params = {}, double ppm = 100.0)
{
    return runDetection("orthogonal", img, ppm, params);
}

// Says whether two results hold the same points, walls, openings and rooms.
bool sameFindings(const DetectionResult& a, const DetectionResult& b)
{
    return a.points == b.points && a.walls == b.walls && a.openings == b.openings && a.rooms == b.rooms;
}

}

TEST_CASE("the orthogonal detector registers itself", "[core][detection][registry]") {
    const Detector* d = findDetector(kDefaultDetectorId);
    REQUIRE(d);
    REQUIRE(d->id() == "orthogonal");
    REQUIRE(d->name() == "Straight walls");
    REQUIRE_FALSE(d->description().empty());
}

TEST_CASE("orthogonal detector on drawn shapes", "[core][detection][orthogonal]") {
    GrayImage img = whiteCanvas(800, 500);

    SECTION("one 0.2 m bar gives one wall with the right ends and thickness") {
        fillRect(img, 100, 190, 500, 210);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 1);
        REQUIRE(r.points.size() == 2);
        REQUIRE(r.walls[0].thickness == Approx(0.2));
        const Vec2 a = r.points[r.walls[0].from];
        const Vec2 b = r.points[r.walls[0].to];
        REQUIRE(a.x == Approx(1.0).margin(0.02));
        REQUIRE(a.y == Approx(2.0).margin(0.02));
        REQUIRE(b.x == Approx(5.0).margin(0.02));
        REQUIRE(b.y == Approx(2.0).margin(0.02));
        REQUIRE(r.openings.empty());
        REQUIRE(r.rooms.empty());
        REQUIRE(r.imageWidth == 800);
        REQUIRE(r.imageHeight == 500);
    }
    SECTION("a vertical bar works the same") {
        fillRect(img, 390, 50, 410, 450);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 1);
        const Vec2 a = r.points[r.walls[0].from];
        const Vec2 b = r.points[r.walls[0].to];
        REQUIRE(a.x == Approx(4.0).margin(0.02));
        REQUIRE(b.x == Approx(4.0).margin(0.02));
        REQUIRE(std::abs(a.y - b.y) == Approx(4.0).margin(0.03));
    }
    SECTION("a 2 pixel line gives nothing") {
        fillRect(img, 100, 199, 500, 201);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.empty());
        REQUIRE(r.points.empty());
    }
    SECTION("a blank page gives nothing") {
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.empty());
    }
    SECTION("an L shares one corner point") {
        fillRect(img, 90, 90, 400, 110);
        fillRect(img, 90, 110, 110, 400);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.points.size() == 3);
        REQUIRE(r.walls.size() == 2);
    }
    SECTION("a T shares one junction point") {
        fillRect(img, 90, 90, 500, 110);
        fillRect(img, 290, 110, 310, 400);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.points.size() == 4);
        REQUIRE(r.walls.size() == 3);
    }
    SECTION("a cross shares one centre point") {
        fillRect(img, 90, 190, 510, 210);
        fillRect(img, 290, 90, 310, 400);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.points.size() == 5);
        REQUIRE(r.walls.size() == 4);
    }
    SECTION("a closed rectangle gives one room") {
        fillRect(img, 90, 90, 510, 110);
        fillRect(img, 90, 390, 510, 410);
        fillRect(img, 90, 110, 110, 390);
        fillRect(img, 490, 110, 510, 390);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.points.size() == 4);
        REQUIRE(r.walls.size() == 4);
        REQUIRE(r.rooms.size() == 1);
        REQUIRE(r.rooms[0].centreLineArea == Approx(4.0 * 3.0).margin(0.05));
    }
    SECTION("a bar with a 0.9 m gap gives one wall and one plain opening") {
        fillRect(img, 100, 190, 350, 210);
        fillRect(img, 440, 190, 700, 210);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 1);
        REQUIRE(r.points.size() == 2);
        REQUIRE(r.openings.size() == 1);
        REQUIRE(r.openings[0].type == OpeningType::Plain);
        REQUIRE(r.openings[0].wall == 0);
        REQUIRE(r.openings[0].width == Approx(0.9).margin(0.03));
        REQUIRE(r.openings[0].offset == Approx(2.95).margin(0.03));
    }
    SECTION("a gap wider than the widest opening gives two walls") {
        fillRect(img, 100, 190, 350, 210);
        fillRect(img, 440, 190, 700, 210);
        DetectionParams p;
        p.maxOpeningWidth = 0.5;
        const DetectionResult r = run(img, p);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 2);
        REQUIRE(r.openings.empty());
    }
    SECTION("a window gap with two thin lines is a window") {
        fillRect(img, 100, 190, 350, 210);
        fillRect(img, 450, 190, 700, 210);
        fillRect(img, 350, 194, 450, 196);
        fillRect(img, 350, 204, 450, 206);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 1);
        REQUIRE(r.openings.size() == 1);
        REQUIRE(r.openings[0].type == OpeningType::Window);
    }
    SECTION("a gap with a swing arc is a door") {
        fillRect(img, 100, 190, 350, 210);
        fillRect(img, 440, 190, 700, 210);
        for (int deg = 0; deg <= 90; ++deg) {
            const double a = deg * std::numbers::pi / 180.0;
            const int x = 350 + static_cast<int>(std::lround(90.0 * std::sin(a)));
            const int y = 200 - static_cast<int>(std::lround(90.0 * std::cos(a)));
            fillRect(img, x, y, x + 2, y + 2);
        }
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.openings.size() == 1);
        REQUIRE(r.openings[0].type == OpeningType::Door);
    }
    SECTION("openings false leaves out the openings but still closes the gap") {
        fillRect(img, 100, 190, 350, 210);
        fillRect(img, 440, 190, 700, 210);
        DetectionParams p;
        p.openings = false;
        const DetectionResult r = run(img, p);
        REQUIRE(r.walls.size() == 1);
        REQUIRE(r.openings.empty());
    }
    SECTION("a solid block thicker than the thickest wall gives nothing") {
        fillRect(img, 100, 100, 300, 300);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.empty());
    }
    SECTION("text sized strokes give nothing") {
        for (int i = 0; i < 6; ++i) {
            fillRect(img, 100 + i * 40, 100, 103 + i * 40, 140);
            fillRect(img, 100 + i * 40, 100, 130 + i * 40, 103);
        }
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.empty());
    }
    SECTION("a bar 3 degrees off gives a wall at that angle") {
        const double a = 3.0 * std::numbers::pi / 180.0;
        fillBar(img, 100, 100, 100 + 600 * std::cos(a), 100 + 600 * std::sin(a), 20);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 1);
        const Vec2 p = r.points[r.walls[0].from];
        const Vec2 q = r.points[r.walls[0].to];
        const double angle = std::atan2(q.y - p.y, q.x - p.x) * 180.0 / std::numbers::pi;
        REQUIRE(std::abs(angle) == Approx(3.0).margin(0.4));
    }
    SECTION("a bar 0.3 degrees off gives an exact horizontal wall") {
        const double a = 0.3 * std::numbers::pi / 180.0;
        fillBar(img, 100, 100, 100 + 650 * std::cos(a), 100 + 650 * std::sin(a), 20);
        const DetectionResult r = run(img);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 1);
        REQUIRE(r.points[r.walls[0].from].y == r.points[r.walls[0].to].y);
    }
}

TEST_CASE("orthogonal detector on apartment-01", "[core][detection][orthogonal][sample]") {
    const auto expected = readSampleExpected(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-01.expected.json");
    const auto image = sampleImage(expected.image);

    SECTION("default settings match the expected file") {
        const DetectionResult r = run(*image, {}, expected.pixelsPerMetre);
        REQUIRE(r.points.size() == 12);
        REQUIRE(r.walls.size() == 16);
        checkAgainstExpected(r, expected, 0.03);
    }
    SECTION("a thinnest wall of 0.15 m drops the inner walls") {
        DetectionParams p;
        p.minThickness = 0.15;
        const DetectionResult r = run(*image, p, expected.pixelsPerMetre);
        requireOk(r.status);
        REQUIRE(r.walls.size() == 4);
        REQUIRE(r.points.size() == 4);
        for (const ProposedWall& w : r.walls) REQUIRE(w.thickness == Approx(0.3));
    }
    SECTION("a thickest wall of 0.2 m drops the outer walls") {
        DetectionParams p;
        p.maxThickness = 0.2;
        const DetectionResult r = run(*image, p, expected.pixelsPerMetre);
        requireOk(r.status);
        // The seven inner walls remain. Their ends that met an outer wall stay at the outer wall's edge,
        // up to 0.15 m from the old junction, because there is no wall left to join.
        REQUIRE(r.walls.size() == 7);
        std::vector<int> matched(r.walls.size(), 0);
        int inner = 0;
        for (const SampleExpected::Wall& w : expected.walls) {
            if (w.thickness != 12.0) continue;
            ++inner;
            const Vec2 a{w.from.x / 100.0, w.from.y / 100.0};
            const Vec2 b{w.to.x / 100.0, w.to.y / 100.0};
            for (std::size_t i = 0; i < r.walls.size(); ++i) {
                const Vec2 from = r.points[r.walls[i].from];
                const Vec2 to = r.points[r.walls[i].to];
                const bool same = (distance(from, a) <= 0.16 && distance(to, b) <= 0.16) || (distance(from, b) <= 0.16 && distance(to, a) <= 0.16);
                if (same && r.walls[i].thickness == Approx(0.12)) ++matched[i];
            }
        }
        REQUIRE(inner == 7);
        for (int m : matched) REQUIRE(m == 1);
    }
    SECTION("sensitivity 0 and 1 still give a valid result") {
        for (double s : {0.0, 1.0}) {
            DetectionParams p;
            p.sensitivity = s;
            const DetectionResult r = run(*image, p, expected.pixelsPerMetre);
            requireOk(r.status);
            requireOk(checkDetectionResult(r));
        }
    }
    SECTION("rooms false and the widest opening 0") {
        DetectionParams p;
        p.rooms = false;
        p.maxOpeningWidth = 0.0;
        const DetectionResult r = run(*image, p, expected.pixelsPerMetre);
        requireOk(r.status);
        REQUIRE(r.rooms.empty());
        REQUIRE(r.openings.empty());
        REQUIRE(r.walls.size() > 16);
    }
    SECTION("two runs give equal results") {
        const DetectionResult a = run(*image, {}, expected.pixelsPerMetre);
        const DetectionResult b = run(*image, {}, expected.pixelsPerMetre);
        REQUIRE(sameFindings(a, b));
    }
    SECTION("two runs at once on two threads give equal results") {
        DetectionResult a;
        DetectionResult b;
        std::thread ta([&] { a = run(*image, {}, expected.pixelsPerMetre); });
        std::thread tb([&] { b = run(*image, {}, expected.pixelsPerMetre); });
        ta.join();
        tb.join();
        requireOk(a.status);
        REQUIRE(sameFindings(a, b));
        REQUIRE(sameFindings(a, run(*image, {}, expected.pixelsPerMetre)));
    }
    SECTION("a stop asked from another thread cancels the run quickly") {
        // Tries shorter and shorter waits so a slow thread start cannot let the run finish first.
        bool cancelled = false;
        std::chrono::steady_clock::duration took{};
        for (int waitMs : {4, 2, 1, 0, 0}) {
            std::stop_source source;
            DetectionResult r;
            std::chrono::steady_clock::time_point returned;
            std::atomic<bool> started{false};
            std::thread worker([&] {
                started = true;
                r = runDetection("orthogonal", *image, expected.pixelsPerMetre, {}, source.get_token());
                returned = std::chrono::steady_clock::now();
            });
            while (!started) std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            const auto asked = std::chrono::steady_clock::now();
            source.request_stop();
            worker.join();
            requireOk(r.status);
            if (r.cancelled) {
                REQUIRE(r.walls.empty());
                cancelled = true;
                took = returned - asked;
                break;
            }
        }
        REQUIRE(cancelled);
#ifdef NDEBUG
        const auto limit = std::chrono::milliseconds(100);
#else
        const auto limit = std::chrono::milliseconds(2000);
#endif
        REQUIRE(took < limit);
    }
}

TEST_CASE("accepting detection on apartment-01 adds every opening", "[core][detection][orthogonal][sample][command]") {
    const auto expected = readSampleExpected(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-01.expected.json");
    const auto image = sampleImage(expected.image);
    const DetectionResult r = run(*image, {}, expected.pixelsPerMetre);
    REQUIRE(r.openings.size() == 9);

    Project p = makeNewProject();
    ApplyDetectionCommand c("f1", r);
    const Project before = p;
    requireOk(c.apply(p));
    requirePlanValid(p.plan);
    const Floor& f = p.plan.floors[0];
    REQUIRE(c.openingIds().size() == 9);
    REQUIRE(c.skippedOpenings() == 0);
    REQUIRE(f.openings.size() == 9);
    REQUIRE(openingProblems(f).empty());

    // Doors keep the hinge jamb and the side of the proposal, on the wall they ended up on.
    int doors = 0;
    for (const Opening& o : f.openings) {
        if (o.type == OpeningType::Door) ++doors;
    }
    REQUIRE(doors == 4);

    SECTION("a second accept is already_exists") {
        const Project accepted = p;
        ApplyDetectionCommand again("f1", run(*image, {}, expected.pixelsPerMetre));
        REQUIRE(again.apply(p).code == ErrorCode::AlreadyExists);
        REQUIRE(p == accepted);
    }
    SECTION("detecting again marks the openings Existing") {
        DetectionResult again = run(*image, {}, expected.pixelsPerMetre);
        markExisting(f, again);
        const ProposalCounts counts = countProposals(again);
        REQUIRE(counts.openings == 9);
        REQUIRE(counts.existingOpenings == 9);
        REQUIRE(counts.newOpenings == 0);
    }
    SECTION("undo gives an equal plan") {
        c.undo(p);
        REQUIRE(p == before);
        requireOk(c.apply(p));
        REQUIRE(p.plan.floors[0].openings.size() == 9);
    }
}

TEST_CASE("accepting detection with openings turned off adds none", "[core][detection][orthogonal][sample][command]") {
    const auto expected = readSampleExpected(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-01.expected.json");
    const auto image = sampleImage(expected.image);
    DetectionParams params;
    params.openings = false;
    const DetectionResult r = run(*image, params, expected.pixelsPerMetre);
    REQUIRE(r.openings.empty());
    Project p = makeNewProject();
    ApplyDetectionCommand c("f1", r);
    requireOk(c.apply(p));
    REQUIRE(p.plan.floors[0].openings.empty());
    REQUIRE(c.openingIds().empty());
}

TEST_CASE("orthogonal detector on apartment-02", "[core][detection][orthogonal][sample]") {
    const auto expected = readSampleExpected(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-02.expected.json");
    const auto image = sampleImage(expected.image);
    const DetectionResult r = run(*image, {}, expected.pixelsPerMetre);
    REQUIRE(r.points.size() == expected.points.size());
    REQUIRE(r.walls.size() == expected.walls.size());
    checkAgainstExpected(r, expected, 0.04);
}

TEST_CASE("orthogonal detector on the house plans", "[core][detection][orthogonal][sample]") {
    for (const char* name : {"house-01-ground", "house-01-upper"}) {
        DYNAMIC_SECTION(name) {
            const auto expected = readSampleExpected(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / (std::string(name) + ".expected.json"));
            const auto image = sampleImage(expected.image);
            const DetectionResult r = run(*image, {}, expected.pixelsPerMetre);
            REQUIRE(r.rooms.size() == 4);
            checkAgainstExpected(r, expected, 0.04);
        }
    }
}
