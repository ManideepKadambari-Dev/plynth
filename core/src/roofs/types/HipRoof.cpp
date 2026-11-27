#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace plynth {

namespace {

constexpr double kStripMargin = 0.01; // metres the outside strips reach past the overhang, so they cover the eaves
constexpr double kLongestStep = 10.0; // the farthest a strip's corner moves out per metre of overhang
constexpr double kPieceSlack = 1e-6;  // square metres the pieces may miss the eaves by, per face

// What the two sets of polygons have in common.
std::vector<WorldPolygon> overlap(const std::vector<WorldPolygon>& a, const std::vector<WorldPolygon>& b)
{
    if (a.empty() || b.empty()) return {};
    return roofparts::without(a, roofparts::without(a, b));
}

// The area of polygons, holes taken off.
double areaOf(const std::vector<WorldPolygon>& polygons)
{
    double total = 0.0;
    for (const WorldPolygon& poly : polygons) {
        total += signedArea(poly.outline);
        for (const std::vector<Vec2>& hole : poly.holes) total += signedArea(hole);
    }
    return total;
}

// The distance from a point to the segment from a to b.
double toSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double size2 = dx * dx + dy * dy;
    const double t = size2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / size2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

// How far a point is from a polygon: 0 inside it, else the distance to its nearest edge.
double distanceTo(Vec2 p, const WorldPolygon& poly)
{
    bool inside = insidePolygon(p, poly.outline);
    for (const std::vector<Vec2>& hole : poly.holes) inside = inside && !insidePolygon(p, hole);
    if (inside) return 0.0;
    double best = std::numeric_limits<double>::max();
    const auto ring = [&](const std::vector<Vec2>& points) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            best = std::min(best, toSegment(p, points[i], points[(i + 1) % points.size()]));
        }
    };
    ring(poly.outline);
    for (const std::vector<Vec2>& hole : poly.holes) ring(hole);
    return best;
}

// The step outward from a corner for each metre the eaves reach past the section, given the inward normals of
// the edges before and after it. It runs along the corner's bisector, so the strips of the two edges at a corner
// share that side.
Vec2 outwardStep(Vec2 before, Vec2 after)
{
    const Vec2 sum{before.x + after.x, before.y + after.y};
    const double c = 1.0 + before.x * after.x + before.y * after.y;
    Vec2 step = c > 1e-9 ? Vec2{-sum.x / c, -sum.y / c} : Vec2{-sum.x, -sum.y};
    const double length = std::hypot(step.x, step.y);
    if (length < 1e-12) return {-before.x, -before.y};
    if (length > kLongestStep) step = {step.x * kLongestStep / length, step.y * kLongestStep / length};
    return step;
}

// The strip outside the edge from a to b that belongs to its face, reach deep. Where the corners' bisectors
// cross before that depth it ends in a point.
WorldPolygon outsideStrip(Vec2 a, Vec2 b, Vec2 stepA, Vec2 stepB, double reach)
{
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    const Vec2 u{(b.x - a.x) / length, (b.y - a.y) / length};
    const double shrink = u.x * (stepA.x - stepB.x) + u.y * (stepA.y - stepB.y);
    WorldPolygon strip;
    if (shrink > 0.0 && length - reach * shrink <= 1e-9) {
        const double depth = length / shrink;
        strip.outline = {{a.x + stepA.x * depth, a.y + stepA.y * depth}, b, a};
    } else {
        strip.outline = {{a.x + stepA.x * reach, a.y + stepA.y * reach},
                         {b.x + stepB.x * reach, b.y + stepB.y * reach},
                         b,
                         a};
    }
    return strip;
}

// One edge of the section a hip stands on, with its plane and the strip outside it.
struct HipEdge {
    Vec2 a;
    Vec2 b;
    Vec2 normal;          // unit, toward the section
    double offset = 0.0;  // the distance inside the edge is normal . p - offset
    WorldPolygon strip;
    Vec2 low;             // the corners of the strip's bounding box
    Vec2 high;
};

// What the straight skeleton gives a hip roof: the pieces on their planes and the lines where they meet.
struct SkeletonHip {
    bool ok = false;
    std::vector<roofparts::Piece> pieces;
    std::vector<roofparts::BreakLine> breaks;
};

// Says whether the bounding boxes of two strips overlap.
bool boxesMeet(const HipEdge& one, const HipEdge& other)
{
    return one.low.x <= other.high.x && other.low.x <= one.high.x && one.low.y <= other.high.y &&
           other.low.y <= one.high.y;
}

