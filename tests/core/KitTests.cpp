#include "BuildFixtures.h"
#include "SchemaSupport.h"
#include "SquareFixtures.h"

#include <plynth/core/Build.h>
#include <plynth/core/BuildJob.h>
#include <plynth/core/Collision.h>
#include <plynth/core/Kit.h>
#include <plynth/core/Openings.h>
#include <plynth/core/commands/DetectionCommands.h>
#include <plynth/core/detection/Detector.h>
#include <plynth/core/detection/GrayImage.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <regex>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
using Catch::Approx;

namespace {

// A square room of four 0.2 m walls with room r1 (a living room) inside, on the given grid.
Project squareRoom(double grid = 0.5)
{
    Project p = squareProjectWithWalls();
    Room r;
    r.id = "r1";
    r.type = "living";
    r.walls = {"w1", "w2", "w3", "w4"};
    p.plan.floors[0].rooms.push_back(r);
    p.settings.gridSize = grid;
    clearMaterialDefaults(p);
    return p;
}

// Adds a second square room to the right of the first, apart from it, with its own points and walls.
void addSecondRoom(Project& p, double shift, const std::string& type)
{
    Floor& f = p.plan.floors[0];
    const double xs[4] = {shift, shift + 4.0, shift + 4.0, shift};
    const double ys[4] = {0.0, 0.0, 4.0, 4.0};
    for (int i = 0; i < 4; ++i) {
        Point pt;
        pt.id = "q" + std::to_string(i + 1);
        pt.x = xs[i];
        pt.y = ys[i];
        f.points.push_back(pt);
    }
    Room r;
    r.id = "r2";
    r.type = type;
    for (int i = 0; i < 4; ++i) {
        Wall w;
        w.id = "v" + std::to_string(i + 1);
        w.from = "q" + std::to_string(i + 1);
        w.to = "q" + std::to_string((i + 1) % 4 + 1);
        w.thickness = 0.2;
        f.walls.push_back(w);
        r.walls.push_back(w.id);
    }
    f.rooms.push_back(r);
}

BuildPlan planOf(const Project& p)
{
    return makeBuildPlan(p, BuildAssetSource{}, sampleMaterialSource());
}

ModularKit kitOf(const Project& p)
{
    return modularKit(p, planOf(p));
}

const KitPiece* pieceById(const ModularKit& kit, const std::string& id)
{
    for (const KitPiece& p : kit.pieces) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::vector<const KitPiece*> piecesOfKind(const ModularKit& kit, KitPieceKind kind)
{
    std::vector<const KitPiece*> out;
    for (const KitPiece& p : kit.pieces) {
        if (p.kind == kind) out.push_back(&p);
    }
    return out;
}

std::vector<const KitPlacement*> placementsOnWall(const ModularKit& kit, const std::string& wall)
{
    std::vector<const KitPlacement*> out;
    for (const KitPlacement& p : kit.placements) {
        if (p.wall == wall) out.push_back(&p);
    }
    return out;
}

double degrees(const KitPlacement& p)
{
    return p.rotationZ * 180.0 / std::numbers::pi;
}

Clipper2Lib::PathD pathOf(const std::vector<Vec2>& ring, bool anticlockwise)
{
    std::vector<Vec2> r = ring;
    if ((signedArea(r) > 0.0) != anticlockwise) std::reverse(r.begin(), r.end());
    Clipper2Lib::PathD path;
    for (const Vec2& p : r) path.emplace_back(p.x, p.y);
    return path;
}

Clipper2Lib::PathsD pathsOfSolid(const WallSolid& s)
{
    Clipper2Lib::PathsD paths{pathOf(s.outline.points, true)};
    for (const WallRing& h : s.holes) paths.push_back(pathOf(h.points, false));
    return paths;
}

// A piece's solids placed in the world as the placement says.
Clipper2Lib::PathsD placed(const KitPiece& piece, const KitPlacement& at)
{
    const double c = std::cos(at.rotationZ);
    const double s = std::sin(at.rotationZ);
    const auto world = [&](const std::vector<Vec2>& ring) {
        std::vector<Vec2> out;
        for (const Vec2& p : ring) out.push_back({at.location.x + c * p.x - s * p.y, at.location.y + s * p.x + c * p.y});
        return out;
    };
    Clipper2Lib::PathsD paths;
    for (const KitSolid& solid : piece.solids) {
        paths.push_back(pathOf(world(solid.outline.points), true));
        for (const KitRing& h : solid.holes) paths.push_back(pathOf(world(h.points), false));
    }
    return paths;
}

struct Cover {
    double solids = 0.0;
    double merged = 0.0;
    double summed = 0.0;
};

// The area of the floor's wall solids, of the union of every placed piece, and of every placed piece added up.
Cover coverOf(const FloorBuild& build, const ModularKit& kit, const std::string& floorId)
{
    Cover cover;
    Clipper2Lib::PathsD solids;
    for (const WallSolid& s : build.wallSolids) {
        const Clipper2Lib::PathsD paths = pathsOfSolid(s);
        solids.insert(solids.end(), paths.begin(), paths.end());
    }
    cover.solids = Clipper2Lib::Area(Clipper2Lib::Union(solids, Clipper2Lib::FillRule::NonZero, 8));

    Clipper2Lib::PathsD all;
    for (const KitPlacement& at : kit.placements) {
        if (at.floor != floorId) continue;
        const KitPiece* piece = pieceById(kit, at.piece);
        REQUIRE(piece);
        const Clipper2Lib::PathsD paths = placed(*piece, at);
        cover.summed += Clipper2Lib::Area(Clipper2Lib::Union(paths, Clipper2Lib::FillRule::NonZero, 8));
        all.insert(all.end(), paths.begin(), paths.end());
    }
    cover.merged = Clipper2Lib::Area(Clipper2Lib::Union(all, Clipper2Lib::FillRule::NonZero, 8));
    return cover;
}

void requireCovers(const BuildPlan& plan, const ModularKit& kit, double tolerance = 1e-6)
{
    for (const FloorBuild& build : plan.floors) {
        if (build.wallSolids.empty()) continue;
        const Cover cover = coverOf(build, kit, build.id);
        REQUIRE(cover.solids > 0.0);
        REQUIRE(std::fabs(cover.merged - cover.solids) < tolerance);
        REQUIRE(std::fabs(cover.summed - cover.solids) < tolerance);
    }
}

// The lowest world X of a placed piece.
double lowestX(const ModularKit& kit, const KitPlacement& at)
{
    double low = 1e9;
    for (const Clipper2Lib::PathD& path : placed(*pieceById(kit, at.piece), at)) {
        for (const Clipper2Lib::PointD& pt : path) low = std::min(low, pt.x);
    }
    return low;
}

int cutEdges(const KitPiece& piece)
{
    int n = 0;
    for (const KitSolid& s : piece.solids) {
        for (const KitEdge& e : s.outline.edges) n += e.cut ? 1 : 0;
    }
    return n;
}

}

TEST_CASE("the square room is cut into one corner and two wall pieces", "[kit]") {
    const Project p = squareRoom();
    const BuildPlan plan = planOf(p);
    const ModularKit kit = modularKit(p, plan);

    REQUIRE(kit.floors.size() == 1);
    REQUIRE(kit.floors[0].floor == "f1");
    REQUIRE(kit.floors[0].gridSize == Approx(0.5));

    REQUIRE(kit.pieces.size() == 3);
    const auto corners = piecesOfKind(kit, KitPieceKind::Corner);
    const auto walls = piecesOfKind(kit, KitPieceKind::Wall);
    REQUIRE(corners.size() == 1);
    REQUIRE(walls.size() == 2);
    REQUIRE(corners[0]->count == 4);
    REQUIRE(corners[0]->length == Approx(0.2));
    REQUIRE(corners[0]->thickness == Approx(0.2));
    REQUIRE(corners[0]->height == Approx(3.0));

    std::vector<double> lengths{walls[0]->length, walls[1]->length};
    std::sort(lengths.begin(), lengths.end());
    REQUIRE(lengths[0] == Approx(0.4));
    REQUIRE(lengths[1] == Approx(0.5));
    for (const KitPiece* w : walls) {
        REQUIRE(w->thickness == Approx(0.2));
        REQUIRE(w->count == (w->length < 0.45 ? 8 : 24));
    }

    REQUIRE(kit.placements.size() == 36);
    std::vector<double> turns;
    for (const KitPlacement& at : kit.placements) {
        if (!at.point.empty()) {
            REQUIRE(at.wall.empty());
            turns.push_back(std::round(degrees(at)));
        } else {
            REQUIRE_FALSE(at.wall.empty());
        }
        REQUIRE(at.location.z == Approx(0.0));
    }
    std::sort(turns.begin(), turns.end());
    REQUIRE(turns == std::vector<double>{0.0, 90.0, 180.0, 270.0});

    SECTION("the placed pieces cover the wall solids exactly") {
        requireCovers(plan, kit);
    }

    SECTION("cut faces are marked, outside faces keep the exterior slot and inside faces the room's") {
        for (const KitPiece& piece : kit.pieces) {
            REQUIRE(piece.solids.size() == 1);
            const KitSolid& solid = piece.solids[0];
            REQUIRE(solid.outline.points.size() == 4);
            for (const KitEdge& e : solid.outline.edges) {
                if (e.cut) {
                    REQUIRE_FALSE(e.edge.side.has_value());
                    REQUIRE(e.edge.room.empty());
                    REQUIRE(e.slot == "Wall_Top");
                } else {
                    REQUIRE(e.edge.side.has_value());
                    REQUIRE((e.slot == "Exterior_Wall" || e.slot == "Living_Wall"));
                    REQUIRE((e.slot == "Living_Wall") == (e.edge.room == "r1"));
                }
            }
            REQUIRE(cutEdges(piece) == 2);
            if (piece.kind != KitPieceKind::Corner) {
                int outside = 0;
                int inside = 0;
                for (const KitEdge& e : solid.outline.edges) {
                    if (e.slot == "Exterior_Wall") ++outside;
                    if (e.slot == "Living_Wall") ++inside;
                }
                REQUIRE(outside == 1);
                REQUIRE(inside == 1);
            }
        }
    }

    SECTION("a wall piece sits on its wall with its X along it, whichever way round its frame is") {
        for (const KitPlacement* at : placementsOnWall(kit, "w1")) {
            REQUIRE(at->location.y == Approx(0.0).margin(1e-9));
            const double turn = std::round(degrees(*at));
            REQUIRE((turn == 0.0 || turn == 180.0));
            const KitPiece* piece = pieceById(kit, at->piece);
            REQUIRE(piece);
            REQUIRE(piece->solids[0].outline.points.size() == 4);
        }
        REQUIRE(lowestX(kit, *placementsOnWall(kit, "w1")[0]) == Approx(0.1));
    }
}

TEST_CASE("ids follow the schema pattern, repeat between runs and sort", "[kit]") {
    const Project p = squareRoom();
    const ModularKit a = kitOf(p);
    const ModularKit b = kitOf(p);
    REQUIRE(a == b);

    const std::regex pattern("^(Wall|Corner|Opening|Curved)_[0-9]+x[0-9]+x[0-9]+_[0-9a-f]{6}$");
    std::set<std::string> ids;
    for (const KitPiece& piece : a.pieces) {
        REQUIRE(std::regex_match(piece.id, pattern));
        REQUIRE(piece.mesh == "SM_Kit_" + piece.id);
        ids.insert(piece.id);
        const int count = static_cast<int>(
            std::count_if(a.placements.begin(), a.placements.end(), [&](const KitPlacement& at) { return at.piece == piece.id; }));
        REQUIRE(piece.count == count);
        REQUIRE(piece.count >= 1);
    }
    REQUIRE(ids.size() == a.pieces.size());
    REQUIRE(std::is_sorted(a.pieces.begin(), a.pieces.end(), [](const KitPiece& x, const KitPiece& y) { return x.id < y.id; }));
    REQUIRE(a.pieces[0].id.rfind("Corner_20x300x20_", 0) == 0);
}

TEST_CASE("a door gives one opening piece with its fill and collision", "[kit]") {
    Project p = squareRoom();
    Opening door = doorOn("w1", 2.0);
    door.id = "o1";
    p.plan.floors[0].openings.push_back(door);
    const BuildPlan plan = planOf(p);
    REQUIRE(plan.floors[0].openings.size() == 1);
    const ModularKit kit = modularKit(p, plan);

    const auto openings = piecesOfKind(kit, KitPieceKind::Opening);
    REQUIRE(openings.size() == 1);
    const KitPiece& piece = *openings[0];
    REQUIRE(piece.count == 1);
    REQUIRE(piece.length == Approx(1.0));
    REQUIRE(piece.openings.size() == 1);
    REQUIRE(piece.openings[0].fill);
    REQUIRE(piece.openings[0].opening.id == "o1");
    REQUIRE(piece.openings[0].opening.threshold.empty());
    REQUIRE(piece.openings[0].opening.centre.x == Approx(0.5));
    REQUIRE(piece.openings[0].opening.cutter.right.size() == piece.openings[0].opening.cutter.left.size());

    const auto onWall = placementsOnWall(kit, "w1");
    REQUIRE(onWall.size() == 7);
    std::vector<double> starts;
    for (const KitPlacement* at : onWall) starts.push_back(lowestX(kit, *at));
    std::sort(starts.begin(), starts.end());
    const std::vector<double> expected{0.1, 0.5, 1.0, 1.5, 2.5, 3.0, 3.5};
    REQUIRE(starts.size() == expected.size());
    for (std::size_t i = 0; i < starts.size(); ++i) REQUIRE(starts[i] == Approx(expected[i]));

    SECTION("nothing below the door head stands in the doorway") {
        bool above = false;
        REQUIRE_FALSE(piece.collision.empty());
        for (const CollisionPiece& c : piece.collision) {
            double minX = 1e9;
            double maxX = -1e9;
            for (const Vec2& pt : c.outline) {
                minX = std::min(minX, pt.x);
                maxX = std::max(maxX, pt.x);
            }
            if (c.bottom < 2.1 - 1e-9) {
                REQUIRE((maxX <= 0.05 + 1e-6 || minX >= 0.95 - 1e-6));
            } else {
                above = true;
                REQUIRE(c.bottom == Approx(2.1));
                REQUIRE(c.top == Approx(3.0));
            }
        }
        REQUIRE(above);
    }

    SECTION("the pieces still cover the walls exactly") {
        requireCovers(plan, kit);
    }
}

TEST_CASE("a window gives one opening piece and keeps its wall closed in collision", "[kit]") {
    Project p = squareRoom();
    Opening window;
    window.id = "o1";
    window.wall = "w1";
    window.offset = 1.0;
    window.type = OpeningType::Window;
    window.width = 1.2;
    window.height = 1.2;
    window.sill = 0.9;
    p.plan.floors[0].openings.push_back(window);
    const BuildPlan plan = planOf(p);
    const ModularKit kit = modularKit(p, plan);

    int openings = 0;
    for (const KitPlacement* at : placementsOnWall(kit, "w1")) {
        const KitPiece* piece = pieceById(kit, at->piece);
        REQUIRE(piece);
        if (piece->kind != KitPieceKind::Opening) continue;
        ++openings;
        REQUIRE(piece->openings.size() == 1);
        REQUIRE(piece->openings[0].fill);
        REQUIRE(piece->openings[0].opening.type == OpeningType::Window);
        REQUIRE(piece->length == Approx(1.9));
        REQUIRE(piece->collision.size() == 1);
        REQUIRE(piece->collision[0].bottom == Approx(0.0));
        REQUIRE(piece->collision[0].top == Approx(3.0));
    }
    REQUIRE(openings == 1);
    requireCovers(plan, kit);
}

TEST_CASE("an opening beside a sharp corner is held by every piece its cut touches", "[kit]") {
    Project p = makeNewProject();
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 2.0, 3.4641016, 0.0}};
    const char* ends[3][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p1"}};
    for (int i = 0; i < 3; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.3;
        f.walls.push_back(w);
    }
    Opening window;
    window.id = "o1";
    window.wall = "w1";
    window.type = OpeningType::Window;
    window.width = 0.6;
    window.height = 1.0;
    window.sill = 0.9;
    for (window.offset = 0.1; window.offset < 2.0; window.offset += 0.005) {
        if (checkOpeningFit(f, window).ok()) break;
    }
    f.openings.push_back(window);
    p.plan.floors[0] = f;
    clearMaterialDefaults(p);
    const BuildPlan plan = planOf(p);
    REQUIRE(plan.floors[0].openings.size() == 1);
    const ModularKit kit = modularKit(p, plan);

    int holders = 0;
    int fills = 0;
    int cornerHolders = 0;
    for (const KitPlacement& at : kit.placements) {
        const KitPiece* piece = pieceById(kit, at.piece);
        REQUIRE(piece);
        for (const KitOpening& o : piece->openings) {
            ++holders;
            if (o.fill) ++fills;
            if (piece->kind == KitPieceKind::Corner) {
                ++cornerHolders;
                REQUIRE_FALSE(o.fill);
            }
        }
    }
    REQUIRE(holders >= 2);
    REQUIRE(cornerHolders >= 1);
    REQUIRE(fills == 1);
    requireCovers(plan, kit, 1e-5);
}

