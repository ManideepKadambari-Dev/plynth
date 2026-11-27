#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

#include <algorithm>
#include <utility>

namespace plynth {

namespace {

constexpr double kGreenLayerDepth = 0.15; // metres of planting layer on the roof's slab
constexpr double kGreenEdgeWidth = 0.3;   // metres of the edge strip round the planting when there is no parapet
constexpr double kGreenEdgeHeight = 0.05; // metres the edge strip stands above the planting

// A green roof: a flat roof whose top is a planting layer kGreenLayerDepth deep on the roof's slab, so the covering
// (the planting, in the roof's material) stands kRoofThickness plus that above base and the edge faces run the
// whole depth. Round it runs its edge in the parapet material: with a parapet, a wall as on a flat roof standing
// on the planting along the inside of the section; without one, a strip kGreenEdgeWidth wide and kGreenEdgeHeight
// high along the eaves, like the gravel border of a planted roof. Neither runs along the floor above.
class GreenRoof final : public RoofType {
public:
    std::string_view id() const override { return "green"; }
    std::string_view name() const override { return "Green"; }
    int sortKey() const override { return 12; }
    bool usesPitch() const override { return false; }
    bool usesDirection() const override { return false; }
    bool usesOverhang() const override { return true; }
    bool usesParapet() const override { return true; }
    Status fits(const RoofInput&) const override { return {}; }
    std::string_view fallback() const override { return kFlatRoofId; }

    std::vector<RoofFace> build(const RoofInput& input) const override
    {
        const bool parapet = input.settings.parapet > 0.0;
        const std::vector<WorldPolygon> layer = parapet ? std::vector<WorldPolygon>{input.section} : input.eaves;
        const double top = input.base + kRoofThickness + kGreenLayerDepth;

        std::vector<RoofFace> faces = roofparts::flatFaces(layer, top, true, RoofPart::Covering);
        auto add = [&faces](std::vector<RoofFace> more) {
            faces.insert(faces.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        };
        add(roofparts::flatFaces(layer, input.base, false, RoofPart::Underside));
        add(roofparts::ringWalls(layer, input.base, top, RoofPart::Edge, input.above));

        // Joined with the floor above, the outline along that storey's wall is no outline at all, so the edge
        // leaves it out and runs up to the wall.
        const std::vector<WorldPolygon> outline =
            roofparts::meetsAbove(layer, input.above) ? roofparts::joined(layer, input.above) : layer;
        const double width = parapet ? kParapetThickness : kGreenEdgeWidth;
        const std::vector<WorldPolygon> band = roofparts::without(layer, roofparts::inset(outline, width));
        const double high = top + (parapet ? input.settings.parapet : kGreenEdgeHeight);
        add(roofparts::flatFaces(band, high, true, RoofPart::Parapet));
        add(roofparts::ringWalls(band, top, high, RoofPart::Parapet, input.above));
        return faces;
    }
};

const RoofTypeRegistrar registrar(std::make_unique<GreenRoof>());

}

}
