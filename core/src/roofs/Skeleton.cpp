#include "roofs/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace plynth::roofskeleton {

namespace {

constexpr double kStraightTolerance = 1e-3; // metres: a point this close to the line between its neighbours is dropped
constexpr double kOnEaves = 1e-7;           // metres: a point this near an edge of the eaves lies on the eaves
constexpr double kWeldDistance = 1e-4;      // metres: inner points this near each other, along a chain, are one point
constexpr double kWeldReach = 2e-4;         // metres: a group of points spread wider than this is left as it is

constexpr double kFacing = 1e-12;  // when 1 + nL . nR is under this, the two edges at a corner face each other
constexpr double kReflex = 1e-12;  // a corner turning right by more than this is a reflex corner
constexpr double kTouch = 1e-9;    // metres, and units of wavefront time: this short or this near counts as none
constexpr double kAhead = 1e-12;   // metres: a corner born nearer than this to an edge's line never hits that edge
constexpr double kClosing = 1e-12; // a speed toward a line under this never reaches it
constexpr double kNoArea = 1e-12;  // square metres: a loop with less is nothing
constexpr double kSamePoint = 1e-12; // metres: points this near are one point
constexpr std::size_t kEventsPerEdge = 64;

// The sum of two vectors.
Vec2 plus(Vec2 a, Vec2 b)
{
    return {a.x + b.x, a.y + b.y};
}

// The difference of two vectors.
Vec2 minus(Vec2 a, Vec2 b)
{
    return {a.x - b.x, a.y - b.y};
}

// A vector made longer or shorter.
Vec2 times(Vec2 a, double s)
{
    return {a.x * s, a.y * s};
}

// The dot product.
double dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

// The cross product: positive when b turns left from a.
double cross(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

// The area of a ring, positive when it turns left, measured from its first point.
double ringArea(const std::vector<Vec2>& ring)
{
    double twice = 0.0;
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) twice += cross(minus(ring[i], ring[0]), minus(ring[i + 1], ring[0]));
    return 0.5 * twice;
}

// The distance from a point to the segment from a to b.
double toSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 d = minus(b, a);
    const double size2 = dot(d, d);
    const double t = size2 > 0.0 ? std::clamp(dot(minus(p, a), d) / size2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - a.x - t * d.x, p.y - a.y - t * d.y);
}

// Says whether a point lies on an edge of the eaves.
bool onEaves(Vec2 p, const std::vector<WorldPolygon>& eaves)
{
    const auto onRing = [&p](const std::vector<Vec2>& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (toSegment(p, ring[i], ring[(i + 1) % ring.size()]) <= kOnEaves) return true;
        }
        return false;
    };
    for (const WorldPolygon& poly : eaves) {
        if (onRing(poly.outline)) return true;
        for (const std::vector<Vec2>& hole : poly.holes) {
            if (onRing(hole)) return true;
        }
    }
    return false;
}

// The ring without a point that repeats the one before it.
void dropRepeats(std::vector<Vec2>& ring)
{
    std::vector<Vec2> kept;
    for (const Vec2& p : ring) {
        if (kept.empty() || !(kept.back() == p)) kept.push_back(p);
    }
    while (kept.size() > 1 && kept.back() == kept.front()) kept.pop_back();
    ring = std::move(kept);
}

// One edge of the area. Its line moves inward at speed 1: at time t it is normal . p = offset + t.
struct Edge {
    Vec2 along;  // unit direction
    Vec2 normal; // unit, toward the area
    double offset = 0.0;
};

// A corner of the moving outline (the wavefront), between the edge before it and the edge after it.
struct Corner {
    Vec2 start;       // where it was at time born
    double born = 0.0;
    Vec2 speed;       // how far it moves in one unit of time
    int left = 0;     // the edge before it
    int right = 0;    // the edge after it
    int node = 0;     // the skeleton point it started from
    int prev = 0;
    int next = 0;
    bool alive = true;
};

