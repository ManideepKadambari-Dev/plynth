#pragma once

#include "ClipperInclude.h"
#include "plynth/core/BuildGeometry.h"
#include "plynth/core/Geometry.h"
#include "plynth/core/Storeys.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// The one place that decides what a ring is: one that turns left is an outline, one that turns right is a hole.
// Clipper's nesting is never asked, since rounding to 1e-8 can put a hole just outside its own outline there.
namespace plynth {

inline constexpr int kRingDigits = 8;       // the decimal places Clipper rounds to
inline constexpr double kOwnerSlack = 1e-7; // how far rounding may put a hole outside its outline, in metres
inline constexpr double kNoTurning = 1e-12; // with less area than this, in square metres, a ring has no sure turning

// The area of a ring, positive when it turns left. It is measured from the ring's first point, so a place far
// from the origin loses no digits.
inline double ringAreaOf(const std::vector<Vec2>& ring)
{
    double twice = 0.0;
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        twice += (ring[i].x - ring[0].x) * (ring[i + 1].y - ring[0].y) -
                 (ring[i].y - ring[0].y) * (ring[i + 1].x - ring[0].x);
    }
    return 0.5 * twice;
}

// A Clipper path as a ring of points.
inline std::vector<Vec2> ringOfPath(const Clipper2Lib::PathD& path)
{
    std::vector<Vec2> ring;
    ring.reserve(path.size());
    for (const Clipper2Lib::PointD& p : path) ring.push_back({p.x, p.y});
    return ring;
}

