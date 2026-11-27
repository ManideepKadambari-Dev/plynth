#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "TestSupport.h"
#include "../../core/src/RingClean.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Document.h>
#include <plynth/core/ExportGroups.h>
#include <plynth/core/Geometry.h>
#include <plynth/core/Kit.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/commands/SettingsCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::readTextFile;
using plynth::test::requireOk;
using plynth::test::TempDir;

namespace {

namespace fs = std::filesystem;

// One micrometre: no edge is shorter and no spike is thinner.
constexpr double kMicrometre = 1e-6;

using Ring = std::vector<Vec2>;

// A repeatable source of numbers, the same on every compiler.
class TestNumbers {
public:
    explicit TestNumbers(std::uint64_t seed) : state_(seed) {}

    // The next whole number.
    std::uint64_t next()
    {
        state_ += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // A number from low up to high.
    double real(double low, double high)
    {
        return low + (high - low) * (static_cast<double>(next() >> 11) / 9007199254740992.0);
    }

    // A whole number from 0 up to count.
    std::size_t pick(std::size_t count)
    {
        return static_cast<std::size_t>(next() % count);
    }

private:
    std::uint64_t state_;
};

// How wide the turn at b is when the ring runs back over itself there, seen from both of its edges.
std::optional<double> spikeWidth(Vec2 a, Vec2 b, Vec2 c)
{
    const Vec2 u{b.x - a.x, b.y - a.y};
    const Vec2 v{c.x - b.x, c.y - b.y};
    const double first = std::hypot(u.x, u.y);
    const double second = std::hypot(v.x, v.y);
    if (first <= 0.0 || second <= 0.0 || u.x * v.x + u.y * v.y >= 0.0) return std::nullopt;
    const double twiceArea = std::fabs(u.x * v.y - u.y * v.x);
    return (std::min)(twiceArea / first, twiceArea / second);
}

// How long an edge is.
double edgeLength(Vec2 a, Vec2 b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

// What is unclean about a ring, or nothing.
std::string ringProblem(const Ring& ring)
{
    if (ring.size() < 3) return "fewer than three points are left";
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[(i + ring.size() - 1) % ring.size()];
        const Vec2 b = ring[i];
        const Vec2 c = ring[(i + 1) % ring.size()];
        if (edgeLength(b, c) < kMicrometre) return "an edge is under a micrometre";
        const std::optional<double> width = spikeWidth(a, b, c);
        if (width && *width < kMicrometre) return "a spike is under a micrometre wide";
    }
    return {};
}

// How far a point is from a straight piece of a line.
double toSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

// What differs between a cleaned ring and the shape it had before the defects were added, or nothing.
std::string shapeProblem(const Ring& shape, const Ring& clean)
{
    if (std::string problem = ringProblem(clean); !problem.empty()) return problem;
    if (std::fabs(signedArea(shape) - signedArea(clean)) > 2e-5) return "the area or the direction changed";
    for (const Vec2& corner : shape) {
        double best = 1e300;
        for (const Vec2& p : clean) best = (std::min)(best, edgeLength(p, corner));
        if (best > 3e-6) return "a real corner was lost";
    }
    for (const Vec2& p : clean) {
        double best = 1e300;
        for (std::size_t i = 0; i < shape.size(); ++i) best = (std::min)(best, toSegment(p, shape[i], shape[(i + 1) % shape.size()]));
        if (best > 3e-6) return "a point off the shape is left";
    }
    return {};
}

// The same ring starting at another of its points.
Ring startingAt(Ring ring, std::size_t first)
{
    std::rotate(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(first % ring.size()), ring.end());
    return ring;
}

// Leaves a point where it is.
Vec2 asItIs(Vec2 p)
{
    return p;
}

// Mirrors a point in the Y axis.
Vec2 mirroredX(Vec2 p)
{
    return {-p.x, p.y};
}

// Mirrors a point in the diagonal.
Vec2 swappedAxes(Vec2 p)
{
    return {p.y, p.x};
}

// Moves a point far from the origin.
Vec2 movedFar(Vec2 p)
{
    return {p.x + 1234.5, p.y - 987.25};
}

// Turns a point by 35 degrees.
Vec2 turned35(Vec2 p)
{
    const double a = 35.0 * std::numbers::pi / 180.0;
    return {p.x * std::cos(a) - p.y * std::sin(a), p.x * std::sin(a) + p.y * std::cos(a)};
}

// A way to show the same ring: each point moved, and the order kept or reversed.
struct RingForm {
    const char* name;
    Vec2 (*move)(Vec2);
    bool backwards;

