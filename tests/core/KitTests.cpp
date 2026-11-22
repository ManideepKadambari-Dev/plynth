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

    SECTION("the piece frames put the origin at the first station and X along the wall") {
        const KitPlacement* first = nullptr;
        for (const KitPlacement& at : kit.placements) {
            if (at.wall == "w1" && !first) first = &at;
        }
        REQUIRE(first);
        REQUIRE(first->location.x == Approx(0.1));
        REQUIRE(first->location.y == Approx(0.0).margin(1e-9));
        REQUIRE(degrees(*first) == Approx(0.0).margin(1e-9));
        const KitPiece* piece = pieceById(kit, first->piece);
        REQUIRE(piece);
        REQUIRE(piece->length == Approx(0.4));
        double minX = 1e9;
        for (const Vec2& pt : piece->solids[0].outline.points) minX = std::min(minX, pt.x);
        REQUIRE(minX == Approx(0.0).margin(1e-9));
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
    for (const KitPlacement* at : onWall) starts.push_back(at->location.x);
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
    REQUIRE(kit.floors.size() == 2);
    REQUIRE(kit.floors[1].elevation == Approx(3.0));
    REQUIRE(kit.pieces.size() == 3);
    REQUIRE(kit.placements.size() == 72);
    for (const KitPlacement& at : kit.placements) REQUIRE(at.location.z == Approx(at.floor == "f1" ? 0.0 : 3.0));
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
