#include "FakeDetector.h"
#include "RoomFixtures.h"
#include "TestSupport.h"

#include <plynth/core/Rooms.h>
#include <plynth/core/commands/RoomCommands.h>
#include <plynth/core/detection/Detector.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// Gives the proposals as a floor: points p1.. and walls w1.. in proposal order, for the chosen walls only.
Floor floorFrom(const DetectionResult& r, const std::vector<std::size_t>& wallIndexes)
{
    Floor f;
    f.id = "f1";
    for (std::size_t i = 0; i < r.points.size(); ++i) addPoint(f, "p" + std::to_string(i + 1), r.points[i].x, r.points[i].y);
    for (std::size_t i : wallIndexes) {
        addWall(f, "w" + std::to_string(i + 1), "p" + std::to_string(r.walls[i].from + 1),
                "p" + std::to_string(r.walls[i].to + 1), r.walls[i].thickness);
    }
    return f;
}

// A blank white image the fake detector accepts.
GrayImage whiteImage()
{
    GrayImage img;
    img.width = 10;
    img.height = 10;
    img.pixels.assign(100, 255);
    return img;
}

// The fixed proposals with rooms filled in.
DetectionResult fixedWithRooms()
{
    DetectionResult r = fixedProposals();
    proposeRooms(r);
    return r;
}

}

