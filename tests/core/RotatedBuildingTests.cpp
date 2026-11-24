#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Rooms.h>
#include <plynth/core/Stairs.h>
#include <plynth/core/Storeys.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

constexpr double kW = kRingMergeDistance;

double distanceToSegment2(const Vec2& p, const Vec2& a, const Vec2& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

double distance3(const Vec3& a, const Vec3& b)
{
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
}

double distanceToSegment3(const Vec3& p, const Vec3& a, const Vec3& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double dz = b.z - a.z;
    const double len2 = dx * dx + dy * dy + dz * dz;
    const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy + (p.z - a.z) * dz) / len2, 0.0, 1.0) : 0.0;
    return distance3(p, {a.x + t * dx, a.y + t * dy, a.z + t * dz});
}

double rounded32(double v)
{
    return static_cast<double>(static_cast<float>(v));
}

// Says whether two segments cross or touch (orientation test with the endpoints).
bool segmentsMeet(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d)
{
    const auto side = [](const Vec2& p, const Vec2& q, const Vec2& r) {
        const double v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
    };
    const int s1 = side(a, b, c);
    const int s2 = side(a, b, d);
    const int s3 = side(c, d, a);
    const int s4 = side(c, d, b);
    if (s1 != s2 && s3 != s4) return true;
    const auto on = [](const Vec2& p, const Vec2& q, const Vec2& r) {
        return std::min(p.x, q.x) <= r.x && r.x <= std::max(p.x, q.x) && std::min(p.y, q.y) <= r.y && r.y <= std::max(p.y, q.y);
    };
    return (s1 == 0 && on(a, b, c)) || (s2 == 0 && on(a, b, d)) || (s3 == 0 && on(c, d, a)) || (s4 == 0 && on(c, d, b));
}

// What was found wrong in everything a build made.
struct Findings {
    double shortest = 1e18;  // shortest ring edge
    double narrowest = 1e18; // smallest distance from a point to an edge it is not on
    int notSimple = 0;       // rings that stop being simple with the right turning direction after float32 rounding
    int rings = 0;
    double perimeter = 0.0;
    int badFaces = 0;     // roof faces facing the wrong way, or holes turning the same way as their outline
    int badCollision = 0; // collision pieces without area or not convex, hulls with fewer than four points
    std::string where;
    std::string simpleWhere;
    std::string faceWhere;
    std::string collisionWhere;

    void edge(double v, const std::string& what)
    {
        if (v < shortest) {
            shortest = v;
            where = what;
        }
    }
    void width(double v, const std::string& what)
    {
        if (v < narrowest) {
            narrowest = v;
            where = what;
        }
    }
};

// Checks a ring in the plane: edges, the width of the ring and its shape after float32 rounding. orientation is
// +1 for a ring turning anticlockwise (an outline) and -1 for a hole.
void checkRing(Findings& f, const std::vector<Vec2>& ring, int orientation, const std::string& what)
{
    ++f.rings;
    const std::size_t n = ring.size();
    if (n < 3) {
        ++f.notSimple;
        f.simpleWhere = what + " has fewer than 3 points";
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % n];
        f.edge(std::hypot(b.x - a.x, b.y - a.y), what);
        f.perimeter += std::hypot(b.x - a.x, b.y - a.y);
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || (j + 1) % n == i) continue;
            f.width(distanceToSegment2(ring[i], ring[j], ring[(j + 1) % n]), what);
        }
    }
    std::vector<Vec2> r32;
    for (const Vec2& p : ring) r32.push_back({rounded32(p.x), rounded32(p.y)});
    bool simple = (signedArea(r32) > 0.0) == (orientation > 0) && signedArea(r32) != 0.0;
    for (std::size_t i = 0; i < n && simple; ++i) {
        for (std::size_t j = i + 1; j < n && simple; ++j) {
            const bool adjacent = j == i + 1 || (i == 0 && j == n - 1);
            if (r32[i].x == r32[j].x && r32[i].y == r32[j].y) simple = false;
            if (adjacent) continue;
            if (segmentsMeet(r32[i], r32[(i + 1) % n], r32[j], r32[(j + 1) % n])) simple = false;
        }
    }
    if (!simple) {
        ++f.notSimple;
        f.simpleWhere = what;
    }
}

// Says whether two rings meet after rounding to float32.
bool ringsMeet(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    const auto rounded = [](const std::vector<Vec2>& ring) {
        std::vector<Vec2> out;
        for (const Vec2& p : ring) out.push_back({rounded32(p.x), rounded32(p.y)});
        return out;
    };
    const std::vector<Vec2> ra = rounded(a);
    const std::vector<Vec2> rb = rounded(b);
    for (std::size_t i = 0; i < ra.size(); ++i) {
        for (std::size_t j = 0; j < rb.size(); ++j) {
            if (segmentsMeet(ra[i], ra[(i + 1) % ra.size()], rb[j], rb[(j + 1) % rb.size()])) return true;
        }
    }
    return false;
}

void checkPolygon(Findings& f, const std::vector<Vec2>& outline, const std::vector<std::vector<Vec2>>& holes, const std::string& what)
{
    checkRing(f, outline, 1, what);
    for (const auto& h : holes) checkRing(f, h, -1, what + " hole");
    std::vector<const std::vector<Vec2>*> rings{&outline};
    for (const auto& h : holes) rings.push_back(&h);
    for (std::size_t i = 0; i < rings.size(); ++i) {
        for (std::size_t j = i + 1; j < rings.size(); ++j) {
            if (ringsMeet(*rings[i], *rings[j])) {
                ++f.notSimple;
                f.simpleWhere = what + " rings cross";
            }
        }
    }
}

// A planar roof face ring in space: edges in 3D, shape after float32 rounding in the plane of the face. No width
// check: a roof's rings are only cleaned point by point.
void checkRing3(Findings& f, const std::vector<Vec3>& ring, const std::string& what)
{
    ++f.rings;
    const std::size_t n = ring.size();
    if (n < 3) {
        ++f.notSimple;
        f.simpleWhere = what + " has fewer than 3 points";
        return;
    }
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 a = ring[i];
        const Vec3 b = ring[(i + 1) % n];
        f.edge(distance3(a, b), what);
        nx += (a.y - b.y) * (a.z + b.z);
        ny += (a.z - b.z) * (a.x + b.x);
        nz += (a.x - b.x) * (a.y + b.y);
    }
    const double ax = std::abs(nx);
    const double ay = std::abs(ny);
    const double az = std::abs(nz);
    std::vector<Vec2> flat;
    for (const Vec3& p : ring) {
        const double x = rounded32(p.x);
        const double y = rounded32(p.y);
        const double z = rounded32(p.z);
        if (az >= ax && az >= ay) flat.push_back({x, y});
        else if (ax >= ay) flat.push_back({y, z});
        else flat.push_back({z, x});
    }
    bool simple = signedArea(flat) != 0.0;
    for (std::size_t i = 0; i < n && simple; ++i) {
        for (std::size_t j = i + 1; j < n && simple; ++j) {
            if (flat[i].x == flat[j].x && flat[i].y == flat[j].y) simple = false;
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            if (segmentsMeet(flat[i], flat[(i + 1) % n], flat[j], flat[(j + 1) % n])) simple = false;
        }
    }
    if (!simple) {
        ++f.notSimple;
        f.simpleWhere = what;
    }
}