TEST_CASE("a curved wall is one curved piece", "[kit]") {
    Project p = makeNewProject();
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}};
    Wall w;
    w.id = "w1";
    w.from = "p1";
    w.to = "p2";
    w.thickness = 0.2;
    w.bulge = 0.8;
    f.walls = {w};
    p.plan.floors[0] = f;
    clearMaterialDefaults(p);
    const BuildPlan plan = planOf(p);
    const ModularKit kit = modularKit(p, plan);

    REQUIRE(kit.pieces.size() == 1);
    REQUIRE(kit.pieces[0].kind == KitPieceKind::Curved);
    REQUIRE(kit.pieces[0].id.rfind("Curved_", 0) == 0);
    REQUIRE(kit.placements.size() == 1);
    REQUIRE(kit.placements[0].wall == "w1");
    REQUIRE(kit.pieces[0].length > 3.9);
    requireCovers(plan, kit);
}

TEST_CASE("a T junction is one corner piece with three cut faces and a cross one with four", "[kit]") {
    const auto build = [](bool cross) {
        Project p = makeNewProject();
        Floor f;
        f.id = "f1";
        std::vector<std::pair<std::string, std::pair<double, double>>> pts = {
            {"p0", {0.0, 0.0}}, {"p1", {-2.0, 0.0}}, {"p2", {2.0, 0.0}}, {"p3", {0.0, 2.0}}};
        if (cross) pts.push_back({"p4", {0.0, -2.0}});
        for (const auto& [id, at] : pts) f.points.push_back({id, at.first, at.second, 0.0});
        int n = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            Wall w;
            w.id = "w" + std::to_string(++n);
            w.from = pts[i].first;
            w.to = "p0";
            w.thickness = 0.2;
            f.walls.push_back(w);
        }
        p.plan.floors[0] = f;
        clearMaterialDefaults(p);
        return p;
    };

    for (const bool cross : {false, true}) {
        const Project p = build(cross);
        const BuildPlan plan = planOf(p);
        const ModularKit kit = modularKit(p, plan);
        const auto corners = piecesOfKind(kit, KitPieceKind::Corner);
        REQUIRE(corners.size() == 1);
        REQUIRE(corners[0]->count == 1);
        REQUIRE(cutEdges(*corners[0]) == (cross ? 4 : 3));
        int atCentre = 0;
        for (const KitPlacement& at : kit.placements) {
            if (at.point == "p0") {
                ++atCentre;
                REQUIRE(at.location.x == Approx(0.0).margin(1e-9));
                REQUIRE(at.location.y == Approx(0.0).margin(1e-9));
            }
        }
        REQUIRE(atCentre == 1);
        requireCovers(plan, kit);
    }
}