// The parts of other strips that win over the strip of edge i: where another edge's plane stands higher, or
// stands as high when it is the same plane and comes first. So where eaves close a narrow gap, the two
// overhangs meet in a valley, which is added to the break lines.
std::vector<WorldPolygon> claimsOver(const std::vector<HipEdge>& edges, std::size_t i,
                                     std::vector<roofparts::BreakLine>& breaks)
{
    std::vector<WorldPolygon> claims;
    const HipEdge& own = edges[i];
    for (std::size_t j = 0; j < edges.size(); ++j) {
        const HipEdge& other = edges[j];
        if (j == i || !boxesMeet(own, other)) continue;
        const Vec2 m{other.normal.x - own.normal.x, other.normal.y - own.normal.y};
        const double c = other.offset - own.offset;
        const double size2 = m.x * m.x + m.y * m.y;
        if (size2 < 1e-12) {
            if (c < -1e-9 || (std::fabs(c) <= 1e-9 && j < i)) claims.push_back(other.strip);
            continue;
        }
        const Vec2 point{m.x * c / size2, m.y * c / size2};
        std::vector<WorldPolygon> won = roofparts::keepHalf({other.strip}, point, m);
        if (won.empty()) continue;
        breaks.push_back({point, {m.y, -m.x}});
        for (WorldPolygon& poly : won) claims.push_back(std::move(poly));
    }
    return claims;
}

// A hip roof on any section through its straight skeleton. Inside the section each edge's piece is its skeleton
// face; outside it, out to the eaves, it is the strip between the bisectors at the edge's two corners, less what
// other strips win over it. Not ok when the skeleton cannot be worked out or the pieces do not add up to the
// eaves.
SkeletonHip skeletonHip(const RoofInput& input)
{
    SkeletonHip out;
    WorldPolygon area;
    area.outline = roofskeleton::withoutStraightPoints(input.section.outline);
    for (const std::vector<Vec2>& hole : input.section.holes) {
        std::vector<Vec2> ring = roofskeleton::withoutStraightPoints(hole);
        if (ring.size() >= 3 && signedArea(ring) < -1e-9) area.holes.push_back(std::move(ring));
    }
    if (area.outline.size() < 3) return out;
    const std::optional<roofskeleton::Skeleton> skeleton = roofskeleton::straightSkeleton(area);
    if (!skeleton) return out;

    const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
    const double reach = std::max(0.0, input.settings.overhang) + kStripMargin;

    std::vector<const std::vector<Vec2>*> rings{&area.outline};
    for (const std::vector<Vec2>& hole : area.holes) rings.push_back(&hole);
    std::vector<HipEdge> edges;
    for (const std::vector<Vec2>* ring : rings) {
        const std::size_t count = ring->size();
        std::vector<Vec2> normals(count);
        for (std::size_t k = 0; k < count; ++k) {
            const Vec2 a = (*ring)[k];
            const Vec2 b = (*ring)[(k + 1) % count];
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            normals[k] = roofparts::leftOf({(b.x - a.x) / length, (b.y - a.y) / length});
        }
        for (std::size_t k = 0; k < count; ++k) {
            HipEdge edge;
            edge.a = (*ring)[k];
            edge.b = (*ring)[(k + 1) % count];
            edge.normal = normals[k];
            edge.offset = normals[k].x * edge.a.x + normals[k].y * edge.a.y;
            const Vec2 stepA = outwardStep(normals[(k + count - 1) % count], normals[k]);
            const Vec2 stepB = outwardStep(normals[k], normals[(k + 1) % count]);
            edge.strip = outsideStrip(edge.a, edge.b, stepA, stepB, reach);
            edge.low = edge.strip.outline.front();
            edge.high = edge.low;
            for (const Vec2& p : edge.strip.outline) {
                edge.low = {std::min(edge.low.x, p.x), std::min(edge.low.y, p.y)};
                edge.high = {std::max(edge.high.x, p.x), std::max(edge.high.y, p.y)};
            }
            edges.push_back(std::move(edge));
        }
    }
    if (edges.size() != skeleton->faces.size()) return out;

    const std::vector<WorldPolygon> outside = roofparts::without(input.eaves, {area});
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const HipEdge& edge = edges[i];
        roofparts::Piece piece;
        piece.top = {slope * edge.normal.x, slope * edge.normal.y, input.base - slope * edge.offset};
        std::vector<WorldPolygon> own;
        for (const std::vector<Vec2>& part : skeleton->faces[i].parts) own.push_back({part, {}});
        piece.area = overlap(own, input.eaves);
        if (!outside.empty()) {
            for (WorldPolygon& poly : roofparts::without(overlap(outside, {edge.strip}), claimsOver(edges, i, out.breaks))) {
                piece.area.push_back(std::move(poly));
            }
        }
        if (!piece.area.empty()) out.pieces.push_back(std::move(piece));
    }

    double covered = 0.0;
    for (const roofparts::Piece& piece : out.pieces) covered += areaOf(piece.area);
    const double wanted = areaOf(input.eaves);
    const double slack = kPieceSlack * static_cast<double>(4 * edges.size() + 4) + 1e-7 * wanted;
    if (std::fabs(covered - wanted) > slack) return out;

    roofskeleton::weldInnerPoints(out.pieces, input.eaves);
    for (const auto& [p, q] : skeleton->arcs) out.breaks.push_back({p, {q.x - p.x, q.y - p.y}});
    out.ok = true;
    return out;
}

