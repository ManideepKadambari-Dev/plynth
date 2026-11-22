#include "plynth/core/Kit.h"

#include "BandOutline.h"
#include "ClipperInclude.h"
#include "Fnv.h"

#include "plynth/core/WallGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <optional>
#include <sstream>
#include <tuple>
#include <utility>

namespace plynth {

namespace {

using Clipper2Lib::PathD;
using Clipper2Lib::PathsD;

constexpr int kPrecision = 8;
constexpr double kClose = 1e-6;
constexpr double kAreaEps = 1e-7;
constexpr double kLevelEps = 1e-9;
constexpr double kReach = 200.0;
constexpr double kHairline = 1e-5; // metres: gaps and slivers thinner than twice this are float noise
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Where a piece's own axes sit in the world: its origin and the turn of its X axis about Z.
struct Frame {
    Vec2 origin;
    double theta = 0.0;
};

// A list of points as a Clipper path.
PathD toPath(const std::vector<Vec2>& points)
{
    PathD path;
    path.reserve(points.size());
    for (const Vec2& p : points) path.emplace_back(p.x, p.y);
    return path;
}

// A Clipper path as a list of points.
std::vector<Vec2> fromPath(const PathD& path)
{
    std::vector<Vec2> points;
    points.reserve(path.size());
    for (const Clipper2Lib::PointD& p : path) points.push_back({p.x, p.y});
    return points;
}

// The ring without points that sit on the one before them.
std::vector<Vec2> withoutRepeats(const std::vector<Vec2>& ring)
{
    std::vector<Vec2> out;
    for (const Vec2& p : ring) {
        if (out.empty() || distance(out.back(), p) >= kClose) out.push_back(p);
    }
    while (out.size() > 1 && distance(out.front(), out.back()) < kClose) out.pop_back();
    return out;
}

// A plan point as a world point and back; the two are the same flip.
Vec2 flipped(Vec2 p)
{
    return {p.x, -p.y};
}

// The ring turned to run the wanted way round.
std::vector<Vec2> orientedRing(std::vector<Vec2> ring, bool anticlockwise)
{
    if ((signedArea(ring) > 0.0) != anticlockwise) std::reverse(ring.begin(), ring.end());
    return ring;
}

// An outline with its holes as Clipper paths: the outline anticlockwise, the holes clockwise.
PathsD pathsOf(const std::vector<Vec2>& outline, const std::vector<std::vector<Vec2>>& holes)
{
    PathsD paths;
    paths.push_back(toPath(orientedRing(outline, true)));
    for (const std::vector<Vec2>& hole : holes) paths.push_back(toPath(orientedRing(hole, false)));
    return paths;
}

// Adds paths to the end of a list of paths.
void append(PathsD& to, const PathsD& more)
{
    to.insert(to.end(), more.begin(), more.end());
}

// A number with tiny noise (below 1e-10) turned into zero.
double cleanValue(double v)
{
    return std::fabs(v) < 1e-10 ? 0.0 : v;
}

// A polygon with holes, from a Clipper result.
struct Poly {
    std::vector<Vec2> outline;
    std::vector<std::vector<Vec2>> holes;
};

// Whether most points of a ring are inside another ring.
bool mostlyInside(const std::vector<Vec2>& inner, const std::vector<Vec2>& outer)
{
    std::size_t in = 0;
    for (const Vec2& p : inner) {
        if (insidePolygon(p, outer)) ++in;
    }
    return in * 2 >= inner.size();
}

// Splits a Clipper result into outlines (anticlockwise) with their holes. Outlines smaller than minArea are dropped.
std::vector<Poly> polysFrom(const PathsD& paths, double minArea)
{
    struct Ring {
        std::vector<Vec2> points;
        double area = 0.0;
    };
    std::vector<Ring> outers;
    std::vector<Ring> holes;
    for (const PathD& path : paths) {
        Ring ring;
        ring.points = withoutRepeats(fromPath(path));
        if (ring.points.size() < 3) continue;
        ring.area = signedArea(ring.points);
        if (std::fabs(ring.area) < 1e-10) continue;
        (ring.area > 0.0 ? outers : holes).push_back(std::move(ring));
    }
    std::vector<Poly> polys(outers.size());
    for (std::size_t i = 0; i < outers.size(); ++i) polys[i].outline = outers[i].points;
    for (const Ring& hole : holes) {
        std::size_t owner = outers.size();
        for (std::size_t i = 0; i < outers.size(); ++i) {
            if (!mostlyInside(hole.points, outers[i].points)) continue;
            if (owner == outers.size() || outers[i].area < outers[owner].area) owner = i;
        }
        if (owner < outers.size()) polys[owner].holes.push_back(hole.points);
    }
    std::vector<Poly> kept;
    for (std::size_t i = 0; i < polys.size(); ++i) {
        if (outers[i].area >= minArea) kept.push_back(std::move(polys[i]));
    }
    return kept;
}

// A world point in the coordinates of a frame.
Vec2 toLocal(Vec2 p, const Frame& f)
{
    const double c = std::cos(f.theta);
    const double s = std::sin(f.theta);
    const double dx = p.x - f.origin.x;
    const double dy = p.y - f.origin.y;
    return {cleanValue(c * dx + s * dy), cleanValue(-s * dx + c * dy)};
}

// A world direction in the coordinates of a frame.
Vec2 dirToLocal(Vec2 v, const Frame& f)
{
    const double c = std::cos(f.theta);
    const double s = std::sin(f.theta);
    return {cleanValue(c * v.x + s * v.y), cleanValue(-s * v.x + c * v.y)};
}

// An angle in radians brought into 0 up to a full turn.
double wrappedAngle(double theta)
{
    double a = std::fmod(theta, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    if (a >= kTwoPi - 1e-9) a = 0.0;
    return a;
}

// A side face of a build solid with the look the build gives it.
struct SourceEdge {
    Vec2 a;
    Vec2 b;
    KitEdge edge;
};

// The material the build gives a face.
std::string faceMaterial(const WallEdge& edge, const FloorMaterials& materials)
{
    if (!edge.side) return materials.wallTop;
    for (const WallFaceMaterial& face : materials.wallFaces) {
        if (face.wall == edge.wall && face.room == edge.room) return face.material;
    }
    return {};
}

// The slot the build gives a face.
std::string faceSlot(const WallEdge& edge, const FloorMaterials& materials)
{
    const std::string top = materials.wallTopSlot.empty() ? std::string(kWallTopSlot) : materials.wallTopSlot;
    if (!edge.side) return top;
    for (const WallFaceMaterial& face : materials.wallFaces) {
        if (face.wall == edge.wall && face.room == edge.room) return face.slot;
    }
    return std::string(kExteriorWallSlot);
}

// Every edge of a build solid with its look.
std::vector<SourceEdge> sourceEdges(const WallSolid& solid, const FloorMaterials& materials)
{
    std::vector<SourceEdge> out;
    const auto collect = [&](const WallRing& ring) {
        for (std::size_t i = 0; i < ring.points.size(); ++i) {
            SourceEdge s;
            s.a = ring.points[i];
            s.b = ring.points[(i + 1) % ring.points.size()];
            s.edge.edge = ring.edges[i];
            s.edge.material = faceMaterial(ring.edges[i], materials);
            s.edge.slot = faceSlot(ring.edges[i], materials);
            out.push_back(std::move(s));
        }
    };
    collect(solid.outline);
    for (const WallRing& hole : solid.holes) collect(hole);
    return out;
}

// Whether two collinear faces look the same, so one can take the other's place.
bool sameLook(const KitEdge& a, const KitEdge& b)
{
    return a.slot == b.slot && a.material == b.material && a.cut == b.cut && a.edge.side.has_value() == b.edge.side.has_value();
}

// A ring with the build's face on every edge that lies along one, and a kit cut on every other edge.
KitRing makeKitRing(const std::vector<Vec2>& input, const std::vector<SourceEdge>& source, const KitEdge& cut)
{
    KitRing ring;
    // Every edge is split at the build's own vertices that lie on it, so a face the piece shares only in part
    // (a strip beside a partition) is found even when rounding dropped the vertex that bounded it.
    const std::vector<Vec2> cleaned = withoutRepeats(input);
    for (std::size_t i = 0; i < cleaned.size(); ++i) {
        const Vec2 a = cleaned[i];
        const Vec2 b = cleaned[(i + 1) % cleaned.size()];
        ring.points.push_back(a);
        const double length = distance(a, b);
        std::vector<std::pair<double, Vec2>> on;
        for (const SourceEdge& s : source) {
            const SegmentProjection hit = projectOntoSegment(s.a, a, b);
            if (hit.distance < kClose && hit.offset > kClose && hit.offset < length - kClose) on.emplace_back(hit.offset, s.a);
        }
        std::sort(on.begin(), on.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        double last = 0.0;
        for (const auto& [offset, v] : on) {
            if (offset - last < kClose) continue;
            ring.points.push_back(v);
            last = offset;
        }
    }
    const std::size_t n = ring.points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = ring.points[i];
        const Vec2 b = ring.points[(i + 1) % n];
        const KitEdge* found = nullptr;
        for (const SourceEdge& s : source) {
            if (projectOntoSegment(a, s.a, s.b).distance > kClose || projectOntoSegment(b, s.a, s.b).distance > kClose) continue;
            if ((b.x - a.x) * (s.b.x - s.a.x) + (b.y - a.y) * (s.b.y - s.a.y) <= 0.0) continue;
            found = &s.edge;
            break;
        }
        ring.edges.push_back(found ? *found : cut);
    }
    bool changed = true;
    while (changed && ring.points.size() > 3) {
        changed = false;
        const std::size_t count = ring.points.size();
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t prev = (i + count - 1) % count;
            const std::size_t next = (i + 1) % count;
            const Vec2 d1{ring.points[i].x - ring.points[prev].x, ring.points[i].y - ring.points[prev].y};
            const Vec2 d2{ring.points[next].x - ring.points[i].x, ring.points[next].y - ring.points[i].y};
            // On the straight line between its neighbours, by distance, so a very short edge still counts.
            const bool onLine = projectOntoSegment(ring.points[i], ring.points[prev], ring.points[next]).distance < kClose;
            if (onLine && detail::dot(d1, d2) > 0.0 && sameLook(ring.edges[prev], ring.edges[i])) {
                ring.points.erase(ring.points.begin() + static_cast<std::ptrdiff_t>(i));
                ring.edges.erase(ring.edges.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return ring;
}

// A kit solid from a cut polygon: its height, its top look and a face on every edge.
KitSolid makeKitSolid(const Poly& poly, const WallSolid& source, const std::vector<SourceEdge>& edges,
                      const FloorMaterials& materials)
{
    KitSolid solid;
    solid.height = source.height;
    solid.topMaterial = materials.wallTop;
    solid.topSlot = materials.wallTopSlot.empty() ? std::string(kWallTopSlot) : materials.wallTopSlot;
    KitEdge cut;
    cut.material = solid.topMaterial;
    cut.slot = solid.topSlot;
    cut.cut = true;
    solid.outline = makeKitRing(poly.outline, edges, cut);
    for (const std::vector<Vec2>& hole : poly.holes) solid.holes.push_back(makeKitRing(hole, edges, cut));
    return solid;
}

// A build solid as Clipper paths.
PathsD pathsOf(const WallSolid& solid)
{
    std::vector<std::vector<Vec2>> holes;
    for (const WallRing& h : solid.holes) holes.push_back(h.points);
    return pathsOf(solid.outline.points, holes);
}

// A kit solid as Clipper paths.
PathsD pathsOf(const KitSolid& solid)
{
    std::vector<std::vector<Vec2>> holes;
    for (const KitRing& h : solid.holes) holes.push_back(h.points);
    return pathsOf(solid.outline.points, holes);
}

// Several kit solids as Clipper paths.
PathsD pathsOf(const std::vector<KitSolid>& solids)
{
    PathsD all;
    for (const KitSolid& s : solids) append(all, pathsOf(s));
    return all;
}

// The ground the cut of an opening covers, in the frame of its cutter, anticlockwise. Empty when the opening has
// no usable cutter.
std::vector<Vec2> footprintOf(const OpeningBuild& o)
{
    const std::size_t n = std::min(o.cutter.right.size(), o.cutter.left.size());
    struct Pair {
        double key = 0.0;
        Vec2 right;
        Vec2 left;
    };
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 r{o.cutter.right[i].x, o.cutter.right[i].y};
        const Vec2 l{o.cutter.left[i].x, o.cutter.left[i].y};
        bool known = false;
        for (const Pair& p : pairs) {
            if (distance(p.right, r) < 1e-9 && distance(p.left, l) < 1e-9) known = true;
        }
        if (known) continue;
        pairs.push_back({detail::dot({r.x - o.centre.x, r.y - o.centre.y}, o.tangent), r, l});
    }
    if (pairs.size() < 2) return {};
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.key < b.key; });
    std::vector<Vec2> ring;
    for (const Pair& p : pairs) ring.push_back(p.right);
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it) ring.push_back(it->left);
    ring = withoutRepeats(ring);
    if (ring.size() < 3 || std::fabs(signedArea(ring)) < 1e-9) return {};
    return orientedRing(std::move(ring), true);
}

// One opening of the floor as the kit sees it, in world coordinates.
struct OpeningInfo {
    OpeningBuild build;
    OpeningMaterialIds materials;
    std::vector<Vec2> footprint;
};

// A wall of the floor: its drawn centre line in world coordinates, measured along from its from point.
struct WallInfo {
    const Wall* wall = nullptr;
    std::vector<Vec2> line;
    std::vector<double> cumulative;
    double length = 0.0;
    double half = 0.0;
    PathsD bands;           // world, anticlockwise
    bool straight = true;   // two points: no bend, no rounded end
    bool reversed = false;  // drawn from its higher end: grid stations count from the other end
    std::vector<std::pair<double, double>> junctions;
};

// The distance along a wall of the place on its centre line closest to a position.
double keyAlong(const WallInfo& w, Vec2 position)
{
    double best = 1e300;
    double key = 0.0;
    for (std::size_t i = 0; i + 1 < w.line.size(); ++i) {
        const SegmentProjection hit = projectOntoSegment(position, w.line[i], w.line[i + 1]);
        if (hit.distance < best) {
            best = hit.distance;
            key = w.cumulative[i] + hit.offset;
        }
    }
    return key;
}

// The segment of a wall that holds a distance along it.
std::size_t segmentAt(const WallInfo& w, double s)
{
    std::size_t i = 0;
    while (i + 2 < w.line.size() && s > w.cumulative[i + 1]) ++i;
    return i;
}

// The place on a wall at a distance along it.
Vec2 pointAt(const WallInfo& w, double s)
{
    s = std::clamp(s, 0.0, w.length);
    const std::size_t i = segmentAt(w, s);
    const double seg = w.cumulative[i + 1] - w.cumulative[i];
    const double k = seg > 0.0 ? (s - w.cumulative[i]) / seg : 0.0;
    return {w.line[i].x + (w.line[i + 1].x - w.line[i].x) * k, w.line[i].y + (w.line[i + 1].y - w.line[i].y) * k};
}

// The unit direction of one segment of a wall.
Vec2 segmentDirection(const WallInfo& w, std::size_t i)
{
    return detail::unit({w.line[i + 1].x - w.line[i].x, w.line[i + 1].y - w.line[i].y});
}

// The direction of travel along a wall; at a bend, the mean of the two segments.
Vec2 tangentAt(const WallInfo& w, double s)
{
    s = std::clamp(s, 0.0, w.length);
    const std::size_t i = segmentAt(w, s);
    if (i > 0 && s - w.cumulative[i] < 1e-9) {
        const Vec2 a = segmentDirection(w, i - 1);
        const Vec2 b = segmentDirection(w, i);
        return detail::unit({a.x + b.x, a.y + b.y});
    }
    if (i + 2 < w.line.size() && w.cumulative[i + 1] - s < 1e-9) {
        const Vec2 a = segmentDirection(w, i);
        const Vec2 b = segmentDirection(w, i + 1);
        return detail::unit({a.x + b.x, a.y + b.y});
    }
    return segmentDirection(w, i);
}

// Everything about one floor that the cutting needs.
struct FloorCtx {
    const Floor* floor = nullptr;
    const FloorBuild* build = nullptr;
    FloorMaterials materials;
    double grid = 0.5;
    std::vector<OpeningInfo> openings;
    std::vector<WallInfo> walls;
    std::vector<std::vector<SourceEdge>> edges; // of each wall solid
};

// Where the band of another wall overlaps a wall's band, seen along the wall: its junction parts (rule 1).
std::vector<std::pair<double, double>> junctionParts(const std::vector<WallInfo>& walls, std::size_t index)
{
    const WallInfo& w = walls[index];
    std::vector<std::pair<double, double>> raw;
    for (std::size_t j = 0; j < walls.size(); ++j) {
        if (j == index) continue;
        const PathsD hit = Clipper2Lib::Intersect(w.bands, walls[j].bands, Clipper2Lib::FillRule::NonZero, kPrecision);
        for (const PathD& path : hit) {
            // Bands that only touch along a diagonal edge leave slivers of float noise: not a junction.
            if (std::fabs(Clipper2Lib::Area(path)) < 1e-6) continue;
            double lo = 1e300;
            double hi = -1e300;
            for (const Clipper2Lib::PointD& p : path) {
                const double key = keyAlong(w, {p.x, p.y});
                lo = std::min(lo, key);
                hi = std::max(hi, key);
            }
            if (hi - lo < 1e-9) continue;
            raw.emplace_back(lo, hi);
        }
    }
    std::sort(raw.begin(), raw.end());
    std::vector<std::pair<double, double>> merged;
    for (const auto& part : raw) {
        if (!merged.empty() && part.first <= merged.back().second + kKitMinPieceLength) {
            merged.back().second = std::max(merged.back().second, part.second);
        } else {
            merged.push_back(part);
        }
    }
    return merged;
}

// Whether a distance lies inside any of the parts, with some slack.
bool insideAny(const std::vector<std::pair<double, double>>& parts, double s, double slack)
{
    for (const auto& part : parts) {
        if (s > part.first - slack && s < part.second + slack) return true;
    }
    return false;
}

// The stations of a straight wall (rule 2).
std::vector<double> stationsOf(const FloorCtx& ctx, const WallInfo& w)
{
    const double length = w.length;
    std::vector<double> forced{0.0, length};
    for (const auto& part : w.junctions) {
        forced.push_back(part.first);
        forced.push_back(part.second);
    }
    std::sort(forced.begin(), forced.end());
    std::vector<double> unique;
    for (const double s : forced) {
        if (unique.empty() || s - unique.back() > 1e-9) unique.push_back(s);
    }
    forced = std::move(unique);

    std::vector<std::pair<double, double>> gaps;
    for (const OpeningInfo& o : ctx.openings) {
        if (o.build.wall != w.wall->id) continue;
        const double c = keyAlong(w, o.build.centre);
        gaps.emplace_back(c - o.build.width / 2.0 - kKitOpeningMargin, c + o.build.width / 2.0 + kKitOpeningMargin);
    }

    std::vector<double> grid;
    for (int k = 1; ctx.grid > 0.0 && k * ctx.grid < length - 1e-9; ++k) {
        const double s = w.reversed ? length - k * ctx.grid : k * ctx.grid;
        if (s <= 1e-9 || s >= length - 1e-9) continue;
        if (insideAny(w.junctions, s, -1e-9)) continue;
        if (insideAny(gaps, s, -1e-9)) continue;
        bool nearForced = false;
        for (const double f : forced) {
            if (std::fabs(f - s) < kKitMinPieceLength) nearForced = true;
        }
        if (nearForced) continue;
        grid.push_back(s);
    }
    std::sort(grid.begin(), grid.end());
    std::vector<double> kept;
    for (const double s : grid) {
        if (!kept.empty() && s - kept.back() < kKitMinPieceLength) continue;
        kept.push_back(s);
    }
    std::vector<double> all = forced;
    all.insert(all.end(), kept.begin(), kept.end());
    std::sort(all.begin(), all.end());
    return all;
}

// A half plane as a big quad: the side of the line through p that the direction t points to.
std::vector<Vec2> halfPlane(Vec2 p, Vec2 t)
{
    const Vec2 n{-t.y, t.x};
    return orientedRing({{p.x + n.x * kReach, p.y + n.y * kReach},
                         {p.x + n.x * kReach + t.x * kReach, p.y + n.y * kReach + t.y * kReach},
                         {p.x - n.x * kReach + t.x * kReach, p.y - n.y * kReach + t.y * kReach},
                         {p.x - n.x * kReach, p.y - n.y * kReach}},
                        true);
}

// The part of a wall's band between two cuts across it. A missing end reaches past the wall's end.
PathsD regionBetween(const WallInfo& w, std::optional<double> from, std::optional<double> to)
{
    PathsD region = w.bands;
    if (from) {
        const Vec2 t = tangentAt(w, *from);
        region = Clipper2Lib::Intersect(region, PathsD{toPath(halfPlane(pointAt(w, *from), t))},
                                        Clipper2Lib::FillRule::NonZero, kPrecision);
    }
    if (to) {
        const Vec2 t = tangentAt(w, *to);
        region = Clipper2Lib::Intersect(region, PathsD{toPath(halfPlane(pointAt(w, *to), {-t.x, -t.y}))},
                                        Clipper2Lib::FillRule::NonZero, kPrecision);
    }
    return region;
}

// One wall piece before it is turned into a kit piece: the stations that bound it and the area it takes.
struct Region {
    KitPieceKind kind = KitPieceKind::Wall;
    std::optional<double> from;
    std::optional<double> to;
    double a = 0.0;
    double b = 0.0;
    PathsD polygon;
};

// The wall pieces of one wall, cut at its stations (rules 2 to 4).
std::vector<Region> regionsOf(const FloorCtx& ctx, const WallInfo& w)
{
    std::vector<Region> out;
    const auto add = [&](double a, double b, KitPieceKind kind) {
        Region r;
        r.kind = kind;
        r.a = a;
        r.b = b;
        if (a > 1e-9) r.from = a;
        if (b < w.length - 1e-9) r.to = b;
        r.polygon = regionBetween(w, r.from, r.to);
        out.push_back(std::move(r));
    };
    if (w.straight) {
        const std::vector<double> stations = stationsOf(ctx, w);
        for (std::size_t i = 0; i + 1 < stations.size(); ++i) {
            const double a = stations[i];
            const double b = stations[i + 1];
            if (b - a < 1e-9) continue;
            if (insideAny(w.junctions, (a + b) / 2.0, 1e-9)) continue;
            add(a, b, KitPieceKind::Wall);
        }
        return out;
    }
    double a = 0.0;
    double b = w.length;
    if (!w.junctions.empty() && w.junctions.front().first < 1e-9) a = w.junctions.front().second;
    if (!w.junctions.empty() && w.junctions.back().second > w.length - 1e-9) b = w.junctions.back().first;
    if (b - a > 1e-6) add(a, b, KitPieceKind::Curved);
    return out;
}

// A piece being worked out, in world coordinates, before its frame is chosen.
struct OpeningRef {
    std::size_t index = 0;
    bool fill = false;
};

struct Cand {
    KitPieceKind kind = KitPieceKind::Wall;
    Id wall;
    Id point;
    std::vector<KitSolid> solids; // world
    std::vector<OpeningRef> openings;
    std::vector<Frame> frames;    // the frames a piece may take, tried in order
    bool halfTurn = false;        // also try the frame turned half a turn about the middle of its length
    Id sortWall;
    Vec2 sortAt;
};

// The shortest distance from a point to the edges of a ring.
double boundaryDistance(Vec2 p, const std::vector<Vec2>& ring)
{
    double best = 1e300;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        best = std::min(best, projectOntoSegment(p, ring[i], ring[(i + 1) % ring.size()]).distance);
    }
    return best;
}

// Whether a point lies in the solids, not in a hole.
bool holdsPoint(const std::vector<KitSolid>& solids, Vec2 p)
{
    for (const KitSolid& s : solids) {
        if (detail::distanceToPolygon(p, s.outline.points) > 1e-7) continue;
        bool inHole = false;
        for (const KitRing& h : s.holes) {
            if (h.points.size() >= 3 && insidePolygon(p, h.points) && boundaryDistance(p, h.points) > 1e-7) inHole = true;
        }
        if (!inHole) return true;
    }
    return false;
}

// The openings whose cut reaches into the area of the solids (rule 7).
std::vector<std::size_t> reachedOpenings(const FloorCtx& ctx, const std::vector<KitSolid>& solids)
{
    const PathsD area = pathsOf(solids);
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < ctx.openings.size(); ++i) {
        if (ctx.openings[i].footprint.size() < 3) continue;
        const PathsD hit = Clipper2Lib::Intersect(area, PathsD{toPath(ctx.openings[i].footprint)},
                                                  Clipper2Lib::FillRule::NonZero, kPrecision);
        if (Clipper2Lib::Area(hit) > kAreaEps) out.push_back(i);
    }
    return out;
}