// The unnormalised normal of a ring in space, pointing to the side it turns anticlockwise seen from.
Vec3 normalOf(const std::vector<Vec3>& ring)
{
    Vec3 n{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3 a = ring[i];
        const Vec3 b = ring[(i + 1) % ring.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

// The area of a planar ring in space.
double area3(const std::vector<Vec3>& ring)
{
    const Vec3 n = normalOf(ring);
    return 0.5 * std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
}

// A roof face: its rings, a covering looking up, an underside looking down, and each hole turning against its
// outline.
void checkFace(Findings& f, const RoofFace& face, const std::string& what)
{
    checkRing3(f, face.outline, what);
    const Vec3 n = normalOf(face.outline);
    if (face.part == RoofPart::Covering && !(n.z > 0.0)) {
        ++f.badFaces;
        f.faceWhere = what + " covering does not look up";
    }
    if (face.part == RoofPart::Underside && !(n.z < 0.0)) {
        ++f.badFaces;
        f.faceWhere = what + " underside does not look down";
    }
    for (const std::vector<Vec3>& hole : face.holes) {
        checkRing3(f, hole, what + " hole");
        const Vec3 h = normalOf(hole);
        if (n.x * h.x + n.y * h.y + n.z * h.z >= 0.0) {
            ++f.badFaces;
            f.faceWhere = what + " hole turns with its outline";
        }
    }
}

double planArea(const std::vector<PlanArea>& areas)
{
    double sum = 0.0;
    for (const PlanArea& a : areas) sum += planAreaSize(a);
    return sum;
}

double worldAreaOf(const WorldPolygon& w)
{
    double sum = signedArea(w.outline);
    for (const auto& h : w.holes) sum += signedArea(h);
    return sum;
}

// What a variant of the setback has besides its two floors.
struct Variant {
    bool stair = false;  // a spiral stair under the upper floor
    bool curved = false; // the ground floor's east wall bulges outward
    bool island = false; // a closed ring of walls inside the ground room, part of the room
};

// Where a turned plan lies, and to how many metres its coordinates are rounded (the command line stores
// them rounded to 1e-6, so every corner is off by up to 5e-7 on its own). 0 is no rounding.
struct Place {
    double dx = 0.0;
    double dy = 0.0;
    double grid = 0.0;
};

// Turns every point and stair of the plan by angle (degrees) about the origin and moves it by (dx, dy), then
// rounds the coordinates to the grid.
void turnPlan(Plan& plan, double angle, double dx, double dy, double grid = 0.0)
{
    const auto snap = [grid](double v) { return grid > 0.0 ? std::round(v / grid) * grid : v; };
    const double c = std::cos(angle * std::numbers::pi / 180.0);
    const double s = std::sin(angle * std::numbers::pi / 180.0);
    for (Floor& f : plan.floors) {
        for (Point& p : f.points) {
            const double x = p.x;
            const double y = p.y;
            p.x = snap(x * c - y * s + dx);
            p.y = snap(x * s + y * c + dy);
        }
        for (Stair& st : f.stairs) {
            const double x = st.x;
            const double y = st.y;
            st.x = snap(x * c - y * s + dx);
            st.y = snap(x * s + y * c + dy);
            st.rotation += angle;
        }
    }
}

// The 4 by 4 m ground floor under an upper floor from (0, 0) to (3, 4), turned by angle (degrees) about the
// origin and moved by (dx, dy). Walls are 0.2 m. The default roof is of the given type.
Project setback(double angle, const Place& place, const std::string& roofType, double slab, const Variant& v = {})
{
    Project project = makeNewProject();
    Plan& plan = project.plan;
    Floor& ground = plan.floors[0];
    addRectangle(ground, 1, 0, 0, 4, 4, 0.2);
    Room r1;
    r1.id = "r1";
    r1.walls = {"w11", "w12", "w13", "w14"};
    if (v.curved) {
        for (Wall& w : ground.walls) {
            if (w.id == "w12") w.bulge = 0.4;
        }
    }
    if (v.island) {
        addRectangle(ground, 3, 0.6, 0.6, 1.6, 1.6, 0.2);
        for (const char* id : {"w31", "w32", "w33", "w34"}) r1.walls.push_back(id);
    }
    ground.rooms.push_back(r1);
    ground.slabThickness = slab;
    if (v.stair) {
        Stair s;
        s.id = "s1";
        s.type = "spiral";
        s.x = 2.0;
        s.y = 2.6;
        ground.stairs.push_back(s);
    }
    Floor upper;
    upper.id = "f2";
    upper.name = "Floor 2";
    addRectangle(upper, 2, 0, 0, 3, 4, 0.2);
    Room r2;
    r2.id = "r2";
    r2.walls = {"w21", "w22", "w23", "w24"};
    upper.rooms.push_back(r2);
    plan.floors.push_back(upper);
    plan.roof.type = roofType;
    plan.roof.direction = angle;
    turnPlan(plan, angle, place.dx, place.dy, place.grid);
    return project;
}

// Everything a build makes, checked and added up.
struct Measured {
    Findings found;
    double slab = 0.0;
    double undersides = 0.0;
    double tops = 0.0;
    double sections = 0.0;
    double above = 0.0;
    double holes = 0.0;
    double overhang = 0.0;
    double bound = 0.0;
    std::size_t roofs = 0;
    std::array<double, 6> roofAreas{}; // by RoofPart: the area of every roof face, less its holes
};

Measured measure(const Project& project)
{
    const Plan& plan = project.plan;
    Measured m;
    Findings& f = m.found;
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        for (const PlanArea& a : roofSectionAreas(plan, i)) {
            checkPolygon(f, a.outline, a.holes, "roof section");
            if (i == 0) m.sections += planAreaSize(a);
        }
        for (const std::vector<PlanArea>& list : {floorFootprint(plan.floors[i]), exposedArea(plan, i), overhangArea(plan, i)}) {
            for (const PlanArea& a : list) checkPolygon(f, a.outline, a.holes, "plan area");
        }
    }
    if (plan.floors.size() > 1) {
        m.above = planArea(floorFootprint(plan.floors[1]));
        m.overhang = planArea(overhangArea(plan, 0));
    }
    // The areas add up but for the cleaning noise: 2 w times the perimeter of every ring of the two footprints
    // and the holes, plus kMinPlanAreaSize for each ring.
    double perimeter = f.perimeter;
    int rings = f.rings;
    for (const Stair& st : plan.floors[0].stairs) {
        const StairLayout layout = stairLayout(plan, plan.floors[0], st);
        if (!layout.status.ok()) continue;
        m.holes += std::abs(signedArea(layout.footprint));
        for (std::size_t k = 0; k < layout.footprint.size(); ++k) {
            const Vec2 a = layout.footprint[k];
            const Vec2 b = layout.footprint[(k + 1) % layout.footprint.size()];
            perimeter += std::hypot(b.x - a.x, b.y - a.y);
        }
        ++rings;
    }
    m.bound = 2.0 * kW * perimeter + kMinPlanAreaSize * rings;

    const BuildPlan built = makeBuildPlan(project, {});
    for (std::size_t i = 0; i < built.floors.size(); ++i) {
        const FloorBuild& fb = built.floors[i];
        for (const WorldPolygon& w : fb.slab.polygons) {
            checkPolygon(f, w.outline, w.holes, "slab");
            if (i == 0) m.slab += worldAreaOf(w);
        }
        for (const WorldPolygon& w : fb.slab.undersides) {
            checkPolygon(f, w.outline, w.holes, "slab underside");
            if (i == 0) m.undersides += worldAreaOf(w);
        }
        for (const WorldPolygon& w : fb.slab.tops) {
            checkPolygon(f, w.outline, w.holes, "slab top");
            if (i == 0) m.tops += worldAreaOf(w);
        }
        for (const RoomBuild& room : fb.rooms) {
            for (const WorldPolygon& w : room.polygons) checkPolygon(f, w.outline, w.holes, "room floor");
            for (const WorldPolygon& w : room.ceilingPolygons) checkPolygon(f, w.outline, w.holes, "room ceiling");
        }
        for (const RoofBuild& roof : fb.roofs) {
            ++m.roofs;
            for (const RoofFace& face : roof.faces) {
                const std::string what = "roof face " + std::string(roofPartName(face.part)) + " of " + roof.type;
                checkFace(f, face, what);
                double size = area3(face.outline);
                for (const auto& h : face.holes) size -= area3(h);
                m.roofAreas[static_cast<std::size_t>(face.part)] += size;
            }
        }
        for (const CollisionShape& shape : built.collision[i]) {
            for (const CollisionPiece& piece : shape.pieces) {
                if (std::abs(signedArea(piece.outline)) <= 0.0 || !isConvexRing(piece.outline)) {
                    ++f.badCollision;
                    f.collisionWhere = "collision piece of " + shape.id;
                }
            }
            for (const auto& hull : shape.hulls) {
                if (hull.size() < 4) {
                    ++f.badCollision;
                    f.collisionWhere = "collision hull of " + shape.id;
                }
            }
        }
    }
    return m;
}

void requireClean(const Measured& m, const std::string& label)
{
    INFO(label);
    INFO("shortest " << m.found.shortest << " narrowest " << m.found.narrowest << " at " << m.found.where);
    INFO("not simple at " << m.found.simpleWhere);
    CHECK(m.found.shortest >= kW);
    CHECK(m.found.narrowest >= kW);
    CHECK(m.found.notSimple == 0);
    INFO("faces " << m.found.faceWhere << " collision " << m.found.collisionWhere);
    CHECK(m.found.badFaces == 0);
    CHECK(m.found.badCollision == 0);
    CHECK(m.roofs == 2);
    // The slab against its parts, and the undersides against the overhang.
    CHECK(std::abs(m.slab - (m.sections + m.tops + m.above - m.holes)) <= m.bound);
    CHECK(std::abs(m.undersides - m.overhang) <= m.bound);
}

void requireSameAs(const Measured& turned, const Measured& straight, const std::string& label)
{
    INFO(label);
    const double bound = std::max(turned.bound, straight.bound);
    CHECK(std::abs(turned.slab - straight.slab) <= bound);
    CHECK(std::abs(turned.undersides - straight.undersides) <= bound);
    CHECK(std::abs(turned.tops - straight.tops) <= bound);
    CHECK(std::abs(turned.sections - straight.sections) <= bound);
    CHECK(std::abs(turned.above - straight.above) <= bound);
    for (std::size_t part = 0; part < turned.roofAreas.size(); ++part) {
        INFO("roof part " << part << " turned " << turned.roofAreas[part] << " straight " << straight.roofAreas[part]);
        CHECK(std::abs(turned.roofAreas[part] - straight.roofAreas[part]) <= 4.0 * bound);
    }
}

std::vector<std::string> allRoofTypeIds()
{
    std::vector<std::string> types;
    for (const RoofType* t : allRoofTypes()) types.emplace_back(t->id());
    return types;
}

void sweep(const Variant& variant, const std::string& name, const std::vector<Place>& places, double slab,
           bool sawtoothOnly = false)
{
    const std::vector<std::string> types = allRoofTypeIds();
    REQUIRE(types.size() >= 5);
    for (const std::string& type : types) {
        if ((type == kSawtoothRoofId) != sawtoothOnly) continue;
        const Measured straight = measure(setback(0.0, Place{}, type, slab, variant));
        requireClean(straight, name + " straight " + type);
        for (const Place& place : places) {
            for (int angle = 0; angle < 90; ++angle) {
                const std::string label = name + " type " + type + " slab " + std::to_string(slab) + " angle " +
                                          std::to_string(angle) + " at " + std::to_string(place.dx) + "," + std::to_string(place.dy) +
                                          " grid " + std::to_string(place.grid);
                const Measured turned = measure(setback(angle, place, type, slab, variant));
                requireClean(turned, label);
                requireSameAs(turned, straight, label);
            }
        }
    }
}

const std::vector<Place> kAllPlaces{{0.0, 0.0, 0.0}, {0.0, 0.0, 1e-6}, {97.3, 88.1, 1e-6}};
const std::vector<Place> kRoundedPlaces{{0.0, 0.0, 1e-6}, {97.3, 88.1, 1e-6}};

}

TEST_CASE("a turned setback builds with clean rings at every degree, thin slab", "[core][build][rotated]")
{
    sweep({}, "plain", kAllPlaces, 0.05);
}

TEST_CASE("a turned setback builds with clean rings at every degree, thick slab", "[core][build][rotated]")
{
    sweep({}, "plain", kAllPlaces, 0.2);
}

TEST_CASE("a turned setback with a stair well builds with clean rings at every degree, thin slab", "[core][build][rotated]")
{
    Variant v;
    v.stair = true;
    sweep(v, "stair", kAllPlaces, 0.05);
}

TEST_CASE("a turned setback with a stair well builds with clean rings at every degree, thick slab", "[core][build][rotated]")
{
    Variant v;
    v.stair = true;
    sweep(v, "stair", kAllPlaces, 0.2);
}

TEST_CASE("a turned setback with a curved wall builds with clean rings at every degree, thin slab", "[core][build][rotated]")
{
    Variant v;
    v.curved = true;
    sweep(v, "curved", kRoundedPlaces, 0.05);
}

TEST_CASE("a turned setback with a curved wall builds with clean rings at every degree, thick slab", "[core][build][rotated]")
{
    Variant v;
    v.curved = true;
    sweep(v, "curved", kRoundedPlaces, 0.2);
}

TEST_CASE("a turned setback with an island builds with clean rings at every degree, thin slab", "[core][build][rotated]")
{
    Variant v;
    v.island = true;
    sweep(v, "island", kAllPlaces, 0.05);
}

TEST_CASE("a turned setback with an island builds with clean rings at every degree, thick slab", "[core][build][rotated]")
{
    Variant v;
    v.island = true;
    sweep(v, "island", kAllPlaces, 0.2);
}

TEST_CASE("a turned plain setback with a sawtooth roof builds with clean rings at every degree, thin slab",
          "[core][build][rotated]")
{
    sweep({}, "plain", kAllPlaces, 0.05, true);
}

TEST_CASE("a turned plain setback with a sawtooth roof builds with clean rings at every degree, thick slab",
          "[core][build][rotated]")
{
    sweep({}, "plain", kAllPlaces, 0.2, true);
}

TEST_CASE("a turned stair setback with a sawtooth roof builds with clean rings at every degree, thin slab",
          "[core][build][rotated]")
{
    Variant v;
    v.stair = true;
    sweep(v, "stair", kAllPlaces, 0.05, true);
}

TEST_CASE("a turned stair setback with a sawtooth roof builds with clean rings at every degree, thick slab",
          "[core][build][rotated]")
{
    Variant v;
    v.stair = true;
    sweep(v, "stair", kAllPlaces, 0.2, true);
}

TEST_CASE("a turned curved setback with a sawtooth roof builds with clean rings at every degree, thin slab",
          "[core][build][rotated]")
{
    Variant v;
    v.curved = true;
    sweep(v, "curved", kRoundedPlaces, 0.05, true);
}

TEST_CASE("a turned curved setback with a sawtooth roof builds with clean rings at every degree, thick slab",
          "[core][build][rotated]")
{
    Variant v;
    v.curved = true;
    sweep(v, "curved", kRoundedPlaces, 0.2, true);
}

TEST_CASE("a turned island setback with a sawtooth roof builds with clean rings at every degree, thin slab",
          "[core][build][rotated]")
{
    Variant v;
    v.island = true;
    sweep(v, "island", kAllPlaces, 0.05, true);
}

TEST_CASE("a turned island setback with a sawtooth roof builds with clean rings at every degree, thick slab",
          "[core][build][rotated]")
{
    Variant v;
    v.island = true;
    sweep(v, "island", kAllPlaces, 0.2, true);
}

namespace {

// The biggest ring of the wall solids of the floor in plan coordinates against the footprint's outer ring, both
// ways, within kSlabSideTolerance.
void requireFootprintOnWalls(const Floor& floor, std::size_t holes, const std::string& label)
{
    INFO(label);
    const std::vector<PlanArea> footprint = floorFootprint(floor);
    REQUIRE(footprint.size() == 1);
    CHECK(footprint[0].holes.size() == holes);
    const std::vector<WallSolid> solids = wallSolids(floor);
    std::vector<Vec2> outer;
    double biggest = 0.0;
    for (const WallSolid& solid : solids) {
        std::vector<Vec2> ring;
        for (const Vec2& w : solid.outline.points) ring.push_back({w.x, -w.y});
        if (std::abs(signedArea(ring)) > biggest) {
            biggest = std::abs(signedArea(ring));
            outer = ring;
        }
    }
    const auto toRing = [](const std::vector<Vec2>& ring, const Vec2& p) {
        double best = 1e18;
        for (std::size_t i = 0; i < ring.size(); ++i) best = std::min(best, distanceToSegment2(p, ring[i], ring[(i + 1) % ring.size()]));
        return best;
    };
    for (const Vec2& p : footprint[0].outline) CHECK(toRing(outer, p) <= kSlabSideTolerance);
    for (const Vec2& p : outer) CHECK(toRing(footprint[0].outline, p) <= kSlabSideTolerance);
}

}

TEST_CASE("the footprint of a turned ground floor lies on its wall solids within the tolerance", "[core][build][rotated]")
{
    for (const Variant& v : {Variant{}, Variant{true, false, false}, Variant{false, true, false}, Variant{false, false, true}}) {
        for (int angle = 0; angle < 90; ++angle) {
            const std::string label = "angle " + std::to_string(angle) + (v.curved ? " curved" : "") + (v.island ? " island" : "");
            const Project project = setback(angle, Place{0.0, 0.0, 1e-6}, "flat", 0.2, v);
            requireFootprintOnWalls(project.plan.floors[0], v.island ? 1 : 0, label);
            const Project moved = setback(angle, Place{97.3, 88.1, 1e-6}, "flat", 0.2, v);
            requireFootprintOnWalls(moved.plan.floors[0], v.island ? 1 : 0, "moved " + label);
        }
    }
}

TEST_CASE("the setback at 20, 50 and 80 degrees has no underside and no ledge", "[core][build][rotated]")
{
    for (const int angle : {20, 50, 80}) {
        for (const Place& place : {Place{0.0, 0.0, 0.0}, Place{0.0, 0.0, 1e-6}, Place{97.3, 88.1, 1e-6}}) {
            INFO("angle " << angle << " at " << place.dx << "," << place.dy << " grid " << place.grid);
            const Project project = setback(angle, place, "flat", 0.2);
            const SlabBuild slab = slabBuild(project.plan, 0, floorElevations(project.plan));
            CHECK(slab.undersides.empty());
            CHECK(roofSectionAreas(project.plan, 0).size() == 1);
            CHECK(overhangArea(project.plan, 0).empty());
        }
    }
}

namespace {

// Two floors: a ground square from 0 to 4 and an upper floor whose square is moved by (dx, dy) and grown by grow
// on every side, then both turned by angle.
Plan twoSquares(double dx, double dy, double grow, double angle)
{
    Plan plan = makeNewProject().plan;
    addRectangle(plan.floors[0], 1, 0, 0, 4, 4, 0.2);
    Room r1;
    r1.id = "r1";
    r1.walls = {"w11", "w12", "w13", "w14"};
    plan.floors[0].rooms.push_back(r1);
    Floor upper;
    upper.id = "f2";
    addRectangle(upper, 2, dx - grow, dy - grow, 4 + dx + grow, 4 + dy + grow, 0.2);
    Room r2;
    r2.id = "r2";
    r2.walls = {"w21", "w22", "w23", "w24"};
    upper.rooms.push_back(r2);
    plan.floors.push_back(upper);
    turnPlan(plan, angle, 0.0, 0.0, 1e-6);
    return plan;
}

}

TEST_CASE("a hairline ring between two floors is left out and a thin one is kept", "[core][build][rotated]")
{
    for (const double angle : {0.0, 30.0, 61.0}) {
        INFO("angle " << angle);
        // The upper floor 5e-6 larger on every side: an overhang ring narrower than the merge distance.
        const Plan bigger = twoSquares(0.0, 0.0, 5e-6, angle);
        CHECK(overhangArea(bigger, 0).empty());
        CHECK(slabBuild(bigger, 0, floorElevations(bigger)).undersides.empty());
        CHECK(exposedArea(bigger, 0).empty());
        // 5e-6 smaller: an exposed ring, no roof section and no ledge.
        const Plan smaller = twoSquares(0.0, 0.0, -5e-6, angle);
        CHECK(exposedArea(smaller, 0).empty());
        CHECK(roofSectionAreas(smaller, 0).empty());
        CHECK(slabBuild(smaller, 0, floorElevations(smaller)).tops.empty());
        // 5e-5 offset on one axis (wider than the merge distance): the ledge and the underside stay, whole.
        const Plan moved = twoSquares(5e-5, 0.0, 0.0, angle);
        const SlabBuild slab = slabBuild(moved, 0, floorElevations(moved));
        double ledge = 0.0;
        for (const WorldPolygon& w : slab.tops) ledge += worldAreaOf(w);
        double under = 0.0;
        for (const WorldPolygon& w : slab.undersides) under += worldAreaOf(w);
        CHECK(ledge == Approx(5e-5 * 4.2).margin(5e-5));
        CHECK(under == Approx(5e-5 * 4.2).margin(5e-5));
    }
}

TEST_CASE("a hairline neck between two wide overhangs is cut", "[core][build][rotated]")
{
    for (const double angle : {0.0, 30.0, 77.0}) {
        for (const double grid : {0.0, 1e-6}) {
            INFO("angle " << angle << " grid " << grid);
            Plan plan = makeNewProject().plan;
            // The ground floor's top outer face is 5e-6 below the upper floor's, so the overhang is two wide wings
            // joined by a strip 5e-6 wide and 4.2 m long.
            addRectangle(plan.floors[0], 1, 0, 0, 4, 2 - 5e-6, 0.2);
            Room r1;
            r1.id = "r1";
            r1.walls = {"w11", "w12", "w13", "w14"};
            plan.floors[0].rooms.push_back(r1);
            Floor upper;
            upper.id = "f2";
            addRectangle(upper, 2, -2, 0, 6, 2, 0.2);
            Room r2;
            r2.id = "r2";
            r2.walls = {"w21", "w22", "w23", "w24"};
            upper.rooms.push_back(r2);
            plan.floors.push_back(upper);
            turnPlan(plan, angle, 50.0, 50.0, grid);
            Findings f;
            const std::vector<PlanArea> overhang = overhangArea(plan, 0);
            for (const PlanArea& a : overhang) checkPolygon(f, a.outline, a.holes, "overhang");
            for (const PlanArea& a : floorFootprint(plan.floors[1])) checkPolygon(f, a.outline, a.holes, "upper footprint");
            INFO("shortest " << f.shortest << " narrowest " << f.narrowest << " at " << f.where);
            CHECK(overhang.size() == 2);
            CHECK(f.shortest >= kW);
            CHECK(f.narrowest >= kW);
            CHECK(f.notSimple == 0);
            const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
            CHECK(slab.undersides.size() == 2);
        }
    }
}

namespace {

// A world position of the slab turned back to the unturned setback's plan coordinates.
Vec2 unturned(const Vec2& world, double angle)
{
    const double px = world.x;
    const double py = -world.y;
    const double c = std::cos(-angle * std::numbers::pi / 180.0);
    const double s = std::sin(-angle * std::numbers::pi / 180.0);
    return {px * c - py * s, px * s + py * c};
}

}

TEST_CASE("the east band of a slab stays an edge when a well ends just short of it", "[core][build][rotated]")
{
    for (const double gap : {5e-5, 5e-6}) {
        for (const double angle : {0.0, 30.0}) {
            INFO("gap " << gap << " angle " << angle);
            Plan plan = makeNewProject().plan;
            addRectangle(plan.floors[0], 1, 0, 0, 4, 4, 0.2);
            Room r1;
            r1.id = "r1";
            r1.walls = {"w11", "w12", "w13", "w14"};
            plan.floors[0].rooms.push_back(r1);
            Floor upper;
            upper.id = "f2";
            plan.floors.push_back(upper);
            Stair s;
            s.id = "s1";
            s.type = "straight";
            s.x = 3.0;
            s.y = 2.0;
            plan.floors[0].stairs.push_back(s);
            const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
            REQUIRE(layout.status.ok());
            double maxX = -1e9;
            for (const Vec2& p : layout.footprint) maxX = std::max(maxX, p.x);
            plan.floors[0].stairs[0].x += (4.1 - gap) - maxX;
            turnPlan(plan, angle, 0.0, 0.0);
            const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
            REQUIRE_FALSE(slab.polygons.empty());
            std::size_t edges = 0;
            double band = 0.0;
            for (std::size_t k = 0; k < slab.polygons.size(); ++k) {
                const std::vector<Vec2>& ring = slab.polygons[k].outline;
                for (std::size_t i = 0; i < ring.size(); ++i) {
                    const Vec2 a = unturned(ring[i], angle);
                    const Vec2 b = unturned(ring[(i + 1) % ring.size()], angle);
                    if (std::abs(a.x - 4.1) < 1e-6 && std::abs(b.x - 4.1) < 1e-6) {
                        ++edges;
                        band += std::hypot(b.x - a.x, b.y - a.y);
                        CHECK(slab.sides[k].outline[i] == SlabSide::Edge);
                    }
                }
            }
            CHECK(edges >= 1);
            // Beside the well the strip is narrower than the merge distance at the smaller gap, so it is left out.
            CHECK(band == Approx(gap < kW ? 2.1 : 4.2).margin(1e-3));
        }
    }
}

TEST_CASE("a well that runs from boundary to boundary stays a well", "[core][build][rotated]")
{
    for (const double angle : {0.0, 30.0}) {
        INFO("angle " << angle);
        Plan plan = makeNewProject().plan;
        addRectangle(plan.floors[0], 1, 0, 0, 1, 6, 0.2);
        Room r1;
        r1.id = "r1";
        r1.walls = {"w11", "w12", "w13", "w14"};
        plan.floors[0].rooms.push_back(r1);
        Floor upper;
        upper.id = "f2";
        plan.floors.push_back(upper);
        Stair s;
        s.id = "s1";
        s.type = "straight";
        s.x = 1.5;
        s.y = 1.0;
        s.width = 1.6;
        s.rotation = 90.0;
        plan.floors[0].stairs.push_back(s);
        const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
        REQUIRE(layout.footprint.size() >= 4);
        double minX = 1e9;
        double maxX = -1e9;
        for (const Vec2& p : layout.footprint) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
        }
        INFO("footprint x " << minX << " to " << maxX);
        REQUIRE(minX < -0.1);
        REQUIRE(maxX > 1.1);
        turnPlan(plan, angle, 0.0, 0.0);
        const SlabBuild slab = slabBuild(plan, 0, floorElevations(plan));
        REQUIRE(slab.polygons.size() == 2);
        for (std::size_t k = 0; k < slab.polygons.size(); ++k) {
            const std::vector<Vec2>& ring = slab.polygons[k].outline;
            std::size_t wells = 0;
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Vec2 a = unturned(ring[i], angle);
                const Vec2 b = unturned(ring[(i + 1) % ring.size()], angle);
                // In the unturned plan: on the building's sides (x -0.1 or 1.1) or ends (y -0.1 or 6.1) an edge face;
                // any other edge is the well's cut across the slab.
                const bool onSide = (std::abs(a.x + 0.1) < 1e-6 && std::abs(b.x + 0.1) < 1e-6) ||
                                    (std::abs(a.x - 1.1) < 1e-6 && std::abs(b.x - 1.1) < 1e-6) ||
                                    (std::abs(a.y + 0.1) < 1e-6 && std::abs(b.y + 0.1) < 1e-6) ||
                                    (std::abs(a.y - 6.1) < 1e-6 && std::abs(b.y - 6.1) < 1e-6);
                CHECK((slab.sides[k].outline[i] == SlabSide::Edge) == onSide);
                if (!onSide) ++wells;
            }
            CHECK(wells >= 1);
        }
    }
}

