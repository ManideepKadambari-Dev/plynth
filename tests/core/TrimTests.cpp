#include "BuildFixtures.h"
#include "RoomFixtures.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Trim.h>
#include <plynth/core/TrimProfiles.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = 3.14159265358979323846;

// The square fixture with room r1 as a project.
Project squareRoom()
{
    Project p = test::squareProjectWithWalls();
    Room r;
    r.id = "r1";
    r.walls = {"w1", "w2", "w3", "w4"};
    p.plan.floors[0].rooms = {r};
    return p;
}

// An opening on a wall of the project's first floor.
void addOpening(Project& p, const std::string& id, const std::string& wall, OpeningType type, double offset, double width,
                double height, double sill, const std::string& shape = "rectangle")
{
    Opening o;
    o.id = id;
    o.wall = wall;
    o.type = type;
    o.offset = offset;
    o.width = width;
    o.height = height;
    o.sill = sill;
    o.shape = shape;
    p.plan.floors[0].openings.push_back(o);
}

// The trim of the first floor of a project.
std::vector<TrimBuild> trimOf(const Project& p, const TrimSettings& settings = {}, const std::vector<WorldPolygon>& atFloor = {},
                              const std::vector<WorldPolygon>& atCeiling = {})
{
    const BuildPlan plan = makeBuildPlan(p, test::sampleAssetSource());
    REQUIRE_FALSE(plan.floors.empty());
    return trimBuild(p.plan.floors[0], plan.floors[0], settings, atFloor, atCeiling);
}

// The trim of one kind, or null.
const TrimBuild* kindOf(const std::vector<TrimBuild>& all, TrimKind kind)
{
    for (const TrimBuild& t : all) {
        if (t.kind == kind) return &t;
    }
    return nullptr;
}

// The path of a sweep: its sections' first points, which lie on the path for every built-in profile.
std::vector<Vec3> pathOf(const Sweep& s)
{
    std::vector<Vec3> out;
    for (const auto& section : s.sections) out.push_back(section.front());
    return out;
}

// The length of a sweep's path, round the loop when closed.
double sweepLength(const Sweep& s)
{
    const std::vector<Vec3> path = pathOf(s);
    double total = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        total += std::sqrt(std::pow(path[i].x - path[i - 1].x, 2) + std::pow(path[i].y - path[i - 1].y, 2)
                           + std::pow(path[i].z - path[i - 1].z, 2));
    }
    if (s.closed && path.size() > 2) {
        total += std::sqrt(std::pow(path.front().x - path.back().x, 2) + std::pow(path.front().y - path.back().y, 2)
                           + std::pow(path.front().z - path.back().z, 2));
    }
    return total;
}

// The length of every sweep of a kind together.
double totalLength(const std::vector<TrimBuild>& all, TrimKind kind)
{
    const TrimBuild* t = kindOf(all, kind);
    if (!t) return 0.0;
    double total = 0.0;
    for (const Sweep& s : t->sweeps) total += sweepLength(s);
    return total;
}

// Says whether no two neighbouring sections of a sweep cross: every profile point moves forward along the path.
bool noFolds(const Sweep& s)
{
    const std::size_t n = s.sections.size();
    const std::size_t steps = s.closed ? n : n - 1;
    for (std::size_t k = 0; k < steps; ++k) {
        const auto& a = s.sections[k];
        const auto& b = s.sections[(k + 1) % n];
        const Vec3 d{b[0].x - a[0].x, b[0].y - a[0].y, b[0].z - a[0].z};
        for (std::size_t j = 0; j < a.size(); ++j) {
            const Vec3 m{b[j].x - a[j].x, b[j].y - a[j].y, b[j].z - a[j].z};
            if (m.x * d.x + m.y * d.y + m.z * d.z <= 0.0) return false;
        }
    }
    return true;
}