// The solids of the floor cut to a region, with their faces.
std::vector<KitSolid> solidsIn(const FloorCtx& ctx, const PathsD& region)
{
    std::vector<KitSolid> out;
    for (std::size_t si = 0; si < ctx.build->wallSolids.size(); ++si) {
        const WallSolid& source = ctx.build->wallSolids[si];
        const PathsD hit = Clipper2Lib::Intersect(pathsOf(source), region, Clipper2Lib::FillRule::NonZero, kPrecision);
        for (const Poly& poly : polysFrom(hit, kAreaEps)) out.push_back(makeKitSolid(poly, source, ctx.edges[si], ctx.materials));
    }
    return out;
}

// The middle of the area of some solids.
Vec2 centroidOf(const std::vector<KitSolid>& solids)
{
    double total = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double minX = 1e300;
    double maxX = -1e300;
    double minY = 1e300;
    double maxY = -1e300;
    const auto add = [&](const KitRing& ring, bool outline) {
        const std::size_t n = ring.points.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = ring.points[i];
            const Vec2 b = ring.points[(i + 1) % n];
            const double c = a.x * b.y - b.x * a.y;
            total += c / 2.0;
            cx += (a.x + b.x) * c;
            cy += (a.y + b.y) * c;
            if (outline) {
                minX = std::min(minX, a.x);
                maxX = std::max(maxX, a.x);
                minY = std::min(minY, a.y);
                maxY = std::max(maxY, a.y);
            }
        }
    };
    for (const KitSolid& s : solids) {
        add(s.outline, true);
        for (const KitRing& h : s.holes) add(h, false);
    }
    if (std::fabs(total) < 1e-12) return {(minX + maxX) / 2.0, (minY + maxY) / 2.0};
    return {cx / (6.0 * total), cy / (6.0 * total)};
}