TEST_CASE("checkDetectionParams", "[core][detection]") {
    DetectionParams p;
    REQUIRE(checkDetectionParams(p).ok());

    const auto refused = [](DetectionParams q) {
        const Status s = checkDetectionParams(q);
        INFO(s.message);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE_FALSE(s.message.empty());
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    SECTION("numbers must be finite") {
        p.minThickness = nan;
        refused(p);
        p = {};
        p.maxThickness = inf;
        refused(p);
        p = {};
        p.sensitivity = nan;
        refused(p);
        p = {};
        p.minLength = inf;
        refused(p);
        p = {};
        p.maxOpeningWidth = nan;
        refused(p);
    }
    SECTION("thicknesses and length must be above zero") {
        p.minThickness = 0.0;
        refused(p);
        p = {};
        p.maxThickness = 0.0;
        refused(p);
        p = {};
        p.minLength = 0.0;
        refused(p);
        p = {};
        p.minLength = -1.0;
        refused(p);
    }
    SECTION("the widest opening may be zero but not below") {
        p.maxOpeningWidth = 0.0;
        REQUIRE(checkDetectionParams(p).ok());
        p.maxOpeningWidth = -0.1;
        refused(p);
    }
    SECTION("thinnest must be below thickest") {
        p.minThickness = 0.5;
        p.maxThickness = 0.5;
        refused(p);
        p.minThickness = 0.6;
        refused(p);
    }
    SECTION("sensitivity from 0 to 1") {
        p.sensitivity = 0.0;
        REQUIRE(checkDetectionParams(p).ok());
        p.sensitivity = 1.0;
        REQUIRE(checkDetectionParams(p).ok());
        p.sensitivity = -0.01;
        refused(p);
        p.sensitivity = 1.01;
        refused(p);
    }
}

TEST_CASE("checkDetectionResult", "[core][detection]") {
    DetectionResult r = fixedWithRooms();
    REQUIRE(checkDetectionResult(r).ok());
    REQUIRE(checkDetectionResult(DetectionResult{}).ok());

    const auto refused = [](const DetectionResult& q) {
        const Status s = checkDetectionResult(q);
        INFO(s.message);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE_FALSE(s.message.empty());
    };

    SECTION("points must be finite") {
        r.points[2].x = std::numeric_limits<double>::quiet_NaN();
        refused(r);
    }
    SECTION("points must be 1 mm apart") {
        r.points.push_back({r.points[0].x + 0.0005, r.points[0].y});
        refused(r);
    }
    SECTION("wall indexes in range") {
        r.walls[1].to = 99;
        refused(r);
    }
    SECTION("walls join two different points") {
        r.walls[1].to = r.walls[1].from;
        refused(r);
    }
    SECTION("thickness above zero") {
        r.walls[0].thickness = 0.0;
        refused(r);
        r.walls[0].thickness = std::numeric_limits<double>::infinity();
        refused(r);
    }
    SECTION("one wall between two points") {
        r.walls.push_back({r.walls[0].to, r.walls[0].from, 0.2, ProposalState::New});
        refused(r);
    }
    SECTION("walls cross only at shared points") {
        DetectionResult x;
        x.points = {{0, 0}, {2, 2}, {0, 2}, {2, 0}};
        x.walls = {{0, 1, 0.2, ProposalState::New}, {2, 3, 0.2, ProposalState::New}};
        refused(x);
        x.points.push_back({1, 1});
        x.walls = {{0, 4, 0.2, ProposalState::New}, {4, 1, 0.2, ProposalState::New}, {2, 4, 0.2, ProposalState::New},
                   {4, 3, 0.2, ProposalState::New}};
        REQUIRE(checkDetectionResult(x).ok());
    }
    SECTION("a point on a wall must be one of its ends") {
        DetectionResult t;
        t.points = {{0, 0}, {4, 0}, {2, 0}, {2, 3}};
        t.walls = {{0, 1, 0.2, ProposalState::New}, {2, 3, 0.2, ProposalState::New}};
        refused(t);
        t.walls = {{0, 2, 0.2, ProposalState::New}, {2, 1, 0.2, ProposalState::New}, {2, 3, 0.2, ProposalState::New}};
        REQUIRE(checkDetectionResult(t).ok());
    }
    SECTION("openings") {
        r.openings[0].wall = 50;
        refused(r);
        r = fixedWithRooms();
        r.openings[0].width = 0.0;
        refused(r);
        r = fixedWithRooms();
        r.openings[0].offset = -1.0;
        refused(r);
        r = fixedWithRooms();
        r.openings[0].offset = 9.0;
        refused(r);
    }
    SECTION("rooms") {
        r.rooms[0].walls.push_back(77);
        refused(r);
    }
}

TEST_CASE("proposeRooms", "[core][detection]") {
    DetectionResult r = fixedProposals();
    r.rooms.push_back({});
    proposeRooms(r);
    REQUIRE(r.rooms.size() == 2);
    for (const ProposedRoom& room : r.rooms) {
        REQUIRE(room.centreLineArea == Approx(12.0));
        REQUIRE(room.walls.size() == 4);
        REQUIRE(room.state == ProposalState::New);
        REQUIRE(room.rings.size() == 1);
    }
    std::set<std::size_t> all;
    for (const ProposedRoom& room : r.rooms) all.insert(room.walls.begin(), room.walls.end());
    REQUIRE(all == std::set<std::size_t>{0, 1, 2, 3, 4, 5, 6});
    const bool leftFirst = r.rooms[0].inside.x < 3.0;
    const ProposedRoom& left = leftFirst ? r.rooms[0] : r.rooms[1];
    const ProposedRoom& right = leftFirst ? r.rooms[1] : r.rooms[0];
    REQUIRE(left.inside.x < 3.0);
    REQUIRE(right.inside.x > 3.0);
    REQUIRE(std::set<std::size_t>(left.walls.begin(), left.walls.end()) == std::set<std::size_t>{0, 4, 5, 6});
    REQUIRE(std::set<std::size_t>(right.walls.begin(), right.walls.end()) == std::set<std::size_t>{1, 2, 3, 6});

    SECTION("a cancelled result gets no rooms") {
        r.cancelled = true;
        proposeRooms(r);
        REQUIRE(r.rooms.empty());
    }
    SECTION("no walls, no rooms") {
        r.walls.clear();
        proposeRooms(r);
        REQUIRE(r.rooms.empty());
    }
}

TEST_CASE("markExisting", "[core][detection]") {
    DetectionResult r = fixedWithRooms();
    const auto states = [&]() {
        std::vector<ProposalState> s;
        for (const ProposedWall& w : r.walls) s.push_back(w.state);
        return s;
    };

    SECTION("everything is New on an empty floor") {
        Floor f;
        f.id = "f1";
        markExisting(f, r);
        for (const ProposedWall& w : r.walls) REQUIRE(w.state == ProposalState::New);
        for (const ProposedRoom& room : r.rooms) REQUIRE(room.state == ProposalState::New);
    }
    SECTION("Existing once the same walls are drawn") {
        const Floor f = floorFrom(r, {0, 1, 2, 3, 4, 5, 6});
        markExisting(f, r);
        for (const ProposedWall& w : r.walls) REQUIRE(w.state == ProposalState::Existing);
    }
    SECTION("Existing when the floor has the wall in two pieces and a little shifted") {
        Floor f;
        f.id = "f1";
        addPoint(f, "a", 0.01, 0.02);
        addPoint(f, "b", 1.7, 0.0);
        addPoint(f, "c", 3.02, -0.01);
        addWall(f, "w1", "a", "b", 0.3);
        addWall(f, "w2", "b", "c", 0.12);
        markExisting(f, r);
        REQUIRE(r.walls[0].state == ProposalState::Existing);
        REQUIRE(r.walls[1].state == ProposalState::New);
    }
    SECTION("Overlaps for a wall half along a drawn wall") {
        Floor f;
        f.id = "f1";
        addPoint(f, "a", 0, 0);
        addPoint(f, "b", 1.5, 0);
        addWall(f, "w1", "a", "b", 0.3);
        markExisting(f, r);
        REQUIRE(r.walls[0].state == ProposalState::Overlaps);
        REQUIRE(r.walls[5].state == ProposalState::New);
    }
    SECTION("a perpendicular wall touching a drawn wall stays New") {
        const Floor f = floorFrom(r, {0});
        markExisting(f, r);
        REQUIRE(r.walls[0].state == ProposalState::Existing);
        REQUIRE(r.walls[5].state == ProposalState::New);
        REQUIRE(r.walls[6].state == ProposalState::New);
    }
    SECTION("a wall that continues a drawn one past its end stays New") {
        const Floor f = floorFrom(r, {0});
        markExisting(f, r);
        REQUIRE(r.walls[1].state == ProposalState::New);
    }
    SECTION("a wall at another angle is not covered") {
        Floor f;
        f.id = "f1";
        addPoint(f, "a", 0, 0);
        addPoint(f, "b", 3, 0.6);
        addWall(f, "w1", "a", "b", 0.3);
        markExisting(f, r);
        REQUIRE(r.walls[0].state == ProposalState::New);
    }
    SECTION("curved existing walls are ignored") {
        Floor f = floorFrom(r, {0});
        f.walls[0].bulge = 0.2;
        markExisting(f, r);
        REQUIRE(r.walls[0].state == ProposalState::New);
    }
    SECTION("states are reset on every run") {
        const Floor all = floorFrom(r, {0, 1, 2, 3, 4, 5, 6});
        markExisting(all, r);
        Floor empty;
        empty.id = "f1";
        markExisting(empty, r);
        for (ProposalState s : states()) REQUIRE(s == ProposalState::New);
    }
    SECTION("rooms are Existing after AddRoomCommand") {
        Project p = makeNewProject();
        p.plan.floors[0] = floorFrom(r, {0, 1, 2, 3, 4, 5, 6});
        AddRoomCommand add("f1", r.rooms[0].inside);
        requireOk(add.apply(p));
        markExisting(p.plan.floors[0], r);
        REQUIRE(r.rooms[0].state == ProposalState::Existing);
        REQUIRE(r.rooms[1].state == ProposalState::New);
    }
    SECTION("an opening is New on an empty floor and Existing when an opening of the floor covers its centre") {
        Floor empty;
        empty.id = "f1";
        markExisting(empty, r);
        REQUIRE(r.openings[0].state == ProposalState::New);

        Floor f = floorFrom(r, {0});
        Opening o;
        o.id = "o1";
        o.wall = "w1";
        o.offset = 1.45;
        o.width = 0.9;
        f.openings = {o};
        markExisting(f, r);
        REQUIRE(r.openings[0].state == ProposalState::Existing);

        // A neighbour that does not cover the centre leaves it New.
        f.openings[0].offset = 0.5;
        markExisting(f, r);
        REQUIRE(r.openings[0].state == ProposalState::New);

        // A covering opening on a wall far away leaves it New.
        f = floorFrom(r, {0, 4});
        f.openings = {o};
        f.openings[0].wall = "w5";
        markExisting(f, r);
        REQUIRE(r.openings[0].state == ProposalState::New);
    }
    SECTION("an opening on a wall with a tiny bulge counts as on a straight wall") {
        Floor f = floorFrom(r, {0});
        f.walls[0].bulge = 0.0004;
        Opening o;
        o.id = "o1";
        o.wall = "w1";
        o.offset = 1.5;
        o.width = 0.9;
        f.openings = {o};
        markExisting(f, r);
        REQUIRE(r.openings[0].state == ProposalState::Existing);
    }
    SECTION("states of openings are reset on every run") {
        Floor f = floorFrom(r, {0});
        Opening o;
        o.id = "o1";
        o.wall = "w1";
        o.offset = 1.5;
        o.width = 0.9;
        f.openings = {o};
        markExisting(f, r);
        REQUIRE(r.openings[0].state == ProposalState::Existing);
        Floor empty;
        empty.id = "f1";
        markExisting(empty, r);
        REQUIRE(r.openings[0].state == ProposalState::New);
    }
    SECTION("a short wall is checked at its middle") {
        DetectionResult s;
        s.points = {{0, 0}, {0.08, 0}};
        s.walls = {{0, 1, 0.1, ProposalState::New}};
        Floor f = floorFrom(s, {0});
        markExisting(f, s);
        REQUIRE(s.walls[0].state == ProposalState::Existing);
    }
}

TEST_CASE("countProposals", "[core][detection]") {
    DetectionResult r = fixedWithRooms();
    r.walls[0].state = ProposalState::Existing;
    r.walls[1].state = ProposalState::Overlaps;
    r.walls[2].state = ProposalState::Overlaps;
    r.rooms[1].state = ProposalState::Existing;
    const ProposalCounts c = countProposals(r);
    REQUIRE(c.points == 6);
    REQUIRE(c.walls == 7);
    REQUIRE(c.newWalls == 4);
    REQUIRE(c.existingWalls == 1);
    REQUIRE(c.overlappingWalls == 2);
    REQUIRE(c.rooms == 2);
    REQUIRE(c.newRooms == 1);
    REQUIRE(c.openings == 1);
    REQUIRE(c.newOpenings == 1);
    REQUIRE(c.existingOpenings == 0);
    r.openings.push_back(r.openings[0]);
    r.openings[0].state = ProposalState::Existing;
    const ProposalCounts again = countProposals(r);
    REQUIRE(again.openings == 2);
    REQUIRE(again.newOpenings == 1);
    REQUIRE(again.existingOpenings == 1);
    REQUIRE(countProposals(DetectionResult{}) == ProposalCounts{});
}

TEST_CASE("detectionSource", "[core][detection]") {
    Plan plan = makeEmptyPlan();
    plan.scale.pixelsPerMetre = 80.0;
    plan.floors[0].image = "plans/floor1.png";
    const std::filesystem::path folder = std::filesystem::path("some") / "project";

    SECTION("a relative image is found in the project folder") {
        const DetectionSource s = detectionSource(plan, folder, "f1");
        requireOk(s.status);
        REQUIRE(s.imageFile == folder / "plans" / "floor1.png");
        REQUIRE(s.pixelsPerMetre == Approx(80.0));
    }
    SECTION("an absolute image stays as it is") {
        const std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path("elsewhere") / "x.png");
        plan.floors[0].image = absolute.generic_string();
        const DetectionSource s = detectionSource(plan, {}, "f1");
        requireOk(s.status);
        REQUIRE(s.imageFile == absolute);
    }
    SECTION("an unknown floor is not found") {
        const DetectionSource s = detectionSource(plan, folder, "f9");
        REQUIRE(s.status.code == ErrorCode::NotFound);
    }
    SECTION("a floor without an image is not found") {
        plan.floors[0].image.reset();
        const DetectionSource s = detectionSource(plan, folder, "f1");
        REQUIRE(s.status.code == ErrorCode::NotFound);
        REQUIRE_FALSE(s.status.message.empty());
    }
    SECTION("no scale is an invalid value") {
        plan.scale.pixelsPerMetre.reset();
        const DetectionSource s = detectionSource(plan, folder, "f1");
        REQUIRE(s.status.code == ErrorCode::InvalidValue);
        REQUIRE(s.status.message.find("Set the scale first") == 0);
    }
}

