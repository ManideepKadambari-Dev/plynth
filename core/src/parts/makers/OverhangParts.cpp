#include "plynth/core/BuildParts.h"
#include "plynth/core/Canopies.h"
#include "plynth/core/MaterialSlots.h"
#include "plynth/core/Materials.h"
#include "plynth/core/Storeys.h"

#include "PlanRingClean.h"
#include "parts/PartGeometry.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace plynth {

namespace {

namespace pg = partgeometry;

// How far the fascia board stands out from the edge of an overhang's slab, in metres.
constexpr double kFasciaBoard = 0.02;
// How high a roof cut under an overhang reaches above the slab's underside, in metres: past everything above.
constexpr double kCutReach = 1000.0;

// The heights of a floor's roofs: the highest point of any face.
double highestOf(const RoofBuild& roof)
{
    double high = -1e300;
    for (const RoofFace& face : roof.faces) {
        for (const Vec3& p : face.outline) high = std::max(high, p.z);
    }
    return high;
}

// A length in metres as text with two decimals.
std::string metres(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", value);
    return text;
}

// Where floor i + 1 reaches past floor i (Canopies.h): a fascia board round the overhang's free edges, the
// warning when it reaches too far, and cuts in the roofs of the floors below floor i wherever they would rise
// through the overhang.
class OverhangMaker final : public PartMaker {
public:
    std::string_view id() const override { return "overhangs"; }
    int order() const override { return 41; }
    std::vector<std::string> kinds() const override { return {"overhang"}; }
    std::string_view exportGroup() const override { return "surfaces"; }

    // The fascia board, problem and roof cuts of the overhang above this floor.
    PartOutput floorParts(const PartInput& input) const override
    {
        PartOutput out;
        if (!input.project || !input.floor || !input.levels || !input.floors) return out;
        const Plan& plan = input.project->plan;
        const std::size_t i = input.floorIndex;
        if (i + 1 >= plan.floors.size() || i + 1 >= input.levels->size()) return out;
        const std::vector<WorldPolygon> over = pg::worldPolygonsOf(overhangArea(plan, i));
        if (over.empty()) return out;
        const Floor& floor = plan.floors[i];
        const Floor& upper = plan.floors[i + 1];
        const double bottom = (*input.levels)[i] + floor.ceilingHeight;
        const double top = (*input.levels)[i + 1];
        const std::vector<WorldPolygon> below = pg::worldPolygonsOf(floorFootprint(floor));
        const std::vector<WorldPolygon> above = pg::worldPolygonsOf(floorFootprint(upper));
        const Clipper2Lib::PathsD overPaths = pg::pathsOf(over);

        // The board: the overhang grown by its depth, less the slab, so it stands only along the free edges.
        const Clipper2Lib::PathsD slab = clipped(Clipper2Lib::ClipType::Union, pg::pathsOf(below), pg::pathsOf(above));
        const std::vector<WorldPolygon> band =
            pg::polygonsOf(clipped(Clipper2Lib::ClipType::Difference, offsetByTurning(overPaths, kFasciaBoard, 5.0), slab));
        if (!band.empty() && top > bottom) {
            const std::string slot =
                partSlot(kFasciaSlot, "Fascia", surfaceDefaultMaterial(*input.project, Surface::WallTop).material).slot;
            BuildPart part;
            part.object = floor.id + "_Overhang";
            part.kind = "overhang";
            part.id = floor.id;
            for (const WorldPolygon& piece : band) {
                PartPrism prism;
                prism.outline = piece.outline;
                prism.holes = piece.holes;
                prism.bottom = bottom;
                prism.top = top;
                prism.slot = slot;
                part.prisms.push_back(std::move(prism));
            }
            out.parts.push_back(std::move(part));
        }

        // The warning when the overhang reaches further past the walls below than kMaxOverhangDepth.
        if (!below.empty()) {
            const Clipper2Lib::PathsD within = offsetByTurning(pg::pathsOf(below), kMaxOverhangDepth, 10.0);
            const Clipper2Lib::PathsD beyond = clipped(Clipper2Lib::ClipType::Difference, overPaths, within);
            if (pg::areaOf(beyond) > pg::kTinyPartArea) {
                double depth = kMaxOverhangDepth;
                for (const Clipper2Lib::PathD& path : beyond) {
                    for (const Vec2& p : ringOfPath(path)) depth = std::max(depth, pg::distanceToEdges(p, below));
                }
                out.problems.push_back({"floor_overhang", BuildSeverity::Warning,
                                        "Floor " + upper.id + " reaches " + metres(depth) +
                                            " m past the walls of the floor below, more than the " + metres(kMaxOverhangDepth) +
                                            " m that stands without extra structure such as posts or brackets.",
                                        upper.id, upper.id, std::string(kCheckStep)});
            }
        }

        // Roofs of the floors below floor i that reach up into the overhang are cut back to it.
        for (std::size_t k = 0; k < i && k < input.floors->size(); ++k) {
            const FloorBuild& lower = (*input.floors)[k];
            for (const RoofBuild& roof : lower.roofs) {
                if (highestOf(roof) <= bottom + 1e-9) continue;
                for (const WorldPolygon& piece : over) {
                    RoofCut cut;
                    cut.floor = lower.id;
                    cut.index = roof.index;
                    cut.cutter.outline = piece.outline;
                    cut.cutter.holes = piece.holes;
                    cut.cutter.bottom = bottom;
                    cut.cutter.top = bottom + kCutReach;
                    out.roofCuts.push_back(std::move(cut));
                }
            }
        }
        return out;
    }
};

const PartMakerRegistrar registrar(std::make_unique<OverhangMaker>());

}

}