// The distance from a point to a segment.
inline double distanceToSegmentOf(const Vec2& p, const Vec2& a, const Vec2& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// An outline with the holes it holds.
struct RingPiece {
    std::vector<Vec2> outline;
    std::vector<std::vector<Vec2>> holes;
};

// Records the holes the reader could not give to an outline while it is the thread's current scope. A scope
// opened inside another passes what it recorded on to that one when it ends.
class RingFaultScope {
public:
    RingFaultScope() : before_(current()) { current() = this; }
    ~RingFaultScope()
    {
        current() = before_;
        if (before_) before_->faults_ += faults_;
    }
    RingFaultScope(const RingFaultScope&) = delete;
    RingFaultScope& operator=(const RingFaultScope&) = delete;

    // Says whether a hole without an outline was recorded since the scope began.
    bool any() const { return faults_ > 0; }

    // Records one in the current scope, if there is one.
    static void record()
    {
        if (current()) ++current()->faults_;
    }

private:
    // The thread's current scope.
    static RingFaultScope*& current()
    {
        thread_local RingFaultScope* scope = nullptr;
        return scope;
    }

    RingFaultScope* before_;
    int faults_ = 0;
};

// The paths moved outward by delta (inward when negative) with mitred corners, each ring alone: an outline by
// delta, a hole as an outline by -delta and turned back. All are united by their turning, so near pieces join
// and no ring needs to know its owner.
inline Clipper2Lib::PathsD offsetByTurning(const Clipper2Lib::PathsD& paths, double delta, double mitreLimit = 10.0);

// A hole no outline holds. When it keeps less than kMinPlanAreaSize after shrinking by half of
// kRingMergeDistance it is noise and is filled. Otherwise its outline is broken: it is left out and recorded.
inline void holeWithoutOutline(const std::vector<Vec2>& hole)
{
    Clipper2Lib::PathD alone;
    for (auto it = hole.rbegin(); it != hole.rend(); ++it) alone.emplace_back(it->x, it->y);
    double left = 0.0;
    for (const Clipper2Lib::PathD& ring : offsetByTurning(Clipper2Lib::PathsD{alone}, -kRingMergeDistance / 2.0)) {
        left += ringAreaOf(ringOfPath(ring));
    }
    if (left >= kMinPlanAreaSize) RingFaultScope::record();
}

// Gives each hole to the smallest outline that is large enough for it and has most of its points inside, both
// give or take slack. Never to the nearest outline: a hole that none holds goes to holeWithoutOutline.
inline std::vector<RingPiece> piecesFromRings(std::vector<std::vector<Vec2>> outlines,
                                              std::vector<std::vector<Vec2>> holes, double slack = kOwnerSlack)
{
    std::vector<RingPiece> pieces;
    std::vector<double> sizes;
    for (std::vector<Vec2>& outline : outlines) {
        sizes.push_back(ringAreaOf(outline));
        pieces.push_back({std::move(outline), {}});
    }
    for (std::vector<Vec2>& hole : holes) {
        double around = 0.0;
        for (std::size_t i = 0; i < hole.size(); ++i) {
            const Vec2 a = hole[i];
            const Vec2 b = hole[(i + 1) % hole.size()];
            around += std::hypot(b.x - a.x, b.y - a.y);
        }
        const double least = -ringAreaOf(hole) - slack * around;
        std::size_t owner = pieces.size();
        for (std::size_t k = 0; k < pieces.size(); ++k) {
            if (sizes[k] < least || (owner < pieces.size() && sizes[owner] <= sizes[k])) continue;
            const std::vector<Vec2>& outline = pieces[k].outline;
            std::size_t within = 0;
            for (const Vec2& p : hole) {
                bool in = insidePolygon(p, outline);
                for (std::size_t i = 0; i < outline.size() && !in; ++i) {
                    in = distanceToSegmentOf(p, outline[i], outline[(i + 1) % outline.size()]) <= slack;
                }
                if (in) ++within;
            }
            if (2 * within > hole.size()) owner = k;
        }
        if (owner < pieces.size()) {
            pieces[owner].holes.push_back(std::move(hole));
        } else {
            holeWithoutOutline(hole);
        }
    }
    return pieces;
}

// Reads the plain paths of a clip as pieces, by each ring's turning.
inline std::vector<RingPiece> piecesByTurning(const Clipper2Lib::PathsD& paths)
{
    std::vector<std::vector<Vec2>> outlines;
    std::vector<std::vector<Vec2>> holes;
    for (const Clipper2Lib::PathD& path : paths) {
        std::vector<Vec2> ring = ringOfPath(path);
        const double size = ringAreaOf(ring);
        if (size > 0.0) outlines.push_back(std::move(ring));
        else if (size < 0.0) holes.push_back(std::move(ring));
    }
    return piecesFromRings(std::move(outlines), std::move(holes));
}

// See the declaration above.
inline Clipper2Lib::PathsD offsetByTurning(const Clipper2Lib::PathsD& paths, double delta, double mitreLimit)
{
    Clipper2Lib::PathsD moved;
    for (const Clipper2Lib::PathD& path : paths) {
        const double size = ringAreaOf(ringOfPath(path));
        if (std::abs(size) < kNoTurning) continue;
        const bool hole = size < 0.0;
        Clipper2Lib::PathD alone = path;
        if (hole) std::reverse(alone.begin(), alone.end());
        Clipper2Lib::PathsD done =
            Clipper2Lib::InflatePaths(Clipper2Lib::PathsD{alone}, hole ? -delta : delta, Clipper2Lib::JoinType::Miter,
                                      Clipper2Lib::EndType::Polygon, mitreLimit, kRingDigits);
        for (Clipper2Lib::PathD& ring : done) {
            if (hole) std::reverse(ring.begin(), ring.end());
            moved.push_back(std::move(ring));
        }
    }
    return moved.empty() ? moved : Clipper2Lib::Union(moved, Clipper2Lib::FillRule::Positive, kRingDigits);
}

// Runs one clip operation as plain paths.
inline Clipper2Lib::PathsD clipped(Clipper2Lib::ClipType type, const Clipper2Lib::PathsD& subject,
                                   const Clipper2Lib::PathsD& clip)
{
    Clipper2Lib::ClipperD clipper(kRingDigits);
    clipper.AddSubject(subject);
    if (!clip.empty()) clipper.AddClip(clip);
    Clipper2Lib::PathsD out;
    clipper.Execute(type, Clipper2Lib::FillRule::NonZero, out);
    return out;
}

// Runs one clip operation and tidies the result by width, as the top of Storeys.h says (w is
// kRingMergeDistance): what is narrower than w outside the area is filled, then what is narrower than w inside
// it is left out. Both times the union or the intersection is with the unmoved area. It always ends in a fresh
// union, since the same points can be joined up in another way.
inline Clipper2Lib::PathsD clipAndTidy(Clipper2Lib::ClipType type, const Clipper2Lib::PathsD& subject,
                                       const Clipper2Lib::PathsD& clip)
{
    const double w = kRingMergeDistance;
    const Clipper2Lib::PathsD raw = clipped(type, subject, clip);
    if (raw.empty()) return {};
    const Clipper2Lib::PathsD closed = offsetByTurning(offsetByTurning(raw, w / 2.0), -w);
    const Clipper2Lib::PathsD filled = clipped(Clipper2Lib::ClipType::Union, raw, closed);
    const Clipper2Lib::PathsD opened = offsetByTurning(offsetByTurning(filled, -w / 2.0), w);
    if (opened.empty()) return {};
    const Clipper2Lib::PathsD area = clipped(Clipper2Lib::ClipType::Intersection, filled, opened);
    return clipped(Clipper2Lib::ClipType::Union, area, {});
}

// The ring cleaned of rounding noise by the rules at the top of Storeys.h: points nearer than
// kRingMergeDistance to the last point kept are dropped (the last against the first too), and so are the tips of
// spikes, until nothing changes. Empty when fewer than three points are left or the area is under
// kMinPlanAreaSize. The turning direction is not changed.
inline std::vector<Vec2> cleanedRingOf(std::vector<Vec2> ring)
{
    bool changed = true;
    while (changed) {
        changed = false;

        std::vector<Vec2> kept;
        kept.reserve(ring.size());
        for (const Vec2& p : ring) {
            if (!kept.empty() && std::hypot(p.x - kept.back().x, p.y - kept.back().y) < kRingMergeDistance) continue;
            kept.push_back(p);
        }
        while (kept.size() > 1 && std::hypot(kept.back().x - kept.front().x, kept.back().y - kept.front().y) < kRingMergeDistance) {
            kept.pop_back();
        }
        if (kept.size() != ring.size()) changed = true;
        ring = std::move(kept);
        if (ring.size() < 3) return {};

        for (std::size_t i = 0; i < ring.size();) {
            const Vec2 p = ring[i];
            const Vec2 a = ring[(i + ring.size() - 1) % ring.size()];
            const Vec2 b = ring[(i + 1) % ring.size()];
            if (distanceToSegmentOf(a, p, b) < kRingMergeDistance || distanceToSegmentOf(b, p, a) < kRingMergeDistance) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                if (ring.size() < 3) return {};
            } else {
                ++i;
            }
        }
    }
    if (std::abs(ringAreaOf(ring)) < kMinPlanAreaSize) return {};
    return ring;
}