TEST_CASE("a wall drawn the other way gives the same pieces", "[kit]") {
    const Project plain = squareRoom();
    Project turned = plain;
    for (Wall& w : turned.plan.floors[0].walls) {
        if (w.id == "w3") std::swap(w.from, w.to);
    }
    const BuildPlan plan = planOf(turned);
    const ModularKit a = kitOf(plain);
    const ModularKit b = modularKit(turned, plan);
    REQUIRE(a.pieces.size() == b.pieces.size());
    for (std::size_t i = 0; i < a.pieces.size(); ++i) {
        REQUIRE(a.pieces[i].id == b.pieces[i].id);
        REQUIRE(a.pieces[i].count == b.pieces[i].count);
    }
    REQUIRE(a.placements.size() == b.placements.size());
    requireCovers(plan, b);
}

TEST_CASE("two equal rooms reuse every piece", "[kit]") {
    const Project one = squareRoom();
    Project two = one;
    addSecondRoom(two, 6.0, "living");
    const ModularKit a = kitOf(one);
    const BuildPlan plan = planOf(two);
    const ModularKit b = modularKit(two, plan);

    REQUIRE(b.pieces.size() == a.pieces.size());
    for (std::size_t i = 0; i < a.pieces.size(); ++i) {
        REQUIRE(a.pieces[i].id == b.pieces[i].id);
        REQUIRE(b.pieces[i].count == 2 * a.pieces[i].count);
    }
    REQUIRE(b.placements.size() == 72);
    requireCovers(plan, b);
}

