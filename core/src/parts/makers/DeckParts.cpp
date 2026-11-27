#include "plynth/core/BuildParts.h"
#include "plynth/core/Collision.h"
#include "plynth/core/Decks.h"
#include "plynth/core/Storeys.h"
#include "plynth/core/WallGeometry.h"

#include "ClipperInclude.h"
#include "PlanRingClean.h"
#include "RingClean.h"

#include <algorithm>
#include <cmath>

namespace plynth {

namespace {

constexpr double kTurnTolerance = 1e-12; // how near zero a cross product counts as a straight line

// Which side of the line from p to q the point r is on: above zero to the left.
double sideOf(Vec2 p, Vec2 q, Vec2 r)
{
    return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
}

// Says whether r lies on the segment from p to q, given that the three are on one line.
bool onSegment(Vec2 p, Vec2 q, Vec2 r)
{
    return r.x >= std::min(p.x, q.x) - 1e-12 && r.x <= std::max(p.x, q.x) + 1e-12 && r.y >= std::min(p.y, q.y) - 1e-12 &&
           r.y <= std::max(p.y, q.y) + 1e-12;
}

// Says whether the segments from a to b and from c to d cross or touch.
bool segmentsMeet(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const double s1 = sideOf(a, b, c);
    const double s2 = sideOf(a, b, d);
    const double s3 = sideOf(c, d, a);
    const double s4 = sideOf(c, d, b);
    if (((s1 > kTurnTolerance && s2 < -kTurnTolerance) || (s1 < -kTurnTolerance && s2 > kTurnTolerance)) &&
        ((s3 > kTurnTolerance && s4 < -kTurnTolerance) || (s3 < -kTurnTolerance && s4 > kTurnTolerance))) {
        return true;
    }
    if (std::fabs(s1) <= kTurnTolerance && onSegment(a, b, c)) return true;
    if (std::fabs(s2) <= kTurnTolerance && onSegment(a, b, d)) return true;
    if (std::fabs(s3) <= kTurnTolerance && onSegment(c, d, a)) return true;
    if (std::fabs(s4) <= kTurnTolerance && onSegment(c, d, b)) return true;
    return false;
}

// Says whether two edges that share a point fold back over each other there.
bool foldsBack(Vec2 into, Vec2 corner, Vec2 out)
{
    const Vec2 a{corner.x - into.x, corner.y - into.y};
    const Vec2 b{out.x - corner.x, out.y - corner.y};
    return std::fabs(a.x * b.y - a.y * b.x) <= kTurnTolerance && a.x * b.x + a.y * b.y < 0.0;
}

// Says whether an outline is a simple polygon with an area: no edge crosses or touches another.
bool simpleOutline(const std::vector<Vec2>& points)
{
    const std::vector<Vec2> ring = detail::withoutRepeats(points);
    const std::size_t n = ring.size();
    if (n < 3 || std::fabs(signedArea(ring)) <= 1e-9) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % n];
        if (foldsBack(a, b, ring[(i + 2) % n])) return false;
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (segmentsMeet(a, b, ring[j], ring[(j + 1) % n])) return false;
        }
    }
    return true;
}

// A ring as a Clipper path, turned left.
Clipper2Lib::PathD leftPath(const std::vector<Vec2>& ring)
{
    Clipper2Lib::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    if (ringAreaOf(ring) < 0.0) std::reverse(path.begin(), path.end());
    return path;
}

// The pieces of paths as tidied rings.
std::vector<RingPiece> piecesOf(const Clipper2Lib::PathsD& paths)
{
    std::vector<RingPiece> out;
    for (const RingPiece& read : piecesByTurning(paths)) {
        for (RingPiece& piece : tidyPiece(read)) out.push_back(std::move(piece));
    }
    return out;
}

// A piece as paths: its outline, then its holes.
Clipper2Lib::PathsD pathsOf(const RingPiece& piece)
{
    Clipper2Lib::PathsD out;
    out.push_back(leftPath(piece.outline));
    for (const std::vector<Vec2>& hole : piece.holes) {
        Clipper2Lib::PathD path = leftPath(hole);
        std::reverse(path.begin(), path.end());
        out.push_back(std::move(path));
    }
    return out;
}

// The deck's outline without the floor's wall bands, so it stops at the outer wall faces.
std::vector<RingPiece> cutByWalls(const Floor& floor, const std::vector<Vec2>& outline)
{
    Clipper2Lib::PathsD bands;
    for (const Wall& wall : floor.walls) {
        for (const std::vector<Vec2>& ring : wallBand(floor, wall)) {
            if (ring.size() >= 3) bands.push_back(leftPath(ring));
        }
    }
    return piecesOf(clipAndTidy(Clipper2Lib::ClipType::Difference, {leftPath(outline)}, bands));
}

