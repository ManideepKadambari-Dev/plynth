#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"
#include "roofs/Valleys.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace plynth {

namespace {

constexpr double kSameCorner = 1e-4; // metres: corners of two pieces this near each other are one corner
constexpr double kOnLevel = 1e-6;    // metres: a corner this near the break height lies on the break line

// The highest corner of a piece, and whether the piece rises to that one corner (a hip end) rather than to a
// ridge.
struct Top {
    Vec2 at;
    double height = -1e18;
    bool point = false;
};

// Finds the top of a piece. Only a piece of one polygon without holes can rise to one corner.
Top topOf(const roofparts::Piece& piece)
{
    Top top;
    if (piece.area.size() != 1 || !piece.area.front().holes.empty()) return top;
    const std::vector<Vec2>& ring = piece.area.front().outline;
    for (const Vec2& p : ring) {
        if (piece.top.at(p) > top.height) {
            top.height = piece.top.at(p);
            top.at = p;
        }
    }
    top.point = !ring.empty();
    for (const Vec2& p : ring) {
        if (piece.top.at(p) > top.height - 1e-6 && std::hypot(p.x - top.at.x, p.y - top.at.y) > kSameCorner) {
            top.point = false;
        }
    }
    return top;
}

// Says whether a piece has a corner at a point.
bool hasCorner(const roofparts::Piece& piece, Vec2 at)
{
    for (const WorldPolygon& poly : piece.area) {
        for (const Vec2& p : poly.outline) {
            if (std::hypot(p.x - at.x, p.y - at.y) <= kSameCorner) return true;
        }
    }
    return false;
}

// The height of the lowest of the planes at a point.
double lowestAt(const std::vector<roofparts::Plane>& planes, Vec2 p)
{
    double lowest = 1e18;
    for (const roofparts::Plane& plane : planes) lowest = std::min(lowest, plane.at(p));
    return lowest;
}

// The area split by which of the planes is the lowest, one piece for each plane that is lowest somewhere.
std::vector<roofparts::Piece> byLowest(const std::vector<WorldPolygon>& area, const std::vector<roofparts::Plane>& planes)
{
    std::vector<roofparts::Piece> out;
    for (std::size_t k = 0; k < planes.size(); ++k) {
        std::vector<WorldPolygon> part = area;
        for (std::size_t j = 0; j < planes.size() && !part.empty(); ++j) {
            if (j == k) continue;
            const Vec2 m{planes[j].a - planes[k].a, planes[j].b - planes[k].b};
            const double c = planes[j].c - planes[k].c;
            const double size2 = m.x * m.x + m.y * m.y;
            if (size2 < 1e-12) {
                // Level planes go to the first of them; a plane always below this one takes it all.
                if (c < -1e-9 || (c <= 1e-9 && j < k)) part.clear();
                continue;
            }
            part = roofparts::keepHalf(part, {-c * m.x / size2, -c * m.y / size2}, m);
        }
        if (!part.empty()) out.push_back({std::move(part), planes[k]});
    }
    return out;
}

// The vertical gable of a gablet on the line from a to b at height level, up to the underside of the lowest of
// the planes, looking to the right of the way from a to b. Empty when that underside stays below level.
std::optional<RoofFace> gablet(Vec2 a, Vec2 b, double level, const std::vector<roofparts::Plane>& planes)
{
    const auto along = [a, b](double t) { return Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; };
    const auto gap = [&](double t) { return lowestAt(planes, along(t)) - kRoofThickness - level; };

    std::vector<double> ts{0.0, 1.0};
    for (std::size_t j = 0; j < planes.size(); ++j) {
        for (std::size_t k = j + 1; k < planes.size(); ++k) {
            const double fa = planes[j].at(a) - planes[k].at(a);
            const double fb = planes[j].at(b) - planes[k].at(b);
            if ((fa > 1e-12 && fb < -1e-12) || (fa < -1e-12 && fb > 1e-12)) ts.push_back(fa / (fa - fb));
        }
    }
    std::sort(ts.begin(), ts.end());

    std::vector<std::pair<double, double>> samples; // place along the line and the gap there
    for (const double t : ts) {
        const double g = gap(t);
        if (!samples.empty()) {
            const auto [pt, pg] = samples.back();
            if ((pg > 1e-9 && g < -1e-9) || (pg < -1e-9 && g > 1e-9)) samples.emplace_back(pt + (t - pt) * pg / (pg - g), 0.0);
        }
        samples.emplace_back(t, g);
    }

    // The lowest of planes less a constant is highest in one stretch, so the gable stands on one stretch.
    std::size_t first = samples.size();
    std::size_t last = 0;
    bool rises = false;
    for (std::size_t k = 0; k < samples.size(); ++k) {
        if (samples[k].second < -1e-9) continue;
        first = std::min(first, k);
        last = std::max(last, k);
        rises = rises || samples[k].second > 1e-9;
    }
    if (!rises || first >= last) return std::nullopt;

    RoofFace face;
    face.part = RoofPart::Gable;
    const Vec2 from = along(samples[first].first);
    const Vec2 to = along(samples[last].first);
    face.outline.push_back({from.x, from.y, level});
    face.outline.push_back({to.x, to.y, level});
    for (std::size_t m = last + 1; m-- > first;) {
        const Vec2 at = along(samples[m].first);
        const Vec3 top{at.x, at.y, level + std::max(0.0, samples[m].second)};
        const Vec3& back = face.outline.back();
        if (std::fabs(back.x - top.x) > 1e-9 || std::fabs(back.y - top.y) > 1e-9 || std::fabs(back.z - top.z) > 1e-9) {
            face.outline.push_back(top);
        }
    }
    const Vec3& front = face.outline.front();
    const Vec3& back = face.outline.back();
    if (face.outline.size() > 3 && std::fabs(back.x - front.x) <= 1e-9 && std::fabs(back.y - front.y) <= 1e-9 &&
        std::fabs(back.z - front.z) <= 1e-9) {
        face.outline.pop_back();
    }
    if (face.outline.size() < 3) return std::nullopt;
    return face;
}

// What one hip end becomes: its piece is cut at level, the part above goes to the planes of the pieces beside
// it, and a gablet stands on the cut from a to b.
struct Gablet {
    std::size_t piece = 0;
    double level = 0.0;
    std::vector<roofparts::Plane> beside;
};

// A dutch gable (gablet) roof: the hip roof of the section, with a small gable at the top of each hip end. Each
// hip end is cut at breakRatio of its rise above base; above the cut the two slopes beside it carry on to the
// cut, so the ridge runs out to it, and a vertical gable closes the triangle between them. Ends whose
// neighbours are hip ends too (a square) stay hipped.
class DutchGableRoof final : public RoofType {
public:
    std::string_view id() const override { return "dutch_gable"; }
    std::string_view name() const override { return "Dutch gable"; }
    int sortKey() const override { return 34; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    bool usesBreakRatio() const override { return true; }

    Status fits(const RoofInput& input) const override
    {
        const RoofType* hip = findRoofType(kHipRoofId);
        if (!hip) return fail(ErrorCode::InvalidValue, "A dutch gable roof is built on the hip roof, which is missing.");
        return hip->fits(input);
    }

    std::string_view fallback() const override
    {
        const RoofType* hip = findRoofType(kHipRoofId);
        return hip ? hip->fallback() : kFlatRoofId;
    }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const RoofType* hip = findRoofType(kHipRoofId);
        if (!hip) return {};
        const std::vector<RoofFace> hipFaces = hip->build(input);
        std::vector<roofparts::Piece> pieces = roofparts::coveringPieces(hipFaces);
        const double ratio = std::clamp(input.settings.breakRatio, 0.0, 1.0);

        std::vector<Top> tops;
        for (const roofparts::Piece& piece : pieces) tops.push_back(topOf(piece));

        // Every hip end is found on the hip's own pieces before any is changed.
        std::vector<Gablet> ends;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const Top& top = tops[i];
            const Vec2 g{pieces[i].top.a, pieces[i].top.b};
            if (!top.point || top.height - input.base < 1e-3 || g.x * g.x + g.y * g.y < 1e-12) continue;
            Gablet end;
            end.piece = i;
            end.level = input.base + ratio * (top.height - input.base);
            bool beside = true;
            for (std::size_t j = 0; j < pieces.size(); ++j) {
                if (j == i || !hasCorner(pieces[j], top.at)) continue;
                beside = beside && !tops[j].point;
                end.beside.push_back(pieces[j].top);
            }
            if (beside && !end.beside.empty()) ends.push_back(std::move(end));
        }