TEST_CASE("detector registry", "[core][detection][registry]") {
    SECTION("the fake detector is found by id") {
        const Detector* d = findDetector(kFakeDetectorId);
        REQUIRE(d);
        REQUIRE(d->id() == kFakeDetectorId);
        REQUIRE_FALSE(d->name().empty());
        REQUIRE_FALSE(d->description().empty());
        REQUIRE(findDetector("nothing-here") == nullptr);
    }
    SECTION("a second registration with the same id is refused") {
        const Detector* before = findDetector(kFakeDetectorId);
        struct Same : Detector {
            std::string_view id() const override { return kFakeDetectorId; }
            std::string_view name() const override { return "x"; }
            std::string_view description() const override { return "x"; }
            DetectionResult detect(const GrayImage&, double, const DetectionParams&, std::stop_token) const override { return {}; }
        };
        REQUIRE_FALSE(registerDetector(std::make_unique<Same>()));
        REQUIRE(findDetector(kFakeDetectorId) == before);
    }
    SECTION("badly formed ids and null are refused") {
        struct Named : Detector {
            explicit Named(std::string_view n) : n_(n) {}
            std::string_view id() const override { return n_; }
            std::string_view name() const override { return "x"; }
            std::string_view description() const override { return "x"; }
            DetectionResult detect(const GrayImage&, double, const DetectionParams&, std::stop_token) const override { return {}; }
            std::string_view n_;
        };
        REQUIRE_FALSE(registerDetector(nullptr));
        REQUIRE_FALSE(registerDetector(std::make_unique<Named>("")));
        REQUIRE_FALSE(registerDetector(std::make_unique<Named>("Upper")));
        REQUIRE_FALSE(registerDetector(std::make_unique<Named>("has space")));
        REQUIRE_FALSE(registerDetector(std::make_unique<Named>("1leading")));
    }
    SECTION("allDetectors is sorted by id") {
        const auto all = allDetectors();
        REQUIRE(all.size() >= 3);
        for (std::size_t i = 1; i < all.size(); ++i) REQUIRE(all[i - 1]->id() < all[i]->id());
        const auto has = [&](std::string_view id) {
            return std::any_of(all.begin(), all.end(), [&](const Detector* d) { return d->id() == id; });
        };
        REQUIRE(has(kFakeDetectorId));
        REQUIRE(has(kFakeSecondDetectorId));
    }
}