    // The ring in this form.
    Ring of(const Ring& ring) const
    {
        Ring result;
        for (const Vec2& p : ring) result.push_back(move(p));
        if (backwards) std::reverse(result.begin(), result.end());
        return result;
    }
};

// Every form a ring is tried in.
std::vector<RingForm> ringForms()
{
    return {{"as it is", &asItIs, false},         {"mirrored", &mirroredX, true}, {"axes swapped", &swappedAxes, true},
            {"running backwards", &asItIs, true}, {"far away", &movedFar, false}, {"turned", &turned35, false}};
}

// A polygon and the same polygon with defects under a micrometre wide.
struct DamagedRing {
    Ring shape;
    Ring ring;
};

// Makes a random polygon and puts repeated points, overshoots, slits or needles on every other corner.
DamagedRing damagedRing(TestNumbers& numbers)
{
    constexpr std::array<double, 5> widths{0.0, 1e-8, 1e-7, 5e-7, 9e-7};
    constexpr std::array<double, 5> depths{1e-5, 1e-3, 0.01, 0.1, 0.3};

    DamagedRing made;
    const std::size_t n = 4 + numbers.pick(7);
    const double step = 2.0 * std::numbers::pi / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double angle = static_cast<double>(i) * step + numbers.real(-0.25, 0.25) * step;
        const double radius = numbers.real(2.0, 4.0);
        made.shape.push_back({radius * std::cos(angle), radius * std::sin(angle)});
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = made.shape[i];
        const Vec2 next = made.shape[(i + 1) % n];
        const Vec2 before = made.shape[(i + n - 1) % n];
        int defect = -1;
        if (i % 2 == 0 && i + 1 < n) defect = static_cast<int>(numbers.pick(5));
        const double depth = depths[numbers.pick(depths.size())];
        const double width = widths[numbers.pick(widths.size())];
        const double sign = numbers.pick(2) == 0 ? 1.0 : -1.0;

        const double length = edgeLength(a, next);
        const Vec2 f{(next.x - a.x) / length, (next.y - a.y) / length};
        const Vec2 side{f.y, -f.x};
        const double lengthBefore = edgeLength(before, a);
        const Vec2 g{(a.x - before.x) / lengthBefore, (a.y - before.y) / lengthBefore};
        // An overshoot on an almost straight corner could be taken for the corner itself, so none goes there.
        if ((defect == 1 || defect == 2) && std::fabs(g.x * f.y - g.y * f.x) < 0.25) defect = -1;

        if (defect == 2) {
            const double h = width * (lengthBefore + depth) / lengthBefore * sign;
            made.ring.push_back({a.x + g.x * depth + g.y * h, a.y + g.y * depth - g.x * h});
        }
        made.ring.push_back(a);
        if (defect == 0) made.ring.push_back({a.x + side.x * 5e-7, a.y + side.y * 5e-7});
        if (defect == 1) {
            const double h = width * (length + depth) / length * sign;
            made.ring.push_back({a.x - f.x * depth + side.x * h, a.y - f.y * depth + side.y * h});
        }
        if (defect == 3 || defect == 4) {
            const Vec2 middle{a.x + f.x * length / 2.0, a.y + f.y * length / 2.0};
            const double reach = defect == 4 ? depth : -depth;
            const Vec2 first{middle.x - f.x * width / 2.0, middle.y - f.y * width / 2.0};
            const Vec2 last{middle.x + f.x * width / 2.0, middle.y + f.y * width / 2.0};
            made.ring.push_back(first);
            made.ring.push_back({first.x + side.x * reach, first.y + side.y * reach});
            made.ring.push_back({last.x + side.x * reach, last.y + side.y * reach});
            made.ring.push_back(last);
        }
    }
    return made;
}

}