// Cleans a ring and cuts it where one of its points is within kRingMergeDistance of an edge that is not its
// neighbour, through points it already has: the two loops share that point. Each loop is cleaned and cut again
// and keeps the way it turns. cut is set when a ring was cut.
inline void cleanAndCut(const std::vector<Vec2>& input, std::vector<std::vector<Vec2>>& outlines,
                        std::vector<std::vector<Vec2>>& holes, bool& cut)
{
    std::vector<Vec2> ring = cleanedRingOf(input);
    if (ring.empty()) return;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || (j + 1) % n == i) continue;
            if (distanceToSegmentOf(ring[i], ring[j], ring[(j + 1) % n]) >= kRingMergeDistance) continue;
            std::vector<Vec2> first;
            for (std::size_t k = (j + 1) % n;; k = (k + 1) % n) {
                first.push_back(ring[k]);
                if (k == i) break;
            }
            std::vector<Vec2> second;
            for (std::size_t k = i;; k = (k + 1) % n) {
                second.push_back(ring[k]);
                if (k == j) break;
            }
            cut = true;
            cleanAndCut(first, outlines, holes, cut);
            cleanAndCut(second, outlines, holes, cut);
            return;
        }
    }
    (ringAreaOf(ring) > 0.0 ? outlines : holes).push_back(std::move(ring));
}

