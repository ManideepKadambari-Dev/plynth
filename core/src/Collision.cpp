#include "plynth/core/Collision.h"

#include "plynth/core/Roofs.h"
#include "plynth/core/WallGeometry.h"

#include "BandOutline.h"
#include "ClipperInclude.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

constexpr int kPrecision = 8;
constexpr double kPointClose = 1e-7;
constexpr double kLevelEps = 1e-9;
constexpr double kFarKey = 1e18;

// One cross section line of a wall: a point on the centre line, the direction across the wall, and how far
// the line reaches to each side. key is the distance along the centre line.
struct CutLine {
    Vec2 point;
    Vec2 across;
    double reach = 0.0;
    double key = 0.0;
    bool atVertex = false;
};

// A door or plain opening as a cut through the wall: the keys of its two edges and the height range it opens.
struct WallCut {
    double from = 0.0;
    double to = 0.0;
    double low = 0.0;
    double high = 0.0;
};

struct HeightRange {
    double low = 0.0;
    double high = 0.0;
};

Vec2 flipY(Vec2 p)
{
    return {p.x, -p.y};
}

Clipper2Lib::PathD toPath(const std::vector<Vec2>& ring)
{
    Clipper2Lib::PathD path;
    for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
    return path;
}

std::vector<Vec2> fromPath(const Clipper2Lib::PathD& path)
{
    std::vector<Vec2> ring;
    for (const Clipper2Lib::PointD& p : path) ring.push_back({p.x, p.y});
    return ring;
}

// Twice the signed area of the triangle a, b, c: positive for a left turn.
double cross(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
}

// Drops repeated points and points that lie on a straight line between their neighbours.
std::vector<Vec2> cleanRing(std::vector<Vec2> ring)
{
    bool changed = true;
    while (changed && ring.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[(i + ring.size() - 1) % ring.size()];
            const Vec2 b = ring[i];
            const Vec2 c = ring[(i + 1) % ring.size()];
            const bool same = distance(a, b) < kPointClose;
            const double along = (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y);
            const bool straight = std::fabs(cross(a, b, c)) < 1e-9 && along > 0.0;
            if (same || straight) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return ring;
}

// Makes a ring anticlockwise.
void makeAnticlockwise(std::vector<Vec2>& ring)
{
    if (signedArea(ring) < 0.0) std::reverse(ring.begin(), ring.end());
}

// The solid height ranges of a wall slice from 0 to height, once the openings that cover it are taken out.
std::vector<HeightRange> solidRanges(double height, std::vector<HeightRange> gaps)
{
    std::sort(gaps.begin(), gaps.end(), [](const HeightRange& a, const HeightRange& b) { return a.low < b.low; });
    std::vector<HeightRange> out;
    double at = 0.0;
    for (const HeightRange& gap : gaps) {
        if (gap.low > at + kLevelEps) out.push_back({at, std::min(gap.low, height)});
        at = std::max(at, gap.high);
    }
    if (height > at + kLevelEps) out.push_back({at, height});
    return out;
}

bool sameRanges(const std::vector<HeightRange>& a, const std::vector<HeightRange>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i].low - b[i].low) > kLevelEps || std::fabs(a[i].high - b[i].high) > kLevelEps) return false;
    }
    return true;
}

// The distance along a polyline of the point on it closest to a position.
double keyAlong(const std::vector<Vec2>& line, const std::vector<double>& cumulative, Vec2 position)
{
    double best = 1e300;
    double key = 0.0;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const SegmentProjection hit = projectOntoSegment(position, line[i], line[i + 1]);
        if (hit.distance < best) {
            best = hit.distance;
            key = cumulative[i] + hit.offset;
        }
    }
    return key;
}

// The region between two cross section lines, wide enough to hold the wall band.
std::vector<Vec2> regionBetween(const CutLine& a, const CutLine& b)
{
    return {{a.point.x + a.across.x * a.reach, a.point.y + a.across.y * a.reach},
            {b.point.x + b.across.x * b.reach, b.point.y + b.across.y * b.reach},
            {b.point.x - b.across.x * b.reach, b.point.y - b.across.y * b.reach},
            {a.point.x - a.across.x * a.reach, a.point.y - a.across.y * a.reach}};
}

