#include "OutlineChecks.h"
#include "TestSupport.h"

#include <plynth/core/OpeningShapes.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A made up shape for the registry tests.
class DummyShape final : public OpeningShape {
public:
    explicit DummyShape(std::string id) : id_(std::move(id)) {}
    std::string_view id() const override { return id_; }
    std::string_view name() const override { return "Dummy"; }
    std::string_view sizeRule() const override { return {}; }
    int sortKey() const override { return 2000; }
    bool builtIn() const override { return false; }
    bool fits(double, double) const override { return true; }
    std::vector<Vec2> outline(double w, double h) const override { return {{-w / 2, 0}, {w / 2, 0}, {0, h}}; }

private:
    std::string id_;
};

}

TEST_CASE("the five built-in shapes are registered in order", "[core][shapes]") {
    const auto all = allOpeningShapes();
    REQUIRE(all.size() >= 5);
    REQUIRE(all[0]->id() == "rectangle");
    REQUIRE(all[1]->id() == "round_arch");
    REQUIRE(all[2]->id() == "flat_arch");
    REQUIRE(all[3]->id() == "pointed_arch");
    REQUIRE(all[4]->id() == "circle");
    for (int i = 0; i < 5; ++i) {
        REQUIRE(all[static_cast<std::size_t>(i)]->builtIn());
        REQUIRE_FALSE(all[static_cast<std::size_t>(i)]->name().empty());
    }
    REQUIRE(all[0]->sizeRule().empty());
    REQUIRE_FALSE(all[1]->sizeRule().empty());
    REQUIRE(findOpeningShape("round_arch") != nullptr);
    REQUIRE(findOpeningShape("nothing") == nullptr);
}

TEST_CASE("every built-in outline is a clean polygon in the box", "[core][shapes]") {
    const std::vector<std::pair<double, double>> sizes = {{0.9, 2.1}, {1.2, 1.2}, {0.5, 2.4}, {3.0, 1.0}, {1.0, 0.9}};
    for (const char* id : {"rectangle", "round_arch", "flat_arch", "pointed_arch", "circle"}) {
        for (const auto& [w, h] : sizes) {
            const OpeningShape* shape = findOpeningShape(id);
            REQUIRE(shape != nullptr);
            if (!shape->fits(w, h)) continue;
            INFO(id << " " << w << " by " << h);
            const std::vector<Vec2> o = shape->outline(w, h);
            REQUIRE(o.size() >= 3);
            plynth::test::requireCleanOutline(o, w, h);
        }
    }
}

TEST_CASE("the size rules", "[core][shapes]") {
    SECTION("rectangle and circle take any size") {
        REQUIRE(checkOpeningShape("rectangle", 0.01, 5.0).ok());
        REQUIRE(checkOpeningShape("circle", 5.0, 0.01).ok());
    }
    SECTION("a round arch 1.0 by 0.4 is refused with its rule") {
        const Status s = checkOpeningShape("round_arch", 1.0, 0.4);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message == std::string(findOpeningShape("round_arch")->sizeRule()));
        REQUIRE(checkOpeningShape("round_arch", 1.0, 0.5).ok());
    }
    SECTION("a pointed arch needs 0.866 for width 1") {
        REQUIRE(checkOpeningShape("pointed_arch", 1.0, 0.8661).ok());
        REQUIRE(checkOpeningShape("pointed_arch", 1.0, 2.0).ok());
        REQUIRE(checkOpeningShape("pointed_arch", 1.0, 0.865).code == ErrorCode::InvalidValue);
    }
    SECTION("a flat arch needs a sixth of the width") {
        REQUIRE(checkOpeningShape("flat_arch", 1.2, 0.2).ok());
        REQUIRE(checkOpeningShape("flat_arch", 1.2, 0.19).code == ErrorCode::InvalidValue);
    }
    SECTION("bad numbers and unknown ids") {
        for (double bad : {0.0, -1.0, std::nan("")}) {
            REQUIRE(checkOpeningShape("rectangle", bad, 1.0).code == ErrorCode::InvalidValue);
            REQUIRE(checkOpeningShape("rectangle", 1.0, bad).code == ErrorCode::InvalidValue);
        }
        const Status s = checkOpeningShape("trapezoid", 1.0, 1.0);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        for (const char* id : {"rectangle", "round_arch", "flat_arch", "pointed_arch", "circle"}) {
            REQUIRE(s.message.find(id) != std::string::npos);
        }
        REQUIRE(s.message.find("trapezoid") != std::string::npos);
    }
}

TEST_CASE("the rectangle outline is the box", "[core][shapes]") {
    const std::vector<Vec2> o = findOpeningShape("rectangle")->outline(0.9, 2.1);
    REQUIRE(o.size() == 4);
    REQUIRE(o[0] == Vec2{-0.45, 0.0});
    REQUIRE(o[1] == Vec2{0.45, 0.0});
    REQUIRE(o[2] == Vec2{0.45, 2.1});
    REQUIRE(o[3] == Vec2{-0.45, 2.1});
}