// A wall leaving a plan point: its id and the unit direction in world coordinates.
struct Arm {
    Id wall;
    Vec2 direction;
};

// The walls that leave a plan point, in wall id order.
std::vector<Arm> armsAt(const FloorCtx& ctx, const Point& point)
{
    std::vector<Arm> arms;
    for (const WallInfo& w : ctx.walls) {
        const std::size_t n = w.line.size();
        if (w.wall->from == point.id) arms.push_back({w.wall->id, segmentDirection(w, 0)});
        if (w.wall->to == point.id) {
            arms.push_back({w.wall->id, detail::unit({w.line[n - 2].x - w.line[n - 1].x, w.line[n - 2].y - w.line[n - 1].y})});
        }
    }
    std::stable_sort(arms.begin(), arms.end(), [](const Arm& a, const Arm& b) { return a.wall < b.wall; });
    return arms;
}

// Corner pieces: what is left of the solids once the wall pieces are taken out, split into parts that do not
// touch (rule 5).
std::vector<Cand> cornerCands(const FloorCtx& ctx, const PathsD& taken)
{
    struct Rem {
        std::size_t solid = 0;
        Poly poly;
    };
    std::vector<Rem> rems;
    for (std::size_t si = 0; si < ctx.build->wallSolids.size(); ++si) {
        PathsD left = taken.empty() ? pathsOf(ctx.build->wallSolids[si])
                                    : Clipper2Lib::Difference(pathsOf(ctx.build->wallSolids[si]), taken,
                                                              Clipper2Lib::FillRule::NonZero, kPrecision);
        if (!taken.empty()) {
            // Pieces cut along a diagonal leave hairline gaps between them. They join the real corner parts into
            // long thin tails, so thin channels are removed: shrink a little, then grow back with sharp corners.
            // The opened shape only decides what stays; the solid itself is clipped to it, so its own vertices are kept.
            PathsD opened = Clipper2Lib::InflatePaths(left, -kHairline, Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon,
                                                      10.0, kPrecision);
            opened = Clipper2Lib::InflatePaths(opened, kHairline, Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon, 10.0,
                                               kPrecision);
            left = Clipper2Lib::Intersect(left, opened, Clipper2Lib::FillRule::NonZero, kPrecision);
        }
        for (Poly& poly : polysFrom(left, kAreaEps)) rems.push_back({si, std::move(poly)});
    }

    std::vector<std::size_t> group(rems.size());
    for (std::size_t i = 0; i < group.size(); ++i) group[i] = i;
    const auto root = [&group](std::size_t i) {
        while (group[i] != i) i = group[i];
        return i;
    };
    for (std::size_t i = 0; i < rems.size(); ++i) {
        for (std::size_t j = i + 1; j < rems.size(); ++j) {
            if (root(i) == root(j)) continue;
            const PathsD a = pathsOf(rems[i].poly.outline, {});
            const PathsD b = pathsOf(rems[j].poly.outline, {});
            const PathsD both = Clipper2Lib::Union(a, b, Clipper2Lib::FillRule::NonZero, kPrecision);
            int outers = 0;
            for (const PathD& path : both) {
                if (Clipper2Lib::Area(path) > 0.0) ++outers;
            }
            if (outers == 1) group[root(j)] = root(i);
        }
    }

    std::vector<Cand> out;
    std::vector<bool> used(rems.size(), false);
    for (std::size_t i = 0; i < rems.size(); ++i) {
        if (used[i]) continue;
        Cand c;
        c.kind = KitPieceKind::Corner;
        for (std::size_t j = i; j < rems.size(); ++j) {
            if (used[j] || root(j) != root(i)) continue;
            used[j] = true;
            const WallSolid& source = ctx.build->wallSolids[rems[j].solid];
            c.solids.push_back(makeKitSolid(rems[j].poly, source, ctx.edges[rems[j].solid], ctx.materials));
        }
        const Vec2 centre = centroidOf(c.solids);
        double best = 1e300;
        const Point* chosen = nullptr;
        std::vector<Arm> chosenArms;
        for (const Point& point : ctx.floor->points) {
            std::vector<Arm> arms = armsAt(ctx, point);
            if (arms.empty()) continue;
            const Vec2 at = planToWorld({point.x, point.y});
            if (!holdsPoint(c.solids, at)) continue;
            const double d = distance(at, centre);
            if (d < best) {
                best = d;
                chosen = &point;
                chosenArms = std::move(arms);
            }
        }
        if (chosen) {
            const Vec2 at = planToWorld({chosen->x, chosen->y});
            c.point = chosen->id;
            for (const Arm& arm : chosenArms) c.frames.push_back({at, std::atan2(arm.direction.y, arm.direction.x)});
            c.sortWall = chosenArms.front().wall;
            c.sortAt = at;
        } else {
            c.frames.push_back({centre, 0.0});
            for (const KitSolid& s : c.solids) {
                for (const KitEdge& e : s.outline.edges) {
                    if (!e.cut && !e.edge.wall.empty() && (c.sortWall.empty() || e.edge.wall < c.sortWall)) c.sortWall = e.edge.wall;
                }
            }
            c.sortAt = centre;
        }
        out.push_back(std::move(c));
    }
    std::stable_sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) {
        if (a.sortWall != b.sortWall) return a.sortWall < b.sortWall;
        if (a.sortAt.x != b.sortAt.x) return a.sortAt.x < b.sortAt.x;
        return a.sortAt.y < b.sortAt.y;
    });
    return out;
}