TEST_CASE("a hairline slit in a double wall leaves no hole, no short edge and no failure", "[core][build][rotated]")
{
    for (const double angle : {0.0, 30.0}) {
        for (const bool junction : {false, true}) {
            for (const std::string& type : {"flat", "gable"}) {
                INFO("angle " << angle << " junction " << junction << " type " << type);
                Project project = makeNewProject();
                Plan& plan = project.plan;
                Floor& f = plan.floors[0];
                const double y = 0.2 + 1e-6;
                addPoint(f, "a", 0, 0);
                addPoint(f, "b", 4, 0);
                addPoint(f, "c", 4, 4);
                addPoint(f, "d", 0, 4);
                addPoint(f, "p", 0, y);
                addPoint(f, "q", 4, y);
                addWall(f, "w1", "a", "b", 0.2);
                addWall(f, "w2", "b", "c", 0.2);
                addWall(f, "w3", "c", "d", 0.2);
                if (junction) {
                    addWall(f, "w4", "d", "p", 0.2);
                    addWall(f, "w5", "p", "a", 0.2);
                } else {
                    addWall(f, "w4", "d", "a", 0.2);
                }
                addWall(f, "w9", "p", "q", 0.2);
                plan.roof.type = type;
                turnPlan(plan, angle, 20.0, 20.0);
                const std::vector<PlanArea> footprint = floorFootprint(f);
                REQUIRE(footprint.size() == 1);
                Findings found;
                checkPolygon(found, footprint[0].outline, footprint[0].holes, "footprint");
                INFO("shortest " << found.shortest << " narrowest " << found.narrowest << " at " << found.where);
                CHECK(found.shortest >= kW);
                CHECK(found.narrowest >= kW);
                CHECK(found.notSimple == 0);
                CHECK(footprint[0].holes.size() == 1);
                const Measured m = measure(project);
                CHECK(m.found.shortest >= kW);
                CHECK(m.found.narrowest >= kW);
                CHECK(m.found.notSimple == 0);
            }
        }
    }
}

