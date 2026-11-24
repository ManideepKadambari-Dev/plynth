#include "TestSupport.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/Collision.h>
#include <plynth/core/StairRailings.h>
#include <plynth/core/Stairs.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;

namespace {

// A two floor plan with one stair of a type on the lower floor, far from any wall.
Plan stairPlan(const std::string& type, StairTurn turn = StairTurn::Right)
{
    Plan plan;
    Floor f1;
    f1.id = "f1";
    f1.ceilingHeight = 3.0;
    f1.slabThickness = 0.2;
    Stair s;
    s.id = "s1";
    s.type = type;
    s.x = 5.0;
    s.y = 5.0;
    s.turn = turn;
    f1.stairs = {s};
    Floor f2;
    f2.id = "f2";
    plan.floors = {f1, f2};
    return plan;
}

// The middle of a section.
Vec3 middle(const std::vector<Vec3>& section)
{
    Vec3 c;
    for (const Vec3& p : section) {
        c.x += p.x;
        c.y += p.y;
        c.z += p.z;
    }
    const double n = static_cast<double>(section.size());
    return {c.x / n, c.y / n, c.z / n};
}

// Says whether a sweep is a post: two sections straight above each other.
bool isPost(const Sweep& s)
{
    if (s.sections.size() != 2) return false;
    const Vec3 a = middle(s.sections[0]);
    const Vec3 b = middle(s.sections[1]);
    return std::hypot(a.x - b.x, a.y - b.y) < 1e-9 && std::fabs(a.z - b.z) > 0.1;
}

// The railings of a kind.
std::vector<StairRailing> ofKind(const std::vector<StairRailing>& all, const std::string& kind)
{
    std::vector<StairRailing> out;
    for (const StairRailing& r : all) {
        if (r.kind == kind) out.push_back(r);
    }
    return out;
}

// The number of rails (sweeps that are not posts).
int railCount(const StairRailing& r)
{
    int n = 0;
    for (const Sweep& s : r.sweeps) n += isPost(s) ? 0 : 1;
    return n;
}

// Checks the posts of a railing stand at most kMaxPostSpacing apart along its rails.
void checkPosts(const StairRailing& r)
{
    std::vector<Vec3> posts;
    for (const Sweep& s : r.sweeps) {
        if (isPost(s)) posts.push_back(middle(s.sections[1]));
    }
    REQUIRE(posts.size() >= 2);
    for (std::size_t i = 1; i < posts.size(); ++i) {
        const double gap = std::hypot(posts[i].x - posts[i - 1].x, posts[i].y - posts[i - 1].y);
        if (gap < 1e-9) continue;
        CHECK(gap <= kMaxPostSpacing + 1e-6);
    }
}

}

TEST_CASE("every stair type gets railings, posts and a guard", "[railings]")
{
    for (const char* type : {"straight", "l_shape", "u_shape", "spiral"}) {
        for (StairTurn turn : {StairTurn::Left, StairTurn::Right}) {
            INFO(type << " " << stairTurnName(turn));
            const Plan plan = stairPlan(type, turn);
            const std::vector<double> levels = floorElevations(plan);
            const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
            REQUIRE(layout.status.ok());
            const std::vector<StairRailing> all = stairRailings(plan, TrimSettings{}, levels);
            const auto railings = ofKind(all, "railing");
            const auto guards = ofKind(all, "guard");
            REQUIRE(railings.size() == 1);
            REQUIRE(guards.size() == 1);
            CHECK(railings[0].floor == "f1");
            CHECK(railings[0].stair == "s1");
            CHECK(railingObjectName(railings[0]) == "SM_f1_s1_Railing");
            CHECK(railingObjectName(guards[0]) == "SM_f2_s1_Guard");
            CHECK(railCount(railings[0]) == (std::string(type) == "spiral" ? 1 : 2));
            checkPosts(railings[0]);

            // The rail stays at the railing height above the steps: from the first step's top to the rise.
            double firstTop = layout.rise;
            for (const StairStep& step : layout.steps) {
                if (step.part != StairPart::Column) firstTop = std::min(firstTop, step.top);
            }
            for (const Sweep& s : railings[0].sweeps) {
                if (isPost(s)) continue;
                for (const auto& section : s.sections) {
                    const double z = middle(section).z;
                    CHECK(z >= firstTop + 0.9 - 1e-6);
                    CHECK(z <= layout.rise + 0.9 + 1e-6);
                }
            }
            for (const Sweep& s : guards[0].sweeps) {
                if (isPost(s)) continue;
                for (const auto& section : s.sections) CHECK_THAT(middle(section).z, WithinAbs(levels[1] + 0.9, 1e-6));
            }
            for (const StairRailing& r : all) {
                REQUIRE_FALSE(r.collision.empty());
                for (const CollisionPiece& piece : r.collision) {
                    CHECK(isConvexRing(piece.outline));
                    CHECK(piece.top > piece.bottom);
                }
            }
        }
    }
}

TEST_CASE("the guard leaves the arrival edge open", "[railings]")
{
    const Plan plan = stairPlan("straight");
    const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    const auto guards = ofKind(stairRailings(plan, TrimSettings{}, floorElevations(plan)), "guard");
    REQUIRE(guards.size() == 1);
    const Vec2 arrival = planToWorld(layout.arrival);
    for (const Sweep& s : guards[0].sweeps) {
        if (isPost(s)) continue;
        for (std::size_t k = 0; k + 1 < s.sections.size(); ++k) {
            const Vec3 a = middle(s.sections[k]);
            const Vec3 b = middle(s.sections[k + 1]);
            const Vec2 mid{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
            CHECK(distance(mid, arrival) > 0.3);
        }
    }
}

TEST_CASE("a stair side along a wall gets no railing", "[railings]")
{
    Plan plan = stairPlan("straight");
    const StairLayout layout = stairLayout(plan, plan.floors[0], plan.floors[0].stairs[0]);
    // A wall beside the stair, along its whole length, just past its right edge.
    const Vec2 a = layout.walkLine.front();
    const Vec2 b = layout.walkLine.back();
    const double len = distance(a, b);
    const Vec2 d{(b.x - a.x) / len, (b.y - a.y) / len};
    const Vec2 r = rightOf(d);
    const double off = plan.floors[0].stairs[0].width / 2.0 + 0.1 + 0.05;
    Floor& f1 = plan.floors[0];
    f1.points = {{"p1", a.x + r.x * off - d.x, a.y + r.y * off - d.y, 0.0}, {"p2", b.x + r.x * off + d.x, b.y + r.y * off + d.y, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    f1.walls = {w};
    const auto railings = ofKind(stairRailings(plan, TrimSettings{}, floorElevations(plan)), "railing");
    REQUIRE(railings.size() == 1);
    CHECK(railCount(railings[0]) == 1);
}

TEST_CASE("railings off gives none", "[railings]")
{
    TrimSettings off;
    off.railing.enabled = false;
    const Plan plan = stairPlan("straight");
    CHECK(stairRailings(plan, off, floorElevations(plan)).empty());
    TrimSettings missing;
    missing.railing.profile = "not_installed";
    const auto all = stairRailings(plan, missing, floorElevations(plan));
    REQUIRE_FALSE(all.empty());
    CHECK(all[0].profileMissing);
}