// What happens next to the wavefront: an edge shrinks to nothing, or a reflex corner hits an edge.
struct Event {
    bool found = false;
    bool split = false;
    double wait = 0.0;
    int corner = -1;
    int edgeStart = -1; // for a hit: the corner at the start of the piece of edge it hits
};

// The wavefront of an area, moved inward event by event while it records the skeleton. Every face is kept as
// directed pieces of its outline: the edge itself, the paths of the corners beside it, and the tops where its
// part of the wavefront ends. Joined up they make the face.
class Wavefront {
public:
    // Starts the wavefront on the rings, which are already moved near the origin.
    explicit Wavefront(const std::vector<std::vector<Vec2>>& rings)
    {
        for (const std::vector<Vec2>& ring : rings) {
            const int count = static_cast<int>(ring.size());
            const int firstEdge = static_cast<int>(edges.size());
            const int firstCorner = static_cast<int>(corners.size());
            const int firstNode = static_cast<int>(nodes.size());
            for (int k = 0; k < count; ++k) {
                const Vec2 a = ring[static_cast<std::size_t>(k)];
                const Vec2 b = ring[static_cast<std::size_t>((k + 1) % count)];
                const double length = std::hypot(b.x - a.x, b.y - a.y);
                if (!(length > 1e-12)) broken = true;
                const Vec2 u = length > 0.0 ? times(minus(b, a), 1.0 / length) : Vec2{1.0, 0.0};
                const Vec2 n{-u.y, u.x};
                edges.push_back({u, n, dot(n, a)});
                pieces.emplace_back();
                pieces.back().emplace_back(firstNode + k, firstNode + (k + 1) % count);
            }
            for (int k = 0; k < count; ++k) nodes.push_back(ring[static_cast<std::size_t>(k)]);
            for (int k = 0; k < count; ++k) {
                Corner c;
                c.start = ring[static_cast<std::size_t>(k)];
                c.left = firstEdge + (k + count - 1) % count;
                c.right = firstEdge + k;
                c.node = firstNode + k;
                c.prev = firstCorner + (k + count - 1) % count;
                c.next = firstCorner + (k + 1) % count;
                c.speed = speedOf(c.left, c.right);
                corners.push_back(c);
            }
        }
    }

    // Moves the wavefront until nothing is left. False when it cannot be worked out.
    bool run()
    {
        if (broken || edges.empty()) return false;
        const std::size_t limit = kEventsPerEdge * edges.size() + 64;
        for (std::size_t step = 0; step < limit; ++step) {
            if (!settle()) return false;
            if (std::none_of(corners.begin(), corners.end(), [](const Corner& c) { return c.alive; })) return true;
            const Event event = nextEvent();
            if (!event.found) return false;
            now += event.wait;
            if (event.split) split(event.corner, event.edgeStart);
            else collapse(event.corner);
        }
        return false;
    }

    std::vector<Edge> edges;
    std::vector<Vec2> nodes;
    std::vector<std::vector<std::pair<int, int>>> pieces; // per edge, the directed pieces of its face's outline
    std::vector<std::pair<int, int>> lines;               // the lines between faces, as node pairs

private:
    // How a corner between two edges moves: it stays on both lines.
    Vec2 speedOf(int left, int right) const
    {
        const Vec2 a = edges[static_cast<std::size_t>(left)].normal;
        const Vec2 b = edges[static_cast<std::size_t>(right)].normal;
        const double c = 1.0 + dot(a, b);
        if (c < kFacing) return {0.0, 0.0};
        return times(plus(a, b), 1.0 / c);
    }

    // The corner with this index.
    Corner& at(int i)
    {
        return corners[static_cast<std::size_t>(i)];
    }

    // The edge with this index.
    const Edge& edge(int i) const
    {
        return edges[static_cast<std::size_t>(i)];
    }

    // Where a corner is at a time.
    Vec2 placeAt(int i, double t)
    {
        const Corner& c = at(i);
        return plus(c.start, times(c.speed, t - c.born));
    }

    // Says whether the two edges at a corner face each other, so the area there has no width.
    bool facing(int i)
    {
        const Corner& c = at(i);
        return 1.0 + dot(edge(c.left).normal, edge(c.right).normal) < kFacing;
    }