// Checks every sweep has sections of one count, at least three points each.
void checkSections(const std::vector<TrimBuild>& all)
{
    for (const TrimBuild& t : all) {
        for (const Sweep& s : t.sweeps) {
            REQUIRE(s.sections.size() >= 2);
            for (const auto& section : s.sections) {
                CHECK(section.size() == s.sections.front().size());
                CHECK(section.size() >= 3);
            }
        }
    }
}

// A world rectangle.
WorldPolygon worldRect(double x0, double y0, double x1, double y1)
{
    return {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, {}};
}

}

TEST_CASE("Skirting runs round the wall faces of a room", "[trim]")
{
    const std::vector<TrimBuild> all = trimOf(squareRoom());
    checkSections(all);
    const TrimBuild* skirting = kindOf(all, TrimKind::Skirting);
    REQUIRE(skirting != nullptr);
    REQUIRE(skirting->sweeps.size() == 1);
    CHECK(skirting->sweeps[0].closed);
    CHECK_FALSE(skirting->profileMissing);
    CHECK_THAT(sweepLength(skirting->sweeps[0]), WithinAbs(15.2, 1e-6));
    for (const Vec3& p : pathOf(skirting->sweeps[0])) {
        CHECK_THAT(p.z, WithinAbs(0.0, 1e-12));
        CHECK(p.x > 0.05);
        CHECK(p.x < 3.95);
    }
    // The profile stands out into the room, not into the wall.
    for (const auto& section : skirting->sweeps[0].sections) {
        for (const Vec3& q : section) {
            CHECK(q.x >= 0.1 - 1e-9);
            CHECK(q.x <= 3.9 + 1e-9);
            CHECK(q.y <= -0.1 + 1e-9);
            CHECK(q.y >= -3.9 - 1e-9);
        }
    }
    const TrimBuild* cornice = kindOf(all, TrimKind::Cornice);
    REQUIRE(cornice != nullptr);
    CHECK_THAT(totalLength(all, TrimKind::Cornice), WithinAbs(15.2, 1e-6));
    CHECK_THAT(pathOf(cornice->sweeps[0]).front().z, WithinAbs(3.0 - 0.08, 1e-12));
    CHECK(kindOf(all, TrimKind::Casing) == nullptr);
    CHECK(kindOf(all, TrimKind::Sill) == nullptr);
    CHECK(kindOf(all, TrimKind::Railing) == nullptr);
}

TEST_CASE("Doors and low windows break the skirting", "[trim]")
{
    Project door = squareRoom();
    addOpening(door, "o1", "w1", OpeningType::Door, 2.0, 0.9, 2.1, 0.0);
    CHECK_THAT(totalLength(trimOf(door), TrimKind::Skirting), WithinAbs(14.16, 1e-6));
    TrimSettings noCasing;
    noCasing.casing.enabled = false;
    CHECK_THAT(totalLength(trimOf(door, noCasing), TrimKind::Skirting), WithinAbs(14.3, 1e-6));
    CHECK_THAT(totalLength(trimOf(door), TrimKind::Cornice), WithinAbs(15.2, 1e-6));

    Project high = squareRoom();
    addOpening(high, "o2", "w2", OpeningType::Window, 2.0, 1.2, 1.2, 0.9);
    CHECK_THAT(totalLength(trimOf(high), TrimKind::Skirting), WithinAbs(15.2, 1e-6));

    Project low = squareRoom();
    addOpening(low, "o2", "w2", OpeningType::Window, 2.0, 1.2, 1.2, 0.05);
    CHECK_THAT(totalLength(trimOf(low), TrimKind::Skirting), WithinAbs(14.0, 1e-6));
}