TEST_CASE("changing one wall's inside material gives new pieces for that room only", "[kit]") {
    Project before = squareRoom();
    addSecondRoom(before, 6.0, "bedroom");
    Project after = before;
    for (Wall& w : after.plan.floors[0].walls) {
        if (w.id == "w1") w.materialInside = "white_paint";
    }
    const ModularKit a = kitOf(before);
    const ModularKit b = kitOf(after);

    const auto idsOn = [](const ModularKit& kit, const std::string& wall) {
        std::set<std::string> ids;
        for (const KitPlacement* at : placementsOnWall(kit, wall)) ids.insert(at->piece);
        return ids;
    };
    REQUIRE(idsOn(a, "w1") != idsOn(b, "w1"));
    for (const char* wall : {"v1", "v2", "v3", "v4"}) REQUIRE(idsOn(a, wall) == idsOn(b, wall));
    REQUIRE(a.placements.size() == b.placements.size());
}

TEST_CASE("a half wall meeting full walls gives pieces of both heights", "[kit]") {
    Project p = squareRoom();
    for (Wall& w : p.plan.floors[0].walls) {
        if (w.id == "w1") w.height = 1.0;
    }
    const BuildPlan plan = planOf(p);
    const ModularKit kit = modularKit(p, plan);
    std::set<long long> heights;
    for (const KitPiece& piece : kit.pieces) heights.insert(std::llround(piece.height * 100.0));
    REQUIRE(heights.count(100) == 1);
    REQUIRE(heights.count(300) == 1);
    for (const KitPlacement* at : placementsOnWall(kit, "w1")) {
        REQUIRE(pieceById(kit, at->piece)->height == Approx(1.0));
    }
    requireCovers(plan, kit);
}

TEST_CASE("every collision piece is convex and inside its piece", "[kit]") {
    Project p = squareRoom();
    Opening door = doorOn("w1", 2.0);
    door.id = "o1";
    Opening window;
    window.id = "o2";
    window.wall = "w2";
    window.offset = 1.5;
    window.type = OpeningType::Window;
    window.width = 1.2;
    window.height = 1.2;
    window.sill = 0.9;
    p.plan.floors[0].openings = {door, window};
    const ModularKit kit = kitOf(p);
    REQUIRE_FALSE(kit.pieces.empty());
    for (const KitPiece& piece : kit.pieces) {
        REQUIRE_FALSE(piece.collision.empty());
        double minX = 1e9;
        double maxX = -1e9;
        double minY = 1e9;
        double maxY = -1e9;
        for (const KitSolid& s : piece.solids) {
            for (const Vec2& pt : s.outline.points) {
                minX = std::min(minX, pt.x);
                maxX = std::max(maxX, pt.x);
                minY = std::min(minY, pt.y);
                maxY = std::max(maxY, pt.y);
            }
        }
        for (const CollisionPiece& c : piece.collision) {
            REQUIRE(isConvexRing(c.outline));
            REQUIRE(c.top > c.bottom);
            REQUIRE(c.bottom >= -1e-9);
            REQUIRE(c.top <= piece.height + 1e-9);
            for (const Vec2& pt : c.outline) {
                REQUIRE(pt.x >= minX - 1e-6);
                REQUIRE(pt.x <= maxX + 1e-6);
                REQUIRE(pt.y >= minY - 1e-6);
                REQUIRE(pt.y <= maxY + 1e-6);
            }
        }
    }
}