// Turns a plan ring into an anticlockwise world ring.
std::vector<Vec2> toWorldRing(const std::vector<Vec2>& plan)
{
    std::vector<Vec2> ring;
    for (const Vec2& p : plan) ring.push_back(flipY(p));
    makeAnticlockwise(ring);
    return ring;
}

// Adds the convex pieces of a ring, from bottom to top.
void addPieces(std::vector<CollisionPiece>& out, const std::vector<Vec2>& ring, double bottom, double top)
{
    std::vector<Vec2> clean = cleanRing(ring);
    if (clean.size() < 3 || std::fabs(signedArea(clean)) < 1e-9) return;
    makeAnticlockwise(clean);
    if (isConvexRing(clean)) {
        out.push_back({std::move(clean), bottom, top});
        return;
    }
    WorldPolygon polygon;
    polygon.outline = std::move(clean);
    for (std::vector<Vec2>& piece : convexPieces(polygon)) out.push_back({std::move(piece), bottom, top});
}

// The cross section line at each interior point of the centre line, along the bisector of the two segments.
// startEnd and endEnd are where the band's ends start, which the band's own cross sections are worked out from.
std::vector<CutLine> vertexLines(const std::vector<Vec2>& line, const std::vector<double>& cumulative, double half,
                                 Vec2 startEnd, Vec2 endEnd)
{
    std::vector<Vec2> pts = line;
    pts.front() = startEnd;
    pts.back() = endEnd;
    std::vector<CutLine> out;
    for (std::size_t i = 1; i + 1 < line.size(); ++i) {
        const Vec2 d1 = detail::unit({pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y});
        const Vec2 d2 = detail::unit({pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y});
        const Vec2 n1 = rightOf(d1);
        const Vec2 n2 = rightOf(d2);
        Vec2 mean = detail::unit({n1.x + n2.x, n1.y + n2.y});
        double c = detail::dot(mean, n1);
        if (c < 0.2) {
            mean = n1;
            c = 1.0;
        }
        out.push_back({line[i], mean, half / c + 0.05, cumulative[i], true});
    }
    return out;
}