// Tidies one piece once: every ring cleaned and cut where it comes within kRingMergeDistance of itself. Without a
// cut the holes stay with the outline. After a cut every hole, old or new, gets its owner afresh among the
// piece's outlines, with kSlabSideTolerance as the slack since cleaning has moved the rings. An outline that does
// not survive takes its holes along, and so does a piece left under kMinPlanAreaSize.
inline std::vector<RingPiece> tidyPieceOnce(const RingPiece& piece)
{
    std::vector<std::vector<Vec2>> outlines;
    std::vector<std::vector<Vec2>> holes;
    bool cut = false;
    cleanAndCut(piece.outline, outlines, holes, cut);
    if (outlines.empty()) return {};
    for (const std::vector<Vec2>& hole : piece.holes) cleanAndCut(hole, outlines, holes, cut);

    std::vector<RingPiece> found;
    if (!cut) found.push_back({std::move(outlines.front()), std::move(holes)});
    else found = piecesFromRings(std::move(outlines), std::move(holes), kSlabSideTolerance);

    std::vector<RingPiece> kept;
    for (RingPiece& p : found) {
        double size = ringAreaOf(p.outline);
        for (const std::vector<Vec2>& hole : p.holes) size += ringAreaOf(hole);
        if (size >= kMinPlanAreaSize) kept.push_back(std::move(p));
    }
    return kept;
}

// How deep rounding may put one ring of a piece across another, in metres (ruling C's noise).
inline constexpr double kRingsApart = 1e-7;

// Says whether two different rings of a piece cross, with each end of the two edges further than depth from
// the other edge.
inline bool ringsCross(const RingPiece& piece, double depth)
{
    std::vector<const std::vector<Vec2>*> rings{&piece.outline};
    for (const std::vector<Vec2>& hole : piece.holes) rings.push_back(&hole);
    const auto side = [](const Vec2& p, const Vec2& q, const Vec2& r) {
        const double v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
    };
    for (std::size_t r = 0; r < rings.size(); ++r) {
        for (std::size_t s = r + 1; s < rings.size(); ++s) {
            const std::vector<Vec2>& a = *rings[r];
            const std::vector<Vec2>& b = *rings[s];
            for (std::size_t j = 0; j < b.size(); ++j) {
                const Vec2 c = b[j];
                const Vec2 d = b[(j + 1) % b.size()];
                for (std::size_t i = 0; i < a.size(); ++i) {
                    const Vec2 p = a[i];
                    const Vec2 q = a[(i + 1) % a.size()];
                    if (side(p, q, c) * side(p, q, d) >= 0 || side(c, d, p) * side(c, d, q) >= 0) continue;
                    const double deep = std::min({distanceToSegmentOf(p, c, d), distanceToSegmentOf(q, c, d),
                                                  distanceToSegmentOf(c, p, q), distanceToSegmentOf(d, p, q)});
                    if (deep > depth) return true;
                }
            }
        }
    }
    return false;
}