namespace {

namespace cl = Clipper2Lib;

constexpr int kDigits = 8;

// A ring as a path turning the way asked, read from its own first point so no digits are lost far away.
cl::PathD facingPath(std::vector<Vec2> ring, bool outline)
{
    std::vector<Vec2> local;
    for (const Vec2& p : ring) local.push_back({p.x - ring[0].x, p.y - ring[0].y});
    if ((signedArea(local) > 0.0) != outline) std::reverse(ring.begin(), ring.end());
    cl::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

// Plan areas in world X and Y as paths.
cl::PathsD worldPaths(const std::vector<PlanArea>& areas)
{
    const auto world = [](const std::vector<Vec2>& ring) {
        std::vector<Vec2> out;
        for (const Vec2& p : ring) out.push_back({p.x, -p.y});
        return out;
    };
    cl::PathsD out;
    for (const PlanArea& a : areas) {
        out.push_back(facingPath(world(a.outline), true));
        for (const std::vector<Vec2>& hole : a.holes) out.push_back(facingPath(world(hole), false));
    }
    return out;
}

// The covering faces of a roof seen from above, as one area.
cl::PathsD coveringFromAbove(const RoofBuild& roof)
{
    const auto flat = [](const std::vector<Vec3>& ring) {
        std::vector<Vec2> out;
        for (const Vec3& p : ring) out.push_back({p.x, p.y});
        return out;
    };
    cl::PathsD all;
    for (const RoofFace& face : roof.faces) {
        if (face.part != RoofPart::Covering) continue;
        all.push_back(facingPath(flat(face.outline), true));
        for (const std::vector<Vec3>& hole : face.holes) all.push_back(facingPath(flat(hole), false));
    }
    return all.empty() ? all : cl::Union(all, cl::FillRule::NonZero, kDigits);
}

// The area of paths that do not overlap.
double pathsArea(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathD& path : paths) {
        std::vector<Vec2> local;
        for (const cl::PointD& p : path) local.push_back({p.x - path[0].x, p.y - path[0].y});
        sum += signedArea(local);
    }
    return sum;
}

// The length of every edge of the paths.
double pathsLength(const cl::PathsD& paths)
{
    double sum = 0.0;
    for (const cl::PathD& path : paths) {
        for (std::size_t i = 0; i < path.size(); ++i) {
            const cl::PointD& p = path[i];
            const cl::PointD& q = path[(i + 1) % path.size()];
            sum += std::hypot(q.x - p.x, q.y - p.y);
        }
    }
    return sum;
}

// How much two areas differ, either way.
double differenceOf(const cl::PathsD& a, const cl::PathsD& b)
{
    return pathsArea(cl::Difference(a, b, cl::FillRule::NonZero, kDigits)) +
           pathsArea(cl::Difference(b, a, cl::FillRule::NonZero, kDigits));
}

// Expects the covering to be the same area as wanted within the bound.
void requireCovering(const RoofBuild& roof, const cl::PathsD& wanted)
{
    const cl::PathsD got = coveringFromAbove(roof);
    const double bound = 2.0 * kW * (pathsLength(got) + pathsLength(wanted)) +
                         kMinPlanAreaSize * static_cast<double>(roof.faces.size() + 4);
    CHECK(differenceOf(got, wanted) <= bound);
}

// Adds a ring of walls with a room inside them and returns the walls' ids.
std::vector<Id> addWalls(Floor& f, const std::string& key, const std::vector<Vec2>& ring,
                         const std::vector<double>& bulges = {})
{
    for (std::size_t i = 0; i < ring.size(); ++i) addPoint(f, "p" + key + std::to_string(i), ring[i].x, ring[i].y);
    std::vector<Id> walls;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const std::string id = "w" + key + std::to_string(i);
        addWall(f, id, "p" + key + std::to_string(i), "p" + key + std::to_string((i + 1) % ring.size()), 0.2);
        if (i < bulges.size()) f.walls.back().bulge = bulges[i];
        walls.push_back(id);
    }
    return walls;
}