TEST_CASE("G1 an overshoot goes in every direction its ring can run", "[core][edges][m11export]") {
    const Ring square{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}};
    const Ring overshoot{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {4.01, 4.0000005}, {0.0, 4.0}};
    for (const RingForm& form : ringForms()) {
        INFO("form " << form.name);
        for (std::size_t start = 0; start < overshoot.size(); ++start) {
            INFO("first point " << start);
            const Ring clean = detail::cleanedWallRing(startingAt(form.of(overshoot), start));
            REQUIRE(clean.size() == 4);
            REQUIRE(shapeProblem(form.of(square), clean) == "");
        }
    }
}

TEST_CASE("G1 an overshoot of any length under a micrometre wide goes", "[core][edges][m11export]") {
    const Ring square{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}};
    for (const double length : {1e-5, 1e-3, 0.01, 0.3, 1.0}) {
        for (const double width : {0.0, 1e-8, 1e-7, 5e-7, 9e-7}) {
            for (const double sign : {1.0, -1.0}) {
                // The corner it overshoots lies this width away from the long edge back.
                const Ring ring{{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {4.0 + length, 4.0 + sign * width * (4.0 + length) / 4.0}, {0.0, 4.0}};
                for (const RingForm& form : ringForms()) {
                    for (std::size_t start = 0; start < ring.size(); ++start) {
                        INFO("length " << length << " width " << width << " sign " << sign);
                        INFO("form " << form.name << " first point " << start);
                        const Ring clean = detail::cleanedWallRing(startingAt(form.of(ring), start));
                        REQUIRE(clean.size() == 4);
                        REQUIRE(shapeProblem(form.of(square), clean) == "");
                    }
                }
            }
        }
    }
}

TEST_CASE("G1 a seeded sweep of damaged rings comes out clean, turned, mirrored and started anywhere", "[core][edges][m11export]") {
    TestNumbers numbers(20261122);
    const std::vector<RingForm> forms = ringForms();
    for (int made = 0; made < 2000; ++made) {
        const DamagedRing sample = damagedRing(numbers);
        for (const RingForm& form : forms) {
            const Ring shape = form.of(sample.shape);
            const Ring ring = form.of(sample.ring);
            for (const std::size_t start : {std::size_t{0}, ring.size() / 3, ring.size() - 1}) {
                const std::string problem = shapeProblem(shape, detail::cleanedWallRing(startingAt(ring, start)));
                if (problem.empty()) continue;
                INFO("seed 20261122 ring " << made << " form " << form.name << " first point " << start);
                REQUIRE(problem == "");
            }
        }
    }
}

TEST_CASE("G2 a ring that is only a sliver goes completely", "[core][edges][m11export]") {
    const std::vector<Ring> slivers{
        {{0.0, 0.0}, {4.0, 0.0}, {2.0, 9e-7}},
        {{0.0, 0.0}, {4.0, 0.0}, {4.0, 5e-7}, {2.0, 4e-7}},
        {{0.0, 0.0}, {4.0, 0.0}, {2.0, 0.0}},
        {{0.0, 0.0}, {0.3, 0.0}, {0.12, 5e-7}},
        {{0.0, 0.0}, {4.0, 0.0}, {3.0, 3e-7}, {2.0, 6e-7}, {1.0, 2e-7}},
    };
    for (std::size_t which = 0; which < slivers.size(); ++which) {
        for (const RingForm& form : ringForms()) {
            for (std::size_t start = 0; start < slivers[which].size(); ++start) {
                INFO("sliver " << which << " form " << form.name << " first point " << start);
                REQUIRE(detail::cleanedWallRing(startingAt(form.of(slivers[which]), start)).size() < 3);
            }
        }
    }
}

