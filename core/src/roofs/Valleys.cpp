#include "roofs/Valleys.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace plynth::roofparts {

namespace {

constexpr double kLevel = 1e-9;     // metres: planes nearer than this are level with each other
constexpr double kParallel = 1e-12; // planes whose slopes differ by less than the root of this are parallel
constexpr double kOnEdge = 1e-6;    // metres: a point this near an edge lies on it
constexpr double kProbe = 1e-5;     // metres: how far beyond an edge the next piece is looked for
constexpr double kStep = 1e-4;      // metres: a piece beyond an edge this much lower leaves a step with an edge
constexpr double kMeet = 1e-6;      // square metres: eaves that overlap by less than this do not meet
constexpr double kTouch = 1e-4;     // metres: a part this near the eaves touches them

// The area of polygons seen from above.
double areaOf(const std::vector<WorldPolygon>& polygons)
{
    double sum = 0.0;
    for (const WorldPolygon& poly : polygons) {
        sum += signedArea(poly.outline);
        for (const std::vector<Vec2>& hole : poly.holes) sum += signedArea(hole);
    }
    return sum;
}

// A box round polygons seen from above.
struct Bounds {
    double x0 = 1e18;
    double y0 = 1e18;
    double x1 = -1e18;
    double y1 = -1e18;
};

// The box round the outlines of the polygons.
Bounds boundsOf(const std::vector<WorldPolygon>& polygons)
{
    Bounds b;
    for (const WorldPolygon& poly : polygons) {
        for (const Vec2& p : poly.outline) {
            b.x0 = std::min(b.x0, p.x);
            b.y0 = std::min(b.y0, p.y);
            b.x1 = std::max(b.x1, p.x);
            b.y1 = std::max(b.y1, p.y);
        }
    }
    return b;
}

// Says whether two boxes overlap or touch.
bool overlap(const Bounds& a, const Bounds& b)
{
    return a.x0 <= b.x1 + kTouch && b.x0 <= a.x1 + kTouch && a.y0 <= b.y1 + kTouch && b.y0 <= a.y1 + kTouch;
}

// What the two sets have in common.
std::vector<WorldPolygon> common(const std::vector<WorldPolygon>& a, const std::vector<WorldPolygon>& b)
{
    if (a.empty() || b.empty() || !overlap(boundsOf(a), boundsOf(b))) return {};
    const std::vector<WorldPolygon> outside = without(a, b);
    if (outside.empty()) return a;
    return without(a, outside);
}

// The part of area where plane top stands above plane other. Where the two are level, all of it when level counts
// and none of it otherwise.
std::vector<WorldPolygon> whereAbove(const std::vector<WorldPolygon>& area, const Plane& top, const Plane& other,
                                     bool levelCounts)
{
    if (area.empty()) return {};
    const Vec2 m{top.a - other.a, top.b - other.b};
    const double c = top.c - other.c;
    const double size2 = m.x * m.x + m.y * m.y;
    if (size2 < kParallel) {
        const Vec2 at = area.front().outline.front();
        const double gap = m.x * at.x + m.y * at.y + c;
        if (gap > kLevel || (levelCounts && gap >= -kLevel)) return area;
        return {};
    }
    return keepHalf(area, {-c * m.x / size2, -c * m.y / size2}, m);
}

// The plane a face lies in, from its outline. Empty for a face that stands upright.
std::optional<Plane> planeOf(const RoofFace& face)
{
    const std::vector<Vec3>& ring = face.outline;
    if (ring.size() < 3) return std::nullopt;
    Vec3 n;
    Vec3 mean;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
        mean.x += a.x;
        mean.y += a.y;
        mean.z += a.z;
    }
    const double size = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (size < 1e-12 || std::fabs(n.z) < 1e-6 * size) return std::nullopt;
    const double count = static_cast<double>(ring.size());
    Plane plane;
    plane.a = -n.x / n.z;
    plane.b = -n.y / n.z;
    plane.c = mean.z / count - plane.a * mean.x / count - plane.b * mean.y / count;
    return plane;
}