// The pieces of one wall: its band cut across at every bend of its centre line and at the edges of every door
// and plain opening, each part raised over the heights the openings leave solid.
void wallPieces(const Floor& floor, const FloorBuild& build, const Wall& wall, std::vector<CollisionPiece>& out)
{
    const WallPath path = wallPath(floor, wall);
    const std::vector<std::vector<Vec2>> bands = wallOutline(floor, wall);
    if (!path.curve.valid || path.centreLine.size() < 2 || bands.empty()) return;
    const std::vector<Vec2>& line = path.centreLine;
    const double half = wall.thickness / 2.0;
    const double height = wallHeight(floor, wall);

    std::vector<double> cumulative(line.size(), 0.0);
    for (std::size_t i = 1; i < line.size(); ++i) cumulative[i] = cumulative[i - 1] + distance(line[i - 1], line[i]);
    const double total = cumulative.back();

    const Vec2 startEnd = path.roundedStart ? line.front() : detail::plus(line.front(), tangentAlong(path.curve, 0.0), -half);
    const Vec2 endEnd =
        path.roundedEnd ? line.back() : detail::plus(line.back(), tangentAlong(path.curve, path.curve.length), half);
    std::vector<CutLine> lines = vertexLines(line, cumulative, half, startEnd, endEnd);
    std::vector<WallCut> cuts;
    for (const OpeningBuild& built : build.openings) {
        if (built.wall != wall.id || built.type == OpeningType::Window) continue;
        const Opening* opening = findOpening(floor, built.id);
        if (!opening) continue;
        WallCut cut;
        cut.low = std::max(0.0, built.sill);
        cut.high = built.sill + built.height >= height - kLevelEps ? height : built.sill + built.height;
        double keys[2];
        for (int k = 0; k < 2; ++k) {
            const double sign = k == 0 ? -1.0 : 1.0;
            const double at = std::clamp(opening->offset + sign * opening->width / 2.0, 0.0, path.curve.length);
            const Vec2 point = pointAlong(path.curve, at);
            keys[k] = keyAlong(line, cumulative, point);
            lines.push_back({point, rightOf(tangentAlong(path.curve, at)), half + 0.05, keys[k], false});
        }
        cut.from = std::min(keys[0], keys[1]);
        cut.to = std::max(keys[0], keys[1]);
        cuts.push_back(cut);
    }
    std::stable_sort(lines.begin(), lines.end(), [](const CutLine& a, const CutLine& b) {
        if (std::fabs(a.key - b.key) > 1e-9) return a.key < b.key;
        return a.atVertex && !b.atVertex;
    });

    const double reachOut = 10.0 * half + 1.0;
    const Vec2 startDir = detail::unit({line[1].x - line[0].x, line[1].y - line[0].y});
    const Vec2 endDir = detail::unit({line.back().x - line[line.size() - 2].x, line.back().y - line[line.size() - 2].y});
    std::vector<CutLine> boundaries;
    boundaries.push_back({detail::plus(line.front(), startDir, -reachOut), rightOf(startDir), half + 0.1, -kFarKey, false});
    for (const CutLine& l : lines) {
        if (l.key < kPointClose || total - l.key < kPointClose) continue;
        if (boundaries.size() > 1 && l.key - boundaries.back().key < kPointClose) {
            if (l.atVertex && !boundaries.back().atVertex) boundaries.back() = l;
            continue;
        }
        boundaries.push_back(l);
    }
    boundaries.push_back({detail::plus(line.back(), endDir, reachOut), rightOf(endDir), half + 0.1, kFarKey, false});

    const std::size_t slices = boundaries.size() - 1;
    std::vector<std::vector<HeightRange>> ranges(slices);
    for (std::size_t i = 0; i < slices; ++i) {
        std::vector<HeightRange> gaps;
        for (const WallCut& cut : cuts) {
            if (cut.from <= boundaries[i].key + 1e-6 && cut.to >= boundaries[i + 1].key - 1e-6) {
                gaps.push_back({cut.low, cut.high});
            }
        }
        ranges[i] = solidRanges(height, std::move(gaps));
    }

    Clipper2Lib::PathsD subject;
    for (const std::vector<Vec2>& band : bands) subject.push_back(toPath(band));

    std::size_t first = 0;
    while (first < slices) {
        std::size_t last = first;
        while (last + 1 < slices && !boundaries[last + 1].atVertex && sameRanges(ranges[first], ranges[last + 1])) ++last;
        const Clipper2Lib::PathsD hit = Clipper2Lib::Intersect(
            subject, Clipper2Lib::PathsD{toPath(regionBetween(boundaries[first], boundaries[last + 1]))},
            Clipper2Lib::FillRule::NonZero, kPrecision);
        for (const HeightRange& range : ranges[first]) {
            for (const Clipper2Lib::PathD& piece : hit) {
                addPieces(out, toWorldRing(fromPath(piece)), build.elevation + range.low, build.elevation + range.high);
            }
        }
        first = last + 1;
    }
}

// Adds a slab piece for every convex piece of a polygon.
void slabPieces(std::vector<CollisionPiece>& out, const WorldPolygon& polygon, double bottom, double top)
{
    for (std::vector<Vec2>& ring : convexPieces(polygon)) out.push_back({std::move(ring), bottom, top});
}

// The plane of a roof face as height over x and y, from the face's normal. Not valid for a vertical face.
struct FacePlane {
    bool valid = false;
    Vec3 origin;
    double slopeX = 0.0;
    double slopeY = 0.0;

    double heightAt(double x, double y) const { return origin.z + slopeX * (x - origin.x) + slopeY * (y - origin.y); }
};

