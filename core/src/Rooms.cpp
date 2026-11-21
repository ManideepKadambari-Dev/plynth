#include "plynth/core/Rooms.h"

#include "plynth/core/WallGeometry.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
#include <clipper2/clipper.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>

namespace plynth {

namespace {

constexpr double kTinyLength = 1e-9;
constexpr double kTinyArea = 1e-12;
constexpr int kClipperPrecision = 8;

// Orders ids so "w9" comes before "w10".
bool idLess(const Id& a, const Id& b)
{
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

// One usable wall of the graph: two distinct nodes and a length above zero. line is the wall's drawn centre line
// from node a to node b. The directions say where the wall heads when it leaves each end, and curvature how
// fast that direction turns (positive turns toward increasing atan2 angle), to order walls that leave a node
// the same way.
struct Edge {
    std::size_t a = 0;
    std::size_t b = 0;
    Id wallId;
    std::vector<Vec2> line;
    Vec2 startDirection;
    Vec2 endDirection;
    double startCurvature = 0.0;
    double endCurvature = 0.0;
};

// A walk around one face, as half edges. Half edge 2e runs from edge e's a to its b, 2e + 1 the other way.
struct Face {
    std::vector<std::size_t> halfEdges;
    double area = 0.0;
    std::size_t component = 0;
};

// A ring found in the graph, ready to hand out. chunks holds the centre line points each wall adds, in step
// with walls, so a ring can be turned to start at any wall.
struct Ring {
    std::vector<Id> walls;
    std::vector<std::vector<Vec2>> chunks;

    std::vector<Vec2> corners() const
    {
        std::vector<Vec2> out;
        for (const auto& chunk : chunks) out.insert(out.end(), chunk.begin(), chunk.end());
        return out;
    }
};

// Finds a position inside the area made by the rings (even odd rule). Tries several horizontal lines and takes
// the middle of the longest stretch inside. Returns false when no stretch exists.
bool interiorPoint(const std::vector<std::vector<Vec2>>& rings, Vec2& out)
{
    double minY = 1e300;
    double maxY = -1e300;
    for (const auto& ring : rings) {
        for (const Vec2& p : ring) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    if (!(maxY > minY)) return false;
    static const double levels[] = {0.5, 0.4, 0.6, 0.3, 0.7, 0.2, 0.8, 0.1, 0.9};
    double bestWidth = 0.0;
    bool found = false;
    for (double level : levels) {
        const double y = minY + (maxY - minY) * level;
        std::vector<double> xs;
        for (const auto& ring : rings) {
            const std::size_t n = ring.size();
            for (std::size_t i = 0, j = n ? n - 1 : 0; i < n; j = i++) {
                const Vec2 a = ring[i];
                const Vec2 b = ring[j];
                if ((a.y > y) != (b.y > y)) xs.push_back((b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x);
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            const double width = xs[i + 1] - xs[i];
            if (width > bestWidth) {
                bestWidth = width;
                out = {(xs[i] + xs[i + 1]) / 2.0, y};
                found = true;
            }
        }
    }
    return found;
}

// Says whether a position is inside the outer ring and outside every island ring.
bool insideRings(Vec2 position, const std::vector<std::vector<Vec2>>& rings)
{
    if (rings.empty() || !insidePolygon(position, rings[0])) return false;
    for (std::size_t i = 1; i < rings.size(); ++i) {
        if (insidePolygon(position, rings[i])) return false;
    }
    return true;
}

// Rotates a ring so it starts at the wall with the smallest id.
void rotateToSmallestWall(Ring& ring)
{
    if (ring.walls.empty()) return;
    std::size_t best = 0;
    for (std::size_t i = 1; i < ring.walls.size(); ++i) {
        if (idLess(ring.walls[i], ring.walls[best])) best = i;
    }
    std::rotate(ring.walls.begin(), ring.walls.begin() + static_cast<std::ptrdiff_t>(best), ring.walls.end());
    std::rotate(ring.chunks.begin(), ring.chunks.begin() + static_cast<std::ptrdiff_t>(best), ring.chunks.end());
}

// Marks the edges that are bridges (removing one splits its piece in two), by a depth first search that does not
// recurse so large plans are safe.
std::vector<bool> findBridges(std::size_t nodeCount, const std::vector<Edge>& edges)
{
    std::vector<std::vector<std::size_t>> adjacent(nodeCount);
    for (std::size_t e = 0; e < edges.size(); ++e) {
        adjacent[edges[e].a].push_back(e);
        adjacent[edges[e].b].push_back(e);
    }
    std::vector<bool> bridge(edges.size(), false);
    std::vector<int> tin(nodeCount, -1);
    std::vector<int> low(nodeCount, 0);
    int timer = 0;
    struct Frame {
        std::size_t node;
        std::size_t parentEdge;
        std::size_t next;
    };
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    for (std::size_t root = 0; root < nodeCount; ++root) {
        if (tin[root] >= 0) continue;
        std::vector<Frame> stack{{root, kNone, 0}};
        tin[root] = low[root] = timer++;
        while (!stack.empty()) {
            Frame& top = stack.back();
            if (top.next < adjacent[top.node].size()) {
                const std::size_t e = adjacent[top.node][top.next++];
                if (e == top.parentEdge) continue;
                const std::size_t other = edges[e].a == top.node ? edges[e].b : edges[e].a;
                if (tin[other] >= 0) {
                    low[top.node] = std::min(low[top.node], tin[other]);
                } else {
                    tin[other] = low[other] = timer++;
                    stack.push_back({other, e, 0});
                }
            } else {
                const Frame done = top;
                stack.pop_back();
                if (!stack.empty()) {
                    const std::size_t parent = stack.back().node;
                    low[parent] = std::min(low[parent], low[done.node]);
                    if (low[done.node] > tin[parent]) bridge[done.parentEdge] = true;
                }
            }
        }
    }
    return bridge;
}

}

namespace {

// Rounds a bulge to a whole number of micrometres, 0 for a wall that counts as straight.
long long bulgeKey(double bulge)
{
    return std::fabs(bulge) < kMinBulge ? 0 : std::llround(bulge * 1e6);
}

// The turning rate of a curve's direction when walking away from one end, positive when the angle grows.
double leavingCurvature(const WallCurve& curve, bool fromStart)
{
    if (!curve.curved) return 0.0;
    const double rate = curve.sweep / curve.length;
    return fromStart ? rate : -rate;
}

// Finds the closed areas of the graph made by the walls of graphFloor, with the drawn shapes of the walls taken
// from geometryFloor (the same floor, or the whole floor when graphFloor holds only some of its walls).
std::vector<ClosedArea> closedAreasOf(const Floor& floor, const Floor& geometryFloor)
{
    std::map<Id, std::size_t> nodeOf;
    std::vector<Vec2> nodes;
    for (const Point& p : floor.points) {
        if (nodeOf.emplace(p.id, nodes.size()).second) nodes.push_back(positionOf(p));
    }

    // Walls with the same two points and the same bend are one wall to the graph; the smallest id stands for them.
    using Key = std::tuple<std::size_t, std::size_t, long long>;
    std::map<Key, const Wall*> unique;
    for (const Wall& w : floor.walls) {
        const auto a = nodeOf.find(w.from);
        const auto b = nodeOf.find(w.to);
        if (a == nodeOf.end() || b == nodeOf.end() || a->second == b->second) continue;
        if (distance(nodes[a->second], nodes[b->second]) <= kTinyLength) continue;
        const bool flipped = a->second > b->second;
        const Key key{std::min(a->second, b->second), std::max(a->second, b->second),
                      flipped ? -bulgeKey(w.bulge) : bulgeKey(w.bulge)};
        const auto it = unique.find(key);
        if (it == unique.end()) {
            unique.emplace(key, &w);
        } else if (idLess(w.id, it->second->id)) {
            it->second = &w;
        }
    }
    std::set<std::pair<std::size_t, std::size_t>> straightPairs;
    for (const auto& [key, wall] : unique) {
        if (std::get<2>(key) == 0) straightPairs.insert({std::get<0>(key), std::get<1>(key)});
    }

    // A straight wall laid exactly over a chain of shorter collinear straight walls adds nothing and confuses the
    // angle order, so the long one is left out.
    std::vector<Edge> all;
    for (const auto& [key, wall] : unique) {
        const std::size_t first = std::get<0>(key);
        const std::size_t second = std::get<1>(key);
        if (std::get<2>(key) == 0) {
            const Vec2 a = nodes[first];
            const Vec2 b = nodes[second];
            const double length = distance(a, b);
            std::vector<std::pair<double, std::size_t>> between;
            for (std::size_t n = 0; n < nodes.size(); ++n) {
                if (n == first || n == second) continue;
                const SegmentProjection projection = projectOntoSegment(nodes[n], a, b);
                if (projection.distance > 1e-9 || projection.offset <= 1e-9 || projection.offset >= length - 1e-9) continue;
                between.emplace_back(projection.offset, n);
            }
            bool covered = !between.empty();
            std::sort(between.begin(), between.end());
            std::size_t previous = first;
            for (std::size_t i = 0; covered && i <= between.size(); ++i) {
                const std::size_t next = i < between.size() ? between[i].second : second;
                const auto link = std::minmax(previous, next);
                covered = straightPairs.count({link.first, link.second}) > 0;
                previous = next;
            }
            if (covered) continue;
        }
        const WallPath path = wallPath(geometryFloor, *wall);
        if (!path.curve.valid || path.centreLine.size() < 2) continue;
        Edge edge;
        edge.a = nodeOf.at(wall->from);
        edge.b = nodeOf.at(wall->to);
        edge.wallId = wall->id;
        edge.line = path.centreLine;
        edge.startDirection = tangentAlong(path.curve, 0.0);
        const Vec2 back = tangentAlong(path.curve, path.curve.length);
        edge.endDirection = {-back.x, -back.y};
        edge.startCurvature = leavingCurvature(path.curve, true);
        edge.endCurvature = leavingCurvature(path.curve, false);
        all.push_back(std::move(edge));
    }

    const std::vector<bool> bridge = findBridges(nodes.size(), all);
    std::vector<Edge> edges;
    for (std::size_t e = 0; e < all.size(); ++e) {
        if (!bridge[e]) edges.push_back(all[e]);
    }
    if (edges.empty()) return {};

    // Pieces: edges joined through shared nodes.
    std::vector<std::size_t> parent(nodes.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t n) {
        while (parent[n] != n) n = parent[n] = parent[parent[n]];
        return n;
    };
    for (const Edge& e : edges) parent[root(e.a)] = root(e.b);

    // Half edges around each node, ordered by angle.
    const auto from = [&](std::size_t h) { return (h & 1) ? edges[h >> 1].b : edges[h >> 1].a; };
    const auto to = [&](std::size_t h) { return (h & 1) ? edges[h >> 1].a : edges[h >> 1].b; };
    std::vector<std::vector<std::size_t>> outgoing(nodes.size());
    for (std::size_t h = 0; h < edges.size() * 2; ++h) outgoing[from(h)].push_back(h);
    std::vector<std::size_t> indexAt(edges.size() * 2, 0);
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        auto& list = outgoing[n];
        const auto direction = [&](std::size_t h) { return (h & 1) ? edges[h >> 1].endDirection : edges[h >> 1].startDirection; };
        const auto curvature = [&](std::size_t h) { return (h & 1) ? edges[h >> 1].endCurvature : edges[h >> 1].startCurvature; };
        const auto angle = [&](std::size_t h) {
            const Vec2 d = direction(h);
            return std::atan2(d.y, d.x);
        };
        std::sort(list.begin(), list.end(), [&](std::size_t x, std::size_t y) {
            const double ax = angle(x);
            const double ay = angle(y);
            if (std::fabs(ax - ay) > 1e-9) return ax < ay;
            const double cx = curvature(x);
            const double cy = curvature(y);
            if (std::fabs(cx - cy) > 1e-12) return cx < cy;
            return idLess(edges[x >> 1].wallId, edges[y >> 1].wallId);
        });
        for (std::size_t i = 0; i < list.size(); ++i) indexAt[list[i]] = i;
    }

    // The points a half edge adds to a ring: its centre line without the last point, which the next one starts with.
    const auto chunkOf = [&](std::size_t h) {
        std::vector<Vec2> chunk = edges[h >> 1].line;
        if (h & 1) std::reverse(chunk.begin(), chunk.end());
        chunk.pop_back();
        return chunk;
    };

    std::vector<Face> faces;
    std::vector<bool> seen(edges.size() * 2, false);
    for (std::size_t start = 0; start < edges.size() * 2; ++start) {
        if (seen[start]) continue;
        Face face;
        std::vector<Vec2> corners;
        std::size_t h = start;
        while (!seen[h]) {
            seen[h] = true;
            face.halfEdges.push_back(h);
            const std::vector<Vec2> chunk = chunkOf(h);
            corners.insert(corners.end(), chunk.begin(), chunk.end());
            const std::size_t twin = h ^ 1u;
            const auto& list = outgoing[from(twin)];
            h = list[(indexAt[twin] + list.size() - 1) % list.size()];
        }
        face.area = signedArea(corners);
        face.component = root(from(start));
        faces.push_back(std::move(face));
    }

    const auto makeRing = [&](const Face& face) {
        Ring ring;
        for (std::size_t h : face.halfEdges) {
            ring.walls.push_back(edges[h >> 1].wallId);
            ring.chunks.push_back(chunkOf(h));
        }
        rotateToSmallestWall(ring);
        return ring;
    };

    struct Area {
        Ring outer;
        std::vector<Vec2> outerCorners;
        std::size_t component = 0;
        double outerArea = 0.0;
        std::vector<Ring> islands;
    };
    std::vector<Area> areas;
    for (const Face& f : faces) {
        if (f.area <= kTinyArea) continue;
        Area a;
        a.outer = makeRing(f);
        a.outerCorners = a.outer.corners();
        a.component = f.component;
        a.outerArea = f.area;
        areas.push_back(std::move(a));
    }
    for (const Face& f : faces) {
        if (f.area >= -kTinyArea) continue;
        Ring ring = makeRing(f);
        const std::vector<Vec2> ringCorners = ring.corners();
        Area* best = nullptr;
        for (Area& a : areas) {
            if (a.component == f.component) continue;
            if (!insidePolygon(ringCorners[0], a.outerCorners)) continue;
            if (!best || a.outerArea < best->outerArea) best = &a;
        }
        if (best) best->islands.push_back(std::move(ring));
    }

    std::vector<ClosedArea> result;
    for (Area& a : areas) {
        std::sort(a.islands.begin(), a.islands.end(), [](const Ring& x, const Ring& y) { return idLess(x.walls[0], y.walls[0]); });
        ClosedArea area;
        area.walls = a.outer.walls;
        area.rings.push_back(a.outerCorners);
        area.centreLineArea = a.outerArea;
        for (const Ring& island : a.islands) {
            area.walls.insert(area.walls.end(), island.walls.begin(), island.walls.end());
            const std::vector<Vec2> islandCorners = island.corners();
            area.rings.push_back(islandCorners);
            area.centreLineArea -= std::abs(signedArea(islandCorners));
        }
        if (!interiorPoint(area.rings, area.inside)) area.inside = area.rings[0][0];
        result.push_back(std::move(area));
    }
    std::stable_sort(result.begin(), result.end(), [](const ClosedArea& x, const ClosedArea& y) {
        return std::lexicographical_compare(x.walls.begin(), x.walls.end(), y.walls.begin(), y.walls.end(), idLess);
    });
    return result;
}

}

// Finds the closed areas of a floor.
std::vector<ClosedArea> findClosedAreas(const Floor& floor)
{
    return closedAreasOf(floor, floor);
}

// Finds the area whose outer ring holds the position and none of whose islands do.
std::optional<ClosedArea> findClosedAreaAt(const Floor& floor, Vec2 position)
{
    for (ClosedArea& a : findClosedAreas(floor)) {
        if (insideRings(position, a.rings)) return std::move(a);
    }
    return std::nullopt;
}

// Works out the rings of the room by finding the closed area of exactly its walls, then takes the wall bands off.
RoomShape roomShape(const Floor& floor, const Room& room)
{
    RoomShape shape;
    if (room.walls.empty()) return shape;

    Floor sub;
    std::set<Id> wanted(room.walls.begin(), room.walls.end());
    if (wanted.size() != room.walls.size()) return shape;
    std::set<Id> pointIds;
    for (const Id& id : room.walls) {
        const Wall* w = findWall(floor, id);
        if (!w) return shape;
        sub.walls.push_back(*w);
        pointIds.insert(w->from);
        pointIds.insert(w->to);
    }
    for (const Point& p : floor.points) {
        if (pointIds.count(p.id)) sub.points.push_back(p);
    }

    const ClosedArea* match = nullptr;
    const std::vector<ClosedArea> areas = closedAreasOf(sub, floor);
    for (const ClosedArea& a : areas) {
        if (std::set<Id>(a.walls.begin(), a.walls.end()) == wanted && a.walls.size() == wanted.size()) {
            match = &a;
            break;
        }
    }
    if (!match) return shape;

    shape.closed = true;
    shape.rings = match->rings;

    Clipper2Lib::PathsD subject;
    for (const auto& ring : match->rings) {
        Clipper2Lib::PathD path;
        for (const Vec2& p : ring) path.emplace_back(p.x, p.y);
        subject.push_back(std::move(path));
    }
    Clipper2Lib::PathsD bands;
    for (const Wall& w : sub.walls) {
        for (const std::vector<Vec2>& outline : wallBand(floor, w)) {
            Clipper2Lib::PathD band;
            for (const Vec2& p : outline) band.emplace_back(p.x, p.y);
            bands.push_back(std::move(band));
        }
    }
    const Clipper2Lib::PathsD merged = Clipper2Lib::Union(bands, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    const Clipper2Lib::PathsD floorPaths =
        Clipper2Lib::Difference(subject, merged, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    for (const Clipper2Lib::PathD& path : floorPaths) {
        std::vector<Vec2> ring;
        for (const Clipper2Lib::PointD& p : path) ring.push_back({p.x, p.y});
        if (ring.size() < 3) continue;
        shape.area += signedArea(ring);
        shape.floorArea.push_back(std::move(ring));
    }
    if (!interiorPoint(shape.floorArea, shape.labelPoint)) shape.labelPoint = match->inside;
    return shape;
}

// The first closed room whose area holds the position.
Id roomAt(const Floor& floor, Vec2 position)
{
    for (const Room& room : floor.rooms) {
        const RoomShape shape = roomShape(floor, room);
        if (shape.closed && insideRings(position, shape.rings)) return room.id;
    }
    return {};
}

}