    // Says whether a corner turns right, into the area.
    bool reflex(int i)
    {
        const Corner& c = at(i);
        return cross(edge(c.left).along, edge(c.right).along) < -kReflex;
    }

    // Adds a skeleton point and gives its number.
    int addNode(Vec2 p)
    {
        nodes.push_back(p);
        return static_cast<int>(nodes.size()) - 1;
    }

    // Adds a piece of a face's outline.
    void addPiece(int edgeIndex, int from, int to)
    {
        if (from != to) pieces[static_cast<std::size_t>(edgeIndex)].push_back({from, to});
    }

    // Adds a line between faces.
    void addLine(int from, int to)
    {
        if (from != to) lines.push_back({from, to});
    }

    // Records the path of a corner from where it started to a skeleton point.
    void addPath(int i, int to)
    {
        const Corner& c = at(i);
        if (c.node == to) return;
        addPiece(c.left, c.node, to);
        addPiece(c.right, to, c.node);
        addLine(c.node, to);
    }

    // The skeleton point where a corner is now, with its path to it recorded.
    int finish(int i)
    {
        if (now - at(i).born <= 0.0) return at(i).node;
        const int id = addNode(placeAt(i, now));
        addPath(i, id);
        return id;
    }

    // Adds a corner born now.
    int addCorner(Vec2 p, int left, int right, int node)
    {
        Corner c;
        c.start = p;
        c.born = now;
        c.left = left;
        c.right = right;
        c.node = node;
        c.speed = speedOf(left, right);
        corners.push_back(c);
        return static_cast<int>(corners.size()) - 1;
    }

    // Makes b follow a.
    void link(int a, int b)
    {
        at(a).next = b;
        at(b).prev = a;
    }

    // Ends a loop of the wavefront with one or two corners: every corner stops where it is and each piece of
    // edge between them gets its top.
    void closeLoop(int i)
    {
        std::vector<int> members{i};
        for (int j = at(i).next; j != i && members.size() < 3; j = at(j).next) members.push_back(j);
        std::vector<int> ends;
        for (const int m : members) ends.push_back(finish(m));
        for (std::size_t k = 0; k < members.size(); ++k) {
            const std::size_t after = (k + 1) % members.size();
            addPiece(at(members[k]).right, ends[after], ends[k]);
            addLine(ends[after], ends[k]);
        }
        for (const int m : members) at(m).alive = false;
    }

    // Folds away a corner whose two edges face each other on one line. The nearer of its two neighbours along
    // that line takes its place, and the part of the line between them gets a top on both faces.
    void foldSpike(int z)
    {
        const int p = at(z).prev;
        const int q = at(z).next;
        const Vec2 along = edge(at(z).right).along;
        const Vec2 here = placeAt(z, now);
        const double toP = std::max(0.0, dot(along, minus(placeAt(p, now), here)));
        const double toQ = std::max(0.0, dot(along, minus(placeAt(q, now), here)));
        const int zNode = finish(z);
        if (toP <= toQ) {
            const int pNode = finish(p);
            addPiece(at(z).left, zNode, pNode);
            addPiece(at(z).right, pNode, zNode);
            addLine(zNode, pNode);
            const int before = at(p).prev;
            const int w = addCorner(nodes[static_cast<std::size_t>(pNode)], at(p).left, at(z).right, pNode);
            at(p).alive = false;
            at(z).alive = false;
            link(before, w);
            link(w, q);
        } else {
            const int qNode = finish(q);
            addPiece(at(z).right, qNode, zNode);
            addPiece(at(z).left, zNode, qNode);
            addLine(zNode, qNode);
            const int after = at(q).next;
            const int w = addCorner(nodes[static_cast<std::size_t>(qNode)], at(z).left, at(q).right, qNode);
            at(q).alive = false;
            at(z).alive = false;
            link(p, w);
            link(w, after);
        }
    }