TEST_CASE("Casings frame both faces of a door without a bottom", "[trim]")
{
    Project p = squareRoom();
    addOpening(p, "o1", "w1", OpeningType::Door, 2.0, 0.9, 2.1, 0.0);
    const std::vector<TrimBuild> all = trimOf(p);
    checkSections(all);
    const TrimBuild* casing = kindOf(all, TrimKind::Casing);
    REQUIRE(casing != nullptr);
    REQUIRE(casing->sweeps.size() == 2);
    bool front = false, back = false;
    for (const Sweep& s : casing->sweeps) {
        CHECK_FALSE(s.closed);
        CHECK_THAT(sweepLength(s), WithinAbs(2.1 + 0.9 + 2.1, 1e-6));
        const std::vector<Vec3> path = pathOf(s);
        CHECK_THAT(path.front().z, WithinAbs(0.0, 1e-12));
        CHECK_THAT(path.back().z, WithinAbs(0.0, 1e-12));
        double top = 0.0;
        for (const Vec3& q : path) top = std::max(top, q.z);
        CHECK_THAT(top, WithinAbs(2.1, 1e-12));
        if (std::fabs(path.front().y + 0.1) < 1e-9) front = true;
        if (std::fabs(path.front().y - 0.1) < 1e-9) back = true;
        // The casing's width lies outside the hole: no point between the jambs below the head.
        for (const auto& section : s.sections) {
            for (const Vec3& q : section) CHECK_FALSE((q.x > 1.55 + 1e-9 && q.x < 2.45 - 1e-9 && q.z < 2.1 - 1e-9));
        }
    }
    CHECK(front);
    CHECK(back);

    Project arch = squareRoom();
    addOpening(arch, "o1", "w1", OpeningType::Door, 2.0, 0.9, 2.1, 0.0, "round_arch");
    const std::vector<TrimBuild> archAll = trimOf(arch);
    const TrimBuild* archCasing = kindOf(archAll, TrimKind::Casing);
    REQUIRE(archCasing != nullptr);
    for (const Sweep& s : archCasing->sweeps) {
        CHECK_THAT(sweepLength(s), WithinAbs(2.0 * (2.1 - 0.45) + kPi * 0.45, 0.01));
        CHECK(noFolds(s));
    }
}

TEST_CASE("Sills reach past both jambs of wide straight windows", "[trim]")
{
    Project p = squareRoom();
    addOpening(p, "o2", "w2", OpeningType::Window, 2.0, 1.2, 1.2, 0.9);
    const std::vector<TrimBuild> all = trimOf(p);
    const TrimBuild* sill = kindOf(all, TrimKind::Sill);
    REQUIRE(sill != nullptr);
    REQUIRE(sill->sweeps.size() == 2);
    for (const Sweep& s : sill->sweeps) {
        CHECK_THAT(sweepLength(s), WithinAbs(1.2 + 2.0 * kSillOverhang, 1e-9));
        double top = -1.0;
        for (const auto& section : s.sections) {
            for (const Vec3& q : section) top = std::max(top, q.z);
        }
        CHECK_THAT(top, WithinAbs(0.9, 1e-9));
    }

    Project circle = squareRoom();
    addOpening(circle, "o2", "w2", OpeningType::Window, 2.0, 1.0, 1.0, 1.0, "circle");
    CHECK(kindOf(trimOf(circle), TrimKind::Sill) == nullptr);

    Project narrow = squareRoom();
    addOpening(narrow, "o2", "w2", OpeningType::Window, 2.0, 0.25, 0.6, 1.0);
    CHECK(kindOf(trimOf(narrow), TrimKind::Sill) == nullptr);
}

TEST_CASE("A half wall has skirting but no cornice", "[trim]")
{
    Project p = squareRoom();
    findWall(p.plan.floors[0], "w3")->height = 1.2;
    const std::vector<TrimBuild> all = trimOf(p);
    checkSections(all);
    auto alongW3 = [](const Vec3& q) { return std::fabs(q.y + 3.9) < 0.01 && q.x > 0.5 && q.x < 3.5; };
    const TrimBuild* cornice = kindOf(all, TrimKind::Cornice);
    REQUIRE(cornice != nullptr);
    for (const Sweep& s : cornice->sweeps) {
        for (const Vec3& q : pathOf(s)) CHECK_FALSE(alongW3(q));
    }
    bool skirtingOnW3 = false;
    for (const Sweep& s : kindOf(all, TrimKind::Skirting)->sweeps) {
        for (std::size_t k = 0; k + 1 < s.sections.size(); ++k) {
            const Vec3 a = s.sections[k][0];
            const Vec3 b = s.sections[k + 1][0];
            if (alongW3({(a.x + b.x) / 2.0, (a.y + b.y) / 2.0, 0.0})) skirtingOnW3 = true;
        }
        if (s.closed && alongW3(s.sections.back()[0])) skirtingOnW3 = true;
    }
    CHECK(skirtingOnW3);
}