// The faces of a hip on a convex section without holes: one plane per outline edge rising inward, each face
// where its plane is the lowest. For such a section that is exactly its straight skeleton.
std::vector<RoofFace> convexHip(const RoofInput& input, const std::vector<Vec2>& ring)
{
    const std::size_t count = ring.size();
    const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
    const double base = input.base;

    // Edge i has the inward unit normal normals[i]; the distance inside it is n . p - offsets[i].
    std::vector<Vec2> normals(count);
    std::vector<double> offsets(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % count];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        normals[i] = roofparts::leftOf({(b.x - a.x) / len, (b.y - a.y) / len});
        offsets[i] = normals[i].x * a.x + normals[i].y * a.y;
    }

    std::vector<roofparts::Piece> pieces;
    std::vector<roofparts::BreakLine> breaks;
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<WorldPolygon> area = input.eaves;
        for (std::size_t j = 0; j < count && !area.empty(); ++j) {
            if (j == i) continue;
            const Vec2 m{normals[j].x - normals[i].x, normals[j].y - normals[i].y};
            const double size2 = m.x * m.x + m.y * m.y;
            if (size2 < 1e-12) continue;
            const double c = offsets[j] - offsets[i];
            const Vec2 point{m.x * c / size2, m.y * c / size2};
            area = roofparts::keepHalf(area, point, m);
            if (j > i) breaks.push_back({point, {m.y, -m.x}});
        }
        if (area.empty()) continue;
        roofparts::Piece piece;
        piece.area = std::move(area);
        piece.top = {slope * normals[i].x, slope * normals[i].y, base - slope * offsets[i]};
        pieces.push_back(std::move(piece));
    }

    roofskeleton::weldInnerPoints(pieces, input.eaves);

    auto height = [=](Vec2 p) {
        double lowest = 1e18;
        for (std::size_t i = 0; i < count; ++i) {
            lowest = std::min(lowest, normals[i].x * p.x + normals[i].y * p.y - offsets[i]);
        }
        return base + slope * lowest;
    };
    return roofparts::pitchedFaces(input, pieces, breaks, height);
}

// Says whether a section is convex without holes, so the plain plane method builds its hip.
bool convexSection(const RoofInput& input)
{
    return input.section.holes.empty() &&
           roofskeleton::convexRing(roofskeleton::withoutStraightPoints(input.section.outline));
}

// A hip roof: one plane per edge of the section rising inward at the pitch, meeting in hips, valleys and ridges.
class HipRoof final : public RoofType {
public:
    std::string_view id() const override { return kHipRoofId; }
    std::string_view name() const override { return "Hip"; }
    int sortKey() const override { return 30; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    std::string_view fallback() const override { return kGableRoofId; }

    Status fits(const RoofInput& input) const override
    {
        if (convexSection(input) || skeletonHip(input).ok) return {};
        return fail(ErrorCode::InvalidValue,
                    "The slopes of a hip roof could not be worked out for this section, so it is built as a gable.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        if (convexSection(input)) return convexHip(input, roofskeleton::withoutStraightPoints(input.section.outline));
        const SkeletonHip hip = skeletonHip(input);
        // Only reached as another type's fallback, since fits fails then: a gable still gives the section a roof.
        if (!hip.ok) {
            const RoofType* gable = findRoofType(kGableRoofId);
            return gable ? gable->build(input) : std::vector<RoofFace>{};
        }
        const std::vector<roofparts::Piece>& pieces = hip.pieces;
        const double base = input.base;
        // The covering's height at a place: the plane of the piece that holds it, or of the nearest piece.
        auto height = [&pieces, base](Vec2 p) {
            double best = std::numeric_limits<double>::max();
            const roofparts::Piece* nearest = nullptr;
            for (const roofparts::Piece& piece : pieces) {
                for (const WorldPolygon& poly : piece.area) {
                    const double d = distanceTo(p, poly);
                    if (d < best) {
                        best = d;
                        nearest = &piece;
                    }
                }
                if (best == 0.0) break;
            }
            return nearest ? nearest->top.at(p) : base;
        };
        return roofparts::pitchedFaces(input, hip.pieces, hip.breaks, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<HipRoof>());

}

}
