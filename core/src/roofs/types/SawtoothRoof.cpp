#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth {

namespace {

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

        // Nothing stands beyond the high side of the last bay.
        const std::vector<WorldPolygon> eaves = roofparts::keepHalf(input.eaves, onLine(hi), backward);

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
        auto onHighSide = [&](Vec2 p, Vec2 q) {
            return std::fabs(across(p) - hi) < 1e-6 && std::fabs(across(q) - hi) < 1e-6;
        };

        for (const WorldPolygon& poly : eaves) {
            for (const std::vector<Vec2>* ring : rings(poly)) {
                for (std::size_t i = 0; i < ring->size(); ++i) {
                    const Vec2 p = (*ring)[i];
                    const Vec2 q = (*ring)[(i + 1) % ring->size()];
                    if (onHighSide(p, q)) continue;
                    const std::vector<double> ts = cuts(p, q);
                    for (std::size_t m = 0; m + 1 < ts.size(); ++m) {
                        const Vec2 a = at(p, q, ts[m]);
                        const Vec2 b = at(p, q, ts[m + 1]);
                        if (std::hypot(b.x - a.x, b.y - a.y) < 1e-6) continue;
                        const roofparts::Plane plane = planeOf(bayAt(across(at(p, q, (ts[m] + ts[m + 1]) / 2.0))));
                        faces.push_back(roofparts::wallFace(a, b, plane.at(a) - kRoofThickness, plane.at(b) - kRoofThickness,
                                                            plane.at(a), plane.at(b), RoofPart::Edge));
                    }
                }
            }
        }

        // Glazing on the high side of each bay, as long as the roof is there.
        for (std::size_t k = 0; k < starts.size(); ++k) {
            const double s = endOf(k);
            std::vector<WorldPolygon> strip = roofparts::keepHalf(eaves, onLine(s - 1e-4), n);
            strip = roofparts::keepHalf(strip, onLine(s), backward);
            for (const WorldPolygon& poly : strip) {
                double t0 = 1e18;
                double t1 = -1e18;
                for (const Vec2& p : poly.outline) {
                    t0 = std::min(t0, d.x * p.x + d.y * p.y);
                    t1 = std::max(t1, d.x * p.x + d.y * p.y);
                }
                if (t1 - t0 < 1e-6) continue;
                const Vec2 from{n.x * s + d.x * t1, n.y * s + d.y * t1};
                const Vec2 to{n.x * s + d.x * t0, n.y * s + d.y * t0};
                const double top = base + (s - starts[k]) * slope;
                faces.push_back(roofparts::wallFace(from, to, base, base, top, top, RoofPart::Glazing));
            }
        }

        // Gable fills on the outline of the section, bay by bay.
        for (const std::vector<Vec2>* ring : rings(input.section)) {
            for (std::size_t i = 0; i < ring->size(); ++i) {
                const Vec2 p = (*ring)[i];
                const Vec2 q = (*ring)[(i + 1) % ring->size()];
                if (onHighSide(p, q)) continue;
                const std::vector<double> ts = cuts(p, q);
                for (std::size_t m = 0; m + 1 < ts.size(); ++m) {
                    const Vec2 a = at(p, q, ts[m]);
                    const Vec2 b = at(p, q, ts[m + 1]);
                    const roofparts::Plane plane = planeOf(bayAt(across(at(p, q, (ts[m] + ts[m + 1]) / 2.0))));
                    if (const auto fill = roofparts::gableFill(a, b, base, plane.at(a) - kRoofThickness - base,
                                                              plane.at(b) - kRoofThickness - base)) {
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