// A length in whole units of the given size (in tenths of a micrometre), rounded half up. The value is first
// snapped to a tenth of a micrometre with integers, so float noise cannot tip a value that sits on a half unit
// (a 0.125 m wall has faces at 62.5 mm and a thickness of 12.5 cm) into the other bin.
long long inUnits(double v, long long unit)
{
    const long long tenthMicrometres = std::llround(v * 1e7);
    long long whole = tenthMicrometres / unit;
    if (tenthMicrometres % unit < 0) --whole;
    const long long rest = tenthMicrometres - whole * unit;
    return rest >= unit / 2 ? whole + 1 : whole;
}

// Millimetres, for comparing and hashing.
long long q(double v)
{
    return inUnits(v, 10000);
}

// Whole centimetres, for the name of a piece.
long long centimetres(double v)
{
    return inUnits(v, 100000);
}

// A point in millimetres, for putting points in order.
std::tuple<long long, long long> lowKey(const Vec2& p)
{
    return {q(p.x), q(p.y)};
}

// Starts a ring at its lowest point.
void rotateToLowest(KitRing& r)
{
    if (r.points.empty()) return;
    std::size_t best = 0;
    for (std::size_t i = 1; i < r.points.size(); ++i) {
        if (lowKey(r.points[i]) < lowKey(r.points[best])) best = i;
    }
    std::rotate(r.points.begin(), r.points.begin() + static_cast<std::ptrdiff_t>(best), r.points.end());
    std::rotate(r.edges.begin(), r.edges.begin() + static_cast<std::ptrdiff_t>(best), r.edges.end());
}