// A ring seen from above.
std::vector<Vec2> fromAbove(const std::vector<Vec3>& ring)
{
    std::vector<Vec2> out;
    out.reserve(ring.size());
    for (const Vec3& p : ring) out.push_back({p.x, p.y});
    return out;
}

// Every ring of a polygon: the outline, then the holes.
std::vector<const std::vector<Vec2>*> ringList(const WorldPolygon& poly)
{
    std::vector<const std::vector<Vec2>*> rings{&poly.outline};
    for (const std::vector<Vec2>& hole : poly.holes) rings.push_back(&hole);
    return rings;
}

// Says whether a point lies inside a polygon: in its outline and in none of its holes.
bool insideOf(const WorldPolygon& poly, Vec2 p)
{
    if (!insidePolygon(p, poly.outline)) return false;
    for (const std::vector<Vec2>& hole : poly.holes) {
        if (insidePolygon(p, hole)) return false;
    }
    return true;
}

// The edge from p to q cut at every one of the corners that lies on it, the parts in order from p.
std::vector<std::pair<Vec2, Vec2>> splitAt(Vec2 p, Vec2 q, const std::vector<Vec2>& corners)
{
    const double dx = q.x - p.x;
    const double dy = q.y - p.y;
    const double len2 = dx * dx + dy * dy;
    std::vector<double> ts{0.0, 1.0};
    if (len2 > 0.0) {
        for (const Vec2& c : corners) {
            const double t = ((c.x - p.x) * dx + (c.y - p.y) * dy) / len2;
            if (t <= 1e-9 || t >= 1.0 - 1e-9) continue;
            if (projectOntoSegment(c, p, q).distance <= kOnEdge) ts.push_back(t);
        }
    }
    std::sort(ts.begin(), ts.end());
    std::vector<std::pair<Vec2, Vec2>> out;
    for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
        if (ts[k + 1] - ts[k] <= 1e-12) continue;
        const Vec2 a = ts[k] <= 0.0 ? p : Vec2{p.x + dx * ts[k], p.y + dy * ts[k]};
        const Vec2 b = ts[k + 1] >= 1.0 ? q : Vec2{p.x + dx * ts[k + 1], p.y + dy * ts[k + 1]};
        out.emplace_back(a, b);
    }
    return out;
}