FacePlane planeOf(const std::vector<Vec3>& ring)
{
    FacePlane plane;
    if (ring.size() < 3) return plane;
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    Vec3 mean{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % ring.size()];
        nx += (a.y - b.y) * (a.z + b.z);
        ny += (a.z - b.z) * (a.x + b.x);
        nz += (a.x - b.x) * (a.y + b.y);
        mean.x += a.x;
        mean.y += a.y;
        mean.z += a.z;
    }
    if (std::fabs(nz) < 1e-9) return plane;
    const double count = static_cast<double>(ring.size());
    plane.valid = true;
    plane.origin = {mean.x / count, mean.y / count, mean.z / count};
    plane.slopeX = -nx / nz;
    plane.slopeY = -ny / nz;
    return plane;
}

// A face seen from above: its outline and holes in x and y.
WorldPolygon seenFromAbove(const RoofFace& face)
{
    WorldPolygon polygon;
    for (const Vec3& p : face.outline) polygon.outline.push_back({p.x, p.y});
    makeAnticlockwise(polygon.outline);
    for (const std::vector<Vec3>& hole : face.holes) {
        std::vector<Vec2> ring;
        for (const Vec3& p : hole) ring.push_back({p.x, p.y});
        polygon.holes.push_back(std::move(ring));
    }
    return polygon;
}

// Adds the hulls of a covering face and the pieces of a parapet face to a roof's shape.
void roofFaceCollision(CollisionShape& shape, const RoofFace& face, double parapetBottom)
{
    const FacePlane plane = planeOf(face.outline);
    if (!plane.valid) return;
    const WorldPolygon polygon = seenFromAbove(face);
    if (face.part == RoofPart::Covering) {
        for (const std::vector<Vec2>& piece : convexPieces(polygon)) {
            std::vector<Vec3> hull;
            for (const Vec2& p : piece) hull.push_back({p.x, p.y, plane.heightAt(p.x, p.y)});
            for (const Vec2& p : piece) hull.push_back({p.x, p.y, plane.heightAt(p.x, p.y) - kRoofThickness});
            shape.hulls.push_back(std::move(hull));
        }
    } else if (face.part == RoofPart::Parapet && std::fabs(plane.slopeX) < 1e-9 && std::fabs(plane.slopeY) < 1e-9) {
        const double top = plane.origin.z;
        if (top <= parapetBottom + kLevelEps) return;
        slabPieces(shape.pieces, polygon, parapetBottom, top);
    }
}

// The lowest Z of the parapet faces of a roof, or a large number when it has none.
double lowestParapet(const RoofBuild& roof)
{
    double lowest = 1e300;
    for (const RoofFace& face : roof.faces) {
        if (face.part != RoofPart::Parapet) continue;
        for (const Vec3& p : face.outline) lowest = std::min(lowest, p.z);
    }
    return lowest;
}

}

// The name of a collision target kind.
std::string_view collisionTargetKindName(CollisionTargetKind kind)
{
    switch (kind) {
    case CollisionTargetKind::Walls: return "walls";
    case CollisionTargetKind::Floor: return "floor";
    case CollisionTargetKind::Ceiling: return "ceiling";
    case CollisionTargetKind::Thresholds: return "thresholds";
    case CollisionTargetKind::Slab: return "slab";
    case CollisionTargetKind::Stair: return "stair";
    case CollisionTargetKind::Roof: return "roof";
    case CollisionTargetKind::Railing: return "railing";
    case CollisionTargetKind::Guard: return "guard";
    }
    return "walls";
}