    // Ends small loops and folds spikes until there are none. False when that does not settle.
    bool settle()
    {
        const std::size_t limit = 4 * kEventsPerEdge * edges.size() + 64;
        for (std::size_t pass = 0; pass < limit; ++pass) {
            bool changed = false;
            for (std::size_t k = 0; k < corners.size(); ++k) {
                const int i = static_cast<int>(k);
                if (!at(i).alive) continue;
                const int j = at(i).next;
                if (j == i || at(j).next == i) {
                    closeLoop(i);
                    changed = true;
                } else if (facing(i)) {
                    foldSpike(i);
                    changed = true;
                }
            }
            if (!changed) return true;
        }
        return false;
    }

    // The next event: the soonest piece of edge to shrink to nothing, or the soonest reflex corner to hit a piece
    // of edge. A shrinking edge wins a tie.
    Event nextEvent()
    {
        Event best;
        for (std::size_t k = 0; k < corners.size(); ++k) {
            const int i = static_cast<int>(k);
            if (!at(i).alive) continue;
            const int j = at(i).next;
            const Vec2 u = edge(at(i).right).along;
            const double length = dot(u, minus(placeAt(j, now), placeAt(i, now)));
            const double rate = dot(u, minus(at(j).speed, at(i).speed));
            double wait = 0.0;
            if (length <= kTouch) wait = 0.0;
            else if (rate < -kClosing) wait = length / -rate;
            else continue;
            if (!best.found || wait < best.wait) best = {true, false, wait, i, -1};
        }
        for (std::size_t k = 0; k < corners.size(); ++k) {
            const int i = static_cast<int>(k);
            if (!at(i).alive || !reflex(i)) continue;
            for (std::size_t m = 0; m < corners.size(); ++m) {
                const int x = static_cast<int>(m);
                if (!at(x).alive || x == i || at(x).next == i) continue;
                const Edge& e = edge(at(x).right);
                // Measured from where the corner was born, so a corner born on the line never hits it.
                const double ahead = dot(e.normal, at(i).start) - (e.offset + at(i).born);
                if (ahead <= kAhead) continue;
                const double closing = 1.0 - dot(e.normal, at(i).speed);
                if (closing <= kClosing) continue;
                const double hitTime = at(i).born + ahead / closing;
                if (hitTime < now - kTouch) continue;
                const double wait = std::max(0.0, hitTime - now);
                if (best.found && wait >= best.wait) continue;
                const double t = now + wait;
                const Vec2 hit = placeAt(i, t);
                const Vec2 from = placeAt(x, t);
                const Vec2 to = placeAt(at(x).next, t);
                const double length = dot(e.along, minus(to, from));
                if (length < -kTouch) continue;
                const double along = dot(e.along, minus(hit, from));
                if (along < -kTouch || along > length + kTouch) continue;
                best = {true, true, wait, i, x};
            }
        }
        return best;
    }

    // A piece of edge has shrunk to nothing: its two corners become one.
    void collapse(int i)
    {
        const int j = at(i).next;
        const Vec2 a = placeAt(i, now);
        const Vec2 b = placeAt(j, now);
        const int id = addNode({(a.x + b.x) / 2.0, (a.y + b.y) / 2.0});
        addPath(i, id);
        addPath(j, id);
        const int before = at(i).prev;
        const int after = at(j).next;
        const int w = addCorner(nodes[static_cast<std::size_t>(id)], at(i).left, at(j).right, id);
        at(i).alive = false;
        at(j).alive = false;
        link(before, w);
        link(w, after);
    }

    // A reflex corner has hit the piece of edge that starts at corner x: it splits the wavefront there into two
    // corners, one on each side of the edge.
    void split(int i, int x)
    {
        const int y = at(x).next;
        const int e = at(x).right;
        const int id = addNode(placeAt(i, now));
        addPath(i, id);
        const int before = at(i).prev;
        const int after = at(i).next;
        const Vec2 p = nodes[static_cast<std::size_t>(id)];
        const int a = addCorner(p, e, at(i).right, id);
        const int b = addCorner(p, at(i).left, e, id);
        at(i).alive = false;
        link(x, a);
        link(a, after);
        link(before, b);
        link(b, y);
    }