// Puts the parts of a piece in a fixed order, so equal pieces list equal data.
void normalize(KitPiece& p)
{
    for (KitSolid& s : p.solids) {
        rotateToLowest(s.outline);
        for (KitRing& h : s.holes) rotateToLowest(h);
        std::sort(s.holes.begin(), s.holes.end(),
                  [](const KitRing& a, const KitRing& b) { return lowKey(a.points.front()) < lowKey(b.points.front()); });
    }
    std::sort(p.solids.begin(), p.solids.end(), [](const KitSolid& a, const KitSolid& b) {
        if (q(a.height) != q(b.height)) return q(a.height) > q(b.height);
        return lowKey(a.outline.points.front()) < lowKey(b.outline.points.front());
    });
    std::sort(p.openings.begin(), p.openings.end(), [](const KitOpening& a, const KitOpening& b) {
        const auto x = lowKey(a.opening.centre);
        const auto y = lowKey(b.opening.centre);
        if (x != y) return x < y;
        return a.opening.id < b.opening.id;
    });
}

// Works out the length, thickness and height of a piece.
void measure(KitPiece& p)
{
    double minX = 1e300;
    double maxX = -1e300;
    double minY = 1e300;
    double maxY = -1e300;
    double height = 0.0;
    for (const KitSolid& s : p.solids) {
        height = std::max(height, s.height);
        for (const Vec2& pt : s.outline.points) {
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y);
            maxY = std::max(maxY, pt.y);
        }
    }
    p.length = maxX - minX;
    p.thickness = maxY - minY;
    p.height = height;
}

// A ring in the coordinates of a frame.
KitRing ringInFrame(const KitRing& ring, const Frame& f)
{
    KitRing out;
    out.edges = ring.edges;
    for (const Vec2& p : ring.points) out.points.push_back(toLocal(p, f));
    return out;
}

// The candidate in the coordinates of a frame.
KitPiece localPiece(const Cand& c, const FloorCtx& ctx, const Frame& f)
{
    KitPiece p;
    p.kind = c.kind;
    if (c.kind == KitPieceKind::Wall && !c.openings.empty()) p.kind = KitPieceKind::Opening;
    for (const KitSolid& s : c.solids) {
        KitSolid local;
        local.height = s.height;
        local.topMaterial = s.topMaterial;
        local.topSlot = s.topSlot;
        local.outline = ringInFrame(s.outline, f);
        for (const KitRing& h : s.holes) local.holes.push_back(ringInFrame(h, f));
        p.solids.push_back(std::move(local));
    }
    for (const OpeningRef& ref : c.openings) {
        const OpeningInfo& info = ctx.openings[ref.index];
        KitOpening o;
        o.opening = info.build;
        o.opening.centre = toLocal(info.build.centre, f);
        o.opening.tangent = dirToLocal(info.build.tangent, f);
        o.opening.right = dirToLocal(info.build.right, f);
        o.opening.threshold.clear();
        for (std::vector<Vec3>* side : {&o.opening.cutter.right, &o.opening.cutter.left}) {
            for (Vec3& pt : *side) {
                const Vec2 l = toLocal({pt.x, pt.y}, f);
                pt = {l.x, l.y, cleanValue(pt.z - ctx.build->elevation)};
            }
        }
        o.materials = info.materials;
        o.fill = ref.fill;
        p.openings.push_back(std::move(o));
    }
    normalize(p);
    measure(p);
    return p;
}

// What an opening is, with no regard to which way along the wall its own box runs. The same door seen from
// the other end of its wall has its box turned round, its hinge and swing named the other way and its cutter
// points in the other order; this puts all of that in the piece's own terms.
struct OpeningKey {
    int hingeSide = 0;  // which way along the opening's own direction the hinge jamb lies
    int swingSide = 0;  // which way the leaf opens, by the main direction of the opening's right side
    std::vector<Vec3> cutter;   // every cutter point, in a fixed order
    std::vector<Vec2> outline;  // the outline with u counted along the opening's main direction, in a fixed order
};

// The key of an opening in a piece's coordinates.
OpeningKey keyOf(const OpeningBuild& b)
{
    OpeningKey k;
    // At 45 degrees the two axes tie, so X wins unless Y is larger by more than float noise.
    const auto sideOf = [](Vec2 v) { return (std::fabs(v.y) > std::fabs(v.x) + 1e-9 ? v.y : v.x) >= 0.0 ? 1 : -1; };
    const double along = sideOf(b.tangent);
    k.hingeSide = (b.hinge == HingeEnd::Start ? -1 : 1) * static_cast<int>(along);
    k.swingSide = (b.swing == WallSide::Right ? 1 : -1) * sideOf(b.right);
    k.cutter = b.cutter.right;
    k.cutter.insert(k.cutter.end(), b.cutter.left.begin(), b.cutter.left.end());
    std::sort(k.cutter.begin(), k.cutter.end(), [](const Vec3& a, const Vec3& c) {
        return std::make_tuple(q(a.x), q(a.y), q(a.z)) < std::make_tuple(q(c.x), q(c.y), q(c.z));
    });
    for (const Vec2& uv : b.outline) k.outline.push_back({along * uv.x, uv.y});
    std::sort(k.outline.begin(), k.outline.end(), [](const Vec2& a, const Vec2& c) { return lowKey(a) < lowKey(c); });
    return k;
}