// The kind with a name, if there is one.
std::optional<CollisionTargetKind> collisionTargetKindFromName(std::string_view name)
{
    for (const CollisionTargetKind kind : {CollisionTargetKind::Walls, CollisionTargetKind::Floor,
                                           CollisionTargetKind::Ceiling, CollisionTargetKind::Thresholds,
                                           CollisionTargetKind::Slab, CollisionTargetKind::Stair, CollisionTargetKind::Roof,
                                           CollisionTargetKind::Railing, CollisionTargetKind::Guard}) {
        if (collisionTargetKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

// True when the ring is convex and anticlockwise.
bool isConvexRing(const std::vector<Vec2>& ring)
{
    const std::size_t n = ring.size();
    if (n < 3) return false;
    if (!(signedArea(ring) > 0.0)) return false;
    double turning = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % n];
        const Vec2 c = ring[(i + 2) % n];
        const double turn = cross(a, b, c);
        if (turn < -1e-9) return false;
        const double along = (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y);
        turning += std::atan2(turn, along);
    }
    return std::fabs(turning - 2.0 * std::numbers::pi) < 1e-6;
}

// Cuts a polygon with holes into convex pieces.
std::vector<std::vector<Vec2>> convexPieces(const WorldPolygon& polygon)
{
    if (polygon.outline.size() < 3 || std::fabs(signedArea(polygon.outline)) < 1e-12) return {};
    std::vector<const std::vector<Vec2>*> rings{&polygon.outline};
    for (const std::vector<Vec2>& hole : polygon.holes) {
        if (hole.size() >= 3) rings.push_back(&hole);
    }

    std::vector<double> levels;
    for (const std::vector<Vec2>* ring : rings) {
        for (const Vec2& p : *ring) levels.push_back(p.y);
    }
    std::sort(levels.begin(), levels.end());
    std::vector<double> unique;
    for (const double y : levels) {
        if (unique.empty() || y - unique.back() > 1e-9) unique.push_back(y);
    }

    // A column of strips built so far: the points up its left and right sides.
    struct Column {
        std::vector<Vec2> left;
        std::vector<Vec2> right;
        double top = 0.0;
        std::size_t order = 0;
    };
    const auto ringOf = [](const Column& column) {
        std::vector<Vec2> ring = column.right;
        ring.insert(ring.end(), column.left.rbegin(), column.left.rend());
        return cleanRing(std::move(ring));
    };
    struct Crossing {
        double mid;
        double x0;
        double x1;
    };

    std::vector<Column> open;
    std::vector<Column> done;
    std::size_t counter = 0;
    for (std::size_t s = 0; s + 1 < unique.size(); ++s) {
        const double y0 = unique[s];
        const double y1 = unique[s + 1];
        std::vector<Crossing> crossings;
        for (const std::vector<Vec2>* ring : rings) {
            for (std::size_t i = 0; i < ring->size(); ++i) {
                const Vec2 a = (*ring)[i];
                const Vec2 b = (*ring)[(i + 1) % ring->size()];
                if (std::fabs(a.y - b.y) < 1e-12) continue;
                if (std::min(a.y, b.y) > y0 + 1e-9 || std::max(a.y, b.y) < y1 - 1e-9) continue;
                const auto xAt = [&](double y) { return a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y); };
                crossings.push_back({xAt((y0 + y1) / 2.0), xAt(y0), xAt(y1)});
            }
        }
        std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) { return a.mid < b.mid; });

        std::vector<Column> next;
        std::vector<bool> used(open.size(), false);
        for (std::size_t k = 0; k + 1 < crossings.size(); k += 2) {
            const Crossing& l = crossings[k];
            const Crossing& r = crossings[k + 1];
            if (r.mid - l.mid < 1e-12) continue;
            bool merged = false;
            for (std::size_t j = 0; j < open.size() && !merged; ++j) {
                if (used[j]) continue;
                const Column& old = open[j];
                if (std::fabs(old.top - y0) > 1e-9 || std::fabs(old.left.back().x - l.x0) > 1e-9 ||
                    std::fabs(old.right.back().x - r.x0) > 1e-9) {
                    continue;
                }
                Column grown = old;
                grown.left.push_back({l.x1, y1});
                grown.right.push_back({r.x1, y1});
                grown.top = y1;
                if (!isConvexRing(ringOf(grown))) continue;
                used[j] = true;
                merged = true;
                next.push_back(std::move(grown));
            }
            if (merged) continue;
            Column fresh;
            fresh.left = {{l.x0, y0}, {l.x1, y1}};
            fresh.right = {{r.x0, y0}, {r.x1, y1}};
            fresh.top = y1;
            fresh.order = counter++;
            next.push_back(std::move(fresh));
        }
        for (std::size_t j = 0; j < open.size(); ++j) {
            if (!used[j]) done.push_back(std::move(open[j]));
        }
        open = std::move(next);
    }
    for (Column& column : open) done.push_back(std::move(column));
    std::sort(done.begin(), done.end(), [](const Column& a, const Column& b) { return a.order < b.order; });

    std::vector<std::vector<Vec2>> out;
    for (const Column& column : done) {
        std::vector<Vec2> ring = ringOf(column);
        if (ring.size() < 3 || signedArea(ring) < 1e-12) continue;
        out.push_back(std::move(ring));
    }
    return out;
}