    std::vector<Corner> corners;
    double now = 0.0; // the wavefront time: how far every edge has moved in
    bool broken = false;
};

// Joins the directed pieces of a face's outline into closed loops of node numbers, the first loop starting with
// the edge itself. Empty when they do not join up.
std::optional<std::vector<std::vector<int>>> loopsOf(const std::vector<std::pair<int, int>>& pieces)
{
    if (pieces.empty()) return std::nullopt;
    std::vector<bool> used(pieces.size(), false);
    std::vector<std::vector<int>> loops;
    for (std::size_t first = 0; first < pieces.size(); ++first) {
        if (used[first]) continue;
        std::vector<int> loop{pieces[first].first};
        used[first] = true;
        int here = pieces[first].second;
        std::size_t steps = 0;
        while (here != loop.front()) {
            if (++steps > pieces.size()) return std::nullopt;
            loop.push_back(here);
            std::size_t pick = pieces.size();
            for (std::size_t k = 0; k < pieces.size(); ++k) {
                if (!used[k] && pieces[k].first == here) {
                    pick = k;
                    break;
                }
            }
            if (pick == pieces.size()) return std::nullopt;
            used[pick] = true;
            here = pieces[pick].second;
        }
        loops.push_back(std::move(loop));
    }
    return loops;
}

}

std::optional<Skeleton> straightSkeleton(const WorldPolygon& area)
{
    std::vector<std::vector<Vec2>> rings{area.outline};
    for (const std::vector<Vec2>& hole : area.holes) rings.push_back(hole);
    if (area.outline.size() < 3 || ringArea(area.outline) <= 0.0) return std::nullopt;
    double wanted = 0.0;
    for (std::size_t r = 0; r < rings.size(); ++r) {
        if (rings[r].size() < 3) return std::nullopt;
        const double size = ringArea(rings[r]);
        if (r > 0 && size >= 0.0) return std::nullopt;
        wanted += size;
    }
    if (wanted <= 0.0) return std::nullopt;

    // Worked out near the origin, so a building far from it loses no digits.
    const Vec2 origin = area.outline.front();
    std::vector<std::vector<Vec2>> local;
    for (const std::vector<Vec2>& ring : rings) {
        std::vector<Vec2> moved;
        for (const Vec2& p : ring) moved.push_back(minus(p, origin));
        local.push_back(std::move(moved));
    }
    Wavefront wave(local);
    if (!wave.run()) return std::nullopt;

    // The rings' own points keep their exact values; the rest are moved back.
    std::vector<Vec2> place;
    for (const std::vector<Vec2>& ring : rings) place.insert(place.end(), ring.begin(), ring.end());
    for (std::size_t k = place.size(); k < wave.nodes.size(); ++k) place.push_back(plus(wave.nodes[k], origin));

    Skeleton out;
    double total = 0.0;
    std::size_t index = 0;
    for (const std::vector<Vec2>& ring : rings) {
        for (std::size_t k = 0; k < ring.size(); ++k, ++index) {
            SkeletonFace face;
            face.from = ring[k];
            face.to = ring[(k + 1) % ring.size()];
            const std::optional<std::vector<std::vector<int>>> loops = loopsOf(wave.pieces[index]);
            if (!loops) return std::nullopt;
            for (const std::vector<int>& loop : *loops) {
                std::vector<Vec2> points;
                for (const int id : loop) {
                    const Vec2 p = place[static_cast<std::size_t>(id)];
                    if (points.empty() || std::hypot(p.x - points.back().x, p.y - points.back().y) > kSamePoint) {
                        points.push_back(p);
                    }
                }
                while (points.size() > 1 &&
                       std::hypot(points.back().x - points.front().x, points.back().y - points.front().y) <= kSamePoint) {
                    points.pop_back();
                }
                const double size = points.size() >= 3 ? ringArea(points) : 0.0;
                if (size < -kNoArea) return std::nullopt;
                if (size <= kNoArea) continue;
                total += size;
                face.parts.push_back(std::move(points));
            }
            out.faces.push_back(std::move(face));
        }
    }
    if (std::fabs(total - wanted) > 1e-7 * std::max(1.0, wanted)) return std::nullopt;

    for (const auto& [a, b] : wave.lines) {
        const Vec2 p = place[static_cast<std::size_t>(a)];
        const Vec2 q = place[static_cast<std::size_t>(b)];
        if (!(p == q)) out.arcs.push_back({p, q});
    }
    return out;
}