TEST_CASE("the kit grid is the project grid and a bigger grid gives longer pieces", "[kit]") {
    Project p = squareRoom(0.5);
    REQUIRE(kitGridSize(p, p.plan.floors[0]) == Approx(0.5));
    p.settings.gridSize = 1.0;
    REQUIRE(kitGridSize(p, p.plan.floors[0]) == Approx(1.0));

    const ModularKit fine = kitOf(squareRoom(0.5));
    const Project coarseProject = squareRoom(1.0);
    const BuildPlan plan = planOf(coarseProject);
    const ModularKit coarse = modularKit(coarseProject, plan);
    REQUIRE(coarse.floors[0].gridSize == Approx(1.0));
    REQUIRE(coarse.placements.size() == 20);
    REQUIRE(coarse.placements.size() < fine.placements.size());
    double longest = 0.0;
    for (const KitPiece& piece : coarse.pieces) longest = std::max(longest, piece.length);
    REQUIRE(longest == Approx(1.0));
    requireCovers(plan, coarse);
}

TEST_CASE("the layout validates and keeps rotations in range", "[kit][schema]") {
    Project p = squareRoom();
    Opening door = doorOn("w1", 2.0);
    door.id = "o1";
    p.plan.floors[0].openings.push_back(door);
    const ModularKit kit = kitOf(p);
    const Json layout = kitLayoutToJson(kit, "Flat", "Flat_Kit.fbx");
    REQUIRE(schemaProblem("kit-layout.schema.json", layout).empty());
    REQUIRE(layout["format"] == "plynth-kit-layout");
    REQUIRE(layout["floors"].size() == 1);
    REQUIRE(layout["pieces"].size() == kit.pieces.size());
    REQUIRE(layout["placements"].size() == kit.placements.size());
    for (const Json& at : layout["placements"]) {
        const double r = at["rotation_z_degrees"].get<double>();
        REQUIRE(r >= 0.0);
        REQUIRE(r < 360.0);
        REQUIRE((at["wall"].is_null() != at["point"].is_null()));
    }
    REQUIRE(layout["pieces"][0]["count"].get<int>() >= 1);
}

TEST_CASE("a plan without walls gives an empty kit", "[kit]") {
    const Project p = makeNewProject();
    const ModularKit kit = kitOf(p);
    REQUIRE(kit.pieces.empty());
    REQUIRE(kit.placements.empty());
    REQUIRE(kit.floors.empty());
    const Json layout = kitLayoutToJson(kit, "Flat", "Flat_Kit.fbx");
    REQUIRE(schemaProblem("kit-layout.schema.json", layout).empty());
}

TEST_CASE("two floors share pieces and sit at their own levels", "[kit]") {
    Project p = squareRoom();
    Floor upper = p.plan.floors[0];
    upper.id = "f2";
    for (Point& pt : upper.points) pt.id = "u" + pt.id;
    for (Wall& w : upper.walls) {
        w.id = "u" + w.id;
        w.from = "u" + w.from;
        w.to = "u" + w.to;
    }
    for (Room& r : upper.rooms) {
        r.id = "u" + r.id;
        for (Id& w : r.walls) w = "u" + w;
    }
    p.plan.floors.push_back(upper);
    const BuildPlan plan = planOf(p);
    const ModularKit kit = modularKit(p, plan);
    const double level = p.plan.floors[0].ceilingHeight + p.plan.floors[0].slabThickness;
    REQUIRE(kit.floors.size() == 2);
    REQUIRE(kit.floors[1].elevation == Approx(level));
    REQUIRE(kit.pieces.size() == 3);
    REQUIRE(kit.placements.size() == 72);
    for (const KitPlacement& at : kit.placements) REQUIRE(at.location.z == Approx(at.floor == "f1" ? 0.0 : level));
}

TEST_CASE("the kit of the sample apartment covers its walls", "[kit][sample]") {
    const std::filesystem::path plans = std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans";
    const GrayImageLoad load = loadGrayImage(plans / "apartment-01.png");
    REQUIRE(load.status.ok());
    const DetectionResult found = runDetection("orthogonal", *load.image, 100.0, {});
    Project p = makeNewProject();
    ApplyDetectionCommand apply("f1", found);
    REQUIRE(apply.apply(p).ok());
    clearMaterialDefaults(p);

    const auto t0 = std::chrono::steady_clock::now();
    const BuildPlan plan = planOf(p);
    const auto t1 = std::chrono::steady_clock::now();
    const ModularKit kit = modularKit(p, plan);
    const auto t2 = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
    WARN("apartment-01 kit: " << ms(t1, t2) << " ms (build plan " << ms(t0, t1) << " ms), " << kit.pieces.size() << " pieces, "
                              << kit.placements.size() << " placements, " << p.plan.floors[0].walls.size() << " walls, "
                              << p.plan.floors[0].openings.size() << " openings");

    REQUIRE_FALSE(kit.pieces.empty());
    REQUIRE(kit.placements.size() > kit.pieces.size());
    // Pieces that match within a millimetre share one mesh, so the cover is exact only to that.
    requireCovers(plan, kit, 1e-3);
    REQUIRE(schemaProblem("kit-layout.schema.json", kitLayoutToJson(kit, "Apartment", "Apartment_Kit.fbx")).empty());
    for (const KitPiece& piece : kit.pieces) {
        for (const CollisionPiece& c : piece.collision) REQUIRE(isConvexRing(c.outline));
    }
}

namespace {

// Whether two kits hold the same pieces by id with the same local geometry, to a tenth of a micrometre.
void requireSamePieces(const ModularKit& a, const ModularKit& b)
{
    REQUIRE(a.pieces.size() == b.pieces.size());
    for (std::size_t i = 0; i < a.pieces.size(); ++i) {
        REQUIRE(a.pieces[i].id == b.pieces[i].id);
        REQUIRE(a.pieces[i].solids.size() == b.pieces[i].solids.size());
        for (std::size_t s = 0; s < a.pieces[i].solids.size(); ++s) {
            const KitRing& x = a.pieces[i].solids[s].outline;
            const KitRing& y = b.pieces[i].solids[s].outline;
            REQUIRE(x.points.size() == y.points.size());
            for (std::size_t k = 0; k < x.points.size(); ++k) {
                REQUIRE(std::fabs(x.points[k].x - y.points[k].x) < 1e-7);
                REQUIRE(std::fabs(x.points[k].y - y.points[k].y) < 1e-7);
                REQUIRE(x.edges[k].slot == y.edges[k].slot);
                REQUIRE(x.edges[k].cut == y.edges[k].cut);
            }
        }
    }
}

}