// The share of the outline's area that lies inside the floor's footprint.
double insideShare(const Floor& floor, const std::vector<Vec2>& outline)
{
    const double whole = std::fabs(signedArea(outline));
    if (whole <= 0.0) return 0.0;
    Clipper2Lib::PathsD footprint;
    for (const PlanArea& area : floorFootprint(floor)) {
        footprint.push_back(leftPath(area.outline));
        for (const std::vector<Vec2>& hole : area.holes) {
            Clipper2Lib::PathD path = leftPath(hole);
            std::reverse(path.begin(), path.end());
            footprint.push_back(std::move(path));
        }
    }
    if (footprint.empty()) return 0.0;
    const Clipper2Lib::PathsD common = Clipper2Lib::Intersect(Clipper2Lib::PathsD{leftPath(outline)}, footprint,
                                                              Clipper2Lib::FillRule::NonZero, kRingDigits);
    return std::fabs(Clipper2Lib::Area(common)) / whole;
}

// A plan ring in world X and Y, keeping its turning direction.
std::vector<Vec2> worldRing(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    for (auto it = ring.rbegin(); it != ring.rend(); ++it) out.push_back(planToWorld(*it));
    return out;
}

// A plan piece as a world polygon.
WorldPolygon worldPolygon(const RingPiece& piece)
{
    WorldPolygon polygon;
    polygon.outline = worldRing(piece.outline);
    for (const std::vector<Vec2>& hole : piece.holes) polygon.holes.push_back(worldRing(hole));
    return polygon;
}

// A problem of a deck found before the build starts.
BuildProblem deckProblem(const Floor& floor, const Deck& deck, std::string code, BuildSeverity severity, std::string message)
{
    return {std::move(code), severity, std::move(message), floor.id, deck.id, std::string(kCheckStep)};
}

// Makes one part per deck: the floor boards or slab, the skirt under it and the collision.
class DeckMaker final : public PartMaker {
public:
    std::string_view id() const override { return "decks"; }
    int order() const override { return 20; }
    std::vector<std::string> kinds() const override { return {"deck"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The decks of one floor.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.floor || !input.project) return out;
        const Floor& floor = *input.floor;
        const double level = input.levels && input.floorIndex < input.levels->size() ? (*input.levels)[input.floorIndex] : 0.0;
        for (const Deck& deck : floor.decks) {
            std::vector<Vec2> outline;
            for (const PlanXY& p : deck.outline) outline.push_back({p.x, p.y});
            if (!simpleOutline(outline)) {
                out.problems.push_back(deckProblem(floor, deck, "deck_outline_invalid", BuildSeverity::Error,
                                                   "Deck " + deck.id + " has an outline that crosses itself or has no area, so it is left out."));
                continue;
            }
            const std::vector<RingPiece> pieces = cutByWalls(floor, outline);
            if (pieces.empty()) {
                out.problems.push_back(deckProblem(floor, deck, "deck_outline_invalid", BuildSeverity::Error,
                                                   "Deck " + deck.id + " lies wholly on walls, so it is left out."));
                continue;
            }
            if (insideShare(floor, outline) > 0.5) {
                out.problems.push_back(deckProblem(floor, deck, "deck_inside_building", BuildSeverity::Warning,
                                                   "Deck " + deck.id + " lies mostly inside the walls of " + floor.name + "."));
            }
            out.parts.push_back(deckPart(*input.project, floor, deck, pieces, level));
        }
        return out;
    }

private:
    // The part of one deck from the pieces of its outline left outside the walls.
    static BuildPart deckPart(const Project& project, const Floor& floor, const Deck& deck,
                              const std::vector<RingPiece>& pieces, double level)
    {
        BuildPart part;
        part.object = floor.id + "_" + deck.id + "_Deck";
        part.kind = "deck";
        part.id = deck.id;
        const std::string slot = partSlot(project, Surface::Deck, deck.material).slot;
        const double top = level + deck.top;
        const double bottom = top - deck.thickness;
        const bool skirt = deck.skirt && deck.top - deck.thickness > 1e-6;
        for (const RingPiece& piece : pieces) {
            const WorldPolygon polygon = worldPolygon(piece);
            part.prisms.push_back({polygon.outline, polygon.holes, bottom, top, slot, std::string()});
            const double from = skirt ? std::min(level, bottom) : bottom;
            for (std::vector<Vec2>& convex : convexPieces(polygon)) part.collision.push_back({std::move(convex), from, top});
        }
        if (skirt) {
            const std::string skirtSlot = partSlot(project, Surface::Deck, deck.skirtMaterial).slot;
            for (const RingPiece& piece : pieces) {
                for (const RingPiece& inner : piecesOf(offsetByTurning(pathsOf(piece), -kSkirtInset))) {
                    const WorldPolygon polygon = worldPolygon(inner);
                    part.prisms.push_back({polygon.outline, polygon.holes, level, bottom, skirtSlot, std::string()});
                }
            }
        }
        return part;
    }
};

const PartMakerRegistrar registrar(std::make_unique<DeckMaker>());

}

}