TEST_CASE("Trim does not fold at concave corners, rounded corners or curved walls", "[trim]")
{
    Project l = test::splitHouseProject();
    Floor lf;
    lf.id = "f1";
    lf.name = "Floor 1";
    test::addPoint(lf, "p1", 0, 0);
    test::addPoint(lf, "p2", 4, 0);
    test::addPoint(lf, "p3", 4, 2);
    test::addPoint(lf, "p4", 2, 2);
    test::addPoint(lf, "p5", 2, 4);
    test::addPoint(lf, "p6", 0, 4);
    test::addWall(lf, "w1", "p1", "p2", 0.2);
    test::addWall(lf, "w2", "p2", "p3", 0.2);
    test::addWall(lf, "w3", "p3", "p4", 0.2);
    test::addWall(lf, "w4", "p4", "p5", 0.2);
    test::addWall(lf, "w5", "p5", "p6", 0.2);
    test::addWall(lf, "w6", "p6", "p1", 0.2);
    Room lr;
    lr.id = "r1";
    lr.walls = {"w1", "w2", "w3", "w4", "w5", "w6"};
    lf.rooms = {lr};
    l.plan.floors[0] = lf;

    Project rounded = squareRoom();
    for (Point& pt : rounded.plan.floors[0].points) pt.cornerRadius = 0.5;

    Project curved = squareRoom();
    findWall(curved.plan.floors[0], "w2")->bulge = -0.4;

    for (const Project* p : {&l, &rounded, &curved}) {
        const std::vector<TrimBuild> all = trimOf(*p);
        checkSections(all);
        for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice}) {
            const TrimBuild* t = kindOf(all, kind);
            REQUIRE(t != nullptr);
            for (const Sweep& s : t->sweeps) CHECK(noFolds(s));
        }
    }
    CHECK_THAT(totalLength(trimOf(l), TrimKind::Skirting), WithinAbs(2.0 * (3.8 + 3.8), 1e-6));
}

TEST_CASE("Stair footprints cut skirting at the floor and cornices at the ceiling", "[trim]")
{
    const Project p = squareRoom();
    const std::vector<WorldPolygon> stair = {worldRect(1.0, -0.5, 2.0, 0.5)};
    const std::vector<TrimBuild> atFloor = trimOf(p, {}, stair, {});
    CHECK_THAT(totalLength(atFloor, TrimKind::Skirting), WithinAbs(14.2, 1e-6));
    CHECK_THAT(totalLength(atFloor, TrimKind::Cornice), WithinAbs(15.2, 1e-6));
    const std::vector<TrimBuild> atCeiling = trimOf(p, {}, {}, stair);
    CHECK_THAT(totalLength(atCeiling, TrimKind::Skirting), WithinAbs(15.2, 1e-6));
    CHECK_THAT(totalLength(atCeiling, TrimKind::Cornice), WithinAbs(14.2, 1e-6));
}