// All the data of a piece that makes it the same as another, to the millimetre, as text.
std::string serialize(const KitPiece& p)
{
    std::ostringstream o;
    o << static_cast<int>(p.kind) << '|';
    const auto ring = [&](const KitRing& r) {
        for (std::size_t i = 0; i < r.points.size(); ++i) {
            const KitEdge& e = r.edges[i];
            o << q(r.points[i].x) << ',' << q(r.points[i].y) << ',' << e.slot << ',' << e.material << ',' << e.cut << ','
              << e.edge.side.has_value() << ';';
        }
    };
    for (const KitSolid& s : p.solids) {
        o << 'S' << q(s.height) << ',' << s.topSlot << ',' << s.topMaterial << '{';
        ring(s.outline);
        for (const KitRing& h : s.holes) {
            o << 'H';
            ring(h);
        }
        o << '}';
    }
    for (const KitOpening& k : p.openings) {
        const OpeningBuild& b = k.opening;
        const OpeningMaterialIds& m = k.materials;
        const OpeningKey key = keyOf(b);
        o << 'O' << static_cast<int>(b.type) << ',' << b.shape << ',' << q(b.width) << ',' << q(b.height) << ',' << q(b.sill) << ','
          << key.hingeSide << ',' << key.swingSide << ',' << q(b.centre.x) << ',' << q(b.centre.y) << ',' << q(b.thickness) << ','
          << b.curved << ',' << k.fill << ',' << m.reveal << ',' << m.threshold << ',' << m.door << ',' << m.frame << ',' << m.glass
          << ',' << m.handle << ',' << m.revealSlot << ',' << m.thresholdSlot << ',' << m.doorSlot << ',' << m.frameSlot << ','
          << m.glassSlot << ',' << m.handleSlot << '[';
        for (const Vec3& pt : key.cutter) o << q(pt.x) << ',' << q(pt.y) << ',' << q(pt.z) << ';';
        o << '/';
        for (const Vec2& pt : key.outline) o << q(pt.x) << ',' << q(pt.y) << ';';
        o << ']';
    }
    return o.str();
}

// Whether two numbers differ by no more than kKitTolerance.
bool near(double a, double b)
{
    return std::fabs(a - b) <= kKitTolerance;
}

// Whether two points are within kKitTolerance in both directions.
bool nearV(Vec2 a, Vec2 b)
{
    return near(a.x, b.x) && near(a.y, b.y);
}

// Whether two rings have the same points and faces within kKitTolerance.
bool sameRing(const KitRing& a, const KitRing& b)
{
    if (a.points.size() != b.points.size()) return false;
    for (std::size_t i = 0; i < a.points.size(); ++i) {
        if (!nearV(a.points[i], b.points[i]) || !sameLook(a.edges[i], b.edges[i])) return false;
    }
    return true;
}

// Whether two lists of 3D points match one by one within kKitTolerance.
bool sameCutter(const std::vector<Vec3>& a, const std::vector<Vec3>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!near(a[i].x, b[i].x) || !near(a[i].y, b[i].y) || !near(a[i].z, b[i].z)) return false;
    }
    return true;
}

// Whether two openings of pieces are the same within kKitTolerance.
bool sameOpening(const KitOpening& a, const KitOpening& b)
{
    const OpeningBuild& x = a.opening;
    const OpeningBuild& y = b.opening;
    if (x.type != y.type || x.shape != y.shape || x.curved != y.curved || a.fill != b.fill) return false;
    if (!near(x.width, y.width) || !near(x.height, y.height) || !near(x.sill, y.sill) || !near(x.thickness, y.thickness)) return false;
    if (!nearV(x.centre, y.centre)) return false;
    const OpeningKey kx = keyOf(x);
    const OpeningKey ky = keyOf(y);
    if (kx.hingeSide != ky.hingeSide || kx.swingSide != ky.swingSide || !sameCutter(kx.cutter, ky.cutter)) return false;
    if (kx.outline.size() != ky.outline.size()) return false;
    for (std::size_t i = 0; i < kx.outline.size(); ++i) {
        if (!nearV(kx.outline[i], ky.outline[i])) return false;
    }
    const OpeningMaterialIds& m = a.materials;
    const OpeningMaterialIds& n = b.materials;
    return m.reveal == n.reveal && m.threshold == n.threshold && m.door == n.door && m.frame == n.frame && m.glass == n.glass &&
           m.handle == n.handle && m.revealSlot == n.revealSlot && m.thresholdSlot == n.thresholdSlot && m.doorSlot == n.doorSlot &&
           m.frameSlot == n.frameSlot && m.glassSlot == n.glassSlot && m.handleSlot == n.handleSlot;
}

// Rule 9: the same kind and everything within kKitTolerance.
bool nearlySame(const KitPiece& a, const KitPiece& b)
{
    if (a.kind != b.kind || a.solids.size() != b.solids.size() || a.openings.size() != b.openings.size()) return false;
    if (!near(a.length, b.length) || !near(a.thickness, b.thickness) || !near(a.height, b.height)) return false;
    for (std::size_t i = 0; i < a.solids.size(); ++i) {
        const KitSolid& x = a.solids[i];
        const KitSolid& y = b.solids[i];
        if (!near(x.height, y.height) || x.topSlot != y.topSlot || x.topMaterial != y.topMaterial) return false;
        if (!sameRing(x.outline, y.outline) || x.holes.size() != y.holes.size()) return false;
        for (std::size_t h = 0; h < x.holes.size(); ++h) {
            if (!sameRing(x.holes[h], y.holes[h])) return false;
        }
    }
    for (std::size_t i = 0; i < a.openings.size(); ++i) {
        if (!sameOpening(a.openings[i], b.openings[i])) return false;
    }
    return true;
}

// Twice the signed area of the triangle a, b, c: positive for a left turn.
double turnOf(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
}