TEST_CASE("G guard, real corners and parts wider than a micrometre keep every point", "[core][edges][m11export]") {
    const double tip = 10.0 * std::tan(std::numbers::pi / 180.0);
    const std::vector<Ring> kept{
        // A corner of 1 degree.
        {{0.0, 0.0}, {10.0, 0.0}, {0.0, tip}, {-1.0, 0.05}},
        // A triangle 2.5 micrometres wide.
        {{0.0, 0.0}, {4.0, 0.0}, {2.0, 2.5e-6}},
        // A slit 2 micrometres wide.
        {{0.0, 0.0}, {2.0 - 1e-6, 0.0}, {2.0 - 1e-6, 1.0}, {2.0 + 1e-6, 1.0}, {2.0 + 1e-6, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}},
        // An overshoot 3 micrometres wide.
        {{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {4.01, 4.0 + 3e-6 * 4.01 / 4.0}, {0.0, 4.0}},
        // A corner cut off over 2 micrometres.
        {{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0 - 2e-6}, {4.0 - 2e-6, 4.0}, {0.0, 4.0}},
    };
    for (std::size_t which = 0; which < kept.size(); ++which) {
        for (const RingForm& form : ringForms()) {
            for (std::size_t start = 0; start < kept[which].size(); ++start) {
                INFO("ring " << which << " form " << form.name << " first point " << start);
                const Ring ring = startingAt(form.of(kept[which]), start);
                REQUIRE(detail::cleanedWallRing(ring) == ring);
            }
        }
    }
}

TEST_CASE("G guard, rings with nothing in them end as nothing", "[core][edges][m11export]") {
    const std::vector<Ring> hollow{
        {},
        {{1.0, 2.0}},
        {{1.0, 2.0}, {3.0, 2.0}},
        {{1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0}},
        {{1.0, 2.0}, {3.0, 2.0}, {1.0, 2.0}, {3.0, 2.0}, {1.0, 2.0}},
    };
    for (std::size_t which = 0; which < hollow.size(); ++which) {
        INFO("ring " << which);
        REQUIRE(detail::cleanedWallRing(hollow[which]).size() < 3);
    }
}

TEST_CASE("G guard, seeded rooms with half walls give clean wall solids, mirrored too", "[core][edges][m11export]") {
    TestNumbers numbers(11);
    constexpr std::array<double, 4> thicknesses{0.1, 0.125, 0.2, 0.3};
    for (int made = 0; made < 300; ++made) {
        const double angle = numbers.real(0.0, 2.0 * std::numbers::pi);
        const double x = numbers.real(-20.0, 20.0);
        const double y = numbers.real(-20.0, 20.0);
        const double thickness = thicknesses[numbers.pick(thicknesses.size())];
        const std::size_t lowWalls = 1 + numbers.pick(14);
        for (const double mirror : {1.0, -1.0}) {
            INFO("seed 11 room " << made << " mirror " << mirror);
            Floor floor = plynth::test::squareFloorWithWalls();
            for (Point& point : floor.points) {
                const double px = point.x;
                const double py = point.y;
                point.x = mirror * (px * std::cos(angle) - py * std::sin(angle) + x);
                point.y = px * std::sin(angle) + py * std::cos(angle) + y;
            }
            for (std::size_t i = 0; i < floor.walls.size(); ++i) {
                floor.walls[i].thickness = thickness;
                if (((lowWalls >> i) & 1U) != 0) floor.walls[i].height = 1.0;
            }

            double covered = 0.0;
            const std::vector<WallSolid> solids = wallSolids(floor);
            REQUIRE_FALSE(solids.empty());
            for (const WallSolid& solid : solids) {
                const Ring& outline = solid.outline.points;
                REQUIRE(ringProblem(outline) == "");
                double around = 0.0;
                for (std::size_t i = 0; i < outline.size(); ++i) around += edgeLength(outline[i], outline[(i + 1) % outline.size()]);
                const double area = std::fabs(signedArea(outline));
                REQUIRE(area > around * kMicrometre);
                covered += area;
                for (const WallRing& hole : solid.holes) {
                    REQUIRE(ringProblem(hole.points) == "");
                    covered -= std::fabs(signedArea(hole.points));
                }
            }
            REQUIRE(std::fabs(covered - 16.0 * thickness) < 1e-5);
        }
    }
}

namespace {

// 64 bit FNV-1a from its published definition, written here and not taken from the program.
std::string standardHash(const std::string& text)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    char digits[17];
    std::snprintf(digits, sizeof digits, "%016llx", static_cast<unsigned long long>(hash));
    return digits;
}

// One straight wall with nothing at its ends, no room and no material, on the 0.5 m grid.
Project oneWall(Vec2 from, Vec2 to, double thickness, std::optional<double> height)
{
    Project project = makeNewProject();
    plynth::test::clearMaterialDefaults(project);
    project.settings.gridSize = 0.5;
    Floor& floor = project.plan.floors[0];
    floor.points = {{"p1", from.x, from.y, 0.0}, {"p2", to.x, to.y, 0.0}};
    Wall wall;
    wall.id = "w1";
    wall.from = "p1";
    wall.to = "p2";
    wall.thickness = thickness;
    wall.height = height;
    floor.walls = {wall};
    return project;
}

// The piece ids of a project's kit.
std::set<std::string> kitIds(const Project& project)
{
    const BuildPlan plan = makeBuildPlan(project, BuildAssetSource{}, plynth::test::sampleMaterialSource());
    const ModularKit kit = modularKit(project, plan);
    std::set<std::string> ids;
    for (const KitPiece& piece : kit.pieces) ids.insert(piece.id);
    return ids;
}

}

