#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/BuildJob.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Roofs.h>
#include <plynth/core/Storeys.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// The shortest edge of a ring, taken round to the first point.
double shortestEdge(const std::vector<Vec2>& ring)
{
    double best = 1e18;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        best = std::min(best, std::hypot(b.x - a.x, b.y - a.y));
    }
    return best;
}

double distance3(const Vec3& a, const Vec3& b)
{
    return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
}

double shortestEdge(const std::vector<Vec3>& ring)
{
    double best = 1e18;
    for (std::size_t i = 0; i < ring.size(); ++i) best = std::min(best, distance3(ring[i], ring[(i + 1) % ring.size()]));
    return best;
}

// The area of a planar polygon in space: half the length of the sum of cross products.
double area3(const std::vector<Vec3>& ring)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3 a = ring[i];
        const Vec3 b = ring[(i + 1) % ring.size()];
        x += a.y * b.z - a.z * b.y;
        y += a.z * b.x - a.x * b.z;
        z += a.x * b.y - a.y * b.x;
    }
    return 0.5 * std::sqrt(x * x + y * y + z * z);
}

double worldAreaOf(const WorldPolygon& w)
{
    double sum = signedArea(w.outline);
    for (const auto& h : w.holes) sum += signedArea(h);
    return sum;
}

// What one build of the setback makes, for comparing a turned copy with the straight one.
struct Measured {
    double shortest = 1e18; // the shortest edge or distance between neighbouring points of anything built
    double slabArea = 0.0;
    double undersideArea = 0.0;
    double ledgeArea = 0.0;
    double sectionArea = 0.0;
    double roofFaceArea = 0.0;
    double footprintArea = 0.0;
    std::size_t roofs = 0;
    std::string where; // what the shortest one belongs to
};

void note(Measured& m, double value, const std::string& what)
{
    if (value < m.shortest) {
        m.shortest = value;
        m.where = what;
    }
}

// The 4 by 4 m ground floor under an upper floor from (0, 0) to (3, 4), all turned by angle (degrees) about
// the origin and moved by (dx, dy). Walls are 0.2 m. The default roof is of the given type.
Measured measure(double angle, double dx, double dy, const std::string& roofType, double slab)
{
    Project project = makeNewProject();
    Plan& plan = project.plan;
    addRectangle(plan.floors[0], 1, 0, 0, 4, 4, 0.2);
    Room r1;
    r1.id = "r1";
    r1.walls = {"w11", "w12", "w13", "w14"};
    plan.floors[0].rooms.push_back(r1);
    plan.floors[0].slabThickness = slab;
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

    const double c = std::cos(angle * std::numbers::pi / 180.0);
    const double s = std::sin(angle * std::numbers::pi / 180.0);
    for (Floor& f : plan.floors) {
        for (Point& p : f.points) {
            const double x = p.x;
            const double y = p.y;
            p.x = x * c - y * s + dx;
            p.y = x * s + y * c + dy;
        }
    }

    Measured m;
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        for (const PlanArea& a : roofSectionAreas(plan, i)) {
            m.sectionArea += planAreaSize(a);
            note(m, shortestEdge(a.outline), "roof section outline");
            for (const auto& h : a.holes) note(m, shortestEdge(h), "roof section hole");
        }
        for (const PlanArea& a : floorFootprint(plan.floors[i])) m.footprintArea += planAreaSize(a);
    }
    const BuildPlan built = makeBuildPlan(project, {});
    for (std::size_t i = 0; i < built.floors.size(); ++i) {
        const FloorBuild& f = built.floors[i];
        const auto polygon = [&](const WorldPolygon& w, const std::string& what) {
            note(m, shortestEdge(w.outline), what);
            for (const auto& h : w.holes) note(m, shortestEdge(h), what + " hole");
        };
        for (const WorldPolygon& w : f.slab.polygons) {
            polygon(w, "slab");
            m.slabArea += worldAreaOf(w);
        }
        for (const WorldPolygon& w : f.slab.undersides) {
            polygon(w, "slab underside");
            m.undersideArea += worldAreaOf(w);
        }
        for (const WorldPolygon& w : f.slab.tops) {
            polygon(w, "slab top");
            m.ledgeArea += worldAreaOf(w);
        }
        for (const RoofBuild& roof : f.roofs) {
            ++m.roofs;
            for (const RoofFace& face : roof.faces) {
                const std::string what = std::string("roof face ") + std::string(roofPartName(face.part)) + " of " + roofType;
                note(m, shortestEdge(face.outline), what);
                double a = area3(face.outline);
                for (const auto& h : face.holes) {
                    note(m, shortestEdge(h), what + " hole");
                    a -= area3(h);
                }
                m.roofFaceArea += a;
            }
        }
        for (const CollisionShape& shape : built.collision[i]) {
            for (const CollisionPiece& piece : shape.pieces) note(m, shortestEdge(piece.outline), "collision piece");
            for (const auto& hull : shape.hulls) {
                for (std::size_t a = 0; a < hull.size(); ++a) {
                    for (std::size_t b = a + 1; b < hull.size(); ++b) note(m, distance3(hull[a], hull[b]), "collision hull");
                }
            }
        }
    }
    return m;
}

// Nothing built may have an edge a float32 at 100 m (about 8e-6 m apart) could merge.
constexpr double kSurvivesFloat32 = 5e-5;

void requireSameAs(const Measured& turned, const Measured& straight)
{
    CHECK(turned.slabArea == Approx(straight.slabArea).margin(1e-6));
    CHECK(turned.undersideArea == Approx(straight.undersideArea).margin(1e-6));
    CHECK(turned.ledgeArea == Approx(straight.ledgeArea).margin(1e-6));
    CHECK(turned.sectionArea == Approx(straight.sectionArea).margin(1e-6));
    CHECK(turned.footprintArea == Approx(straight.footprintArea).margin(1e-6));
    CHECK(turned.roofs == straight.roofs);
}

}

TEST_CASE("a turned setback builds with no edge float32 could lose", "[core][build][rotated]")
{
    std::vector<std::string> types;
    for (const RoofType* t : allRoofTypes()) types.emplace_back(t->id());
    REQUIRE(types.size() >= 5);
    for (const double slab : {0.05, 0.2}) {
        for (const std::string& type : types) {
            const Measured straight = measure(0.0, 0.0, 0.0, type, slab);
            REQUIRE(straight.roofs == 2);
            for (int angle = 0; angle <= 85; angle += 5) {
                INFO("type " << type << " slab " << slab << " angle " << angle);
                const Measured turned = measure(angle, 0.0, 0.0, type, slab);
                INFO("shortest " << turned.shortest << " at " << turned.where);
                CHECK(turned.shortest > kSurvivesFloat32);
                requireSameAs(turned, straight);
                if (type == "flat") CHECK(turned.roofFaceArea == Approx(straight.roofFaceArea).margin(1e-5));
            }
            for (const auto& [dx, dy] : std::vector<std::pair<double, double>>{{37.123, 81.456}, {95.0, 3.7}, {60.25, 60.75}}) {
                INFO("type " << type << " slab " << slab << " at " << dx << "," << dy << " angle 30");
                const Measured turned = measure(30.0, dx, dy, type, slab);
                INFO("shortest " << turned.shortest << " at " << turned.where);
                CHECK(turned.shortest > kSurvivesFloat32);
                requireSameAs(turned, straight);
                if (type == "flat") CHECK(turned.roofFaceArea == Approx(straight.roofFaceArea).margin(1e-5));
            }
        }
    }
}