// Drops repeated points and points on a straight line between their neighbours.
std::vector<Vec2> cleanedRing(std::vector<Vec2> ring)
{
    bool changed = true;
    while (changed && ring.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[(i + ring.size() - 1) % ring.size()];
            const Vec2 b = ring[i];
            const Vec2 c = ring[(i + 1) % ring.size()];
            const bool same = distance(a, b) < 1e-7;
            const double along = (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y);
            if (same || (std::fabs(turnOf(a, b, c)) < 1e-9 && along > 0.0)) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return ring;
}

// Adds the convex prisms of a polygon between two heights.
void addConvex(std::vector<CollisionPiece>& out, const Poly& poly, double bottom, double top)
{
    std::vector<Vec2> ring = cleanedRing(poly.outline);
    if (ring.size() < 3 || signedArea(ring) < 1e-9) return;
    if (poly.holes.empty() && isConvexRing(ring)) {
        out.push_back({std::move(ring), bottom, top});
        return;
    }
    WorldPolygon polygon;
    polygon.outline = std::move(ring);
    polygon.holes = poly.holes;
    for (std::vector<Vec2>& piece : convexPieces(polygon)) out.push_back({std::move(piece), bottom, top});
}

// Rule 11: the collision of a piece, in its own frame.
std::vector<CollisionPiece> pieceCollision(const KitPiece& piece)
{
    struct Gap {
        std::vector<Vec2> footprint;
        double low = 0.0;
        double high = 0.0;
    };
    struct Slab {
        double low = 0.0;
        double high = 0.0;
        std::vector<std::size_t> active;
    };
    std::vector<CollisionPiece> out;
    for (const KitSolid& solid : piece.solids) {
        const PathsD area = pathsOf(solid);
        std::vector<Gap> gaps;
        for (const KitOpening& k : piece.openings) {
            if (k.opening.type == OpeningType::Window) continue;
            std::vector<Vec2> footprint = footprintOf(k.opening);
            if (footprint.size() < 3) continue;
            const PathsD hit =
                Clipper2Lib::Intersect(area, PathsD{toPath(footprint)}, Clipper2Lib::FillRule::NonZero, kPrecision);
            if (Clipper2Lib::Area(hit) < kAreaEps) continue;
            Gap g;
            g.footprint = std::move(footprint);
            g.low = std::clamp(k.opening.sill, 0.0, solid.height);
            g.high = k.opening.sill + k.opening.height >= solid.height - kLevelEps ? solid.height
                                                                                  : k.opening.sill + k.opening.height;
            if (g.high - g.low < kLevelEps) continue;
            gaps.push_back(std::move(g));
        }
        std::vector<double> levels{0.0, solid.height};
        for (const Gap& g : gaps) {
            levels.push_back(g.low);
            levels.push_back(g.high);
        }
        std::sort(levels.begin(), levels.end());
        std::vector<double> unique;
        for (const double z : levels) {
            if (unique.empty() || z - unique.back() > kLevelEps) unique.push_back(z);
        }
        std::vector<Slab> slabs;
        for (std::size_t i = 0; i + 1 < unique.size(); ++i) {
            Slab s;
            s.low = unique[i];
            s.high = unique[i + 1];
            for (std::size_t g = 0; g < gaps.size(); ++g) {
                if (gaps[g].low <= s.low + kLevelEps && gaps[g].high >= s.high - kLevelEps) s.active.push_back(g);
            }
            if (!slabs.empty() && slabs.back().active == s.active) {
                slabs.back().high = s.high;
            } else {
                slabs.push_back(std::move(s));
            }
        }
        for (const Slab& slab : slabs) {
            PathsD region = area;
            if (!slab.active.empty()) {
                PathsD cuts;
                for (const std::size_t g : slab.active) cuts.push_back(toPath(gaps[g].footprint));
                region = Clipper2Lib::Difference(area, cuts, Clipper2Lib::FillRule::NonZero, kPrecision);
            }
            for (const Poly& poly : polysFrom(region, kAreaEps)) addConvex(out, poly, slab.low, slab.high);
        }
    }
    return out;
}

// The kind as it starts a piece id.
const char* kindWord(KitPieceKind kind)
{
    switch (kind) {
    case KitPieceKind::Wall: return "Wall";
    case KitPieceKind::Corner: return "Corner";
    case KitPieceKind::Opening: return "Opening";
    case KitPieceKind::Curved: return "Curved";
    }
    return "Wall";
}

// The pieces and placements found so far.
struct Builder {
    std::vector<KitPiece> pieces;
    std::vector<KitPlacement> placements;

    // Rule 10. A second piece that would get a name already in use gets a different hash.
    std::string uniqueId(const KitPiece& piece, const std::string& text) const
    {
        for (int salt = 0;; ++salt) {
            const std::string hashed = salt == 0 ? text : text + "#" + std::to_string(salt);
            char digits[17];
            std::snprintf(digits, sizeof digits, "%016llx", static_cast<unsigned long long>(detail::fnv1a(hashed)));
            const std::string id = std::string(kindWord(piece.kind)) + "_" + std::to_string(centimetres(piece.length)) + "x" +
                                   std::to_string(centimetres(piece.height)) + "x" +
                                   std::to_string(centimetres(piece.thickness)) + "_" + std::string(digits, 6);
            const bool taken = std::any_of(pieces.begin(), pieces.end(), [&](const KitPiece& p) { return p.id == id; });
            if (!taken) return id;
        }
    }

    // Adds one placement of a piece.
    void record(const std::string& id, const Cand& c, const FloorCtx& ctx, const Frame& f)
    {
        KitPlacement p;
        p.piece = id;
        p.floor = ctx.floor->id;
        if (c.kind != KitPieceKind::Corner) p.wall = c.wall;
        p.point = c.point;
        p.location = {f.origin.x, f.origin.y, ctx.build->elevation};
        p.rotationZ = wrappedAngle(f.theta);
        placements.push_back(std::move(p));
    }

    // Rule 9: a placement of a piece found before, or a new piece in the frame whose data sorts first, so the
    // name and shape do not depend on which wall or direction found it.
    void place(const Cand& c, const FloorCtx& ctx)
    {
        std::vector<Frame> frames = c.frames;
        std::vector<KitPiece> locals;
        for (const Frame& f : frames) locals.push_back(localPiece(c, ctx, f));
        if (c.halfTurn) {
            double minX = 1e300;
            double maxX = -1e300;
            for (const KitSolid& s : locals.front().solids) {
                for (const Vec2& p : s.outline.points) {
                    minX = std::min(minX, p.x);
                    maxX = std::max(maxX, p.x);
                }
            }
            const double along = minX + maxX;
            const Frame base = frames.front();
            Frame turned;
            turned.origin = {base.origin.x + std::cos(base.theta) * along, base.origin.y + std::sin(base.theta) * along};
            turned.theta = base.theta + std::numbers::pi;
            frames.push_back(turned);
            locals.push_back(localPiece(c, ctx, turned));
        }

        for (const KitPiece& existing : pieces) {
            for (std::size_t v = 0; v < locals.size(); ++v) {
                if (nearlySame(locals[v], existing)) {
                    record(existing.id, c, ctx, frames[v]);
                    return;
                }
            }
        }

        std::size_t best = 0;
        std::string bestKey = serialize(locals[0]);
        for (std::size_t v = 1; v < locals.size(); ++v) {
            std::string key = serialize(locals[v]);
            if (key < bestKey) {
                bestKey = std::move(key);
                best = v;
            }
        }
        KitPiece piece = std::move(locals[best]);
        piece.id = uniqueId(piece, bestKey);
        piece.mesh = std::string(kKitMeshPrefix) + piece.id;
        piece.collision = pieceCollision(piece);
        const std::string id = piece.id;
        pieces.push_back(std::move(piece));
        record(id, c, ctx, frames[best]);
    }
};

// Works out every piece of one floor and adds them to the builder.
void cutFloor(const FloorCtx& ctx, Builder& builder)
{
    std::vector<Cand> cands;
    PathsD taken;
    for (const WallInfo& w : ctx.walls) {
        for (Region& region : regionsOf(ctx, w)) {
            if (!taken.empty()) {
                const PathsD overlap =
                    Clipper2Lib::Intersect(region.polygon, taken, Clipper2Lib::FillRule::NonZero, kPrecision);
                if (Clipper2Lib::Area(overlap) > kAreaEps) {
                    region.polygon = Clipper2Lib::Difference(region.polygon, taken, Clipper2Lib::FillRule::NonZero, kPrecision);
                }
            }
            Cand c;
            c.kind = region.kind;
            c.wall = w.wall->id;
            c.solids = solidsIn(ctx, region.polygon);
            if (c.solids.empty()) continue;
            append(taken, region.polygon);
            for (const std::size_t i : reachedOpenings(ctx, c.solids)) c.openings.push_back({i, false});
            const Vec2 start = pointAt(w, region.a);
            Vec2 direction = tangentAt(w, region.a);
            if (region.kind == KitPieceKind::Curved) {
                const Vec2 end = pointAt(w, region.b);
                if (distance(start, end) > 1e-9) direction = {end.x - start.x, end.y - start.y};
            }
            c.frames.push_back({start, std::atan2(direction.y, direction.x)});
            c.halfTurn = true;
            cands.push_back(std::move(c));
        }
    }
    if (!taken.empty()) taken = Clipper2Lib::Union(taken, Clipper2Lib::FillRule::NonZero, kPrecision);
    for (Cand& c : cornerCands(ctx, taken)) {
        for (const std::size_t i : reachedOpenings(ctx, c.solids)) c.openings.push_back({i, false});
        cands.push_back(std::move(c));
    }

    std::vector<bool> filled(ctx.openings.size(), false);
    for (Cand& c : cands) {
        for (OpeningRef& ref : c.openings) {
            if (filled[ref.index] || !holdsPoint(c.solids, ctx.openings[ref.index].build.centre)) continue;
            ref.fill = true;
            filled[ref.index] = true;
        }
    }
    for (const Cand& c : cands) builder.place(c, ctx);
}

// Whether a wall runs from its higher end to its lower one, by X and then Y.
bool drawnFromHigherEnd(Vec2 from, Vec2 to)
{
    return from.x > to.x + kClose || (std::fabs(from.x - to.x) <= kClose && from.y > to.y + kClose);
}

// Gathers what the cutting of one floor needs.
FloorCtx makeContext(const Project& project, const BuildPlan& plan, std::size_t index, const Floor& floor)
{
    FloorCtx ctx;
    ctx.floor = &floor;
    ctx.build = &plan.floors[index];
    if (index < plan.floorMaterials.size()) ctx.materials = plan.floorMaterials[index];
    ctx.grid = kitGridSize(project, floor);

    for (std::size_t i = 0; i < ctx.build->openings.size(); ++i) {
        OpeningInfo info;
        info.build = ctx.build->openings[i];
        if (i < ctx.materials.openings.size()) info.materials = ctx.materials.openings[i];
        info.footprint = footprintOf(info.build);
        ctx.openings.push_back(std::move(info));
    }
    for (const WallSolid& solid : ctx.build->wallSolids) ctx.edges.push_back(sourceEdges(solid, ctx.materials));

    for (const Wall& wall : floor.walls) {
        const WallPath path = wallPath(floor, wall);
        if (!path.curve.valid || path.centreLine.size() < 2) continue;
        WallInfo w;
        w.wall = &wall;
        w.half = wall.thickness / 2.0;
        for (const Vec2& p : path.centreLine) w.line.push_back(flipped(p));
        w.cumulative.assign(w.line.size(), 0.0);
        for (std::size_t i = 1; i < w.line.size(); ++i) w.cumulative[i] = w.cumulative[i - 1] + distance(w.line[i - 1], w.line[i]);
        w.length = w.cumulative.back();
        for (const std::vector<Vec2>& band : wallBand(floor, wall)) {
            std::vector<Vec2> world;
            for (const Vec2& p : band) world.push_back(flipped(p));
            w.bands.push_back(toPath(orientedRing(std::move(world), true)));
        }
        if (w.bands.empty() || w.length < 1e-9) continue;
        w.straight = w.line.size() == 2;
        w.reversed = drawnFromHigherEnd(w.line.front(), w.line.back());
        ctx.walls.push_back(std::move(w));
    }
    for (std::size_t i = 0; i < ctx.walls.size(); ++i) ctx.walls[i].junctions = junctionParts(ctx.walls, i);
    return ctx;
}

}