TEST_CASE("K guard, the hash of these tests is the published FNV-1a", "[core][edges][m11export]") {
    REQUIRE(standardHash("") == "cbf29ce484222325");
    REQUIRE(standardHash("a") == "af63dc4c8601ec8c");
    REQUIRE(standardHash("foobar") == "85944171f73967e8");
}

TEST_CASE("K kit ids are the published FNV-1a of the pieces' text", "[core][edges][m11export]") {
    // The text of each piece by Kit.h rules 9 and 10, worked out by hand: millimetres in the stored frame.
    const std::string startText = "0|S3000,Wall_Top,{-100,-100,Exterior_Wall,,0,1;500,-100,Wall_Top,,0,0;"
                                  "500,100,Exterior_Wall,,0,1;-100,100,Wall_Top,,1,0;}";
    const std::string endText = "0|S3000,Wall_Top,{0,-100,Exterior_Wall,,0,1;600,-100,Wall_Top,,0,0;"
                                "600,100,Exterior_Wall,,0,1;0,100,Wall_Top,,1,0;}";
    const std::set<std::string> expected{"Wall_60x300x20_de43a2", "Wall_60x300x20_cd9de9"};
    const std::set<std::string> fromText{"Wall_60x300x20_" + standardHash(startText).substr(0, 6),
                                         "Wall_60x300x20_" + standardHash(endText).substr(0, 6)};
    REQUIRE(expected == fromText);

    SECTION("along X") {
        REQUIRE(kitIds(oneWall({0.0, 0.0}, {1.0, 0.0}, 0.2, std::nullopt)) == expected);
    }
    SECTION("drawn the other way") {
        REQUIRE(kitIds(oneWall({1.0, 0.0}, {0.0, 0.0}, 0.2, std::nullopt)) == expected);
    }
    SECTION("along Y and moved") {
        REQUIRE(kitIds(oneWall({2.35, 1.5}, {2.35, 2.5}, 0.2, std::nullopt)) == expected);
    }
}

TEST_CASE("K kit ids of a thicker and lower wall use the same hash", "[core][edges][m11export]") {
    const std::string startText = "0|S2500,Wall_Top,{-150,-150,Exterior_Wall,,0,1;500,-150,Wall_Top,,0,0;"
                                  "500,150,Exterior_Wall,,0,1;-150,150,Wall_Top,,1,0;}";
    const std::string endText = "0|S2500,Wall_Top,{0,-150,Exterior_Wall,,0,1;650,-150,Wall_Top,,0,0;"
                                "650,150,Exterior_Wall,,0,1;0,150,Wall_Top,,1,0;}";
    const std::set<std::string> expected{"Wall_65x250x30_1f807a", "Wall_65x250x30_0eaa1b"};
    const std::set<std::string> fromText{"Wall_65x250x30_" + standardHash(startText).substr(0, 6),
                                         "Wall_65x250x30_" + standardHash(endText).substr(0, 6)};
    REQUIRE(expected == fromText);
    REQUIRE(kitIds(oneWall({0.0, 0.0}, {1.0, 0.0}, 0.3, 2.5)) == expected);
}