// Adds a room inside rings of walls.
void addRoomOf(Floor& f, const std::string& key, const std::vector<Id>& walls)
{
    Room room;
    room.id = "r" + key;
    room.walls = walls;
    f.rooms.push_back(std::move(room));
}

// The corners of a rectangle, clockwise on screen from the top left.
std::vector<Vec2> rectangle(double x0, double y0, double x1, double y1)
{
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// Turns a plan position by angle degrees about the origin.
Vec2 turnedAbout(const Vec2& p, double angle)
{
    const double c = std::cos(angle * std::numbers::pi / 180.0);
    const double s = std::sin(angle * std::numbers::pi / 180.0);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

// Says whether two rings of one polygon cross deeper than rounding can put them where corners touch.
bool crossesDeeply(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    constexpr double kTouchDepth = 1e-7;
    const auto side = [](const Vec2& p, const Vec2& q, const Vec2& r) {
        const double v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
    };
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < b.size(); ++j) {
            const Vec2 p = a[i];
            const Vec2 q = a[(i + 1) % a.size()];
            const Vec2 r = b[j];
            const Vec2 s = b[(j + 1) % b.size()];
            if (side(p, q, r) * side(p, q, s) >= 0 || side(r, s, p) * side(r, s, q) >= 0) continue;
            if (std::min({distanceToSegment2(p, r, s), distanceToSegment2(q, r, s), distanceToSegment2(r, p, q),
                          distanceToSegment2(s, p, q)}) > kTouchDepth) {
                return true;
            }
        }
    }
    return false;
}