// The grid the kit cuts a floor's walls on.
double kitGridSize(const Project& project, const Floor&)
{
    return project.settings.gridSize > 0.0 ? project.settings.gridSize : kDefaultGridSize;
}

// The name of a piece kind.
std::string_view kitPieceKindName(KitPieceKind kind)
{
    switch (kind) {
    case KitPieceKind::Wall: return "wall";
    case KitPieceKind::Corner: return "corner";
    case KitPieceKind::Opening: return "opening";
    case KitPieceKind::Curved: return "curved";
    }
    return "wall";
}

// The kind with a name, if there is one.
std::optional<KitPieceKind> kitPieceKindFromName(std::string_view name)
{
    for (const KitPieceKind kind : {KitPieceKind::Wall, KitPieceKind::Corner, KitPieceKind::Opening, KitPieceKind::Curved}) {
        if (kitPieceKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

// Works out the kit of a project from its build plan.
ModularKit modularKit(const Project& project, const BuildPlan& plan)
{
    ModularKit kit;
    Builder builder;
    for (std::size_t i = 0; i < plan.floors.size(); ++i) {
        const FloorBuild& build = plan.floors[i];
        const Floor* floor = findFloor(project.plan, build.id);
        if (!floor || build.wallSolids.empty()) continue;
        const FloorCtx ctx = makeContext(project, plan, i, *floor);
        if (ctx.walls.empty()) continue;
        kit.floors.push_back({floor->id, build.elevation, ctx.grid});
        cutFloor(ctx, builder);
    }
    kit.pieces = std::move(builder.pieces);
    kit.placements = std::move(builder.placements);
    for (KitPiece& piece : kit.pieces) {
        piece.count = static_cast<int>(std::count_if(kit.placements.begin(), kit.placements.end(),
                                                     [&](const KitPlacement& p) { return p.piece == piece.id; }));
    }
    std::sort(kit.pieces.begin(), kit.pieces.end(), [](const KitPiece& a, const KitPiece& b) { return a.id < b.id; });
    return kit;
}

// The layout file's form.
Json kitLayoutToJson(const ModularKit& kit, std::string_view projectName, std::string_view kitFile)
{
    const auto rounded = [](double v) { return std::round(v * 1e6) / 1e6; };
    const auto idOrNull = [](const Id& id) { return id.empty() ? Json(nullptr) : Json(id); };

    Json json = Json::object();
    json["format"] = std::string(kKitLayoutFormat);
    json["version"] = kKitLayoutVersion;
    json["project"] = std::string(projectName);
    json["kit_file"] = std::string(kitFile);
    json["coordinates"] = "blender_world_metres";

    Json floors = Json::array();
    for (const KitFloorGrid& f : kit.floors) {
        Json item = Json::object();
        item["id"] = f.floor;
        item["elevation"] = f.elevation;
        item["grid_size"] = f.gridSize;
        floors.push_back(std::move(item));
    }
    json["floors"] = std::move(floors);

    Json pieces = Json::array();
    for (const KitPiece& p : kit.pieces) {
        Json item = Json::object();
        item["id"] = p.id;
        item["mesh"] = p.mesh;
        item["kind"] = std::string(kitPieceKindName(p.kind));
        item["length"] = rounded(p.length);
        item["height"] = rounded(p.height);
        item["thickness"] = rounded(p.thickness);
        item["count"] = p.count;
        pieces.push_back(std::move(item));
    }
    json["pieces"] = std::move(pieces);

    Json placements = Json::array();
    for (const KitPlacement& pl : kit.placements) {
        std::string mesh = std::string(kKitMeshPrefix) + pl.piece;
        for (const KitPiece& p : kit.pieces) {
            if (p.id == pl.piece) mesh = p.mesh;
        }
        double degrees = std::fmod(rounded(pl.rotationZ * 180.0 / std::numbers::pi), 360.0);
        if (degrees < 0.0) degrees += 360.0;
        if (degrees >= 360.0) degrees = 0.0;
        Json item = Json::object();
        item["piece"] = pl.piece;
        item["mesh"] = mesh;
        item["floor"] = pl.floor;
        item["wall"] = idOrNull(pl.wall);
        item["point"] = idOrNull(pl.point);
        item["location"] = Json::array({rounded(pl.location.x), rounded(pl.location.y), rounded(pl.location.z)});
        item["rotation_z_degrees"] = degrees;
        placements.push_back(std::move(item));
    }
    json["placements"] = std::move(placements);
    return json;
}

}