namespace {

// The options file of a folder.
fs::path optionsIn(const fs::path& folder)
{
    return folder / fs::path(std::string(kExportOptionsFileName));
}

// Writes text exactly as given.
void writeExactly(const fs::path& file, const std::string& text)
{
    std::ofstream written(file, std::ios::binary | std::ios::trunc);
    written << text;
    written.close();
    REQUIRE(written.good());
}

// A saved project in folder A under a temporary folder, open in a document, and a folder B for the copy.
struct SaveAsFixture {
    explicit SaveAsFixture(const std::string& tag) : dir(tag), from(dir.path() / "A"), to(dir.path() / "B")
    {
        fs::create_directories(from);
        Project project = makeNewProject();
        requireOk(createProject(project, from, from));
        requireOk(document.open(from));
    }

    TempDir dir;
    fs::path from;
    fs::path to;
    Document document;
};

// The options the fixtures save: the walls group renamed and the kit on.
ExportOptions shellWithKit()
{
    ExportOptions options;
    options.groups.renames = {{"structure", "Shell"}};
    options.links[std::string(kUnrealLinkId)].kit = true;
    return options;
}

}

TEST_CASE("L Save As keeps the group edits and the kit switch", "[core][edges][m11export]") {
    SaveAsFixture f("edges-saveas-options");
    requireOk(writeExportOptions(f.from, shellWithKit()));
    const std::string bytes = readTextFile(optionsIn(f.from));
    REQUIRE_FALSE(bytes.empty());

    requireOk(f.document.saveAs(f.to));
    REQUIRE(fs::equivalent(f.document.folder(), f.to));
    REQUIRE(fs::is_regular_file(optionsIn(f.to)));
    CHECK(readTextFile(optionsIn(f.to)) == bytes);
    CHECK(readTextFile(optionsIn(f.from)) == bytes);
    const ExportOptionsRead read = readExportOptions(f.to);
    requireOk(read.status);
    CHECK(read.options == shellWithKit());
}

TEST_CASE("L Save As copies the options byte for byte as they are on disk now", "[core][edges][m11export]") {
    SaveAsFixture f("edges-saveas-bytes");
    // Written after the project was opened, in a layout Plynth would not write itself.
    const std::string bytes =
        "{\r\n\t\"version\":1,  \"format\":\"plynth-export-options\",\r\n\t\"kit\":{\"enabled\":true}}\r\n";
    writeExactly(optionsIn(f.from), bytes);
    requireOk(f.document.saveAs(f.to));
    REQUIRE(fs::is_regular_file(optionsIn(f.to)));
    CHECK(readTextFile(optionsIn(f.to)) == bytes);
}

TEST_CASE("L Save As keeps an options file that cannot be read, so it can still be repaired", "[core][edges][m11export]") {
    const std::vector<std::string> texts{"{not json", "{\"format\":\"plynth-export-options\",\"version\":99}", ""};
    for (const std::string& bytes : texts) {
        INFO("text " << bytes);
        SaveAsFixture f("edges-saveas-bad");
        writeExactly(optionsIn(f.from), bytes);
        requireOk(f.document.saveAs(f.to));
        REQUIRE(fs::is_regular_file(optionsIn(f.to)));
        CHECK(readTextFile(optionsIn(f.to)) == bytes);
        CHECK(readTextFile(optionsIn(f.from)) == bytes);
    }
}

TEST_CASE("L Save As never replaces or takes over an options file already in the other folder", "[core][edges][m11export]") {
    for (const bool hasOwn : {true, false}) {
        INFO("the project has options " << hasOwn);
        SaveAsFixture f("edges-saveas-stray");
        if (hasOwn) requireOk(writeExportOptions(f.from, shellWithKit()));
        fs::create_directories(f.to);
        const std::string stray = "{\"format\":\"plynth-export-options\",\"version\":1}";
        writeExactly(optionsIn(f.to), stray);

        CHECK_FALSE(f.document.saveAs(f.to).ok());
        CHECK(readTextFile(optionsIn(f.to)) == stray);
        CHECK_FALSE(isProjectFolder(f.to));
        CHECK(fs::equivalent(f.document.folder(), f.from));
    }
}

