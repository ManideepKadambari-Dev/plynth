#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