// The covering's outline where the header says it must be: the section grown by the overhang.
cl::PathsD eavesOf(const Plan& plan)
{
    const cl::PathsD section = worldPaths(roofSectionAreas(plan, 0));
    if (plan.roof.overhang <= 0.0) return section;
    return cl::InflatePaths(section, plan.roof.overhang, cl::JoinType::Miter, cl::EndType::Polygon, 5.0, kDigits);
}

// How far the nearest covering is from a plan position, seen from above.
double coverGap(const RoofBuild& roof, const Vec2& planPosition)
{
    const Vec2 at{planPosition.x, -planPosition.y};
    double best = 1e18;
    for (const RoofFace& face : roof.faces) {
        if (face.part != RoofPart::Covering) continue;
        std::vector<Vec2> ring;
        for (const Vec3& p : face.outline) ring.push_back({p.x, p.y});
        if (insidePolygon(at, ring)) return 0.0;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            best = std::min(best, distanceToSegment2(at, ring[i], ring[(i + 1) % ring.size()]));
        }
    }
    return best;
}

}

TEST_CASE("a corner touching the edge below puts no covering or hull over the upper floor", "[core][build][rotated]")
{
    for (const double out : {-4e-6, 0.0, 4e-6}) {
        for (const double angle : {0.0, 30.0}) {
            for (const std::string& type : allRoofTypeIds()) {
                for (const double overhang : {0.0, 0.3}) {
                    INFO("out " << out << " angle " << angle << " type " << type << " overhang " << overhang);
                    Project project = makeNewProject();
                    Plan& plan = project.plan;
                    addRoomOf(plan.floors[0], "a", addWalls(plan.floors[0], "a", rectangle(0, 0, 4, 4)));
                    Floor upper;
                    upper.id = "f2";
                    const double y = 2.958582644 + out;
                    addRoomOf(upper, "d", addWalls(upper, "d", {{2, y - 1}, {3, y}, {2, y + 1}, {1, y}}));
                    plan.floors.push_back(std::move(upper));
                    plan.roof.type = type;
                    plan.roof.overhang = overhang;
                    plan.roof.parapet = 0.0;
                    plan.roof.direction = angle;
                    turnPlan(plan, angle, 0.0, 0.0);

                    const cl::PathsD storey =
                        cl::InflatePaths(worldPaths(floorFootprint(plan.floors[1])), -(kSlabSideTolerance + 1e-6),
                                         cl::JoinType::Miter, cl::EndType::Polygon, 10.0, kDigits);
                    const BuildPlan built = makeBuildPlan(project, {});
                    REQUIRE_FALSE(built.floors[0].roofs.empty());
                    cl::PathsD all;
                    for (const RoofBuild& roof : built.floors[0].roofs) {
                        const cl::PathsD covering = coveringFromAbove(roof);
                        CHECK(pathsArea(cl::Intersect(covering, storey, cl::FillRule::NonZero, kDigits)) <= 1e-7);
                        all.insert(all.end(), covering.begin(), covering.end());
                    }
                    for (const CollisionShape& shape : built.collision[0]) {
                        for (const std::vector<Vec3>& hull : shape.hulls) {
                            std::vector<Vec2> outline;
                            for (std::size_t i = 0; i < hull.size() / 2; ++i) outline.push_back({hull[i].x, hull[i].y});
                            if (outline.size() < 3) continue;
                            const cl::PathsD path{facingPath(outline, true)};
                            CHECK(pathsArea(cl::Intersect(path, storey, cl::FillRule::NonZero, kDigits)) <= 1e-7);
                        }
                    }
                    if (overhang == 0.0) {
                        const cl::PathsD sections = worldPaths(roofSectionAreas(plan, 0));
                        const double bound = 2.0 * kW * (pathsLength(all) + pathsLength(sections)) + 32.0 * kMinPlanAreaSize;
                        CHECK(differenceOf(all, sections) <= bound);
                    }
                    // Touching corners need not be simple in float32. They keep their turning, area and edges.
                    for (const PlanArea& area : exposedArea(plan, 0)) {
                        Findings f;
                        checkRing(f, area.outline, 1, "exposed outline");
                        CHECK(f.shortest >= kW);
                        CHECK(signedArea(area.outline) >= kMinPlanAreaSize);
                        for (const std::vector<Vec2>& hole : area.holes) {
                            checkRing(f, hole, -1, "exposed hole");
                            CHECK(f.shortest >= kW);
                            CHECK(-signedArea(hole) >= kMinPlanAreaSize);
                            CHECK_FALSE(crossesDeeply(area.outline, hole));
                        }
                    }
                }
            }
        }
    }
}

