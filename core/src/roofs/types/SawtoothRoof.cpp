#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

// How near a bay line a side counts as on it, and how far below the line glass looks for roof (three quarters
// of it), in metres. It is wider than rounding and cleaning together can move a point.
constexpr double kSideBand = 1e-4;

// A stretch along the roof's direction.
struct Span {
    double from = 0.0;
    double to = 0.0;
};

// A piece of a side: the bay whose plane it takes, and whether glass stands on it.
struct SidePart {
    Vec2 a;
    Vec2 b;
    std::size_t bay = 0;
    bool underGlass = false;
};

// A sawtooth roof: bays kSawtoothBaySpan wide across the direction, each a plane rising toward the left of the
// direction, with a vertical glazed face at the high side of each bay.
class SawtoothRoof final : public RoofType {
public:
    std::string_view id() const override { return kSawtoothRoofId; }
    std::string_view name() const override { return "Sawtooth"; }
    int sortKey() const override { return 50; }
    bool usesPitch() const override { return true; }
    bool usesDirection() const override { return true; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return false; }
    Status fits(const RoofInput&) const override { return {}; }
    std::string_view fallback() const override { return kFlatRoofId; }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const Vec2 d = input.direction;
        const Vec2 n = roofparts::leftOf(d);
        const Vec2 backward{-n.x, -n.y};
        auto across = [&](Vec2 p) { return n.x * p.x + n.y * p.y; };
        double lo = 1e18;
        double hi = -1e18;
        for (const Vec2& p : input.section.outline) {
            lo = std::min(lo, across(p));
            hi = std::max(hi, across(p));
        }
        const double slope = std::tan(input.settings.pitch * std::numbers::pi / 180.0);
        const double base = input.base;

        std::vector<double> starts;
        for (double s = lo; s < hi - 1e-9; s += kSawtoothBaySpan) starts.push_back(s);
        auto endOf = [&](std::size_t k) { return std::min(starts[k] + kSawtoothBaySpan, hi); };
        auto planeOf = [&](std::size_t k) { return roofparts::Plane{slope * n.x, slope * n.y, base - starts[k] * slope}; };
        auto bayAt = [&](double s) {
            std::size_t k = 0;
            while (k + 1 < starts.size() && s >= starts[k + 1]) ++k;
            return k;
        };
        auto onLine = [&](double s) { return Vec2{n.x * s, n.y * s}; };

        // Nothing stands beyond the high side of the last bay. The cut is cleaned again, since where a ring meets
        // that line within rounding it leaves a hairline spike along it.
        const std::vector<WorldPolygon> eaves =
            roofparts::tidied(roofparts::keepHalf(input.eaves, onLine(hi), backward));

        std::vector<RoofFace> faces;
        std::vector<std::vector<WorldPolygon>> areas(starts.size());
        for (std::size_t k = 0; k < starts.size(); ++k) {
            areas[k] = eaves;
            if (k > 0) areas[k] = roofparts::keepHalf(areas[k], onLine(starts[k]), n);
            areas[k] = roofparts::keepHalf(areas[k], onLine(endOf(k)), backward);
            for (const WorldPolygon& poly : areas[k]) {
                faces.push_back(roofparts::planeFace(poly, planeOf(k), 0.0, true, RoofPart::Covering));
            }
        }
        for (std::size_t k = 0; k < starts.size(); ++k) {
            for (const WorldPolygon& poly : areas[k]) {
                faces.push_back(roofparts::planeFace(poly, planeOf(k), kRoofThickness, false, RoofPart::Underside));
            }
        }

        // Parameters along the edge from p to q where the bays change.
        auto cuts = [&](Vec2 p, Vec2 q) {
            std::vector<double> ts{0.0, 1.0};
            const double sp = across(p);
            const double sq = across(q);
            for (std::size_t k = 1; k < starts.size(); ++k) {
                if ((sp < starts[k] - 1e-9 && sq > starts[k] + 1e-9) || (sp > starts[k] + 1e-9 && sq < starts[k] - 1e-9)) {
                    ts.push_back((starts[k] - sp) / (sq - sp));
                }
            }
            std::sort(ts.begin(), ts.end());
            return ts;
        };
        auto at = [](Vec2 p, Vec2 q, double t) { return Vec2{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t}; };
        auto rings = [](const WorldPolygon& poly) {
            std::vector<const std::vector<Vec2>*> all{&poly.outline};
            for (const auto& h : poly.holes) all.push_back(&h);
            return all;
        };
        auto alongOf = [&](Vec2 p) { return d.x * p.x + d.y * p.y; };

