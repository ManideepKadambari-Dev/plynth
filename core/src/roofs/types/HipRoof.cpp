#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace plynth {

namespace {

constexpr double kStraightTolerance = 1e-3; // metres: a point this close to the line between its neighbours is dropped

// The outline without points that lie on the line between their neighbours.
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
            const double cross = (here.x - prev.x) * (next.y - prev.y) - (here.y - prev.y) * (next.x - prev.x);
            if (len < 1e-12 || std::fabs(cross) / len <= kStraightTolerance) {
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return ring;
}

constexpr double kOnEaves = 1e-7;    // metres: a point this near an edge of the eaves lies on the eaves
constexpr double kWeldDistance = 1e-4; // metres: inner points this near each other, along a chain, are one point
constexpr double kWeldReach = 2e-4;    // metres: a group of points spread wider than this is left as it is

// The distance from a point to the segment from a to b.
double toSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double size2 = dx * dx + dy * dy;
    double t = size2 > 0.0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / size2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
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

// Moves the inner points of the faces that come within kWeldDistance of each other, along a chain, to their mean,
// so faces that meet at one apex share it exactly. Rounding in the outline otherwise spreads the apex of a round
// room over ridges up to some 3e-5 m long, and tidying then cuts a face back from it. Points on the eaves never
// move.
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

// Says whether a ring turns left at every point.
bool convex(const std::vector<Vec2>& ring)
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

// A hip roof: one plane per outline edge rising inward, each face where its plane is the lowest.
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
        if (input.section.holes.empty() && convex(withoutStraightPoints(input.section.outline))) return {};
        return fail(ErrorCode::InvalidValue,
                    "A hip roof needs a section without holes or inward corners, so it is built as a gable.");
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const std::vector<Vec2> ring = withoutStraightPoints(input.section.outline);
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

        weldInnerPoints(pieces, input.eaves);

        auto height = [=](Vec2 p) {
            double lowest = 1e18;
            for (std::size_t i = 0; i < count; ++i) {
                lowest = std::min(lowest, normals[i].x * p.x + normals[i].y * p.y - offsets[i]);
            }
            return base + slope * lowest;
        };
        return roofparts::pitchedFaces(input, pieces, breaks, height);
    }
};

const RoofTypeRegistrar registrar(std::make_unique<HipRoof>());

}

}