TEST_CASE("runDetection", "[core][detection]") {
    const GrayImage image = whiteImage();
    const DetectionParams params;

    SECTION("gives the detector's result, with rooms, and fills the bookkeeping") {
        const DetectionResult r = runDetection(kFakeDetectorId, image, 100.0, params);
        requireOk(r.status);
        REQUIRE_FALSE(r.cancelled);
        REQUIRE(r.detector == kFakeDetectorId);
        REQUIRE(r.pixelsPerMetre == Approx(100.0));
        REQUIRE(r.params == params);
        REQUIRE(r.imageWidth == 10);
        REQUIRE(r.imageHeight == 10);
        REQUIRE(r.points.size() == 6);
        REQUIRE(r.walls.size() == 7);
        REQUIRE(r.openings.size() == 1);
        REQUIRE(r.rooms.size() == 2);
        REQUIRE(r.elapsedMs >= 0.0);
        REQUIRE(r.elapsedMs < 5000.0);
        REQUIRE(checkDetectionResult(r).ok());
    }
    SECTION("an unknown detector lists the known ids") {
        const DetectionResult r = runDetection("nope", image, 100.0, params);
        REQUIRE(r.status.code == ErrorCode::InvalidValue);
        REQUIRE(r.status.message.find("nope") != std::string::npos);
        REQUIRE(r.status.message.find(std::string(kFakeDetectorId)) != std::string::npos);
        REQUIRE(r.status.message.find(std::string(kFakeSecondDetectorId)) != std::string::npos);
        REQUIRE(r.walls.empty());
    }
    SECTION("bad params are refused") {
        DetectionParams bad;
        bad.sensitivity = 2.0;
        const DetectionResult r = runDetection(kFakeDetectorId, image, 100.0, bad);
        REQUIRE(r.status.code == ErrorCode::InvalidValue);
        REQUIRE(r.walls.empty());
    }
    SECTION("a bad scale is refused") {
        REQUIRE(runDetection(kFakeDetectorId, image, 0.0, params).status.code == ErrorCode::InvalidValue);
        REQUIRE(runDetection(kFakeDetectorId, image, std::numeric_limits<double>::quiet_NaN(), params).status.code
                == ErrorCode::InvalidValue);
    }
    SECTION("an empty image is an image error") {
        REQUIRE(runDetection(kFakeDetectorId, GrayImage{}, 100.0, params).status.code == ErrorCode::ImageError);
        GrayImage short_;
        short_.width = 5;
        short_.height = 5;
        short_.pixels.assign(3, 0);
        REQUIRE(runDetection(kFakeDetectorId, short_, 100.0, params).status.code == ErrorCode::ImageError);
    }
    SECTION("a stop asked before the start gives cancelled with an ok status") {
        std::stop_source source;
        source.request_stop();
        const DetectionResult r = runDetection(kFakeDetectorId, image, 100.0, params, source.get_token());
        requireOk(r.status);
        REQUIRE(r.cancelled);
        REQUIRE(r.points.empty());
        REQUIRE(r.walls.empty());
        REQUIRE(r.rooms.empty());
    }
    SECTION("openings false empties the openings") {
        DetectionParams q;
        q.openings = false;
        const DetectionResult r = runDetection(kFakeDetectorId, image, 100.0, q);
        requireOk(r.status);
        REQUIRE(r.openings.empty());
        REQUIRE(r.walls.size() == 7);
        REQUIRE(r.rooms.size() == 2);
    }
    SECTION("rooms false leaves the rooms empty") {
        DetectionParams q;
        q.rooms = false;
        const DetectionResult r = runDetection(kFakeDetectorId, image, 100.0, q);
        requireOk(r.status);
        REQUIRE(r.rooms.empty());
        REQUIRE(r.openings.size() == 1);
    }
    SECTION("a detector that breaks the rules is refused") {
        const DetectionResult r = runDetection(kFakeBrokenDetectorId, image, 100.0, params);
        REQUIRE(r.status.code == ErrorCode::InvalidValue);
        REQUIRE(r.status.message.find(std::string(kFakeBrokenDetectorId)) != std::string::npos);
        REQUIRE(r.walls.empty());
    }
}