std::vector<Vec2> withoutStraightPoints(std::vector<Vec2> ring)
{
    bool changed = true;
    while (changed && ring.size() > 3) {
        changed = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 prev = ring[(i + ring.size() - 1) % ring.size()];
            const Vec2 here = ring[i];
            const Vec2 next = ring[(i + 1) % ring.size()];
            const double len = std::hypot(next.x - prev.x, next.y - prev.y);
            const double turn = (here.x - prev.x) * (next.y - prev.y) - (here.y - prev.y) * (next.x - prev.x);
            if (len < 1e-12 || std::fabs(turn) / len <= kStraightTolerance) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return ring;
}

bool convexRing(const std::vector<Vec2>& ring)
{
    if (ring.size() < 3) return false;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[(i + 1) % ring.size()];
        const Vec2 c = ring[(i + 2) % ring.size()];
        if ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) < -1e-9) return false;
    }
    return true;
}

void weldInnerPoints(std::vector<roofparts::Piece>& pieces, const std::vector<WorldPolygon>& eaves)
{
    std::vector<Vec2*> inner;
    for (roofparts::Piece& piece : pieces) {
        for (WorldPolygon& poly : piece.area) {
            for (Vec2& p : poly.outline) {
                if (!onEaves(p, eaves)) inner.push_back(&p);
            }
            for (std::vector<Vec2>& hole : poly.holes) {
                for (Vec2& p : hole) {
                    if (!onEaves(p, eaves)) inner.push_back(&p);
                }
            }
        }
    }
    const std::size_t count = inner.size();
    std::vector<std::size_t> group(count);
    std::iota(group.begin(), group.end(), std::size_t{0});
    const auto root = [&group](std::size_t i) {
        while (group[i] != i) i = group[i] = group[group[i]];
        return i;
    };
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (std::hypot(inner[i]->x - inner[j]->x, inner[i]->y - inner[j]->y) < kWeldDistance) {
                group[root(i)] = root(j);
            }
        }
    }
    std::vector<std::vector<std::size_t>> members(count);
    for (std::size_t i = 0; i < count; ++i) members[root(i)].push_back(i);
    for (const std::vector<std::size_t>& list : members) {
        if (list.size() < 2) continue;
        Vec2 mean{0.0, 0.0};
        for (const std::size_t k : list) {
            mean.x += inner[k]->x;
            mean.y += inner[k]->y;
        }
        mean = {mean.x / static_cast<double>(list.size()), mean.y / static_cast<double>(list.size())};
        bool tight = true;
        for (const std::size_t k : list) {
            tight = tight && std::hypot(inner[k]->x - mean.x, inner[k]->y - mean.y) <= kWeldReach;
        }
        if (!tight) continue;
        for (const std::size_t k : list) *inner[k] = mean;
    }

    for (roofparts::Piece& piece : pieces) {
        std::vector<WorldPolygon> kept;
        for (WorldPolygon& poly : piece.area) {
            dropRepeats(poly.outline);
            if (poly.outline.size() < 3) continue;
            std::vector<std::vector<Vec2>> holes;
            for (std::vector<Vec2>& hole : poly.holes) {
                dropRepeats(hole);
                if (hole.size() >= 3) holes.push_back(std::move(hole));
            }
            poly.holes = std::move(holes);
            kept.push_back(std::move(poly));
        }
        piece.area = std::move(kept);
    }
    std::erase_if(pieces, [](const roofparts::Piece& piece) { return piece.area.empty(); });
}

}