TEST_CASE("the round arch has straight sides and a half circle", "[core][shapes]") {
    const std::vector<Vec2> o = findOpeningShape("round_arch")->outline(1.0, 2.0);
    // The side tops are at height 1.5 and the top middle is a corner at 2.0.
    bool topMiddle = false;
    for (const Vec2& p : o) {
        if (std::fabs(p.x) < 1e-9 && std::fabs(p.y - 2.0) < 1e-9) topMiddle = true;
        if (std::fabs(std::fabs(p.x) - 0.5) < 1e-9 && p.y > 0.0) REQUIRE(p.y <= 1.5 + 1e-9);
        // Every curved point is on the circle of radius 0.5 around (0, 1.5).
        if (p.y > 1.5 + 1e-9) REQUIRE(std::hypot(p.x, p.y - 1.5) == Approx(0.5).margin(1e-9));
    }
    REQUIRE(topMiddle);
    const double area = signedArea(o);
    REQUIRE(area == Approx(1.0 * 1.5 + kPi * 0.25 / 2.0).epsilon(0.005));
}

TEST_CASE("the flat arch top is at the box top and its ends are width / 6 lower", "[core][shapes]") {
    const double w = 1.2;
    const double h = 1.0;
    const std::vector<Vec2> o = findOpeningShape("flat_arch")->outline(w, h);
    double top = 0.0;
    double topX = 99.0;
    double sideTop = 0.0;
    for (const Vec2& p : o) {
        if (p.y > top) {
            top = p.y;
            topX = p.x;
        }
        if (std::fabs(std::fabs(p.x) - w / 2.0) < 1e-9) sideTop = std::max(sideTop, p.y);
    }
    REQUIRE(top == Approx(h).margin(1e-9));
    REQUIRE(topX == Approx(0.0).margin(1e-9));
    REQUIRE(sideTop == Approx(h - w / 6.0).margin(1e-9));
}

TEST_CASE("the pointed arch meets at the top middle with arcs of radius width", "[core][shapes]") {
    const double w = 1.0;
    const double h = 2.0;
    const std::vector<Vec2> o = findOpeningShape("pointed_arch")->outline(w, h);
    const double spring = h - w * std::sqrt(3.0) / 2.0;
    bool apex = false;
    for (const Vec2& p : o) {
        if (std::fabs(p.x) < 1e-9 && std::fabs(p.y - h) < 1e-9) apex = true;
        if (p.y > spring + 1e-9) {
            // Points on the right half are on the circle around the left side's top end, and the other way round.
            const double r = p.x > 0 ? std::hypot(p.x + w / 2.0, p.y - spring) : std::hypot(p.x - w / 2.0, p.y - spring);
            REQUIRE(r == Approx(w).margin(1e-9));
        }
    }
    REQUIRE(apex);
}

TEST_CASE("the circle fills the box", "[core][shapes]") {
    const std::vector<Vec2> o = findOpeningShape("circle")->outline(1.0, 1.0);
    REQUIRE(signedArea(o) == Approx(kPi / 4.0).epsilon(0.005));
    const std::vector<Vec2> oval = findOpeningShape("circle")->outline(2.0, 1.0);
    REQUIRE(signedArea(oval) == Approx(kPi / 2.0).epsilon(0.005));
}

TEST_CASE("openingOutline adds the offset and the sill", "[core][shapes]") {
    Opening o;
    o.offset = 3.0;
    o.width = 0.9;
    o.height = 2.1;
    o.sill = 0.5;
    o.shape = "rectangle";
    const std::vector<Vec2> outline = openingOutline(o);
    REQUIRE(outline.size() == 4);
    REQUIRE(outline[0].x == Approx(3.0 - 0.45));
    REQUIRE(outline[0].y == Approx(0.5));
    REQUIRE(outline[2].x == Approx(3.0 + 0.45));
    REQUIRE(outline[2].y == Approx(2.6));
}

TEST_CASE("openingOutline is empty for an unknown shape or a size that does not fit", "[core][shapes]") {
    Opening o;
    o.width = 1.0;
    o.height = 0.4;
    o.shape = "round_arch";
    REQUIRE(openingOutline(o).empty());
    o.shape = "no_such_shape";
    o.height = 2.0;
    REQUIRE(openingOutline(o).empty());
}

TEST_CASE("registering shapes", "[core][shapes]") {
    SECTION("a second registration of rectangle is refused") {
        REQUIRE_FALSE(registerOpeningShape(std::make_unique<DummyShape>("rectangle")));
        REQUIRE(findOpeningShape("rectangle")->builtIn());
    }
    SECTION("a new id is accepted once") {
        REQUIRE(registerOpeningShape(std::make_unique<DummyShape>("t_dummy_one")));
        REQUIRE(findOpeningShape("t_dummy_one") != nullptr);
        REQUIRE_FALSE(registerOpeningShape(std::make_unique<DummyShape>("t_dummy_one")));
        bool found = false;
        for (const OpeningShape* s : allOpeningShapes()) found = found || s->id() == "t_dummy_one";
        REQUIRE(found);
    }
    SECTION("malformed ids are refused") {
        REQUIRE_FALSE(registerOpeningShape(std::make_unique<DummyShape>("Upper")));
        REQUIRE_FALSE(registerOpeningShape(std::make_unique<DummyShape>("1abc")));
        REQUIRE_FALSE(registerOpeningShape(std::make_unique<DummyShape>("")));
        REQUIRE_FALSE(registerOpeningShape(std::make_unique<DummyShape>("has-dash")));
        REQUIRE_FALSE(registerOpeningShape(nullptr));
    }
    SECTION("the list is sorted by key then id") {
        const auto all = allOpeningShapes();
        for (std::size_t i = 1; i < all.size(); ++i) {
            const bool ordered = all[i - 1]->sortKey() < all[i]->sortKey()
                || (all[i - 1]->sortKey() == all[i]->sortKey() && all[i - 1]->id() < all[i]->id());
            REQUIRE(ordered);
        }
    }
}