TEST_CASE("piece ids and geometry do not depend on which wall is found first", "[kit][stable]") {
    Project plain = squareRoom();
    Opening door = doorOn("w2", 2.0);
    door.id = "o1";
    plain.plan.floors[0].openings.push_back(door);
    const ModularKit base = kitOf(plain);

    SECTION("reversing the first wall") {
        Project p = plain;
        std::swap(p.plan.floors[0].walls[0].from, p.plan.floors[0].walls[0].to);
        requireSamePieces(base, kitOf(p));
    }
    SECTION("reversing every wall") {
        Project p = plain;
        for (Wall& w : p.plan.floors[0].walls) std::swap(w.from, w.to);
        // The same door seen from the other end hangs on the other jamb and swings to the other side.
        for (Opening& o : p.plan.floors[0].openings) {
            o.offset = 4.0 - o.offset;
            o.hinge = o.hinge == HingeEnd::Start ? HingeEnd::End : HingeEnd::Start;
            o.swing = o.swing == WallSide::Right ? WallSide::Left : WallSide::Right;
        }
        requireSamePieces(base, kitOf(p));
    }
    SECTION("reordering the walls") {
        Project p = plain;
        std::reverse(p.plan.floors[0].walls.begin(), p.plan.floors[0].walls.end());
        requireSamePieces(base, kitOf(p));
        p = plain;
        std::rotate(p.plan.floors[0].walls.begin(), p.plan.floors[0].walls.begin() + 2, p.plan.floors[0].walls.end());
        requireSamePieces(base, kitOf(p));
    }
    SECTION("removing the first of two equal rooms") {
        Project two = squareRoom();
        addSecondRoom(two, 6.0, "living");
        Floor& f = two.plan.floors[0];
        f.rooms.erase(f.rooms.begin());
        f.walls.erase(f.walls.begin(), f.walls.begin() + 4);
        f.points.erase(f.points.begin(), f.points.begin() + 4);
        Project single = squareRoom();
        addSecondRoom(single, 6.0, "living");
        Floor& g = single.plan.floors[0];
        g.rooms.pop_back();
        g.walls.erase(g.walls.begin() + 4, g.walls.end());
        g.points.erase(g.points.begin() + 4, g.points.end());
        requireSamePieces(kitOf(single), kitOf(two));
    }
}

TEST_CASE("piece ids do not change with float noise from where a thin room stands", "[kit][stable]") {
    const auto room = [](double dx, double dy) {
        Project p = squareRoom();
        for (Wall& w : p.plan.floors[0].walls) w.thickness = 0.125;
        for (Point& pt : p.plan.floors[0].points) {
            pt.x += dx;
            pt.y += dy;
        }
        return p;
    };
    const ModularKit base = kitOf(room(0.0, 0.0));
    REQUIRE_FALSE(base.pieces.empty());
    for (const double dx : {0.1, 0.3, 1.7, 3.3, 12.35}) {
        for (const double dy : {0.0, 0.7, 5.1}) requireSamePieces(base, kitOf(room(dx, dy)));
    }
}

TEST_CASE("piece names do not change as a 0.125 m room moves over the plan", "[kit][stable]") {
    const auto room = [](double dx, double dy) {
        Project p = squareRoom();
        Floor& f = p.plan.floors[0];
        for (Wall& w : f.walls) w.thickness = 0.125;
        std::rotate(f.walls.begin(), f.walls.begin() + 1, f.walls.end());
        for (Point& pt : f.points) {
            pt.x += dx;
            pt.y += dy;
        }
        return p;
    };
    const ModularKit base = kitOf(room(2.05, 2.05));
    REQUIRE_FALSE(base.pieces.empty());
    int moved = 0;
    for (int k = 0; k <= 2000; ++k) {
        const double shift = 0.05 * k;
        const ModularKit kit = kitOf(room(2.05 + shift, 2.05 + shift));
        if (kit.pieces.size() != base.pieces.size()) {
            FAIL("piece count changed at shift " << shift);
        }
        for (std::size_t i = 0; i < kit.pieces.size(); ++i) {
            if (kit.pieces[i].id != base.pieces[i].id) FAIL("piece " << i << " renamed at shift " << shift << ": " << base.pieces[i].id << " became " << kit.pieces[i].id);
        }
        ++moved;
    }
    REQUIRE(moved == 2001);
}