TEST_CASE("L Save As fails whole when the options cannot be copied, and works once they can", "[core][edges][m11export]") {
    SaveAsFixture f("edges-saveas-blocked");
    requireOk(writeExportOptions(f.from, shellWithKit()));
    const std::string bytes = readTextFile(optionsIn(f.from));
    requireOk(f.document.execute(std::make_unique<SetExportModeCommand>(ExportMode::Grouped)));
    REQUIRE(f.document.isModified());
    fs::create_directories(optionsIn(f.to));

    const Status saved = f.document.saveAs(f.to);
    REQUIRE_FALSE(saved.ok());
    CHECK(fs::equivalent(f.document.folder(), f.from));
    CHECK(f.document.isModified());
    CHECK_FALSE(isProjectFolder(f.to));
    CHECK_FALSE(fs::exists(f.to / "plan.json"));
    CHECK(readTextFile(optionsIn(f.from)) == bytes);

    fs::remove(optionsIn(f.to));
    requireOk(f.document.saveAs(f.to));
    CHECK(readTextFile(optionsIn(f.to)) == bytes);
}

TEST_CASE("L guard, Save As makes no options file where there was none", "[core][edges][m11export]") {
    SaveAsFixture f("edges-saveas-none");
    requireOk(f.document.saveAs(f.to));
    CHECK(isProjectFolder(f.to));
    CHECK_FALSE(fs::exists(optionsIn(f.to)));

    Document unsaved;
    const fs::path first = f.dir.path() / "C";
    requireOk(unsaved.saveAs(first));
    CHECK_FALSE(fs::exists(optionsIn(first)));
}

TEST_CASE("L guard, Save As into the project's own folder leaves the options alone", "[core][edges][m11export]") {
    SaveAsFixture f("edges-saveas-same");
    requireOk(writeExportOptions(f.from, shellWithKit()));
    const std::string bytes = readTextFile(optionsIn(f.from));
    requireOk(f.document.saveAs(f.from));
    CHECK(readTextFile(optionsIn(f.from)) == bytes);
}

TEST_CASE("L guard, a refused Save As writes no options into the other folder", "[core][edges][m11export]") {
    SaveAsFixture f("edges-saveas-refused");
    requireOk(writeExportOptions(f.from, shellWithKit()));

    SECTION("the folder holds another project") {
        Project other = makeNewProject();
        fs::create_directories(f.to);
        requireOk(createProject(other, f.to, f.to));
        ExportOptions theirs;
        theirs.groups.renames = {{"surfaces", "Ground"}};
        requireOk(writeExportOptions(f.to, theirs));
        const std::string bytes = readTextFile(optionsIn(f.to));
        CHECK(f.document.saveAs(f.to).code == ErrorCode::AlreadyExists);
        CHECK(readTextFile(optionsIn(f.to)) == bytes);
    }
    SECTION("the document is read only") {
        f.document.setReadOnly(true, "A session runs.");
        CHECK(f.document.saveAs(f.to).code == ErrorCode::Locked);
        CHECK_FALSE(fs::exists(optionsIn(f.to)));
    }
    SECTION("the plan image is missing") {
        Project project = f.document.project();
        project.plan.floors[0].image = "plans/missing.png";
        f.document.replace(project, f.from, true);
        CHECK(f.document.saveAs(f.to).code == ErrorCode::NotFound);
        CHECK_FALSE(fs::exists(optionsIn(f.to)));
    }
    SECTION("the plan cannot be written") {
        fs::create_directories(f.to / "plan.json");
        CHECK_FALSE(f.document.saveAs(f.to).ok());
        CHECK_FALSE(fs::exists(optionsIn(f.to)));
        CHECK(fs::equivalent(f.document.folder(), f.from));
    }
}