namespace {

// Checks the hip apex of round rooms of these radii, where 0 is the D bay.
void checkHipApex(const std::vector<double>& radii)
{
    for (const double radius : radii) {
        // Radius 0 stands for the D bay of radius 2.
        const bool bay = radius == 0.0;
        const double r = bay ? 2.0 : radius;
        const double sweepAngle = bay ? std::numbers::pi : std::numbers::pi / 2.0;
        const int steps = arcSegmentCount(r, sweepAngle) * (bay ? 1 : 4);
        std::vector<Vec2> ring;
        if (bay) ring.push_back({-3, -2});
        for (int k = 0; k < steps + (bay ? 1 : 0); ++k) {
            const double a = bay ? std::numbers::pi * (static_cast<double>(k) / steps - 0.5) : 2.0 * std::numbers::pi * k / steps;
            ring.push_back({r * std::cos(a), r * std::sin(a)});
        }
        if (bay) ring.push_back({-3, 2});
        for (const double angle : {0.0, 30.0}) {
            INFO("straight walls, radius " << radius << " angle " << angle);
            Plan plan = makeNewProject().plan;
            addRoomOf(plan.floors[0], "o", addWalls(plan.floors[0], "o", ring));
            plan.roof.type = "hip";
            plan.roof.direction = angle;
            turnPlan(plan, angle, 0.0, 0.0);
            const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, floorElevations(plan)).roofs;
            REQUIRE(roofs.size() == 1);
            REQUIRE(roofs[0].type == kHipRoofId);
            int reached = 0;
            for (const RoofFace& face : roofs[0].faces) {
                if (face.part != RoofPart::Covering) continue;
                double best = 1e18;
                for (const Vec3& p : face.outline) best = std::min(best, std::hypot(p.x, p.y));
                if (best <= 2.0 * kW) ++reached;
            }
            CHECK(reached >= steps);
            requireCovering(roofs[0], eavesOf(plan));
        }
    }
}

}

TEST_CASE("a hip on a round room or a D bay keeps its apex", "[core][build][rotated]")
{
    checkHipApex({2.0, 4.0, 0.0});
}

TEST_CASE("a hip on a round room of radius 10 keeps its apex", "[core][build][rotated]")
{
    checkHipApex({10.0});
}