// The collision of one floor.
std::vector<CollisionShape> floorCollision(const Floor& floor, const FloorBuild& build)
{
    std::vector<CollisionShape> shapes;

    CollisionShape walls;
    walls.kind = CollisionTargetKind::Walls;
    walls.id = floor.id;
    for (const Wall& wall : floor.walls) wallPieces(floor, build, wall, walls.pieces);
    if (!walls.pieces.empty()) shapes.push_back(std::move(walls));

    const double down = std::min(kCollisionSlabThickness, build.slabBelow.value_or(kCollisionSlabThickness));
    const double up = std::min(kCollisionSlabThickness, floor.slabThickness);

    for (const RoomBuild& room : build.rooms) {
        CollisionShape floorShape;
        floorShape.kind = CollisionTargetKind::Floor;
        floorShape.id = room.id;
        CollisionShape ceilingShape;
        ceilingShape.kind = CollisionTargetKind::Ceiling;
        ceilingShape.id = room.id;
        for (const WorldPolygon& polygon : room.polygons) {
            slabPieces(floorShape.pieces, polygon, room.floorZ - down, room.floorZ);
        }
        for (const WorldPolygon& polygon : room.ceilingPolygons) {
            slabPieces(ceilingShape.pieces, polygon, room.ceilingZ, room.ceilingZ + up);
        }
        if (!floorShape.pieces.empty()) shapes.push_back(std::move(floorShape));
        if (!ceilingShape.pieces.empty()) shapes.push_back(std::move(ceilingShape));
    }

    CollisionShape thresholds;
    thresholds.kind = CollisionTargetKind::Thresholds;
    thresholds.id = floor.id;
    for (const OpeningBuild& opening : build.openings) {
        if (opening.type == OpeningType::Window || opening.threshold.size() < 3) continue;
        WorldPolygon polygon;
        polygon.outline = opening.threshold;
        makeAnticlockwise(polygon.outline);
        slabPieces(thresholds.pieces, polygon, build.elevation - down, build.elevation);
    }
    if (!thresholds.pieces.empty()) shapes.push_back(std::move(thresholds));

    CollisionShape slab;
    slab.kind = CollisionTargetKind::Slab;
    slab.id = floor.id;
    for (const WorldPolygon& polygon : build.slab.polygons) slabPieces(slab.pieces, polygon, build.slab.bottom, build.slab.top);
    if (!slab.pieces.empty()) shapes.push_back(std::move(slab));

    for (const StairBuild& built : build.stairs) {
        CollisionShape stair;
        stair.kind = CollisionTargetKind::Stair;
        stair.id = built.id;
        for (const StairStepBuild& step : built.steps) addPieces(stair.pieces, step.outline, step.bottom, step.top);
        if (!stair.pieces.empty()) shapes.push_back(std::move(stair));
    }

    for (const RoofBuild& built : build.roofs) {
        CollisionShape roof;
        roof.kind = CollisionTargetKind::Roof;
        roof.id = floor.id;
        roof.index = built.index;
        const double parapetBottom = lowestParapet(built);
        for (const RoofFace& face : built.faces) roofFaceCollision(roof, face, parapetBottom);
        if (!roof.pieces.empty() || !roof.hulls.empty()) shapes.push_back(std::move(roof));
    }
    return shapes;
}

}