// Says whether the edge from a to b of a piece in plane top is an edge of the roof: no piece lies just beyond it
// (to its right, outside the piece), or the one that does is lower by more than a step.
bool roofEdge(Vec2 a, Vec2 b, const Plane& top, const std::vector<Piece>& all)
{
    const double len = std::hypot(b.x - a.x, b.y - a.y);
    if (len <= 0.0) return false;
    const Vec2 mid{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
    const Vec2 probe{mid.x + (b.y - a.y) / len * kProbe, mid.y - (b.x - a.x) / len * kProbe};
    for (const Piece& piece : all) {
        for (const WorldPolygon& poly : piece.area) {
            if (insideOf(poly, probe)) return top.at(mid) - piece.top.at(probe) > kStep;
        }
    }
    return true;
}

// Says whether a point lies on the outline of the eaves.
bool onEaves(Vec2 p, const std::vector<WorldPolygon>& eaves)
{
    for (const WorldPolygon& poly : eaves) {
        for (const std::vector<Vec2>* ring : ringList(poly)) {
            for (std::size_t i = 0; i < ring->size(); ++i) {
                if (projectOntoSegment(p, (*ring)[i], (*ring)[(i + 1) % ring->size()]).distance <= kTouch) return true;
            }
        }
    }
    return false;
}

// Says whether a polygon shares a stretch of its outline with the outline of the eaves, not only a point.
bool touches(const WorldPolygon& poly, const std::vector<WorldPolygon>& eaves)
{
    const std::vector<Vec2>& ring = poly.outline;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        if (std::hypot(b.x - a.x, b.y - a.y) <= kTouch) continue;
        if (onEaves(a, eaves) && onEaves(b, eaves) && onEaves({(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, eaves)) {
            return true;
        }
    }
    return false;
}

// The pieces by which roof b carries on over the eaves of roof a: the planes of b that reach a's eaves, kept
// beyond b's own eaves wherever the lowest of them stands above a's roof, in the parts that join b's eaves. Each
// piece is where its plane is the lowest of them. A gable wing so runs into the main roof up to its valleys.
std::vector<Piece> reach(const JoinSection& b, const std::vector<Piece>& piecesB, const JoinSection& a,
                         const std::vector<Piece>& piecesA)
{
    std::vector<Plane> planes;
    for (const Piece& piece : piecesB) {
        if (areaOf(common(piece.area, a.input.eaves)) > kMeet) planes.push_back(piece.top);
    }
    if (planes.empty()) return {};

    std::vector<WorldPolygon> parts;
    for (const Piece& piece : piecesA) {
        std::vector<WorldPolygon> part = piece.area;
        for (const Plane& plane : planes) part = whereAbove(part, plane, piece.top, false);
        if (part.empty()) continue;
        for (WorldPolygon& poly : without(part, b.input.eaves)) parts.push_back(std::move(poly));
    }

    // Only parts joined to b's eaves along a stretch, directly or through other such parts, are kept. A part
    // that meets them at a point only (beyond a ridge as high as b's) is not part of b's roof.
    std::vector<WorldPolygon> linked;
    std::vector<char> taken(parts.size(), 0);
    bool grew = true;
    while (grew) {
        grew = false;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (taken[i] || !(touches(parts[i], b.input.eaves) || touches(parts[i], linked))) continue;
            taken[i] = 1;
            linked.push_back(parts[i]);
            grew = true;
        }
    }
    if (linked.empty()) return {};

    std::vector<Piece> out;
    for (std::size_t k = 0; k < planes.size(); ++k) {
        std::vector<WorldPolygon> area = linked;
        for (std::size_t j = 0; j < planes.size() && !area.empty(); ++j) {
            if (j != k) area = whereAbove(area, planes[j], planes[k], j > k);
        }
        if (!area.empty()) out.push_back({std::move(area), planes[k]});
    }
    return out;
}

}

std::vector<Piece> coveringPieces(const std::vector<RoofFace>& faces)
{
    std::vector<Piece> out;
    for (const RoofFace& face : faces) {
        if (face.part != RoofPart::Covering) continue;
        const std::optional<Plane> plane = planeOf(face);
        if (!plane) continue;
        WorldPolygon poly;
        poly.outline = fromAbove(face.outline);
        for (const std::vector<Vec3>& hole : face.holes) poly.holes.push_back(fromAbove(hole));
        Piece piece;
        piece.area.push_back(std::move(poly));
        piece.top = *plane;
        out.push_back(std::move(piece));
    }
    return out;
}

std::vector<RoofFace> pieceFaces(const std::vector<Piece>& own, const std::vector<Piece>& all,
                                 const std::vector<WorldPolygon>& above)
{
    std::vector<RoofFace> faces;
    for (const Piece& piece : own) {
        for (const WorldPolygon& poly : piece.area) faces.push_back(planeFace(poly, piece.top, 0.0, true, RoofPart::Covering));
    }
    for (const Piece& piece : own) {
        for (const WorldPolygon& poly : piece.area) {
            faces.push_back(planeFace(poly, piece.top, kRoofThickness, false, RoofPart::Underside));
        }
    }

    std::vector<Vec2> corners;
    for (const Piece& piece : all) {
        for (const WorldPolygon& poly : piece.area) {
            for (const std::vector<Vec2>* ring : ringList(poly)) corners.insert(corners.end(), ring->begin(), ring->end());
        }
    }

    for (const Piece& piece : own) {
        for (const WorldPolygon& poly : piece.area) {
            for (const std::vector<Vec2>* ring : ringList(poly)) {
                for (std::size_t i = 0; i < ring->size(); ++i) {
                    const Vec2 p = (*ring)[i];
                    const Vec2 q = (*ring)[(i + 1) % ring->size()];
                    std::vector<std::pair<Vec2, Vec2>> runs;
                    for (const auto& [from, to] : splitAt(p, q, corners)) {
                        if (!roofEdge(from, to, piece.top, all)) continue;
                        if (!runs.empty() && runs.back().second == from) {
                            runs.back().second = to;
                        } else {
                            runs.emplace_back(from, to);
                        }
                    }
                    for (const auto& run : runs) {
                        for (const auto& [u, v] : freeParts(run.first, run.second, above)) {
                            if (std::hypot(v.x - u.x, v.y - u.y) < kOnEdge) continue;
                            const double tu = piece.top.at(u);
                            const double tv = piece.top.at(v);
                            faces.push_back(wallFace(u, v, tu - kRoofThickness, tv - kRoofThickness, tu, tv, RoofPart::Edge));
                        }
                    }
                }
            }
        }
    }
    return faces;
}