TEST_CASE("Disabled trim kinds and unknown profiles", "[trim]")
{
    Project p = squareRoom();
    addOpening(p, "o1", "w1", OpeningType::Door, 2.0, 0.9, 2.1, 0.0);
    addOpening(p, "o2", "w2", OpeningType::Window, 2.0, 1.2, 1.2, 0.9);
    TrimSettings off;
    for (TrimKind kind : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill, TrimKind::Railing}) {
        trimStyle(off, kind).enabled = false;
    }
    CHECK(trimOf(p, off).empty());
    TrimSettings noSkirting;
    noSkirting.skirting.enabled = false;
    const std::vector<TrimBuild> rest = trimOf(p, noSkirting);
    CHECK(kindOf(rest, TrimKind::Skirting) == nullptr);
    REQUIRE(rest.size() == 3);
    CHECK(rest[0].kind == TrimKind::Cornice);
    CHECK(rest[1].kind == TrimKind::Casing);
    CHECK(rest[2].kind == TrimKind::Sill);

    TrimSettings missing;
    missing.skirting.profile = "not_installed";
    TrimSettings square;
    square.skirting.profile = "square";
    // Keep the lists alive: kindOf points into them.
    const std::vector<TrimBuild> missingAll = trimOf(p, missing);
    const std::vector<TrimBuild> squareAll = trimOf(p, square);
    const TrimBuild* withMissing = kindOf(missingAll, TrimKind::Skirting);
    const TrimBuild* withSquare = kindOf(squareAll, TrimKind::Skirting);
    REQUIRE(withMissing != nullptr);
    REQUIRE(withSquare != nullptr);
    CHECK(withMissing->profileMissing);
    CHECK_FALSE(withSquare->profileMissing);
    CHECK(withMissing->sweeps == withSquare->sweeps);
}

TEST_CASE("A wall between two rooms gets skirting on both faces", "[trim]")
{
    Project p = test::splitHouseProject();
    Room r1;
    r1.id = "r1";
    r1.walls = {"w1", "w7", "w5", "w6"};
    Room r2;
    r2.id = "r2";
    r2.walls = {"w2", "w3", "w4", "w7"};
    p.plan.floors[0].rooms = {r1, r2};
    const std::vector<TrimBuild> all = trimOf(p);
    CHECK_THAT(totalLength(all, TrimKind::Skirting), WithinAbs(2.0 * 2.0 * (2.7 + 3.7), 1e-6));
    bool left = false, right = false;
    for (const Sweep& s : kindOf(all, TrimKind::Skirting)->sweeps) {
        for (const Vec3& q : pathOf(s)) {
            // Wall bands go through Clipper2's integer grid, so faces are exact to about a micrometre.
            if (std::fabs(q.x - 2.85) < 1e-5) left = true;
            if (std::fabs(q.x - 3.15) < 1e-5) right = true;
        }
    }
    CHECK(left);
    CHECK(right);
}

TEST_CASE("A sweep places the profile right of the path and mitres its corners", "[trim]")
{
    const std::vector<Vec2> box = {{0, 0}, {0.02, 0}, {0.02, 0.1}, {0, 0.1}};
    const Sweep open = sweepPath({{0, 0, 0}, {1, 0, 0}, {1, 0, 0}}, false, box, {0, 0, 1});
    REQUIRE(open.sections.size() == 2);
    CHECK_FALSE(open.closed);
    CHECK_THAT(open.sections[0][1].y, WithinAbs(-0.02, 1e-12));
    CHECK_THAT(open.sections[0][2].z, WithinAbs(0.1, 1e-12));
    CHECK_THAT(open.sections[1][1].x, WithinAbs(1.0, 1e-12));

    const Sweep loop = sweepPath({{0, 0, 0}, {0, 4, 0}, {4, 4, 0}, {4, 0, 0}, {0, 0, 0}}, true, box, {0, 0, 1});
    REQUIRE(loop.sections.size() == 4);
    CHECK(loop.closed);
    // The clockwise loop puts the profile inside; the corner point sits on the mitre, 0.02 in from both sides.
    CHECK_THAT(loop.sections[0][1].x, WithinAbs(0.02, 1e-12));
    CHECK_THAT(loop.sections[0][1].y, WithinAbs(0.02, 1e-12));
    CHECK(noFolds(loop));
    CHECK(sweepPath({{0, 0, 0}, {0, 0, 0}}, false, box, {0, 0, 1}).sections.empty());
}