TEST_CASE("a round room of curved walls has no hole in the middle of its roof", "[core][build][rotated]")
{
    for (const double radius : {2.0, 4.0, 10.0, 0.0}) {
        const bool bay = radius == 0.0;
        for (const double angle : {0.0, 30.0}) {
            INFO("curved walls, radius " << radius << " angle " << angle);
            Plan plan = makeNewProject().plan;
            Floor& f = plan.floors[0];
            if (bay) {
                addRoomOf(f, "o", addWalls(f, "o", {{-3, -2}, {0, -2}, {0, 2}, {-3, 2}}, {0.0, -2.0, 0.0, 0.0}));
            } else {
                const double bulge = -radius * (1.0 - std::sqrt(0.5));
                addRoomOf(f, "o",
                          addWalls(f, "o", {{0, -radius}, {radius, 0}, {0, radius}, {-radius, 0}}, {bulge, bulge, bulge, bulge}));
            }
            plan.roof.type = "hip";
            plan.roof.direction = angle;
            turnPlan(plan, angle, 0.0, 0.0);
            const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, floorElevations(plan)).roofs;
            REQUIRE(roofs.size() == 1);
            for (const double out : {0.0, 2e-4, 1e-3, 5e-3}) {
                for (int k = 0; k < 12; ++k) {
                    const double a = 0.0123 + k * std::numbers::pi / 6.0;
                    CHECK(coverGap(roofs[0], {out * std::cos(a), out * std::sin(a)}) <= 2.0 * kW);
                }
            }
            requireCovering(roofs[0], eavesOf(plan));
        }
    }
}

TEST_CASE("pieces nearer than the merge distance join and further ones stay apart", "[core][build][rotated]")
{
    for (const double gap : {1e-6, 5e-6, 5e-5}) {
        for (const double angle : {0.0, 30.0}) {
            for (const bool freeWall : {false, true}) {
                INFO("gap " << gap << " angle " << angle << " free wall " << freeWall);
                Plan plan = makeNewProject().plan;
                Floor& f = plan.floors[0];
                addRoomOf(f, "a", addWalls(f, "a", rectangle(0, 0, 4, 4)));
                if (freeWall) {
                    addPoint(f, "p", 4.2 + gap, 0);
                    addPoint(f, "q", 4.2 + gap, 4);
                    addWall(f, "w9", "p", "q", 0.2);
                } else {
                    addRoomOf(f, "b", addWalls(f, "b", rectangle(4.2 + gap, 0, 8.2 + gap, 4)));
                }
                turnPlan(plan, angle, 0.0, 0.0);
                const std::size_t wanted = gap < kW ? 1u : 2u;
                CHECK(floorFootprint(plan.floors[0]).size() == wanted);
                CHECK(roofSectionAreas(plan, 0).size() == wanted);
            }
        }
    }
    for (const double angle : {0.0, 30.0}) {
        INFO("piece in a courtyard, angle " << angle);
        Plan plan = makeNewProject().plan;
        Floor& f = plan.floors[0];
        std::vector<Id> walls = addWalls(f, "a", rectangle(0, 0, 10, 10));
        const std::vector<Id> inner = addWalls(f, "b", rectangle(2, 2, 8, 8));
        walls.insert(walls.end(), inner.begin(), inner.end());
        addRoomOf(f, "a", walls);
        addRoomOf(f, "c", addWalls(f, "c", rectangle(2.2 + 5e-6, 4, 4, 6)));
        turnPlan(plan, angle, 0.0, 0.0);
        const std::vector<PlanArea> footprint = floorFootprint(f);
        REQUIRE(footprint.size() == 1);
        CHECK(footprint[0].holes.size() == 1);
    }
}

TEST_CASE("an eaves tip cut by the floor above keeps the mitre limit of 5", "[core][build][rotated]")
{
    for (const double corner : {14.0, 20.0, 22.0, 30.0}) {
        for (const double overhang : {0.3, 2.0}) {
            for (const double angle : {0.0, 30.0}) {
                for (const std::string& type : allRoofTypeIds()) {
                    INFO("corner " << corner << " overhang " << overhang << " angle " << angle << " type " << type);
                    Plan plan = makeNewProject().plan;
                    addRoomOf(plan.floors[0], "a", addWalls(plan.floors[0], "a", rectangle(0, 0, 6, 4)));
                    const double a = corner * std::numbers::pi / 180.0;
                    const double bend = 0.5 + (0.1 - 0.1 / std::cos(a)) / std::tan(a);
                    const double east = -0.1 + 5.5 * std::tan(a) + 0.1 / std::cos(a);
                    Floor upper;
                    upper.id = "f2";
                    addRoomOf(upper, "u", addWalls(upper, "u", {{0, 0}, {bend, 0}, {6, east}, {6, 4}, {0, 4}}));
                    plan.floors.push_back(std::move(upper));
                    plan.roof.type = type;
                    plan.roof.overhang = overhang;
                    plan.roof.parapet = 0.0;
                    plan.roof.direction = angle;
                    turnPlan(plan, angle, 0.0, 0.0);
                    const cl::PathsD section = worldPaths(roofSectionAreas(plan, 0));
                    REQUIRE_FALSE(section.empty());
                    double furthest = 0.0;
                    for (const RoofBuild& roof : floorRoofs(plan, 0, floorElevations(plan)).roofs) {
                        for (const RoofFace& face : roof.faces) {
                            for (const Vec3& p : face.outline) {
                                double best = 1e18;
                                for (const cl::PathD& path : section) {
                                    for (std::size_t i = 0; i < path.size(); ++i) {
                                        const cl::PointD& s = path[i];
                                        const cl::PointD& t = path[(i + 1) % path.size()];
                                        best = std::min(best, distanceToSegment2({p.x, p.y}, {s.x, s.y}, {t.x, t.y}));
                                    }
                                }
                                furthest = std::max(furthest, best);
                            }
                        }
                    }
                    CHECK(furthest <= 5.0 * overhang + kSlabSideTolerance);
                    if (corner > 25.0 && type != kSawtoothRoofId) CHECK(furthest >= 0.99 * overhang / std::sin(a / 2.0));
                }
            }
        }
    }
}

TEST_CASE("the eaves of a narrow U leave no slit", "[core][build][rotated]")
{
    for (const std::string& type : allRoofTypeIds()) {
        if (!findRoofType(type)->usesPitch()) continue;
        for (const double angle : {0.0, 30.0}) {
            INFO("type " << type << " angle " << angle);
            Plan plan = makeNewProject().plan;
            // The arms' outer wall faces are 0.6 apart, so the two eaves of 0.3 meet.
            addRoomOf(plan.floors[0], "u",
                      addWalls(plan.floors[0], "u", {{0, 0}, {3, 0}, {3, 4}, {3.8, 4}, {3.8, 0}, {6.8, 0}, {6.8, 6}, {0, 6}}));
            plan.roof.type = type;
            plan.roof.overhang = 0.3;
            plan.roof.direction = angle;
            turnPlan(plan, angle, 0.0, 0.0);
            const std::vector<RoofBuild> roofs = floorRoofs(plan, 0, floorElevations(plan)).roofs;
            REQUIRE(roofs.size() == 1);
            for (const RoofFace& face : roofs[0].faces) {
                Findings f;
                checkFace(f, face, "face of the narrow U");
                CHECK(f.shortest >= kW);
                CHECK(face.holes.empty());
            }
            for (const double along : {0.5, 1.3, 2.3, 3.4}) CHECK(coverGap(roofs[0], turnedAbout({3.4, along}, angle)) <= kW);
        }
    }
}