namespace {

// A triangle of 0.3 m walls with a door on w1 close to its sharp corner, so the corner piece holds the door.
Project triangleWithDoor(HingeEnd hinge)
{
    Project p = makeNewProject();
    Floor f;
    f.id = "f1";
    f.points = {{"p1", 0.0, 0.0, 0.0}, {"p2", 4.0, 0.0, 0.0}, {"p3", 2.0, 3.4641016, 0.0}};
    const char* ends[3][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p1"}};
    for (int i = 0; i < 3; ++i) {
        Wall w;
        w.id = "w" + std::to_string(i + 1);
        w.from = ends[i][0];
        w.to = ends[i][1];
        w.thickness = 0.3;
        f.walls.push_back(w);
    }
    Opening door = doorOn("w1", 0.5);
    door.id = "o1";
    door.width = 0.6;
    for (door.offset = 0.1; door.offset < 2.0; door.offset += 0.005) {
        if (checkOpeningFit(f, door).ok()) break;
    }
    door.hinge = hinge;
    f.openings.push_back(door);
    p.plan.floors[0] = f;
    clearMaterialDefaults(p);
    return p;
}

}

TEST_CASE("a reversed wall at a corner piece keeps its name, and opposite hinges differ", "[kit][stable]") {
    const Project plain = triangleWithDoor(HingeEnd::Start);
    const ModularKit base = kitOf(plain);
    int cornerHolders = 0;
    for (const KitPiece& piece : base.pieces) {
        if (piece.kind == KitPieceKind::Corner && !piece.openings.empty()) ++cornerHolders;
    }
    REQUIRE(cornerHolders >= 1);

    Project turned = plain;
    Floor& f = turned.plan.floors[0];
    std::swap(f.walls[0].from, f.walls[0].to);
    Opening& o = f.openings[0];
    o.offset = 4.0 - o.offset;
    o.hinge = o.hinge == HingeEnd::Start ? HingeEnd::End : HingeEnd::Start;
    o.swing = o.swing == WallSide::Right ? WallSide::Left : WallSide::Right;
    requireSamePieces(base, kitOf(turned));

    const ModularKit other = kitOf(triangleWithDoor(HingeEnd::End));
    std::set<std::string> a;
    std::set<std::string> b;
    for (const KitPiece& piece : base.pieces) a.insert(piece.id);
    for (const KitPiece& piece : other.pieces) b.insert(piece.id);
    REQUIRE(a != b);
}

TEST_CASE("a door in a 45 degree corner keeps its piece names as the room moves and its wall turns round", "[kit][stable]") {
    const auto build = [](double dx, double dy, bool reversed) {
        Project p = makeNewProject();
        Floor f;
        f.id = "f1";
        f.points = {{"p1", dx, dy, 0.0}, {"p2", dx + 4.0, dy, 0.0}, {"p3", dx + 4.0, dy + 4.0, 0.0}};
        const char* ends[3][2] = {{"p1", "p2"}, {"p2", "p3"}, {"p3", "p1"}};
        for (int i = 0; i < 3; ++i) {
            Wall w;
            w.id = "w" + std::to_string(i + 1);
            w.from = ends[i][0];
            w.to = ends[i][1];
            w.thickness = 0.3;
            f.walls.push_back(w);
        }
        Opening door = doorOn("w1", 0.5);
        door.id = "o1";
        door.width = 0.6;
        for (door.offset = 0.1; door.offset < 2.0; door.offset += 0.005) {
            if (checkOpeningFit(f, door).ok()) break;
        }
        if (reversed) {
            std::swap(f.walls[0].from, f.walls[0].to);
            door.offset = 4.0 - door.offset;
            door.hinge = door.hinge == HingeEnd::Start ? HingeEnd::End : HingeEnd::Start;
            door.swing = door.swing == WallSide::Right ? WallSide::Left : WallSide::Right;
        }
        f.openings.push_back(door);
        p.plan.floors[0] = f;
        clearMaterialDefaults(p);
        return p;
    };
    const ModularKit base = kitOf(build(0.0, 0.0, false));
    int cornerHolders = 0;
    for (const KitPiece& piece : base.pieces) {
        if (piece.kind == KitPieceKind::Corner && !piece.openings.empty()) ++cornerHolders;
    }
    REQUIRE(cornerHolders >= 1);
    requireSamePieces(base, kitOf(build(0.0, 0.0, true)));
    for (int k = 0; k < 300; ++k) {
        const double shift = 0.37 * k;
        INFO("shift " << shift);
        requireSamePieces(base, kitOf(build(shift, shift * 0.61, k % 2 == 1)));
    }
}

namespace {

// How long the visible faces of the walls are, per slot, as the build's solids have them.
std::map<std::string, double> buildFaceLengths(const BuildPlan& plan)
{
    std::map<std::string, double> out;
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        const FloorMaterials& m = plan.floorMaterials[i];
        const auto add = [&](const WallRing& ring) {
            for (std::size_t k = 0; k < ring.points.size(); ++k) {
                const WallEdge& e = ring.edges[k];
                std::string slot = m.wallTopSlot;
                if (e.side) {
                    slot = "Exterior_Wall";
                    for (const WallFaceMaterial& f : m.wallFaces) {
                        if (f.wall == e.wall && f.room == e.room) slot = f.slot;
                    }
                }
                out[slot] += distance(ring.points[k], ring.points[(k + 1) % ring.points.size()]);
            }
        };
        for (const WallSolid& solid : plan.floors[i].wallSolids) {
            add(solid.outline);
            for (const WallRing& h : solid.holes) add(h);
        }
    }
    return out;
}

// The same from the kit: every placed piece's faces that are not kit cuts.
std::map<std::string, double> kitFaceLengths(const ModularKit& kit)
{
    std::map<std::string, double> out;
    for (const KitPlacement& at : kit.placements) {
        const KitPiece* piece = pieceById(kit, at.piece);
        REQUIRE(piece);
        for (const KitSolid& solid : piece->solids) {
            std::vector<const KitRing*> rings{&solid.outline};
            for (const KitRing& h : solid.holes) rings.push_back(&h);
            for (const KitRing* ring : rings) {
                for (std::size_t k = 0; k < ring->points.size(); ++k) {
                    if (ring->edges[k].cut) continue;
                    out[ring->edges[k].slot] += distance(ring->points[k], ring->points[(k + 1) % ring->points.size()]);
                }
            }
        }
    }
    return out;
}

// Every visible wall face of the build must be in the kit, with its own slot, and nothing but cut faces may be new.
void requireFacesKept(const BuildPlan& plan, const ModularKit& kit, double tolerance = 1e-5)
{
    const std::map<std::string, double> want = buildFaceLengths(plan);
    const std::map<std::string, double> got = kitFaceLengths(kit);
    for (const auto& [slot, length] : want) {
        INFO("slot " << slot);
        REQUIRE(got.count(slot) == 1);
        REQUIRE(std::fabs(got.at(slot) - length) < tolerance);
    }
    for (const auto& [slot, length] : got) {
        INFO("slot " << slot);
        REQUIRE(want.count(slot) == 1);
    }
}

// Walls meeting at a point o: a main wall of 0.3 m through it, and a partition across it (both halves when
// cross), turned by an angle and moved.
Project junctionProject(double angleDegrees, double partition, bool cross, double dx, double dy)
{
    Project p = makeNewProject();
    Floor f;
    f.id = "f1";
    const double a0 = angleDegrees * std::numbers::pi / 180.0;
    f.points.push_back({"o", dx, dy, 0.0});
    const int arms = cross ? 4 : 3;
    for (int k = 0; k < arms; ++k) {
        const double a = a0 + k * std::numbers::pi / 2.0;
        const std::string id = "e" + std::to_string(k);
        f.points.push_back({id, dx + 2.0 * std::cos(a), dy + 2.0 * std::sin(a), 0.0});
        Wall w;
        w.id = "w" + std::to_string(k + 1);
        w.from = id;
        w.to = "o";
        w.thickness = (k == 0 || k == 2) ? 0.3 : partition;
        f.walls.push_back(w);
    }
    p.plan.floors[0] = f;
    clearMaterialDefaults(p);
    return p;
}

// A square room of 4 m, turned about its first corner, with one wall of half height.
Project turnedRoom(double angleDegrees, bool halfWall)
{
    Project p = squareRoom();
    Floor& f = p.plan.floors[0];
    const double a = angleDegrees * std::numbers::pi / 180.0;
    for (Point& pt : f.points) {
        const double x = pt.x;
        const double y = pt.y;
        pt.x = x * std::cos(a) - y * std::sin(a) + 3.0;
        pt.y = x * std::sin(a) + y * std::cos(a) + 3.0;
    }
    if (halfWall) f.walls[0].height = 1.0;
    return p;
}

}

