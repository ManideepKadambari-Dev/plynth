#include "TestSupport.h"

#include <plynth/core/Geometry.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

using namespace plynth;
using Catch::Approx;

TEST_CASE("scaleFromImagePoints", "[core][geometry]") {
    SECTION("500 pixels over 5 metres is 100") {
        const ScaleMeasure m = scaleFromImagePoints({100, 100}, {600, 100}, 5.0);
        REQUIRE(m.status.ok());
        REQUIRE(m.pixelsPerMetre == Approx(100.0));
        REQUIRE(m.pixelDistance == Approx(500.0));
    }
    SECTION("works on a slope") {
        const ScaleMeasure m = scaleFromImagePoints({0, 0}, {30, 40}, 2.0);
        REQUIRE(m.status.ok());
        REQUIRE(m.pixelsPerMetre == Approx(25.0));
    }
    SECTION("refusals") {
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(scaleFromImagePoints({0, 0}, {0.5, 0}, 5.0).status.code == ErrorCode::InvalidValue);
        REQUIRE(scaleFromImagePoints({0, 0}, {100, 0}, 0.0).status.code == ErrorCode::InvalidValue);
        REQUIRE(scaleFromImagePoints({0, 0}, {100, 0}, -1.0).status.code == ErrorCode::InvalidValue);
        REQUIRE(scaleFromImagePoints({0, 0}, {100, 0}, nan).status.code == ErrorCode::InvalidValue);
        REQUIRE(scaleFromImagePoints({nan, 0}, {100, 0}, 1.0).status.code == ErrorCode::InvalidValue);
        REQUIRE(scaleFromImagePoints({0, 0}, {100, std::numeric_limits<double>::infinity()}, 1.0).status.code
                == ErrorCode::InvalidValue);
    }
}

TEST_CASE("signedArea", "[core][geometry]") {
    const std::vector<Vec2> clockwise{{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    const std::vector<Vec2> anticlockwise{{0, 0}, {0, 2}, {2, 2}, {2, 0}};
    REQUIRE(signedArea(clockwise) == Approx(4.0));
    REQUIRE(signedArea(anticlockwise) == Approx(-4.0));
    REQUIRE(signedArea(std::vector<Vec2>{}) == 0.0);
    REQUIRE(signedArea(std::vector<Vec2>{{1, 1}, {2, 2}}) == Approx(0.0));
}

TEST_CASE("insidePolygon", "[core][geometry]") {
    const std::vector<Vec2> l{{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};
    REQUIRE(insidePolygon({1, 1}, l));
    REQUIRE(insidePolygon({3, 1}, l));
    REQUIRE(insidePolygon({1, 3}, l));
    REQUIRE_FALSE(insidePolygon({3, 3}, l));
    REQUIRE_FALSE(insidePolygon({-1, 1}, l));
    REQUIRE_FALSE(insidePolygon({5, 1}, l));
    REQUIRE_FALSE(insidePolygon({1, 1}, std::vector<Vec2>{}));
}

TEST_CASE("projectOntoSegment", "[core][geometry]") {
    const Vec2 a{0, 0};
    const Vec2 b{4, 0};
    SECTION("inside") {
        const SegmentProjection p = projectOntoSegment({1, 3}, a, b);
        REQUIRE(p.position == Vec2{1, 0});
        REQUIRE(p.offset == Approx(1.0));
        REQUIRE(p.distance == Approx(3.0));
    }
    SECTION("past both ends") {
        const SegmentProjection before = projectOntoSegment({-2, 0}, a, b);
        REQUIRE(before.position == a);
        REQUIRE(before.offset == 0.0);
        REQUIRE(before.distance == Approx(2.0));
        const SegmentProjection after = projectOntoSegment({7, 4}, a, b);
        REQUIRE(after.position == b);
        REQUIRE(after.offset == Approx(4.0));
        REQUIRE(after.distance == Approx(5.0));
    }
    SECTION("zero length") {
        const SegmentProjection p = projectOntoSegment({3, 4}, a, a);
        REQUIRE(p.position == a);
        REQUIRE(p.offset == 0.0);
        REQUIRE(p.distance == Approx(5.0));
    }
}

TEST_CASE("distance, positionOf and wallLength", "[core][geometry]") {
    REQUIRE(distance({0, 0}, {3, 4}) == Approx(5.0));
    Floor f;
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 3.0, 4.0, 0.0}};
    REQUIRE(positionOf(f.points[1]) == Vec2{3.0, 4.0});
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    REQUIRE(wallLength(f, w) == Approx(5.0));
    w.to = "p9";
    REQUIRE(wallLength(f, w) == 0.0);
}