// Tidies one piece as tidyPieceOnce does, then checks its rings together: a piece whose rings cross each other
// is united again, read by turning, cut and cleaned again, at most three times. One whose rings still cross
// deeper than kRingsApart is then recorded as a broken outline (holeWithoutOutline's fault) and kept as it is.
// A hole is never bridged.
inline std::vector<RingPiece> tidyPiece(const RingPiece& piece)
{
    std::vector<RingPiece> done = tidyPieceOnce(piece);
    for (int round = 0; round < 3; ++round) {
        std::vector<RingPiece> next;
        bool again = false;
        for (RingPiece& p : done) {
            if (!ringsCross(p, 0.0)) {
                next.push_back(std::move(p));
                continue;
            }
            again = true;
            Clipper2Lib::PathsD paths;
            paths.push_back({});
            for (const Vec2& v : p.outline) paths.back().emplace_back(v.x, v.y);
            for (const std::vector<Vec2>& hole : p.holes) {
                paths.push_back({});
                for (const Vec2& v : hole) paths.back().emplace_back(v.x, v.y);
            }
            for (const RingPiece& read : piecesByTurning(clipped(Clipper2Lib::ClipType::Union, paths, {}))) {
                for (RingPiece& q : tidyPieceOnce(read)) next.push_back(std::move(q));
            }
        }
        done = std::move(next);
        if (!again) return done;
    }
    for (const RingPiece& p : done) {
        if (ringsCross(p, kRingsApart)) RingFaultScope::record();
    }
    return done;
}

// A roof face tidied as a piece in its own plane, seen from the side it faces. The points that are kept are the
// face's own points, unmoved. New points of a piece united again lie on the face's plane.
inline std::vector<RoofFace> tidyRoofFace(const RoofFace& face)
{
    if (face.outline.size() < 3) return {};
    const Vec3 o = face.outline.front();
    Vec3 n{0.0, 0.0, 0.0};
    for (std::size_t i = 1; i + 1 < face.outline.size(); ++i) {
        const Vec3 a{face.outline[i].x - o.x, face.outline[i].y - o.y, face.outline[i].z - o.z};
        const Vec3 b{face.outline[i + 1].x - o.x, face.outline[i + 1].y - o.y, face.outline[i + 1].z - o.z};
        n.x += a.y * b.z - a.z * b.y;
        n.y += a.z * b.x - a.x * b.z;
        n.z += a.x * b.y - a.y * b.x;
    }
    const double size = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (0.5 * size < kMinPlanAreaSize) return {};
    n = {n.x / size, n.y / size, n.z / size};
    const bool steep = std::abs(n.z) < 0.5;
    const Vec3 seed = steep ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
    Vec3 u{seed.y * n.z - seed.z * n.y, seed.z * n.x - seed.x * n.z, seed.x * n.y - seed.y * n.x};
    const double length = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = {u.x / length, u.y / length, u.z / length};
    const Vec3 v{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};

    std::vector<Vec2> flat;
    std::vector<Vec3> own;
    const auto flattened = [&](const std::vector<Vec3>& ring) {
        std::vector<Vec2> out;
        for (const Vec3& p : ring) {
            const double x = p.x - o.x;
            const double y = p.y - o.y;
            const double z = p.z - o.z;
            out.push_back({x * u.x + y * u.y + z * u.z, x * v.x + y * v.y + z * v.z});
            flat.push_back(out.back());
            own.push_back(p);
        }
        return out;
    };
    // A point that tidying kept is found again among the face's own. A piece united again has new points too,
    // and those are put on the face's plane.
    const auto lifted = [&](const std::vector<Vec2>& ring) {
        std::vector<Vec3> out;
        for (const Vec2& p : ring) {
            const auto at = std::find(flat.begin(), flat.end(), p);
            if (at != flat.end()) {
                out.push_back(own[static_cast<std::size_t>(at - flat.begin())]);
            } else {
                out.push_back({o.x + p.x * u.x + p.y * v.x, o.y + p.x * u.y + p.y * v.y, o.z + p.x * u.z + p.y * v.z});
            }
        }
        return out;
    };
    RingPiece piece;
    piece.outline = flattened(face.outline);
    for (const std::vector<Vec3>& hole : face.holes) piece.holes.push_back(flattened(hole));

    std::vector<RoofFace> out;
    for (const RingPiece& p : tidyPiece(piece)) {
        RoofFace made;
        made.part = face.part;
        made.outline = lifted(p.outline);
        for (const std::vector<Vec2>& hole : p.holes) made.holes.push_back(lifted(hole));
        out.push_back(std::move(made));
    }
    return out;
}

}