TEST_CASE("T and cross junctions of unequal walls keep every visible face", "[kit][faces]") {
    for (const double angle : {0.0, 30.0, 45.0}) {
        for (const double partition : {0.12, 0.125}) {
            for (const bool cross : {false, true}) {
                INFO("angle " << angle << " partition " << partition << " cross " << cross);
                const Project base = junctionProject(angle, partition, cross, 0.0, 0.0);
                const BuildPlan plan = planOf(base);
                const ModularKit kit = modularKit(base, plan);
                requireFacesKept(plan, kit);
                requireCovers(plan, kit, 1e-5);

                int atCentre = 0;
                for (const KitPlacement& at : kit.placements) {
                    if (at.point != "o") continue;
                    ++atCentre;
                    const KitPiece* piece = pieceById(kit, at.piece);
                    REQUIRE(piece);
                    REQUIRE(piece->kind == KitPieceKind::Corner);
                    REQUIRE(cutEdges(*piece) == (cross ? 4 : 3));
                    for (const KitSolid& s : piece->solids) {
                        for (const KitEdge& e : s.outline.edges) {
                            if (!e.cut) REQUIRE(e.edge.side.has_value());
                        }
                    }
                }
                REQUIRE(atCentre == 1);

                for (const auto& shift : {std::pair{0.37, 0.2257}, std::pair{5.55, 3.1}, std::pair{12.35, 40.7}}) {
                    requireSamePieces(kit, kitOf(junctionProject(angle, partition, cross, shift.first, shift.second)));
                }
            }
        }
    }
}

TEST_CASE("a half wall keeps every visible face, straight and on a diagonal", "[kit][faces]") {
    for (const double angle : {0.0, 30.0, 45.0}) {
        for (const bool half : {false, true}) {
            INFO("angle " << angle << " half " << half);
            const Project p = turnedRoom(angle, half);
            const BuildPlan plan = planOf(p);
            const ModularKit kit = modularKit(p, plan);
            requireFacesKept(plan, kit);
            requireCovers(plan, kit, 1e-5);
        }
    }
}

TEST_CASE("the kit of the sample apartment keeps every visible face", "[kit][faces][sample]") {
    const GrayImageLoad load = loadGrayImage(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-01.png");
    REQUIRE(load.status.ok());
    const DetectionResult found = runDetection("orthogonal", *load.image, 100.0, {});
    Project p = makeNewProject();
    ApplyDetectionCommand apply("f1", found);
    REQUIRE(apply.apply(p).ok());
    clearMaterialDefaults(p);
    const BuildPlan plan = planOf(p);
    // Pieces that match within a millimetre share one mesh, so the lengths agree to a few millimetres in all.
    requireFacesKept(plan, modularKit(p, plan), 0.02);
}

TEST_CASE("the kit leaves slabs, stairs and roofs out and takes the slot names they share with the walls", "[kit][faces]") {
    Project p = squareRoom();
    Floor upper = p.plan.floors[0];
    upper.id = "f2";
    for (Point& pt : upper.points) pt.id = "u" + pt.id;
    for (Wall& w : upper.walls) {
        w.id = "u" + w.id;
        w.from = "u" + w.from;
        w.to = "u" + w.to;
    }
    for (Room& r : upper.rooms) {
        r.id = "u" + r.id;
        for (Id& w : r.walls) w = "u" + w;
    }
    p.plan.floors.push_back(upper);
    Stair stair;
    stair.id = "s1";
    stair.type = "u_shape";
    stair.x = 2.0;
    stair.y = 0.5;
    p.plan.floors[0].stairs.push_back(stair);
    // The walls show tile outside and the slab edge has no material, so the name they share gets the material added.
    for (Floor& f : p.plan.floors) {
        for (Wall& w : f.walls) w.materialOutside = "tile";
    }

    const BuildPlan plan = planOf(p);
    REQUIRE(plan.floors.size() == 2);
    REQUIRE_FALSE(plan.floors[0].slab.polygons.empty());
    REQUIRE_FALSE(plan.floors[0].stairs.empty());
    REQUIRE_FALSE(plan.floors[1].roofs.empty());
    REQUIRE(plan.floorMaterials[0].slab.edgeSlot != "Exterior_Wall");

    const ModularKit kit = modularKit(p, plan);
    requireCovers(plan, kit);
    requireFacesKept(plan, kit);
    std::set<std::string> slots;
    for (const BuildSlot& slot : plan.materialSlots) slots.insert(slot.name);
    int outside = 0;
    for (const KitPiece& piece : kit.pieces) {
        REQUIRE(piece.height == Approx(3.0));
        for (const KitSolid& solid : piece.solids) {
            REQUIRE(slots.count(solid.topSlot) == 1);
            for (const KitEdge& e : solid.outline.edges) {
                if (e.cut) continue;
                INFO("slot " << e.slot);
                REQUIRE(slots.count(e.slot) == 1);
                REQUIRE(e.slot != plan.floorMaterials[0].slab.edgeSlot);
                if (e.slot.rfind("Exterior_Wall_", 0) == 0) ++outside;
            }
        }
    }
    REQUIRE(outside > 0);
}