        // Glass stands where the roof is just below a bay line: where a line a little below it lies in the eaves.
        std::vector<std::vector<Span>> panes(starts.size());
        for (std::size_t k = 0; k < starts.size(); ++k) {
            const double probe = endOf(k) - 0.75 * kSideBand;
            std::vector<double> crossings;
            for (const WorldPolygon& poly : eaves) {
                for (const std::vector<Vec2>* ring : rings(poly)) {
                    for (std::size_t i = 0; i < ring->size(); ++i) {
                        const Vec2 p = (*ring)[i];
                        const Vec2 q = (*ring)[(i + 1) % ring->size()];
                        const double sp = across(p);
                        const double sq = across(q);
                        if ((sp <= probe) == (sq <= probe)) continue;
                        crossings.push_back(alongOf(at(p, q, (probe - sp) / (sq - sp))));
                    }
                }
            }
            std::sort(crossings.begin(), crossings.end());
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                if (crossings[i + 1] - crossings[i] < 1e-6) continue;
                panes[k].push_back({crossings[i], crossings[i + 1]});
            }
        }

        // A side on a bay line takes one bay's plane whole. A high side is cut where glass on it ends, a low side
        // nowhere. Any other edge is cut where the bays change.
        auto sideParts = [&](Vec2 p, Vec2 q) {
            const Vec2 left = roofparts::leftOf({q.x - p.x, q.y - p.y});
            const double facing = left.x * n.x + left.y * n.y;
            auto bothOn = [&](double s) {
                return std::fabs(across(p) - s) <= kSideBand && std::fabs(across(q) - s) <= kSideBand;
            };
            std::vector<SidePart> parts;
            for (std::size_t k = 0; facing < 0.0 && k < starts.size(); ++k) {
                if (!bothOn(endOf(k))) continue;
                const double dp = alongOf(p);
                const double dq = alongOf(q);
                std::vector<double> ts{0.0, 1.0};
                for (const Span& pane : panes[k]) {
                    for (const double end : {pane.from, pane.to}) {
                        if (end > std::min(dp, dq) && end < std::max(dp, dq)) ts.push_back((end - dp) / (dq - dp));
                    }
                }
                std::sort(ts.begin(), ts.end());
                for (std::size_t m = 0; m + 1 < ts.size(); ++m) {
                    const double middle = alongOf(at(p, q, (ts[m] + ts[m + 1]) / 2.0));
                    bool held = false;
                    for (const Span& pane : panes[k]) held = held || (middle >= pane.from && middle <= pane.to);
                    parts.push_back({at(p, q, ts[m]), at(p, q, ts[m + 1]), k, held});
                }
                return parts;
            }
            for (std::size_t k = 1; facing > 0.0 && k < starts.size(); ++k) {
                if (!bothOn(starts[k])) continue;
                parts.push_back({p, q, k, false});
                return parts;
            }
            const std::vector<double> ts = cuts(p, q);
            for (std::size_t m = 0; m + 1 < ts.size(); ++m) {
                parts.push_back({at(p, q, ts[m]), at(p, q, ts[m + 1]),
                                 bayAt(across(at(p, q, (ts[m] + ts[m + 1]) / 2.0))), false});
            }
            return parts;
        };

        for (const WorldPolygon& poly : eaves) {
            for (const std::vector<Vec2>* ring : rings(poly)) {
                for (std::size_t i = 0; i < ring->size(); ++i) {
                    for (const SidePart& part : sideParts((*ring)[i], (*ring)[(i + 1) % ring->size()])) {
                        Vec2 a = part.a;
                        Vec2 b = part.b;
                        const roofparts::Plane plane = planeOf(part.bay);
                        double topA = plane.at(a);
                        double topB = plane.at(b);
                        double bottomA = topA - kRoofThickness;
                        double bottomB = topB - kRoofThickness;
                        if (part.underGlass) {
                            // The glass starts at the base, so only what hangs below that is fascia.
                            if (bottomA >= base && bottomB >= base) continue;
                            const double t = bottomA == bottomB ? 0.0 : (base - bottomA) / (bottomB - bottomA);
                            if (bottomA > base) {
                                a = at(part.a, part.b, t);
                                bottomA = base;
                            } else if (bottomB > base) {
                                b = at(part.a, part.b, t);
                                bottomB = base;
                            }
                            topA = base;
                            topB = base;
                        }
                        if (std::hypot(b.x - a.x, b.y - a.y) < 1e-6) continue;
                        faces.push_back(roofparts::wallFace(a, b, bottomA, bottomB, topA, topB, RoofPart::Edge));
                    }
                }
            }
        }

        // Glazing on the high side of each bay, as long as the roof is there.
        for (std::size_t k = 0; k < starts.size(); ++k) {
            const double s = endOf(k);
            for (const Span& pane : panes[k]) {
                const Vec2 from{n.x * s + d.x * pane.to, n.y * s + d.y * pane.to};
                const Vec2 to{n.x * s + d.x * pane.from, n.y * s + d.y * pane.from};
                const double top = base + (s - starts[k]) * slope;
                faces.push_back(roofparts::wallFace(from, to, base, base, top, top, RoofPart::Glazing));
            }
        }

        // Gable fills on the outline of the section, bay by bay, but where glass closes it.
        for (const std::vector<Vec2>* ring : rings(input.section)) {
            for (std::size_t i = 0; i < ring->size(); ++i) {
                for (const SidePart& part : sideParts((*ring)[i], (*ring)[(i + 1) % ring->size()])) {
                    if (part.underGlass) continue;
                    const roofparts::Plane plane = planeOf(part.bay);
                    if (const auto fill = roofparts::gableFill(part.a, part.b, base, plane.at(part.a) - kRoofThickness - base,
                                                              plane.at(part.b) - kRoofThickness - base)) {
                        faces.push_back(*fill);
                    }
                }
            }
        }
        return faces;
    }
};

const RoofTypeRegistrar registrar(std::make_unique<SawtoothRoof>());

}

}