void joinSections(std::vector<JoinSection>& sections, const std::vector<WorldPolygon>& above)
{
    const std::size_t count = sections.size();
    std::vector<char> joinedIn(count, 0);
    std::vector<std::vector<char>> meets(count, std::vector<char>(count, 0));
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (!sections[i].pitched || !sections[j].pitched) continue;
            if (areaOf(common(sections[i].input.eaves, sections[j].input.eaves)) <= kMeet) continue;
            meets[i][j] = 1;
            meets[j][i] = 1;
            joinedIn[i] = 1;
            joinedIn[j] = 1;
        }
    }
    if (std::find(joinedIn.begin(), joinedIn.end(), char{1}) == joinedIn.end()) return;

    std::vector<std::vector<Piece>> own(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (joinedIn[i]) own[i] = coveringPieces(sections[i].faces);
    }

    // Each roof first carries on over its neighbours where it stays above them, from its pieces as built.
    // Where two neighbours' eaves overlap, what a roof carries on over the second is kept only beyond the first.
    std::vector<std::vector<Piece>> grown = own;
    for (std::size_t b = 0; b < count; ++b) {
        for (std::size_t a = 0; a < count; ++a) {
            if (!meets[b][a]) continue;
            for (Piece& piece : reach(sections[b], own[b], sections[a], own[a])) {
                for (std::size_t k = own[b].size(); k < grown[b].size() && !piece.area.empty(); ++k) {
                    if (overlap(boundsOf(piece.area), boundsOf(grown[b][k].area))) {
                        piece.area = without(piece.area, grown[b][k].area);
                    }
                }
                if (!piece.area.empty()) grown[b].push_back(std::move(piece));
            }
        }
    }

    // Then every piece gives up what a piece of another roof covers higher up, all judged on the grown pieces.
    std::vector<std::vector<Piece>> kept = grown;
    for (std::size_t s = 0; s < count; ++s) {
        if (!joinedIn[s]) continue;
        for (Piece& piece : kept[s]) {
            const Bounds box = boundsOf(piece.area);
            for (std::size_t t = 0; t < count && !piece.area.empty(); ++t) {
                if (t == s || !joinedIn[t]) continue;
                for (const Piece& other : grown[t]) {
                    if (piece.area.empty()) break;
                    if (!overlap(box, boundsOf(other.area))) continue;
                    const std::vector<WorldPolygon> higher = whereAbove(other.area, other.top, piece.top, t < s);
                    if (!higher.empty()) piece.area = without(piece.area, higher);
                }
            }
        }
        std::erase_if(kept[s], [](const Piece& piece) { return piece.area.empty(); });
    }

    std::vector<Piece> all;
    for (std::size_t s = 0; s < count; ++s) {
        if (joinedIn[s]) all.insert(all.end(), kept[s].begin(), kept[s].end());
    }
    for (std::size_t s = 0; s < count; ++s) {
        if (!joinedIn[s]) continue;
        std::vector<RoofFace> faces = pieceFaces(kept[s], all, above);
        for (RoofFace& face : sections[s].faces) {
            if (face.part == RoofPart::Covering || face.part == RoofPart::Underside || face.part == RoofPart::Edge) continue;
            faces.push_back(std::move(face));
        }
        sections[s].faces = std::move(faces);
    }
}

}