        std::vector<RoofFace> gables;
        std::vector<roofparts::Piece> carried;
        for (const Gablet& end : ends) {
            roofparts::Piece& piece = pieces[end.piece];
            const Vec2 g{piece.top.a, piece.top.b};
            const double size2 = g.x * g.x + g.y * g.y;
            const Vec2 onLevel{g.x * (end.level - piece.top.c) / size2, g.y * (end.level - piece.top.c) / size2};
            const std::vector<WorldPolygon> upper = roofparts::keepHalf(piece.area, onLevel, g);
            if (upper.empty()) continue;

            // The slopes beside the end must stand above it over all of the part they take over, and the cut
            // must run between two corners on the level.
            const double length = std::sqrt(size2);
            const Vec2 e{g.y / length, -g.x / length};
            bool above = true;
            double from = 1e18;
            double to = -1e18;
            Vec2 a;
            Vec2 b;
            for (const WorldPolygon& poly : upper) {
                for (const Vec2& p : poly.outline) {
                    above = above && lowestAt(end.beside, p) >= piece.top.at(p) - 1e-6;
                    if (std::fabs(piece.top.at(p) - end.level) > kOnLevel) continue;
                    const double s = e.x * p.x + e.y * p.y;
                    if (s < from) {
                        from = s;
                        a = p;
                    }
                    if (s > to) {
                        to = s;
                        b = p;
                    }
                }
            }
            if (!above || to - from < 1e-6) continue;

            piece.area = roofparts::keepHalf(piece.area, onLevel, {-g.x, -g.y});
            for (roofparts::Piece& part : byLowest(upper, end.beside)) carried.push_back(std::move(part));
            if (const std::optional<RoofFace> face = gablet(a, b, end.level, end.beside)) gables.push_back(*face);
        }
        pieces.insert(pieces.end(), carried.begin(), carried.end());
        std::erase_if(pieces, [](const roofparts::Piece& piece) { return piece.area.empty(); });

        std::vector<RoofFace> faces = roofparts::pieceFaces(pieces, pieces, input.above);
        for (const RoofFace& face : hipFaces) {
            if (face.part == RoofPart::Covering || face.part == RoofPart::Underside || face.part == RoofPart::Edge) continue;
            faces.push_back(face);
        }
        faces.insert(faces.end(), gables.begin(), gables.end());
        return faces;
    }
};

const RoofTypeRegistrar registrar(std::make_unique<DutchGableRoof>());

}

}
