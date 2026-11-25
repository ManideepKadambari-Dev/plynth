#include "plynth/core/Roofs.h"
#include "roofs/RoofParts.h"

namespace plynth {

namespace {

// A flat roof: a thin layer over the eaves outline, with a parapet round the section when one is set. Along the
// floor above it has neither edge faces nor parapet, since that storey's wall rises there.
class FlatRoof final : public RoofType {
public:
    std::string_view id() const override { return kFlatRoofId; }
    std::string_view name() const override { return "Flat"; }
    int sortKey() const override { return 10; }
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
        const double top = input.base + kRoofThickness;

        std::vector<RoofFace> faces = roofparts::flatFaces(layer, top, true, RoofPart::Covering);
        auto add = [&faces](std::vector<RoofFace> more) {
            faces.insert(faces.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        };
        add(roofparts::flatFaces(layer, input.base, false, RoofPart::Underside));
        add(roofparts::ringWalls(layer, input.base, top, RoofPart::Edge, input.above));
        if (parapet) {
            // Joined with the floor above, the outline along that storey's wall is no outline at all, so the
            // band leaves it out and runs up to the wall.
            const std::vector<WorldPolygon> outline =
                roofparts::meetsAbove(layer, input.above) ? roofparts::joined(layer, input.above) : layer;
            const std::vector<WorldPolygon> inner = roofparts::inset(outline, kParapetThickness);
            const std::vector<WorldPolygon> band = roofparts::without(layer, inner);
            const double high = top + input.settings.parapet;
            add(roofparts::flatFaces(band, high, true, RoofPart::Parapet));
            add(roofparts::ringWalls(band, top, high, RoofPart::Parapet, input.above));
        }
        return faces;
    }
};

const RoofTypeRegistrar registrar(std::make_unique<FlatRoof>());

}

}
